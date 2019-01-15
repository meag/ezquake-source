// Alias model rendering (modern)

#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "gl_aliasmodel.h"

static glm_program_t drawShellPolyProgram;
static GLint drawShell_modelViewMatrix;
static GLint drawShell_projectionMatrix;
static GLint drawShell_shellSize;
static GLint drawShell_color;
static GLint drawShell_materialTex;
static GLint drawShell_time;

static glm_program_t drawAliasModelProgram;
static GLint drawAliasModel_modelViewMatrix;
static GLint drawAliasModel_projectionMatrix;
static GLint drawAliasModel_color;
static GLint drawAliasModel_materialTex;
static GLint drawAliasModel_skinTex;
static GLint drawAliasModel_shellSize;
static GLint drawAliasModel_time;
static GLint drawAliasModel_shellMode;
static GLint drawAliasModel_shell_alpha;
static GLint drawAliasModel_applyTexture;
static GLint drawAliasModel_textureIndex;
static GLint drawAliasModel_scaleS;
static GLint drawAliasModel_scaleT;

static void GLM_QueueAliasModelDraw(model_t* model, GLuint vao, byte* color, int start, int count, qbool texture, GLuint texture_index, float scaleS, float scaleT, int effects, qbool is_texture_array);

typedef struct glm_aliasmodel_req_s {
	GLuint vbo_count;
	GLuint instanceCount;
	GLuint vbo_start;
	GLuint baseInstance;
	float mvMatrix[16];
	float texScale[2];
	GLuint vao;
	GLuint texture_array;
	int texture_index;
	byte color[4];
	int texture_model;
	int effects;
	qbool is_texture_array;
} glm_aliasmodel_req_t;

typedef struct DrawArraysIndirectCommand_s {
	GLuint count;
	GLuint instanceCount;
	GLuint first;
	GLuint baseInstance;
} DrawArraysIndirectCommand_t;

#define MAX_ALIASMODEL_BATCH 32
static glm_aliasmodel_req_t aliasmodel_requests[MAX_ALIASMODEL_BATCH];
static int batch_count = 0;
static GLuint prev_texture_array = 0;
static qbool in_batch_mode = false;

static void GLM_CompileAliasModelProgram(void)
{
	if (!drawAliasModelProgram.program) {
		GL_VFDeclare(model_alias);

		// Initialise program for drawing image
		GLM_CreateVFProgram("AliasModel", GL_VFParams(model_alias), &drawAliasModelProgram);

		drawAliasModel_modelViewMatrix = glGetUniformLocation(drawAliasModelProgram.program, "modelViewMatrix");
		drawAliasModel_projectionMatrix = glGetUniformLocation(drawAliasModelProgram.program, "projectionMatrix");
		drawAliasModel_color = glGetUniformLocation(drawAliasModelProgram.program, "color");
		drawAliasModel_materialTex = glGetUniformLocation(drawAliasModelProgram.program, "materialTex");
		drawAliasModel_skinTex = glGetUniformLocation(drawAliasModelProgram.program, "skinTex");
		drawAliasModel_applyTexture = glGetUniformLocation(drawAliasModelProgram.program, "apply_texture");
		drawAliasModel_shellSize = glGetUniformLocation(drawAliasModelProgram.program, "shellSize");
		drawAliasModel_time = glGetUniformLocation(drawAliasModelProgram.program, "time");
		drawAliasModel_shellMode = glGetUniformLocation(drawAliasModelProgram.program, "shellMode");
		drawAliasModel_textureIndex = glGetUniformLocation(drawAliasModelProgram.program, "textureIndex");
		drawAliasModel_scaleS = glGetUniformLocation(drawAliasModelProgram.program, "scaleS");
		drawAliasModel_scaleT = glGetUniformLocation(drawAliasModelProgram.program, "scaleT");
		drawAliasModel_shell_alpha = glGetUniformLocation(drawAliasModelProgram.program, "shell_alpha");
	}
}

// Drawing single frame from an alias model (no lerping)
void GLM_DrawSimpleAliasFrame(model_t* model, aliashdr_t* paliashdr, int pose1, qbool scrolldir, GLuint texture, GLuint fb_texture, GLuint textureEnvMode, float scaleS, float scaleT, int effects, qbool is_texture_array)
{
	int vertIndex = paliashdr->vertsOffset + pose1 * paliashdr->vertsPerPose;
	byte color[4];
	float l;
	qbool texture_model = (custom_model == NULL);

	// FIXME: This has been converted from 4 bytes already?
	if (r_modelcolor[0] < 0) {
		color[0] = color[1] = color[2] = 255;
	}
	else {
		color[0] = r_modelcolor[0] * 255;
		color[1] = r_modelcolor[1] * 255;
		color[2] = r_modelcolor[2] * 255;
	}
	color[3] = r_modelalpha * 255;

	if (paliashdr->vao) {
		int* order = (int *)((byte *)paliashdr + paliashdr->commands);
		int count;

		while ((count = *order++)) {
			GLenum drawMode = GL_TRIANGLE_STRIP;

			if (count < 0) {
				count = -count;
				drawMode = GL_TRIANGLE_FAN;
			}

			// texture coordinates now stored in the VBO
			order += 2 * count;

			// TODO: model lerping between frames
			// TODO: Vertex lighting etc
			// TODO: shadedots[vert.l] ... need to copy shadedots to program somehow
			// TODO: Coloured lighting per-vertex?
			l = shadedots[0] / 127.0;
			l = (l * shadelight + ambientlight) / 256.0;
			l = min(l, 1);

			if (custom_model == NULL) {
				if (r_modelcolor[0] < 0) {
					// normal color
					color[0] = color[1] = color[2] = l * 255;
				}
				else {
					// forced
					color[0] *= l;
					color[1] *= l;
					color[2] *= l;
				}
			}
			else {
				color[0] = custom_model->color_cvar.color[0];
				color[1] = custom_model->color_cvar.color[1];
				color[2] = custom_model->color_cvar.color[2];
			}

			GLM_QueueAliasModelDraw(model, paliashdr->vao, color, vertIndex, count, texture_model, texture, scaleS, scaleT, effects, is_texture_array);

			vertIndex += count;
		}
	}
}

static void GLM_AliasModelBatchDraw(int start, int end, float mvMatrix[MAX_ALIASMODEL_BATCH][16], float colors[MAX_ALIASMODEL_BATCH][4], float* texScaleS, float* texScaleT, float* texture_indexes, int* texture_models, int* shellModes)
{
	int i;

	glUniformMatrix4fv(drawAliasModel_modelViewMatrix, batch_count, GL_FALSE, (const GLfloat*) mvMatrix);
	glUniform4fv(drawAliasModel_color, batch_count, (const GLfloat*) colors);
	glUniform1fv(drawAliasModel_scaleS, batch_count, texScaleS);
	glUniform1fv(drawAliasModel_scaleT, batch_count, texScaleT);
	glUniform1fv(drawAliasModel_textureIndex, batch_count, texture_indexes);
	glUniform1iv(drawAliasModel_applyTexture, batch_count, texture_models);
	glUniform1iv(drawAliasModel_shellMode, batch_count, shellModes);

	for (i = start; i < end; ++i) {
		const DrawArraysIndirectCommand_t *cmd = (const DrawArraysIndirectCommand_t *)((byte*)aliasmodel_requests + i * sizeof(aliasmodel_requests[0]));

		if (shellModes[i]) {
			GL_AlphaBlendFlags(GL_BLEND_ENABLED);
			// FIXME: Should be able to move this to Begin() ... but, could be R_DrawViewModel() and not batched...
			glBlendFunc(GL_ONE, GL_ONE);
		}
		else {
			GL_AlphaBlendFlags(GL_BLEND_DISABLED);
		}
		glDrawArraysInstancedBaseInstance(GL_TRIANGLE_STRIP, cmd->first, cmd->count, cmd->instanceCount, cmd->baseInstance);
	}
}

static void GLM_FlushAliasModelBatch(void)
{
	int i;
	float projectionMatrix[16];

	float mvMatrix[MAX_ALIASMODEL_BATCH][16];
	float colors[MAX_ALIASMODEL_BATCH][4];
	float texScaleS[MAX_ALIASMODEL_BATCH];
	float texScaleT[MAX_ALIASMODEL_BATCH];
	float texture_indexes[MAX_ALIASMODEL_BATCH];
	int texture_models[MAX_ALIASMODEL_BATCH];
	int shellModes[MAX_ALIASMODEL_BATCH];
	qbool was_texture_array = false;
	int base = 0;
	int non_texture_array = -1;
	GLuint texture_array = 0;

	GLM_GetMatrix(GL_PROJECTION, projectionMatrix);

	GLM_CompileAliasModelProgram();
	GL_UseProgram(drawAliasModelProgram.program);
	glUniformMatrix4fv(drawAliasModel_projectionMatrix, 1, GL_FALSE, projectionMatrix);
	glUniform1i(drawAliasModel_materialTex, 0);
	glUniform1i(drawAliasModel_skinTex, 1);
	glUniform1f(drawAliasModel_shellSize, bound(0, gl_powerupshells_size.value, 20));
	glUniform1f(drawAliasModel_time, cl.time);
	glUniform1f(drawAliasModel_shell_alpha, bound(0, gl_powerupshells.value, 1));

	for (i = 0; i < batch_count; ++i) {
		glm_aliasmodel_req_t* req = &aliasmodel_requests[i];
		qbool array_switch = (req->is_texture_array && texture_array != req->texture_array);
		qbool non_array_switch = (!req->is_texture_array && non_texture_array != req->texture_index);
		qbool texture_mode_switch = (texture_array && array_switch) || (non_texture_array != -1 && non_array_switch);

		if (i == 0) {
			glBindVertexArray(req->vao);
		}

		if (texture_mode_switch) {
			GLM_AliasModelBatchDraw(base, i, mvMatrix, colors, texScaleS, texScaleT, texture_indexes, texture_models, shellModes);

			base = i;
		}

		if (non_array_switch) {
			glActiveTexture(GL_TEXTURE0 + 1);
			glBindTexture(GL_TEXTURE_2D, req->texture_index);

			non_texture_array = req->texture_index;
		}

		if (array_switch) {
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D_ARRAY, req->texture_array);

			prev_texture_array = req->texture_array;
		}

		memcpy(&mvMatrix[i], req->mvMatrix, sizeof(mvMatrix[i]));
		colors[i][0] = req->color[0] * 1.0f / 255;
		colors[i][1] = req->color[1] * 1.0f / 255;
		colors[i][2] = req->color[2] * 1.0f / 255;
		colors[i][3] = req->color[3] * 1.0f / 255;
		texture_indexes[i] = req->texture_index;
		texScaleS[i] = req->texScale[0];
		texScaleT[i] = req->texScale[1];
		texture_models[i] = req->is_texture_array ? req->texture_model : 2;
		shellModes[i] = req->effects;

		aliasmodel_requests[i].baseInstance = i;
		aliasmodel_requests[i].instanceCount = 1;
	}

	if (batch_count) {
		GLM_AliasModelBatchDraw(base, batch_count, mvMatrix, colors, texScaleS, texScaleT, texture_indexes, texture_models, shellModes);

		batch_count = 0;
	}
}

static void GLM_SetPowerupShellColor(float* shell_color, float base_level, float effect_level, int effects)
{
	base_level = bound(0, base_level, 1);
	effect_level = bound(0, effect_level, 1);

	shell_color[0] = shell_color[1] = shell_color[2] = base_level;
	shell_color[3] = bound(0, gl_powerupshells.value, 1);
	if (effects & EF_RED)
		shell_color[0] += effect_level;
	if (effects & EF_GREEN)
		shell_color[1] += effect_level;
	if (effects & EF_BLUE)
		shell_color[2] += effect_level;
}

static void GLM_QueueAliasModelDrawImpl(model_t* model, GLuint vao, byte* color, int start, int count, qbool texture, GLuint texture_index, float scaleS, float scaleT, int effects, qbool is_texture_array)
{
	glm_aliasmodel_req_t* req;

	if (batch_count >= MAX_ALIASMODEL_BATCH) {
		GLM_FlushAliasModelBatch();
	}

	if (!vao) {
		return;
	}

	req = &aliasmodel_requests[batch_count];
	GL_GetMatrix(GL_MODELVIEW, req->mvMatrix);
	// TODO: angles
	req->vbo_start = start;
	req->vbo_count = count;
	req->texScale[0] = is_texture_array ? scaleS : 1.0f;
	req->texScale[1] = is_texture_array ? scaleT : 1.0f;
	req->texture_model = texture;
	req->vao = vao;
	req->texture_array = model->texture_arrays[0];
	req->texture_index = texture_index;
	req->instanceCount = 1;
	req->effects = effects;
	req->is_texture_array = is_texture_array;
	memcpy(req->color, color, 4);
	++batch_count;
}

static void GLM_QueueAliasModelDraw(model_t* model, GLuint vao, byte* color, int start, int count, qbool texture, GLuint texture_index, float scaleS, float scaleT, int effects, qbool is_texture_array)
{
	GLM_QueueAliasModelDrawImpl(model, vao, color, start, count, texture, texture_index, scaleS, scaleT, 0, is_texture_array);

	if (effects) {
		// always allow powerupshells for specs or demos.
		// do not allow powerupshells for eyes in other cases
		if (bound(0, gl_powerupshells.value, 1) && ((cls.demoplayback || cl.spectator) || model->modhint != MOD_EYES)) {
			effects &= (EF_RED | EF_GREEN | EF_BLUE);
		}

		if (effects) {
			GLM_QueueAliasModelDrawImpl(model, vao, color_white, start, count, true, 0, 1, 1, effects, true);
		}
	}

	if (!in_batch_mode) {
		glDisable(GL_CULL_FACE);
		GLM_FlushAliasModelBatch();
		glEnable(GL_CULL_FACE);
	}
}

void GL_BeginDrawAliasModels(void)
{
	if (GL_ShadersSupported()) {
		GLM_CompileAliasModelProgram();

		glActiveTexture(GL_TEXTURE0);
		prev_texture_array = 0;
		glDisable(GL_CULL_FACE);
		in_batch_mode = true;
		batch_count = 0;
	}
}

void GL_EndDrawAliasModels(void)
{
	if (GL_ShadersSupported()) {
		if (batch_count) {
			GLM_FlushAliasModelBatch();
		}
		in_batch_mode = false;
		glEnable(GL_CULL_FACE);
	}
}
