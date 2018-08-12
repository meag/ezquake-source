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

// Code to display .md3 files

#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "r_aliasmodel_md3.h"
#include "r_aliasmodel.h"
#include "vx_vertexlights.h" 
#include "r_matrix.h"

void GLM_DrawAlias3Model(entity_t* ent)
{
	extern cvar_t cl_drawgun, r_viewmodelsize, r_lerpframes, gl_fb_models;
	extern byte	*shadedots;
	extern byte	r_avertexnormal_dots[SHADEDOT_QUANT][NUMVERTEXNORMALS];
	extern void R_AliasSetupLighting(entity_t* ent);

	float lerpfrac = 1;
	float oldMatrix[16];

	int frame1 = ent->oldframe, frame2 = ent->frame;
	model_t* mod = ent->model;
	md3model_t* md3Model = (md3model_t *)Mod_Extradata(mod);
	surfinf_t* surfaceInfo = MD3_ExtraSurfaceInfoForModel(md3Model);
	md3Header_t* pHeader = MD3_HeaderForModel(md3Model);
	int surfnum;
	md3Surface_t* surf;

	int vertsPerFrame = 0;
	MD3_ForEachSurface(pHeader, surf, surfnum) {
		vertsPerFrame += 3 * surf[surfnum].numTriangles;
	}

	R_PushModelviewMatrix(oldMatrix);
	R_RotateForEntity(ent);
	if ((ent->renderfx & RF_WEAPONMODEL) && r_viewmodelsize.value < 1) {
		// perform scalling for r_viewmodelsize
		R_ScaleModelview(0.5 + bound(0, r_viewmodelsize.value, 1) / 2, 1, 1);
	}

	// 
	r_modelalpha = ((ent->renderfx & RF_WEAPONMODEL) && gl_mtexable) ? bound(0, cl_drawgun.value, 1) : 1;
	if (ent->alpha) {
		r_modelalpha = ent->alpha;
	}

	R_AliasSetupLighting(ent);
	shadedots = r_avertexnormal_dots[((int) (ent->angles[1] * (SHADEDOT_QUANT / 360.0))) & (SHADEDOT_QUANT - 1)];

	if (gl_fb_models.integer) {
		ambientlight = 999999;
	}
	if (frame1 >= pHeader->numFrames) {
		frame1 = pHeader->numFrames - 1;
	}
	if (frame2 >= pHeader->numFrames) {
		frame2 = pHeader->numFrames - 1;
	}

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

	GLM_DrawAliasModelFrame(
		ent, mod, mod->vbo_start + vertsPerFrame * frame1, mod->vbo_start + vertsPerFrame * frame2, vertsPerFrame,
		surfaceInfo[0].texnum, null_texture_reference, false, ent->effects, ent->renderfx
	);

	R_PopModelviewMatrix(oldMatrix);
}
