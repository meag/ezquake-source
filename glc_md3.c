/*
Copyright (C) 2011 fuh and ezQuake team

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// Code to display MD3 models (classic GL/immediate-mode)

#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "r_aliasmodel_md3.h"
#include "vx_vertexlights.h" 
#include "r_matrix.h"
#include "r_state.h"
#include "r_aliasmodel.h"
#include "glc_local.h"
#include "r_renderer.h"
#include "r_program.h"
#include "tr_types.h"
#include "rulesets.h"

void GLC_SetPowerupShellColor(int layer_no, int effects);
qbool GLC_AliasModelStandardCompile(void);

static void GLC_AliasModelLightPointMD3(float color[4], const entity_t* ent, ezMd3XyzNormal_t* vert1, ezMd3XyzNormal_t* vert2, float lerpfrac)
{
	float l;
	extern cvar_t amf_lighting_vertex, amf_lighting_colour;

	// VULT VERTEX LIGHTING
	if (amf_lighting_vertex.integer && !ent->full_light) {
		vec3_t lc;

		l = VLight_LerpLightByAngles(vert1->normal_lat, vert1->normal_lng, vert2->normal_lat, vert2->normal_lng, lerpfrac, ent->angles[0], ent->angles[1]);
		l *= (ent->shadelight + ent->ambientlight) / 256.0;
		l = min(l, 1);

		if (amf_lighting_colour.integer) {
			int i;
			for (i = 0; i < 3; i++) {
				lc[i] = l * ent->lightcolor[i] / 255;
				lc[i] = min(lc[i], 1);
			}
		}
		else {
			VectorSet(lc, l, l, l);
		}

		if (ent->r_modelcolor[0] < 0) {
			// normal color
			VectorCopy(lc, color);
		}
		else {
			color[0] = ent->r_modelcolor[0] * lc[0];
			color[1] = ent->r_modelcolor[1] * lc[1];
			color[2] = ent->r_modelcolor[2] * lc[2];
		}
	}
	else {
		float yaw_rad = ent->angles[YAW] * M_PI / 180.0;
		vec3_t angleVector = { cos(-yaw_rad), sin(yaw_rad), 1 };

		VectorNormalize(angleVector);
		l = FloatInterpolate(DotProduct(angleVector, vert1->normal), lerpfrac, DotProduct(angleVector, vert2->normal));
		l = (l * ent->shadelight + ent->ambientlight) / 256.0;
		l = min(l, 1);

		if (ent->custom_model == NULL) {
			if (ent->r_modelcolor[0] < 0) {
				color[0] = color[1] = color[2] = l;
			}
			else {
				color[0] = ent->r_modelcolor[0] * l;
				color[1] = ent->r_modelcolor[1] * l;
				color[2] = ent->r_modelcolor[2] * l;
			}
		}
		else {
			color[0] = ent->custom_model->color_cvar.color[0] / 255.0f;
			color[1] = ent->custom_model->color_cvar.color[1] / 255.0f;
			color[2] = ent->custom_model->color_cvar.color[2] / 255.0f;
		}
	}

	color[0] *= ent->r_modelalpha;
	color[1] *= ent->r_modelalpha;
	color[2] *= ent->r_modelalpha;
	color[3] = ent->r_modelalpha;
}

static void GLC_DrawMD3Frame(const entity_t* ent, md3Header_t* pheader, int frame1, int frame2, float lerpfrac, const surfinf_t* surface_info, qbool invalidate_texture, qbool outline)
{
	md3Surface_t *surf;
	int surfnum;
	ezMd3XyzNormal_t *verts, *v1, *v2;
	md3St_t *tc;
	unsigned int* tris;
	int numtris, i;
	const int distance = MD3_INTERP_MAXDIST / MD3_XYZ_SCALE;

	MD3_ForEachSurface(pheader, surf, surfnum) {
		// loop through the surfaces.
		int pose1 = frame1 * surf->numVerts;
		int pose2 = frame2 * surf->numVerts;

		if (R_TextureReferenceIsValid(surface_info[surfnum].texnum) && !invalidate_texture) {
			renderer.TextureUnitBind(0, surface_info[surfnum].texnum);
		}

		//skin texture coords.
		tc = MD3_SurfaceTextureCoords(surf);
		verts = MD3_SurfaceVertices(surf);

		tris = (unsigned int*)MD3_SurfaceTriangles(surf);
		numtris = surf->numTriangles * 3;

		GLC_Begin(GL_TRIANGLES);
		for (i = 0; i < numtris; i++) {
			float s, t;
			float vertexColor[4], interpolated_verts[3];

			v1 = verts + *tris + pose1;
			v2 = verts + *tris + pose2;

			s = tc[*tris].s;
			t = tc[*tris].t;

			lerpfrac = VectorL2Compare(v1->xyz, v2->xyz, distance) ? lerpfrac : 1;

			if (!outline) {
				GLC_AliasModelLightPointMD3(vertexColor, ent, v1, v2, lerpfrac);
				R_CustomColor(vertexColor[0], vertexColor[1], vertexColor[2], vertexColor[3]);
			}

			VectorInterpolate(v1->xyz, lerpfrac, v2->xyz, interpolated_verts);
			glTexCoord2f(s, t);
			GLC_Vertex3fv(interpolated_verts);

			tris++;
		}
		GLC_End();

		frameStats.classic.polycount[polyTypeAliasModel] += surf->numTriangles;
	}
}

/*
To draw, for each surface, run through the triangles, getting tex coords from s+t, 
*/
void GLC_DrawAlias3Model(entity_t *ent)
{
	extern cvar_t cl_drawgun, r_viewmodelsize, r_lerpframes, gl_fb_models;
	extern byte	*shadedots;
	extern byte	r_avertexnormal_dots[SHADEDOT_QUANT][NUMVERTEXNORMALS];
	extern void R_AliasSetupLighting(entity_t *ent);

	float lerpfrac;
	float vertexColor[4];

	md3model_t *mhead;
	md3Header_t *pheader;
	model_t *mod;
	int surfnum;
	md3Surface_t *surf;
	qbool invalidate_texture = false;
	extern cvar_t gl_program_aliasmodels, gl_outline;

	int frame1 = ent->oldframe, frame2 = ent->frame;
	qbool outline;
	surfinf_t* sinf;
	float oldMatrix[16];

	mod = ent->model;

	R_PushModelviewMatrix(oldMatrix);
	R_RotateForEntity(ent);
	R_ScaleModelview((ent->renderfx & RF_WEAPONMODEL) ? bound(0.5, r_viewmodelsize.value, 1) : 1, 1, 1);

	// 
	ent->r_modelalpha = ((ent->renderfx & RF_WEAPONMODEL) && gl_mtexable) ? bound(0, cl_drawgun.value, 1) : 1;
	ent->r_modelalpha = (ent->alpha ? ent->alpha : ent->r_modelalpha);
	ent->r_modelcolor[0] = -1;  // by default no solid fill color for model, using texture

	R_AliasSetupLighting(ent);
	shadedots = r_avertexnormal_dots[((int)(ent->angles[1] * (SHADEDOT_QUANT / 360.0))) & (SHADEDOT_QUANT - 1)];

	mhead = (md3model_t *)Mod_Extradata(mod);
	pheader = MD3_HeaderForModel(mhead);

	frame1 = bound(0, frame1, pheader->numFrames - 1);
	frame2 = bound(0, frame2, pheader->numFrames - 1);

	if (!r_lerpframes.value || ent->framelerp < 0 || ent->oldframe == ent->frame) {
		lerpfrac = 1.0;
	}
	else {
		lerpfrac = min(ent->framelerp, 1);
	}

	if (lerpfrac == 1) {
		lerpfrac = 0;
		frame1 = frame2;
	}

	R_AliasModelColor(ent, vertexColor, &invalidate_texture);
	outline = ((gl_outline.integer & 1) && ent->r_modelalpha == 1 && !RuleSets_DisallowModelOutline(ent->model));

	sinf = MD3_ExtraSurfaceInfoForModel(mhead);
	if (ent->skinnum >= 0 && ent->skinnum < pheader->numSkins) {
		sinf += ent->skinnum * pheader->numSurfaces;
	}

	if (gl_program_aliasmodels.integer && buffers.supported && GL_Supported(R_SUPPORT_RENDERING_SHADERS) && GLC_AliasModelStandardCompile()) {
		float angle_radians = -ent->angles[YAW] * M_PI / 180.0;
		vec3_t angle_vector = { cos(angle_radians), sin(angle_radians), 1 };
		int vertsPerFrame = mod->vertsInVBO / pheader->numFrames;
		int first_vert = mod->vbo_start + vertsPerFrame * frame1;
		int vert_index;

		// Temporarily disable caustics
		R_ProgramUse(r_program_aliasmodel_std_glc);
		R_ProgramUniform3fv(r_program_uniform_aliasmodel_std_glc_angleVector, angle_vector);
		R_ProgramUniform1f(r_program_uniform_aliasmodel_std_glc_shadelight, ent->shadelight / 256.0f);
		R_ProgramUniform1f(r_program_uniform_aliasmodel_std_glc_ambientlight, ent->ambientlight / 256.0f);
		R_ProgramUniform1i(r_program_uniform_aliasmodel_std_glc_fsTextureEnabled, invalidate_texture ? 0 : 1);
		R_ProgramUniform1f(r_program_uniform_aliasmodel_std_glc_fsMinLumaMix, 1.0f - (ent->full_light ? bound(0, gl_fb_models.integer, 1) : 0));
		R_ProgramUniform1f(r_program_uniform_aliasmodel_std_glc_fsCausticEffects, 0 /*ent->renderfx & RF_CAUSTICS ? 1 : 0*/);
		R_ProgramUniform1f(r_program_uniform_aliasmodel_std_glc_lerpFraction, lerpfrac);
		R_ProgramUniform1f(r_program_uniform_aliasmodel_std_glc_time, cl.time);

		GLC_StateBeginDrawAliasFrameProgram(sinf->texnum, null_texture_reference, ent->renderfx, ent->custom_model, ent->r_modelalpha);
		R_CustomColor(vertexColor[0], vertexColor[1], vertexColor[2], vertexColor[3]);
		vert_index = first_vert;
		MD3_ForEachSurface(pheader, surf, surfnum) {
			if (R_TextureReferenceIsValid(sinf[surfnum].texnum) && !invalidate_texture) {
				renderer.TextureUnitBind(0, sinf[surfnum].texnum);
			}

			GL_DrawArrays(GL_TRIANGLES, vert_index, 3 * surf->numTriangles);
			vert_index += 3 * surf->numTriangles;
		}
		if (outline) {
			if (ent->renderfx & RF_CAUSTICS) {
				R_ProgramUniform1f(r_program_uniform_aliasmodel_std_glc_fsCausticEffects, 0);
			}
			GLC_StateBeginAliasOutlineFrame();
			vert_index = first_vert;
			MD3_ForEachSurface(pheader, surf, surfnum) {
				GL_DrawArrays(GL_TRIANGLES, vert_index, 3 * surf->numTriangles);
				vert_index += 3 * surf->numTriangles;
			}
		}
		R_ProgramUse(r_program_none);
	}
	else {
		// Immediate mode
		R_ProgramUse(r_program_none);
		GLC_StateBeginMD3Draw(ent->r_modelalpha, R_TextureReferenceIsValid(sinf->texnum) && !invalidate_texture, ent->renderfx & RF_WEAPONMODEL);
		GLC_DrawMD3Frame(ent, pheader, frame1, frame2, lerpfrac, sinf, invalidate_texture, false);

		if (outline) {
			GLC_StateBeginAliasOutlineFrame();
			GLC_DrawMD3Frame(ent, pheader, frame1, frame2, lerpfrac, sinf, true, true);
		}
	}
	R_PopModelviewMatrix(oldMatrix);
}

/*
To draw, for each surface, run through the triangles, getting tex coords from s+t,
*/
void GLC_DrawAlias3ModelPowerupShell(entity_t *ent)
{
	extern cvar_t cl_drawgun, r_viewmodelsize, r_lerpframes, gl_fb_models;
	extern byte	*shadedots;
	extern byte	r_avertexnormal_dots[SHADEDOT_QUANT][NUMVERTEXNORMALS];
	extern void R_AliasSetupLighting(entity_t *ent);

	float lerpfrac;
	int distance = MD3_INTERP_MAXDIST / MD3_XYZ_SCALE;
	vec3_t interpolated_verts;

	md3model_t *mhead;
	md3Header_t *pheader;
	model_t *mod;
	int surfnum, numtris, i;
	md3Surface_t *surf;

	int frame1 = ent->oldframe, frame2 = ent->frame;
	ezMd3XyzNormal_t *verts, *v1, *v2;

	unsigned int	*tris;
	md3St_t *tc;

	//	float ang;
	float oldMatrix[16];
	float scroll[4];
	int layer_no;

	mod = ent->model;
	scroll[0] = cos(cl.time * 1.5);
	scroll[1] = sin(cl.time * 1.1);
	scroll[2] = cos(cl.time * -0.5);
	scroll[3] = sin(cl.time * -0.5);

	R_PushModelviewMatrix(oldMatrix);
	R_RotateForEntity(ent);

	// 
	ent->r_modelalpha = ((ent->renderfx & RF_WEAPONMODEL) && gl_mtexable) ? bound(0, cl_drawgun.value, 1) : 1;
	ent->r_modelalpha = ent->alpha ? ent->alpha : ent->r_modelalpha;

	if (ent->renderfx & RF_WEAPONMODEL) {
		R_ScaleModelview(0.5 + bound(0, r_viewmodelsize.value, 1) / 2, 1, 1);
	}

	R_AliasSetupLighting(ent);
	shadedots = r_avertexnormal_dots[((int)(ent->angles[1] * (SHADEDOT_QUANT / 360.0))) & (SHADEDOT_QUANT - 1)];

	mhead = (md3model_t *)Mod_Extradata(mod);
	pheader = (md3Header_t *)((char *)mhead + mhead->md3model);

	frame1 = bound(0, frame1, pheader->numFrames - 1);
	frame2 = bound(0, frame2, pheader->numFrames - 1);

	if (!r_lerpframes.integer || ent->framelerp < 0 || ent->oldframe == ent->frame) {
		lerpfrac = 1.0;
	}
	else {
		lerpfrac = min(ent->framelerp, 1);
	}

	R_ProgramUse(r_program_none);
	GLC_StateBeginAliasPowerupShell(ent->renderfx & RF_WEAPONMODEL);

	for (layer_no = 0; layer_no <= 1; ++layer_no) {
		surf = MD3_FirstSurface(pheader);
		for (surfnum = 0; surfnum < pheader->numSurfaces; surfnum++) {
			// loop through the surfaces.
			int pose1 = frame1 * surf->numVerts;
			int pose2 = frame2 * surf->numVerts;

			//skin texture coords.
			tc = MD3_SurfaceTextureCoords(surf);
			verts = MD3_SurfaceVertices(surf);

			tris = (unsigned int *)((char *)surf + surf->ofsTriangles);
			numtris = surf->numTriangles * 3;

			GLC_SetPowerupShellColor(layer_no, ent->effects);
			GLC_Begin(GL_TRIANGLES);
			for (i = 0; i < numtris; i++) {
				float s, t;
				vec3_t vec1pos, vec2pos;

				v1 = verts + *tris + pose1;
				v2 = verts + *tris + pose2;

				s = tc[*tris].s * 2.0f + scroll[layer_no * 2];
				t = tc[*tris].t * 2.0f + scroll[layer_no * 2 + 1];

				lerpfrac = VectorL2Compare(v1->xyz, v2->xyz, distance) ? lerpfrac : 1;
				VectorAdd(v1->normal, v1->xyz, vec1pos);
				VectorAdd(v2->normal, v2->xyz, vec2pos);
				VectorInterpolate(vec1pos, lerpfrac, vec2pos, interpolated_verts);

				glTexCoord2f(s, t);
				GLC_Vertex3fv(interpolated_verts);

				tris++;
			}
			GLC_End();

			//NEXT!   Getting cocky!
			surf = (md3Surface_t *)((char *)surf + surf->ofsEnd);
		}
	}

	R_PopModelviewMatrix(oldMatrix);
}
