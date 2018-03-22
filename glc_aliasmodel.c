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

// Alias model (.mdl) rendering, classic (immediate mode) GL only
// Most code taken from gl_rmain.c

#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "vx_stuff.h"
#include "vx_vertexlights.h"
#include "utils.h"
#include "qsound.h"
#include "hud.h"
#include "hud_common.h"
#include "gl_bloom.h"
#include "rulesets.h"
#include "teamplay.h"
#include "gl_aliasmodel.h"
#include "crc.h"

static void GLC_DrawPowerupShell(aliashdr_t* paliashdr, int pose, trivertx_t* verts1, trivertx_t* verts2, float lerpfrac, qbool scrolldir);
static void GLC_DrawAliasOutlineFrame(model_t* model, int pose1, int pose2);
static void GLC_DrawAliasShadow(aliashdr_t *paliashdr, int posenum, vec3_t shadevector, vec3_t lightspot);

// Which pose to use if shadow to be drawn
static int lastposenum;

extern float r_avertexnormals[NUMVERTEXNORMALS][3];

extern cvar_t    r_lerpframes;
extern cvar_t    gl_outline;
extern cvar_t    gl_outline_width;

extern float     r_framelerp;
extern float     r_lerpdistance;
extern qbool     full_light;
extern vec3_t    lightcolor;
extern float     apitch;
extern float     ayaw;

void GLC_DrawAliasFrame(model_t* model, int pose1, int pose2, qbool mtex, qbool scrolldir, texture_ref texture, texture_ref fb_texture, GLenum textureEnvMode, qbool shells_only, qbool outline)
{
	int *order, count;
	vec3_t interpolated_verts;
	float l, lerpfrac;
	trivertx_t *verts1, *verts2;
	//VULT COLOURED MODEL LIGHTS
	int i;
	vec3_t lc;
	aliashdr_t* paliashdr = (aliashdr_t*)Mod_Extradata(model);

	GLC_StateBeginDrawAliasFrame(textureEnvMode, texture, fb_texture, mtex, r_modelalpha, custom_model, shells_only);

	lerpfrac = r_framelerp;
	lastposenum = (lerpfrac >= 0.5) ? pose2 : pose1;

	verts2 = verts1 = (trivertx_t *) ((byte *) paliashdr + paliashdr->posedata);

	verts1 += pose1 * paliashdr->poseverts;
	verts2 += pose2 * paliashdr->poseverts;

	order = (int *) ((byte *) paliashdr + paliashdr->commands);

	if (shells_only || r_shellcolor[0] || r_shellcolor[1] || r_shellcolor[2]) {
		GLC_DrawPowerupShell(paliashdr, pose1, verts1, verts2, lerpfrac, scrolldir);
	}
	else {
		for ( ; ; ) {
			count = *order++;
			if (!count) {
				break;
			}

			if (count < 0) {
				count = -count;
				glBegin(GL_TRIANGLE_FAN);
			}
			else {
				glBegin(GL_TRIANGLE_STRIP);
			}

			do {
				// texture coordinates come from the draw list
				if (mtex) {
					qglMultiTexCoord2f(GL_TEXTURE0, ((float *)order)[0], ((float *)order)[1]);
					qglMultiTexCoord2f(GL_TEXTURE1, ((float *)order)[0], ((float *)order)[1]);
				}
				else {
					glTexCoord2f(((float *)order)[0], ((float *)order)[1]);
				}

				order += 2;

				if ((currententity->renderfx & RF_LIMITLERP)) {
					lerpfrac = VectorL2Compare(verts1->v, verts2->v, r_lerpdistance) ? r_framelerp : 1;
				}

				// VULT VERTEX LIGHTING
				if (amf_lighting_vertex.value && !full_light) {
					l = VLight_LerpLight(verts1->lightnormalindex, verts2->lightnormalindex, lerpfrac, apitch, ayaw);
				}
				else {
					l = FloatInterpolate(shadedots[verts1->lightnormalindex], lerpfrac, shadedots[verts2->lightnormalindex]) / 127.0;
					l = (l * shadelight + ambientlight) / 256.0;
				}
				l = min(l, 1);

				//VULT COLOURED MODEL LIGHTS
				if (amf_lighting_colour.value && !full_light) {
					for (i = 0;i < 3;i++) {
						lc[i] = lightcolor[i] / 256 + l;
					}

					if (r_modelcolor[0] < 0) {
						glColor4f(lc[0], lc[1], lc[2], r_modelalpha); // normal color
					}
					else {
						glColor4f(r_modelcolor[0] * lc[0], r_modelcolor[1] * lc[1], r_modelcolor[2] * lc[2], r_modelalpha); // forced
					}
				}
				else if (custom_model == NULL) {
					if (r_modelcolor[0] < 0) {
						glColor4f(l, l, l, r_modelalpha); // normal color
					}
					else {
						glColor4f(r_modelcolor[0] * l, r_modelcolor[1] * l, r_modelcolor[2] * l, r_modelalpha); // forced
					}
				}

				VectorInterpolate(verts1->v, lerpfrac, verts2->v, interpolated_verts);
				glVertex3fv(interpolated_verts);

				verts1++;
				verts2++;
			} while (--count);

			glEnd();
		}
	}

	GLC_StateEndDrawAliasFrame();

	if (outline) {
		GLC_DrawAliasOutlineFrame(model, pose1, pose2);
	}
}

static void GLC_DrawAliasOutlineFrame(model_t* model, int pose1, int pose2)
{
	int *order, count;
	vec3_t interpolated_verts;
	float lerpfrac;
	trivertx_t* verts1;
	trivertx_t* verts2;
	aliashdr_t* paliashdr = (aliashdr_t*) Mod_Extradata(model);

	GLC_StateBeginAliasOutlineFrame();

	lerpfrac = r_framelerp;
	lastposenum = (lerpfrac >= 0.5) ? pose2 : pose1;

	verts2 = verts1 = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);

	verts1 += pose1 * paliashdr->poseverts;
	verts2 += pose2 * paliashdr->poseverts;

	order = (int *)((byte *)paliashdr + paliashdr->commands);

	for (;;) {
		count = *order++;

		if (!count) {
			break;
		}

		if (count < 0) {
			count = -count;
			glBegin(GL_TRIANGLE_FAN);
		}
		else {
			glBegin(GL_TRIANGLE_STRIP);
		}

		do {
			order += 2;

			if ((currententity->renderfx & RF_LIMITLERP))
				lerpfrac = VectorL2Compare(verts1->v, verts2->v, r_lerpdistance) ? r_framelerp : 1;

			VectorInterpolate(verts1->v, lerpfrac, verts2->v, interpolated_verts);
			glVertex3fv(interpolated_verts);

			verts1++;
			verts2++;
		} while (--count);

		glEnd();
	}

	GLC_StateEndAliasOutlineFrame();
}

static void GLC_DrawPowerupShell(aliashdr_t* paliashdr, int pose, trivertx_t* verts1, trivertx_t* verts2, float lerpfrac, qbool scrolldir)
{
	int *order, count;
	float scroll[2];
	float v[3];
	float shell_size = bound(0, gl_powerupshells_size.value, 20);
	int vertIndex = paliashdr->vertsOffset + pose * paliashdr->vertsPerPose;

	if (!GL_TextureReferenceIsValid(shelltexture)) {
		return;
	}

	// LordHavoc: set the state to what we need for rendering a shell
	GLC_StateBeginAliasPowerupShell();

	if (scrolldir) {
		scroll[0] = cos(cl.time * -0.5); // FIXME: cl.time ????
		scroll[1] = sin(cl.time * -0.5);
	}
	else {
		scroll[0] = cos(cl.time * 1.5);
		scroll[1] = sin(cl.time * 1.1);
	}

	// get the vertex count and primitive type
	order = (int *)((byte *)paliashdr + paliashdr->commands);
	for (;;) {
		GLenum drawMode = GL_TRIANGLE_STRIP;

		count = *order++;
		if (!count) {
			break;
		}

		if (count < 0) {
			count = -count;
			drawMode = GL_TRIANGLE_FAN;
		}

		// alpha so we can see colour underneath still
		glColor4f(r_shellcolor[0], r_shellcolor[1], r_shellcolor[2], bound(0, gl_powerupshells.value, 1));

		glBegin(drawMode);
		do {
			glTexCoord2f(((float *)order)[0] * 2.0f + scroll[0], ((float *)order)[1] * 2.0f + scroll[1]);

			order += 2;

			v[0] = r_avertexnormals[verts1->lightnormalindex][0] * shell_size + verts1->v[0];
			v[1] = r_avertexnormals[verts1->lightnormalindex][1] * shell_size + verts1->v[1];
			v[2] = r_avertexnormals[verts1->lightnormalindex][2] * shell_size + verts1->v[2];
			v[0] += lerpfrac * (r_avertexnormals[verts2->lightnormalindex][0] * shell_size + verts2->v[0] - v[0]);
			v[1] += lerpfrac * (r_avertexnormals[verts2->lightnormalindex][1] * shell_size + verts2->v[1] - v[1]);
			v[2] += lerpfrac * (r_avertexnormals[verts2->lightnormalindex][2] * shell_size + verts2->v[2] - v[2]);

			glVertex3f(v[0], v[1], v[2]);

			verts1++;
			verts2++;
		} while (--count);
		glEnd();
	}

	GLC_StateEndAliasPowerupShell();
}

void GLC_AliasModelPowerupShell(entity_t* ent, maliasframedesc_t* oldframe, maliasframedesc_t* frame)
{
	// FIXME: think need put it after caustics
	if ((ent->effects & (EF_RED | EF_GREEN | EF_BLUE)) && bound(0, gl_powerupshells.value, 1)) {
		model_t* clmodel = ent->model;

		// always allow powerupshells for specs or demos.
		// do not allow powerupshells for eyes in other cases
		if ((cls.demoplayback || cl.spectator) || ent->model->modhint != MOD_EYES) {
			R_DrawPowerupShell(clmodel, ent->effects, 0, oldframe, frame);
			R_DrawPowerupShell(clmodel, ent->effects, 1, oldframe, frame);

			memset(r_shellcolor, 0, sizeof(r_shellcolor));
		}
	}
}

void GLC_UnderwaterCaustics(entity_t* ent, model_t* clmodel, maliasframedesc_t* oldframe, maliasframedesc_t* frame, aliashdr_t* paliashdr, float scaleS, float scaleT)
{
	// Underwater caustics on alias models of QRACK -->
#define GL_RGB_SCALE 0x8573

	if ((gl_caustics.value) && (GL_TextureReferenceIsValid(underwatertexture) && gl_mtexable && R_PointIsUnderwater(ent->origin))) {
		GLC_StateBeginUnderwaterCaustics();

		R_SetupAliasFrame(clmodel, oldframe, frame, true, false, false, underwatertexture, null_texture_reference, GL_DECAL, scaleS, scaleT, 0, false, false);

		GLC_StateEndUnderwaterCaustics();
	}
	// <-- Underwater caustics on alias models of QRACK
}

void GLC_AliasModelShadow(entity_t* ent, aliashdr_t* paliashdr, vec3_t shadevector, vec3_t lightspot)
{
	float theta;
	float oldMatrix[16];
	static float shadescale = 0;

	if (!shadescale) {
		shadescale = 1 / sqrt(2);
	}
	theta = -ent->angles[1] / 180 * M_PI;

	VectorSet(shadevector, cos(theta) * shadescale, sin(theta) * shadescale, shadescale);

	GL_PushMatrix(GL_MODELVIEW, oldMatrix);
	glTranslatef(ent->origin[0], ent->origin[1], ent->origin[2]);
	glRotatef(ent->angles[1], 0, 0, 1);

	GLC_StateBeginAliasModelShadow();
	GLC_DrawAliasShadow(paliashdr, lastposenum, shadevector, lightspot);
	GLC_StateEndAliasModelShadow();

	GL_PopMatrix(GL_MODELVIEW, oldMatrix);
}

static void GLC_DrawAliasShadow(aliashdr_t *paliashdr, int posenum, vec3_t shadevector, vec3_t lightspot)
{
	int *order, count;
	vec3_t point;
	float lheight = currententity->origin[2] - lightspot[2], height = 1 - lheight;
	trivertx_t *verts;

	verts = (trivertx_t *) ((byte *) paliashdr + paliashdr->posedata);
	verts += posenum * paliashdr->poseverts;
	order = (int *) ((byte *) paliashdr + paliashdr->commands);

	while ((count = *order++)) {
		// get the vertex count and primitive type
		if (count < 0) {
			count = -count;
			glBegin (GL_TRIANGLE_FAN);
		} else {
			glBegin (GL_TRIANGLE_STRIP);
		}

		do {
			//no texture for shadows
			order += 2;

			// normals and vertexes come from the frame list
			point[0] = verts->v[0] * paliashdr->scale[0] + paliashdr->scale_origin[0];
			point[1] = verts->v[1] * paliashdr->scale[1] + paliashdr->scale_origin[1];
			point[2] = verts->v[2] * paliashdr->scale[2] + paliashdr->scale_origin[2];

			point[0] -= shadevector[0] * (point[2] +lheight);
			point[1] -= shadevector[1] * (point[2] + lheight);
			point[2] = height;
			//height -= 0.001;
			glVertex3fv (point);

			verts++;
		} while (--count);

		glEnd ();
	}	
}
