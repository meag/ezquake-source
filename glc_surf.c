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
// glc_surf.c: classic surface-related refresh code

#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "rulesets.h"
#include "utils.h"

// This is a chain of polys, only used in classic when multi-texturing not available
glpoly_t *fullbright_polys[MAX_GLTEXTURES];
glpoly_t *luma_polys[MAX_GLTEXTURES];

extern glpoly_t *caustics_polys;
extern glpoly_t *detail_polys;

void GLC_ClearLightmapPolys(void);

void GLC_ClearTextureChains(void)
{
	GLC_ClearLightmapPolys();
	memset(fullbright_polys, 0, sizeof(fullbright_polys));
	memset(luma_polys, 0, sizeof(luma_polys));
}

void GLC_DrawMapOutline(model_t *model)
{
	msurface_t *s;
	int waterline, i, k;
	float *v;
	vec3_t n;

	GLC_StateBeginDrawMapOutline();

	for (i = 0; i < model->numtextures; i++) {
		if (!model->textures[i] || (!model->textures[i]->texturechain[0] && !model->textures[i]->texturechain[1]))
			continue;

		for (waterline = 0; waterline < 2; waterline++) {
			if (!(s = model->textures[i]->texturechain[waterline]))
				continue;

			for (; s; s = s->texturechain) {
				v = s->polys->verts[0];
				VectorCopy(s->plane->normal, n);
				VectorNormalize(n);

				glBegin(GL_LINE_LOOP);
				for (k = 0; k < s->polys->numverts; k++, v += VERTEXSIZE) {
					glVertex3fv(v);
				}
				glEnd();
			}
		}
	}
}

void DrawGLPoly(glpoly_t *p)
{
	int i;
	float *v;

	glBegin(GL_POLYGON);
	v = p->verts[0];
	for (i = 0; i < p->numverts; i++, v += VERTEXSIZE) {
		glTexCoord2f(v[3], v[4]);
		glVertex3fv(v);
	}
	glEnd();
}

void GLC_RenderFullbrights(void)
{
	int i;
	glpoly_t *p;
	texture_ref texture;

	GLC_StateBeginRenderFullbrights();

	for (i = 1; i < MAX_GLTEXTURES; i++) {
		if (!fullbright_polys[i]) {
			continue;
		}

		texture.index = i;
		GL_EnsureTextureUnitBound(GL_TEXTURE0, texture);
		for (p = fullbright_polys[i]; p; p = p->fb_chain) {
			DrawGLPoly(p);
		}
		fullbright_polys[i] = NULL;
	}

	GLC_StateEndRenderFullbrights();
}

void GLC_RenderLumas(void)
{
	int i;
	glpoly_t *p;
	texture_ref texture;

	GLC_StateBeginRenderLumas();

	for (i = 1; i < MAX_GLTEXTURES; i++) {
		if (!luma_polys[i]) {
			continue;
		}

		texture.index = i;
		GL_EnsureTextureUnitBound(GL_TEXTURE0, texture);
		for (p = luma_polys[i]; p; p = p->luma_chain) {
			DrawGLPoly(p);
		}
		luma_polys[i] = NULL;
	}

	GLC_StateEndRenderLumas();
}

void EmitDetailPolys(void)
{
	glpoly_t *p;
	int i;
	float *v;

	if (!detail_polys) {
		return;
	}

	GLC_StateBeginEmitDetailPolys();

	for (p = detail_polys; p; p = p->detail_chain) {
		glBegin(GL_POLYGON);
		v = p->verts[0];
		for (i = 0; i < p->numverts; i++, v += VERTEXSIZE) {
			glTexCoord2f(v[7] * 18, v[8] * 18);
			glVertex3fv(v);
		}
		glEnd();
	}

	GLC_StateEndEmitDetailPolys();

	detail_polys = NULL;
}
