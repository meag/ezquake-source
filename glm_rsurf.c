/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// glm_surf.c: surface-related refresh code (modern OpenGL)

#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "rulesets.h"
#include "utils.h"

extern GLuint lightmap_texture_array;

static glm_program_t turbPolyProgram;
static GLint turb_modelViewMatrix;
static GLint turb_projectionMatrix;
static GLint turb_materialTex;
static GLint turb_alpha;
static GLint turb_time;

// Very similar to GLM_DrawPoly, but with manipulation of texture coordinates
static void GLM_CompileTurbPolyProgram(void)
{
	if (!turbPolyProgram.program) {
		GL_VFDeclare(turb_poly);

		// Initialise program for drawing image
		GLM_CreateVFProgram("Turb poly", GL_VFParams(turb_poly), &turbPolyProgram);

		turb_modelViewMatrix = glGetUniformLocation(turbPolyProgram.program, "modelViewMatrix");
		turb_projectionMatrix = glGetUniformLocation(turbPolyProgram.program, "projectionMatrix");
		turb_materialTex = glGetUniformLocation(turbPolyProgram.program, "materialTex");
		turb_alpha = glGetUniformLocation(turbPolyProgram.program, "alpha");
		turb_time = glGetUniformLocation(turbPolyProgram.program, "time");
	}
}

void GLM_DrawIndexedTurbPolys(unsigned int vao, GLushort* indices, int vertices, float alpha)
{
	GLM_CompileTurbPolyProgram();

	if (turbPolyProgram.program && vao) {
		float modelViewMatrix[16];
		float projectionMatrix[16];

		GLM_GetMatrix(GL_MODELVIEW, modelViewMatrix);
		GLM_GetMatrix(GL_PROJECTION, projectionMatrix);

		GL_UseProgram(turbPolyProgram.program);
		glUniformMatrix4fv(turb_modelViewMatrix, 1, GL_FALSE, modelViewMatrix);
		glUniformMatrix4fv(turb_projectionMatrix, 1, GL_FALSE, projectionMatrix);
		glUniform1i(turb_materialTex, 0);
		glUniform1f(turb_alpha, alpha);
		glUniform1f(turb_time, cl.time);

		GL_BindVertexArray(vao);
		//glDisable(GL_CULL_FACE);
		glDrawElements(GL_TRIANGLE_STRIP, vertices, GL_UNSIGNED_SHORT, indices);
		//glEnable(GL_CULL_FACE);
	}
}

void GLM_DrawTurbPolys(unsigned int vao, int vertices, float alpha)
{
	GLM_CompileTurbPolyProgram();

	if (turbPolyProgram.program && vao) {
		float modelViewMatrix[16];
		float projectionMatrix[16];

		GLM_GetMatrix(GL_MODELVIEW, modelViewMatrix);
		GLM_GetMatrix(GL_PROJECTION, projectionMatrix);

		GL_UseProgram(turbPolyProgram.program);
		glUniformMatrix4fv(turb_modelViewMatrix, 1, GL_FALSE, modelViewMatrix);
		glUniformMatrix4fv(turb_projectionMatrix, 1, GL_FALSE, projectionMatrix);
		glUniform1i(turb_materialTex, 0);
		glUniform1f(turb_alpha, alpha);
		glUniform1f(turb_time, cl.time);

		GL_BindVertexArray(vao);
		//glDisable(GL_CULL_FACE);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, vertices);
		//glEnable(GL_CULL_FACE);
	}
}

static glm_program_t drawFlatPolyProgram;
static GLint drawFlat_modelViewMatrix;
static GLint drawFlat_projectionMatrix;
static GLint drawFlat_color;
static GLint drawFlat_materialTex;
static GLint drawFlat_lightmapTex;
static GLint drawFlat_apply_lightmap;
static GLint drawFlat_apply_texture;
static GLint drawFlat_alpha_texture;

static void Compile_DrawFlatPolyProgram(void)
{
	GL_VFDeclare(generic_poly)

		// Initialise program for drawing image
		GLM_CreateVFProgram("Drawflat poly", GL_VFParams(generic_poly), &drawFlatPolyProgram);

	drawFlat_modelViewMatrix = glGetUniformLocation(drawFlatPolyProgram.program, "modelViewMatrix");
	drawFlat_projectionMatrix = glGetUniformLocation(drawFlatPolyProgram.program, "projectionMatrix");
	drawFlat_color = glGetUniformLocation(drawFlatPolyProgram.program, "color");
	drawFlat_materialTex = glGetUniformLocation(drawFlatPolyProgram.program, "materialTex");
	drawFlat_lightmapTex = glGetUniformLocation(drawFlatPolyProgram.program, "lightmapTex");
	drawFlat_apply_lightmap = glGetUniformLocation(drawFlatPolyProgram.program, "apply_lightmap");
	drawFlat_apply_texture = glGetUniformLocation(drawFlatPolyProgram.program, "apply_texture");
	drawFlat_alpha_texture = glGetUniformLocation(drawFlatPolyProgram.program, "alpha_texture");
}

static glm_program_t lightmapPolyProgram;
static GLint lightmapPoly_modelViewMatrix;
static GLint lightmapPoly_projectionMatrix;
static GLint lightmapPoly_color;
static GLint lightmapPoly_materialTex;
static GLint lightmapPoly_lightmapTex;
static GLint lightmapPoly_apply_lightmap;
static GLint lightmapPoly_apply_texture;
static GLint lightmapPoly_alpha_texture;

static qbool uniforms_set = false;

static void Compile_LightmapPolyProgram(void)
{
	GL_VFDeclare(lightmaparray_poly)

		// Initialise program for drawing image
		GLM_CreateVFProgram("Lightmap poly", GL_VFParams(lightmaparray_poly), &lightmapPolyProgram);

	lightmapPoly_modelViewMatrix = glGetUniformLocation(lightmapPolyProgram.program, "modelViewMatrix");
	lightmapPoly_projectionMatrix = glGetUniformLocation(lightmapPolyProgram.program, "projectionMatrix");
	lightmapPoly_color = glGetUniformLocation(lightmapPolyProgram.program, "color");
	lightmapPoly_materialTex = glGetUniformLocation(lightmapPolyProgram.program, "materialTex");
	lightmapPoly_lightmapTex = glGetUniformLocation(lightmapPolyProgram.program, "lightmapTex");
	lightmapPoly_apply_lightmap = glGetUniformLocation(lightmapPolyProgram.program, "apply_lightmap");
	lightmapPoly_apply_texture = glGetUniformLocation(lightmapPolyProgram.program, "apply_texture");
	lightmapPoly_alpha_texture = glGetUniformLocation(lightmapPolyProgram.program, "alpha_texture");
}

qbool GLM_PrepareLightmapProgram(GLenum type, byte* color, unsigned int vao, qbool apply_lightmap, qbool apply_texture, qbool alpha_texture)
{
	if (!lightmapPolyProgram.program) {
		Compile_LightmapPolyProgram();
	}

	if (lightmapPolyProgram.program && vao) {
		if (!uniforms_set) {
			float modelViewMatrix[16];
			float projectionMatrix[16];

			GLM_GetMatrix(GL_MODELVIEW, modelViewMatrix);
			GLM_GetMatrix(GL_PROJECTION, projectionMatrix);

			GL_UseProgram(lightmapPolyProgram.program);
			glUniformMatrix4fv(lightmapPoly_modelViewMatrix, 1, GL_FALSE, modelViewMatrix);
			glUniformMatrix4fv(lightmapPoly_projectionMatrix, 1, GL_FALSE, projectionMatrix);
			glUniform4f(lightmapPoly_color, color[0] * 1.0f / 255, color[1] * 1.0f / 255, color[2] * 1.0f / 255, color[3] * 1.0f / 255);
			glUniform1i(lightmapPoly_materialTex, 0);
			glUniform1i(lightmapPoly_lightmapTex, 2);
			glUniform1i(lightmapPoly_apply_lightmap, apply_lightmap ? 1 : 0);
			glUniform1i(lightmapPoly_apply_texture, apply_texture ? 1 : 0);
			glUniform1i(lightmapPoly_alpha_texture, alpha_texture ? 1 : 0);

			GL_BindVertexArray(vao);
		}
		return true;
	}

	return false;
}

void GLM_DrawMultiLightmapIndexedPolygonByType(GLenum type, byte* color, unsigned int vao, GLushort** indices, GLsizei* lengths, int count, qbool apply_lightmap, qbool apply_texture, qbool alpha_texture)
{
	if (GLM_PrepareLightmapProgram(type, color, vao, apply_lightmap, apply_texture, alpha_texture)) {
		glMultiDrawElements(type, lengths, GL_UNSIGNED_SHORT, indices, count);
	}
}

void GLM_DrawLightmapIndexedPolygonByType(GLenum type, byte* color, unsigned int vao, GLushort* indices, int count, qbool apply_lightmap, qbool apply_texture, qbool alpha_texture)
{
	if (GLM_PrepareLightmapProgram(type, color, vao, apply_lightmap, apply_texture, alpha_texture)) {
		glDrawElements(type, count, GL_UNSIGNED_SHORT, indices);
	}
}

static glm_program_t drawworld;
static GLint drawworld_modelViewMatrix;
static GLint drawworld_projectionMatrix;
static GLint drawworld_materialTex;
static GLint drawworld_detailTex;
static GLint drawworld_lightmapTex;
static GLint drawworld_causticsTex;
static GLint drawworld_drawDetailTex;

static void Compile_DrawWorldProgram(void)
{
	GL_VFDeclare(drawworld)

	// Initialise program for drawing image
	GLM_CreateVFProgram("DrawWorld", GL_VFParams(drawworld), &drawworld);

	drawworld_modelViewMatrix = glGetUniformLocation(drawworld.program, "modelViewMatrix");
	drawworld_projectionMatrix = glGetUniformLocation(drawworld.program, "projectionMatrix");
	drawworld_drawDetailTex = glGetUniformLocation(drawworld.program, "drawDetailTex");
	drawworld_materialTex = glGetUniformLocation(drawworld.program, "materialTex");
	drawworld_detailTex = glGetUniformLocation(drawworld.program, "detailTex");
	drawworld_lightmapTex = glGetUniformLocation(drawworld.program, "lightmapTex");
	drawworld_causticsTex = glGetUniformLocation(drawworld.program, "causticsTex");
}

static void GLM_EnterBatchedWorldRegion(unsigned int vao, qbool detail_tex)
{
	float modelViewMatrix[16];
	float projectionMatrix[16];

	if (!drawworld.program) {
		Compile_DrawWorldProgram();
	}

	GLM_GetMatrix(GL_MODELVIEW, modelViewMatrix);
	GLM_GetMatrix(GL_PROJECTION, projectionMatrix);

	GL_UseProgram(drawworld.program);
	glUniformMatrix4fv(drawworld_modelViewMatrix, 1, GL_FALSE, modelViewMatrix);
	glUniformMatrix4fv(drawworld_projectionMatrix, 1, GL_FALSE, projectionMatrix);
	glUniform1i(drawworld_drawDetailTex, detail_tex ? 1 : 0);
	glUniform1i(drawworld_materialTex, 0);
	glUniform1i(drawworld_detailTex, 1);
	glUniform1i(drawworld_lightmapTex, 2);
	glUniform1i(drawworld_causticsTex, 3);

	GL_BindVertexArray(vao);
}

/*
void GLM_EnterBatchedPolyRegion(byte* color, unsigned int vao, qbool apply_lightmap, qbool apply_texture, qbool alpha_texture)
{
	float modelViewMatrix[16];
	float projectionMatrix[16];

	if (lightmap_texture_array) {
		if (!lightmapPolyProgram.program) {
			Compile_LightmapPolyProgram();
		}

		GLM_GetMatrix(GL_MODELVIEW, modelViewMatrix);
		GLM_GetMatrix(GL_PROJECTION, projectionMatrix);

		GL_UseProgram(lightmapPolyProgram.program);
		glUniformMatrix4fv(lightmapPoly_modelViewMatrix, 1, GL_FALSE, modelViewMatrix);
		glUniformMatrix4fv(lightmapPoly_projectionMatrix, 1, GL_FALSE, projectionMatrix);
		glUniform4f(lightmapPoly_color, color[0] * 1.0f / 255, color[1] * 1.0f / 255, color[2] * 1.0f / 255, color[3] * 1.0f / 255);
		glUniform1i(lightmapPoly_materialTex, 0);
		glUniform1i(lightmapPoly_lightmapTex, 2);
		glUniform1i(lightmapPoly_apply_lightmap, apply_lightmap ? 1 : 0);
		glUniform1i(lightmapPoly_apply_texture, apply_texture ? 1 : 0);
		glUniform1i(lightmapPoly_alpha_texture, alpha_texture ? 1 : 0);

		GL_BindVertexArray(vao);
	}
	else {
		if (!drawFlatPolyProgram.program) {
			Compile_DrawFlatPolyProgram();
		}

		GLM_GetMatrix(GL_MODELVIEW, modelViewMatrix);
		GLM_GetMatrix(GL_PROJECTION, projectionMatrix);

		GL_UseProgram(drawFlatPolyProgram.program);
		glUniformMatrix4fv(drawFlat_modelViewMatrix, 1, GL_FALSE, modelViewMatrix);
		glUniformMatrix4fv(drawFlat_projectionMatrix, 1, GL_FALSE, projectionMatrix);
		glUniform4f(drawFlat_color, color[0] * 1.0f / 255, color[1] * 1.0f / 255, color[2] * 1.0f / 255, color[3] * 1.0f / 255);
		glUniform1i(drawFlat_materialTex, 0);
		glUniform1i(drawFlat_lightmapTex, 2);
		glUniform1i(drawFlat_apply_lightmap, apply_lightmap ? 1 : 0);
		glUniform1i(drawFlat_apply_texture, apply_texture ? 1 : 0);
		glUniform1i(drawFlat_alpha_texture, alpha_texture ? 1 : 0);

		GL_BindVertexArray(vao);
	}
	uniforms_set = true;
}
*/

void GLM_ExitBatchedPolyRegion(void)
{
	uniforms_set = false;
}

void GLM_DrawIndexedPolygonByType(GLenum type, byte* color, unsigned int vao, GLushort* indices, int count, qbool apply_lightmap, qbool apply_texture, qbool alpha_texture)
{
	if (!drawFlatPolyProgram.program) {
		Compile_DrawFlatPolyProgram();
	}

	if (drawFlatPolyProgram.program && vao) {
		if (!uniforms_set) {
			float modelViewMatrix[16];
			float projectionMatrix[16];

			GLM_GetMatrix(GL_MODELVIEW, modelViewMatrix);
			GLM_GetMatrix(GL_PROJECTION, projectionMatrix);

			GL_UseProgram(drawFlatPolyProgram.program);
			glUniformMatrix4fv(drawFlat_modelViewMatrix, 1, GL_FALSE, modelViewMatrix);
			glUniformMatrix4fv(drawFlat_projectionMatrix, 1, GL_FALSE, projectionMatrix);
			glUniform4f(drawFlat_color, color[0] * 1.0f / 255, color[1] * 1.0f / 255, color[2] * 1.0f / 255, color[3] * 1.0f / 255);
			glUniform1i(drawFlat_materialTex, 0);
			glUniform1i(drawFlat_lightmapTex, 2);
			glUniform1i(drawFlat_apply_lightmap, apply_lightmap ? 1 : 0);
			glUniform1i(drawFlat_apply_texture, apply_texture ? 1 : 0);
			glUniform1i(drawFlat_alpha_texture, alpha_texture ? 1 : 0);

			GL_BindVertexArray(vao);
		}

		glDrawElements(type, count, GL_UNSIGNED_SHORT, indices);
	}
}

void GLM_DrawLightmapArrayPolygonByType(GLenum type, byte* color, unsigned int vao, int start, int vertices, qbool apply_lightmap, qbool apply_texture, qbool alpha_texture)
{
	if (GLM_PrepareLightmapProgram(type, color, vao, apply_lightmap, apply_texture, alpha_texture)) {
		glDrawArrays(type, start, vertices);
	}
}

// Very simple polygon drawing until we fix
void GLM_DrawPolygonByType(GLenum type, byte* color, unsigned int vao, int start, int vertices, qbool apply_lightmap, qbool apply_texture, qbool alpha_texture)
{
	if (!drawFlatPolyProgram.program) {
		Compile_DrawFlatPolyProgram();
	}

	if (drawFlatPolyProgram.program && vao) {
		float modelViewMatrix[16];
		float projectionMatrix[16];

		GLM_GetMatrix(GL_MODELVIEW, modelViewMatrix);
		GLM_GetMatrix(GL_PROJECTION, projectionMatrix);

		GL_UseProgram(drawFlatPolyProgram.program);
		glUniformMatrix4fv(drawFlat_modelViewMatrix, 1, GL_FALSE, modelViewMatrix);
		glUniformMatrix4fv(drawFlat_projectionMatrix, 1, GL_FALSE, projectionMatrix);
		glUniform4f(drawFlat_color, color[0] * 1.0f / 255, color[1] * 1.0f / 255, color[2] * 1.0f / 255, color[3] * 1.0f / 255);
		glUniform1i(drawFlat_materialTex, 0);
		glUniform1i(drawFlat_lightmapTex, 2);
		glUniform1i(drawFlat_apply_lightmap, apply_lightmap ? 1 : 0);
		glUniform1i(drawFlat_apply_texture, apply_texture ? 1 : 0);
		glUniform1i(drawFlat_alpha_texture, alpha_texture ? 1 : 0);

		GL_BindVertexArray(vao);
		glDrawArrays(type, start, vertices);
	}
}

void GLM_DrawPolygon(byte* color, unsigned int vao, int start, int vertices, qbool apply_lightmap, qbool apply_texture, qbool alpha_texture)
{
	GLM_DrawPolygonByType(GL_TRIANGLE_FAN, color, vao, start, vertices, apply_lightmap, apply_texture, alpha_texture);
}

void GLM_DrawFlatPoly(byte* color, unsigned int vao, int vertices, qbool apply_lightmap)
{
	GLM_DrawPolygon(color, vao, 0, vertices, apply_lightmap, false, false);
}

void GLM_DrawTexturedPoly(byte* color, unsigned int vao, int start, int vertices, qbool apply_lightmap, qbool alpha_test)
{
	GLM_DrawPolygon(color, vao, start, vertices, apply_lightmap, true, alpha_test);
}

void GLM_DrawTexturedWorld(model_t* model)
{
	GLushort indices[4096];
	int i, waterline, v;
	msurface_t* surf;
	qbool draw_detail_texture = gl_detail.integer && detailtexture;
	qbool draw_caustics = gl_caustics.integer && underwatertexture;

	GLM_EnterBatchedWorldRegion(model->vao, draw_detail_texture);

	// Bind lightmap array
	GL_SelectTexture(GL_TEXTURE2);
	GL_BindTexture(GL_TEXTURE_2D_ARRAY, lightmap_texture_array);
	if (draw_detail_texture) {
		GL_SelectTexture(GL_TEXTURE1);
		GL_Bind(detailtexture);
	}
	GL_SelectTexture(GL_TEXTURE0);

	for (i = 0; i < model->texture_array_count; ++i) {
		texture_t* base_tex = model->textures[model->texture_array_first[i]];
		qbool first_in_this_array = true;
		int texIndex;
		int count = 0;

		if (!base_tex || !base_tex->size_start) {
			continue;
		}

		for (texIndex = model->texture_array_first[i]; texIndex >= 0 && texIndex < model->numtextures; texIndex = model->textures[texIndex]->next_same_size) {
			texture_t* tex = model->textures[texIndex];

			if (!tex->texturechain[0] && !tex->texturechain[1]) {
				continue;
			}

			// Going to draw at least one surface, so bind the texture array
			if (first_in_this_array) {
				GL_BindTexture(GL_TEXTURE_2D_ARRAY, model->texture_arrays[i]);
				first_in_this_array = false;
			}

			for (waterline = 0; waterline < 2; waterline++) {
				for (surf = tex->texturechain[waterline]; surf; surf = surf->texturechain) {
					int newVerts = surf->polys->numverts;

					if (count + 3 + newVerts > sizeof(indices) / sizeof(indices[0])) {
						glDrawElements(GL_TRIANGLE_STRIP, count, GL_UNSIGNED_SHORT, indices);
						count = 0;
					}

					// Degenerate triangle strips
					if (count) {
						int prev = count - 1;

						if (count % 2 == 1) {
							indices[count++] = indices[prev];
						}
						indices[count++] = indices[prev];
						indices[count++] = surf->polys->vbo_start;
					}

					for (v = 0; v < newVerts; ++v) {
						indices[count++] = surf->polys->vbo_start + v;
					}
				}
			}
		}

		if (count) {
			glDrawElements(GL_TRIANGLE_STRIP, count, GL_UNSIGNED_SHORT, indices);
		}
	}

	GLM_ExitBatchedPolyRegion();
	return;
}

void GLM_DrawFlat(model_t* model);

void GLM_DrawWorld(model_t* model)
{
	const qbool use_texture_array = true;

	if (model->texture_array_count) {
		GLM_DrawTexturedWorld(model);
	}
}

void GLM_CreateVAOForWarpPoly(msurface_t* surf)
{
	if (!surf->polys->vbo) {
		int totalVerts = 0;
		int totalPolys = 0;
		int index = 0;
		float* verts;
		glpoly_t* p;

		for (p = surf->polys; p; p = p->next) {
			totalVerts += p->numverts;
			++totalPolys;
		}
		glGenBuffers(1, &surf->polys->vbo);
		glBindBufferExt(GL_ARRAY_BUFFER, surf->polys->vbo);

		verts = Q_malloc(sizeof(float) * (totalVerts + 2 * (totalPolys - 1)) * VERTEXSIZE);
		for (p = surf->polys; p; p = p->next) {
			if (index) {
				// Duplicate previous and next to create triangle strip
				memcpy(&verts[index * VERTEXSIZE], &verts[(index - 1) * VERTEXSIZE], sizeof(float) * VERTEXSIZE);
				++index;
				memcpy(&verts[index * VERTEXSIZE], p->verts, sizeof(float) * VERTEXSIZE);
				++index;
			}

			// Convert triangle fan to strip
			{
				int last = p->numverts - 1;
				int next = 1;

				memcpy(&verts[index * VERTEXSIZE], p->verts, sizeof(float) * VERTEXSIZE);
				++index;
				while (next <= last) {
					memcpy(&verts[index * VERTEXSIZE], &p->verts[next], sizeof(float) * VERTEXSIZE);
					++index;
					++next;

					if (last >= next) {
						memcpy(&verts[index * VERTEXSIZE], &p->verts[last], sizeof(float) * VERTEXSIZE);
						++index;
						--last;
					}
				}
			}
		}
		glBufferDataExt(GL_ARRAY_BUFFER, (totalVerts + 2 * (totalPolys - 1)) * VERTEXSIZE * sizeof(float), verts, GL_STATIC_DRAW);
		Q_free(verts);
	}

	if (!surf->polys->vao) {
		glGenVertexArrays(1, &surf->polys->vao);
		GL_BindVertexArray(surf->polys->vao);
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glEnableVertexAttribArray(2);
		glEnableVertexAttribArray(3);
		glEnableVertexAttribArray(5);
		glBindBufferExt(GL_ARRAY_BUFFER, surf->polys->vbo);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * VERTEXSIZE, (void*) 0);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * VERTEXSIZE, (void*) (sizeof(float) * 3));
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float) * VERTEXSIZE, (void*) (sizeof(float) * 5));
		glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(float) * VERTEXSIZE, (void*) (sizeof(float) * 7));
		glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(float) * VERTEXSIZE, (void*) (sizeof(float) * 10));
	}
}

void GLM_CreateVAOForPoly(glpoly_t *poly)
{
	if (!poly->vbo) {
		glGenBuffers(1, &poly->vbo);
		glBindBufferExt(GL_ARRAY_BUFFER, poly->vbo);
		glBufferDataExt(GL_ARRAY_BUFFER, poly->numverts * VERTEXSIZE * sizeof(float), poly->verts, GL_STATIC_DRAW);
	}

	if (!poly->vao) {
		glGenVertexArrays(1, &poly->vao);
		GL_BindVertexArray(poly->vao);
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glEnableVertexAttribArray(2);
		glEnableVertexAttribArray(3);
		glEnableVertexAttribArray(4);
		glEnableVertexAttribArray(5);
		glBindBufferExt(GL_ARRAY_BUFFER, poly->vbo);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * VERTEXSIZE, (void*) 0);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * VERTEXSIZE, (void*) (sizeof(float) * 3));
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float) * VERTEXSIZE, (void*) (sizeof(float) * 5));
		glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(float) * VERTEXSIZE, (void*) (sizeof(float) * 7));
		glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(float) * VERTEXSIZE, (void*) (sizeof(float) * 9));
		glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(float) * VERTEXSIZE, (void*) (sizeof(float) * 10));
	}
}

void GLM_NewMap(void)
{
}

//draws transparent textures for HL world and nonworld models
void R_DrawAlphaChain(msurface_t* alphachain)
{
	int k;
	msurface_t *s;
	texture_t *t;
	float *v;

	if (!alphachain)
		return;

	GL_AlphaBlendFlags(GL_ALPHATEST_ENABLED);
	for (s = alphachain; s; s = s->texturechain) {
		t = s->texinfo->texture;
		R_RenderDynamicLightmaps(s);

		//bind the world texture
		GL_DisableMultitexture();
		GL_Bind(t->gl_texturenum);

		if (gl_mtexable) {
			GLC_MultitextureLightmap(s->lightmaptexturenum);
		}

		glBegin(GL_POLYGON);
		v = s->polys->verts[0];
		for (k = 0; k < s->polys->numverts; k++, v += VERTEXSIZE) {
			if (gl_mtexable) {
				qglMultiTexCoord2f(GL_TEXTURE0, v[3], v[4]);
				qglMultiTexCoord2f(GL_TEXTURE1, v[5], v[6]);
			}
			else {
				glTexCoord2f(v[3], v[4]);
			}
			glVertex3fv(v);
		}
		glEnd();
	}

	alphachain = NULL;

	GL_AlphaBlendFlags(GL_ALPHATEST_DISABLED);
	GL_DisableMultitexture();
	GL_TextureEnvMode(GL_REPLACE);
}



