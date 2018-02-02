
#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"

// For drawing sprites in 3D space
// Always facing camera, send as point

extern glm_vao_t aliasModel_vao;

static glm_program_t spriteProgram;
static GLuint spriteProgram_RefdefCvars_block;
static GLuint spriteProgram_SpriteData_block;
static uniform_block_spritedata_t spriteData;
static buffer_ref ubo_spriteData;

typedef struct glm_sprite_s {
	vec3_t origin;
	float scale;
	float texScale[2];
	texture_ref texture_array;
	int texture_index;
} glm_sprite_t;

static glm_sprite_t sprite_batch[MAX_SPRITE_BATCH];
static int batch_count = 0;

// Temporary
static buffer_ref simpleItemVBO;
static glm_vao_t simpleItemVAO;
static void GL_CompileSpriteProgram(void);

static void GL_PrepareSprites(void)
{
	GL_CompileSpriteProgram();

	if (!GL_BufferReferenceIsValid(simpleItemVBO)) {
		float verts[4][5] = { { 0 } };

		VectorSet(verts[0], 0, -1, -1);
		verts[0][3] = 1;
		verts[0][4] = 1;

		VectorSet(verts[1], 0, -1, 1);
		verts[1][3] = 1;
		verts[1][4] = 0;

		VectorSet(verts[2], 0, 1, 1);
		verts[2][3] = 0;
		verts[2][4] = 0;

		VectorSet(verts[3], 0, 1, -1);
		verts[3][3] = 0;
		verts[3][4] = 1;

		simpleItemVBO = GL_GenFixedBuffer(GL_ARRAY_BUFFER, "sprites", sizeof(verts), verts, GL_STATIC_DRAW);
	}

	if (!simpleItemVAO.vao) {
		GL_GenVertexArray(&simpleItemVAO);

		GL_ConfigureVertexAttribPointer(&simpleItemVAO, simpleItemVBO, 0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 5, (void*) 0);
		GL_ConfigureVertexAttribPointer(&simpleItemVAO, simpleItemVBO, 1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 5, (void*) (sizeof(float) * 3));
	}
}

static texture_ref prev_texture_array;
static qbool first_sprite_draw = true;

void GL_BeginDrawSprites(void)
{
	batch_count = 0;
	first_sprite_draw = true;
}

void GL_FlushSpriteBatch(void)
{
	int i;
	float oldMatrix[16];
	float projectionMatrix[16];

	if (batch_count && first_sprite_draw) {
		GL_EnterRegion("Sprites");

		GL_PrepareSprites();

		GL_SelectTexture(GL_TEXTURE0);
		GL_TextureReferenceInvalidate(prev_texture_array);

		first_sprite_draw = false;
	}

	glDisable(GL_CULL_FACE);

	GL_PushMatrix(GL_MODELVIEW, oldMatrix);
	GLM_GetMatrix(GL_PROJECTION, projectionMatrix);

	memset(&spriteData, 0, sizeof(spriteData));
	for (i = 0; i < batch_count; ++i) {
		glm_sprite_t* sprite = &sprite_batch[i];

		// FIXME: need to batch the calls?!
		if (!GL_TextureReferenceEqual(sprite->texture_array, prev_texture_array)) {
			GL_BindTextureUnit(GL_TEXTURE0, sprite->texture_array);
			prev_texture_array = sprite->texture_array;
		}

		GL_PopMatrix(GL_MODELVIEW, r_world_matrix);
		GL_Translate(GL_MODELVIEW, sprite->origin[0], sprite->origin[1], sprite->origin[2]);
		if (true)
		{
			float* tempMatrix = spriteData.sprites[i].modelView;

			GL_PushMatrix(GL_MODELVIEW, tempMatrix);
			// x = -y
			tempMatrix[0] = 0;
			tempMatrix[4] = -sprite->scale;
			tempMatrix[8] = 0;

			// y = z
			tempMatrix[1] = 0;
			tempMatrix[5] = 0;
			tempMatrix[9] = sprite->scale;

			// z = -x
			tempMatrix[2] = -sprite->scale;
			tempMatrix[6] = 0;
			tempMatrix[10] = 0;

			GL_PopMatrix(GL_MODELVIEW, tempMatrix);
		}

		spriteData.sprites[i].tex[0] = sprite->texScale[0];
		spriteData.sprites[i].tex[1] = sprite->texScale[1];

		spriteData.sprites[i].skinNumber = sprite->texture_index;
	}

	GL_UseProgram(spriteProgram.program);
	GL_UpdateVBO(ubo_spriteData, sizeof(spriteData), &spriteData);

	GL_BindVertexArray(&aliasModel_vao);
	glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, 4, batch_count);

	GL_PopMatrix(GL_MODELVIEW, oldMatrix);
	glEnable(GL_CULL_FACE);

	++frameStats.draw_calls;
	batch_count = 0;
}

void GL_EndDrawSprites(void)
{
	if (GL_ShadersSupported()) {
		if (batch_count) {
			GL_FlushSpriteBatch();
		}

		if (!first_sprite_draw) {
			GL_LeaveRegion();
		}
	}
}

static void GL_CompileSpriteProgram(void)
{
	if (GLM_ProgramRecompileNeeded(&spriteProgram, 0)) {
		char include_text[512];
		GL_VFDeclare(sprite);

		include_text[0] = '\0';
		strlcpy(include_text, va("#define MAX_INSTANCEID %d\n", MAX_SPRITE_BATCH), sizeof(include_text));

		// Initialise program for drawing image
		GLM_CreateVFProgramWithInclude("Sprites", GL_VFParams(sprite), &spriteProgram, include_text);
	}

	if (spriteProgram.program && !spriteProgram.uniforms_found) {
		GLint size;

		spriteProgram_RefdefCvars_block = glGetUniformBlockIndex(spriteProgram.program, "RefdefCvars");
		spriteProgram_SpriteData_block = glGetUniformBlockIndex(spriteProgram.program, "SpriteData");

		glGetActiveUniformBlockiv(spriteProgram.program, spriteProgram_SpriteData_block, GL_UNIFORM_BLOCK_DATA_SIZE, &size);

		glUniformBlockBinding(spriteProgram.program, spriteProgram_SpriteData_block, GL_BINDINGPOINT_SPRITEDATA_CVARS);

		ubo_spriteData = GL_GenUniformBuffer("sprite-data", &spriteData, sizeof(spriteData));
		GL_BindBufferBase(ubo_spriteData, GL_BINDINGPOINT_SPRITEDATA_CVARS);

		spriteProgram.uniforms_found = true;
	}
}

void GL_EndDrawEntities(void)
{
}

void GLM_DrawSimpleItem(model_t* model, int texture_index, vec3_t origin, vec3_t angles, float scale, float scale_s, float scale_t)
{
	glm_sprite_t* sprite;

	if (batch_count >= MAX_SPRITE_BATCH) {
		GL_FlushSpriteBatch();
	}

	sprite = &sprite_batch[batch_count];
	VectorCopy(origin, sprite->origin);
	// TODO: angles
	sprite->scale = scale;
	sprite->texScale[0] = scale_s;
	sprite->texScale[1] = scale_t;
	sprite->texture_array = model->simpletexture_array;
	sprite->texture_index = texture_index;
	++batch_count;
}

void GLM_DrawSpriteModel(entity_t* e)
{
	vec3_t right, up;
	mspriteframe_t *frame;
	msprite2_t *psprite;

	// don't even bother culling, because it's just a single
	// polygon without a surface cache
	psprite = (msprite2_t*)Mod_Extradata(e->model);	//locate the proper data
	frame = R_GetSpriteFrame(e, psprite);

	if (!frame) {
		return;
	}

	if (psprite->type == SPR_ORIENTED) {
		// bullet marks on walls
		AngleVectors(e->angles, NULL, right, up);
	}
	else if (psprite->type == SPR_FACING_UPRIGHT) {
		VectorSet(up, 0, 0, 1);
		right[0] = e->origin[1] - r_origin[1];
		right[1] = -(e->origin[0] - r_origin[0]);
		right[2] = 0;
		VectorNormalizeFast(right);
	}
	else if (psprite->type == SPR_VP_PARALLEL_UPRIGHT) {
		VectorSet(up, 0, 0, 1);
		VectorCopy(vright, right);
	}
	else {	// normal sprite
		VectorCopy(vup, up);
		VectorCopy(vright, right);
	}

	// MEAG: TODO: Angles
	{
		glm_sprite_t* sprite;

		if (batch_count >= MAX_SPRITE_BATCH) {
			GL_FlushSpriteBatch();
		}

		sprite = &sprite_batch[batch_count];
		VectorCopy(e->origin, sprite->origin);
		// TODO: angles
		sprite->scale = 5;
		sprite->texScale[0] = frame->gl_scalingS;
		sprite->texScale[1] = frame->gl_scalingT;
		sprite->texture_array = frame->gl_arraynum;
		sprite->texture_index = frame->gl_arrayindex;
		++batch_count;
	}
}
