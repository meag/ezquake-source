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

// Alias model (.mdl) processing
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

#ifndef CLIENTONLY
extern cvar_t     maxclients;
#define IsLocalSinglePlayerGame() (com_serveractive && cls.state == ca_active && !cl.deathmatch && maxclients.value == 1)
#else
#define IsLocalSinglePlayerGame() (0)
#endif

static vec3_t    shadevector;
static qbool     full_light;
static float     r_framelerp;
static float     r_lerpdistance;
static int       lastposenum;
static float     apitch;
static float     ayaw;
static vec3_t    vertexlight;
static vec3_t    dlight_color;

float     r_shellcolor[3];
float     r_modelcolor[3];
float     r_modelalpha;
float     shadelight;
float     ambientlight;
custom_model_color_t* custom_model = NULL;

static cvar_t    r_lerpmuzzlehack                    = {"r_lerpmuzzlehack", "1"};
static cvar_t    gl_shaftlight                       = {"gl_shaftlight", "1"};
static cvar_t    gl_powerupshells_effect1level       = {"gl_powerupshells_effect1level", "0.75"};
static cvar_t    gl_powerupshells_base1level         = {"gl_powerupshells_base1level", "0.05"};
static cvar_t    gl_powerupshells_effect2level       = {"gl_powerupshells_effect2level", "0.4"};
static cvar_t    gl_powerupshells_base2level         = {"gl_powerupshells_base2level", "0.1"};

extern vec3_t    lightcolor;
extern float     bubblecolor[NUM_DLIGHTTYPES][4];

extern cvar_t    r_lerpframes;
extern cvar_t    gl_outline;
extern cvar_t    gl_outline_width;

static void GL_DrawAliasOutlineFrame(aliashdr_t *paliashdr, int pose1, int pose2);
static void GL_DrawAliasShadow(aliashdr_t *paliashdr, int posenum);

void GLM_DrawSimpleAliasFrame(aliashdr_t* paliashdr, int pose1, qbool scrolldir);
void R_SetupAliasFrame(maliasframedesc_t *oldframe, maliasframedesc_t *frame, aliashdr_t *paliashdr, qbool mtex, qbool scrolldir, qbool outline);
void R_AliasSetupLighting(entity_t *ent);

custom_model_color_t custom_model_colors[] = {
	// LG beam
	{
		{ "gl_custom_lg_color", "", CVAR_COLOR },
		{ "gl_custom_lg_fullbright", "1" },
		&amf_lightning,
		MOD_THUNDERBOLT
	},
	// Rockets
	{
		{ "gl_custom_rocket_color", "", CVAR_COLOR },
		{ "gl_custom_rocket_fullbright", "1" },
		NULL,
		MOD_ROCKET
	},
	// Grenades
	{
		{ "gl_custom_grenade_color", "", CVAR_COLOR },
		{ "gl_custom_grenade_fullbright", "1" },
		NULL,
		MOD_GRENADE
	},
	// Spikes
	{
		{ "gl_custom_spike_color", "", CVAR_COLOR },
		{ "gl_custom_spike_fullbright", "1" },
		&amf_part_spikes,
		MOD_SPIKE
	}
};

// precalculated dot products for quantized angles
byte      r_avertexnormal_dots[SHADEDOT_QUANT][NUMVERTEXNORMALS] =
#include "anorm_dots.h"
;
float     r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};
byte      *shadedots = r_avertexnormal_dots[0];

extern vec3_t lightspot;
extern cvar_t gl_meshdraw;

void R_DrawPowerupShell(
	int effects, int layer_no, float base_level, float effect_level,
	maliasframedesc_t *oldframe, maliasframedesc_t *frame, aliashdr_t *paliashdr
)
{
	base_level = bound(0, base_level, 1);
	effect_level = bound(0, effect_level, 1);

	r_shellcolor[0] = r_shellcolor[1] = r_shellcolor[2] = base_level;

	if (effects & EF_RED)
		r_shellcolor[0] += effect_level;
	if (effects & EF_GREEN)
		r_shellcolor[1] += effect_level;
	if (effects & EF_BLUE)
		r_shellcolor[2] += effect_level;

	GL_DisableMultitexture();
	GL_TextureEnvMode(GL_MODULATE);
	R_SetupAliasFrame (oldframe, frame, paliashdr, false, layer_no == 1, false);
}

int GL_GenerateShellTexture(void)
{
	int x, y, d;
	byte data[32][32][4];
	for (y = 0;y < 32;y++)
	{
		for (x = 0;x < 32;x++)
		{
			d = (sin(x * M_PI / 8.0f) + cos(y * M_PI / 8.0f)) * 64 + 64;
			if (d < 0)
				d = 0;
			if (d > 255)
				d = 255;
			data[y][x][0] = data[y][x][1] = data[y][x][2] = d;
			data[y][x][3] = 255;
		}
	}

	return GL_LoadTexture("shelltexture", 32, 32, &data[0][0][0], TEX_MIPMAP, 4);
}

static qbool IsFlameModel(model_t* model)
{
	return (!strcmp(model->name, "progs/flame.mdl") ||
		!strcmp(model->name, "progs/flame0.mdl") ||
		!strcmp(model->name, "progs/flame3.mdl"));
}

static void R_RenderAliasModel(
	model_t* model, aliashdr_t *paliashdr, byte *color32bit, int local_skincolormode, 
	GLuint texture, GLuint fb_texture, maliasframedesc_t* oldframe, maliasframedesc_t* frame, qbool outline
)
{
	int i;

	r_modelcolor[0] = -1;  // by default no solid fill color for model, using texture

	if (gl_meshdraw.integer) {
		glDisable(GL_CULL_FACE);
	}
	if (color32bit) {
		static GLenum modes[] = { GL_MODULATE, GL_REPLACE, GL_BLEND, GL_DECAL, GL_ADD, GL_MODULATE };

		// force some color for such model
		for (i = 0; i < 3; i++) {
			r_modelcolor[i] = (float)color32bit[i] / 255.0;
			r_modelcolor[i] = bound(0, r_modelcolor[i], 1);
		}

		// particletexture is just solid white texture
		GL_DisableMultitexture();
		GL_Bind(local_skincolormode ? texture : particletexture);

		// we may use different methods for filling model surfaces, mixing(modulate), replace, add etc..
		GL_TextureEnvMode(modes[bound(0, local_skincolormode, sizeof(modes) / sizeof(modes[0]) - 1)]);

		R_SetupAliasFrame(oldframe, frame, paliashdr, false, false, outline);

		r_modelcolor[0] = -1;  // by default no solid fill color for model, using texture
	}
	else {
		if (fb_texture && gl_mtexable) {
			GL_DisableMultitexture();
			GL_Bind(texture);
			GL_TextureEnvMode(GL_MODULATE);

			GL_EnableMultitexture();
			GL_Bind(fb_texture);
			GL_TextureEnvMode(GL_DECAL);

			R_SetupAliasFrame(oldframe, frame, paliashdr, true, false, outline);

			GL_DisableMultitexture();
		}
		else {
			GL_DisableMultitexture();
			GL_Bind(texture);
			GL_TextureEnvMode(GL_MODULATE);

			R_SetupAliasFrame(oldframe, frame, paliashdr, false, false, outline);

			if (fb_texture) {
				GL_TextureEnvMode(GL_REPLACE);
				GL_Bind(fb_texture);

				GL_AlphaBlendFlags(GL_BLEND_ENABLED);
				R_SetupAliasFrame(oldframe, frame, paliashdr, false, false, false);
				GL_AlphaBlendFlags(GL_BLEND_DISABLED);
			}
		}
	}

	if (gl_meshdraw.integer) {
		glEnable(GL_CULL_FACE);
	}
}

void R_DrawAliasModel(entity_t *ent)
{
	int i, anim, skinnum, texture, fb_texture, playernum = -1, local_skincolormode;
	float scale;
	vec3_t mins, maxs;
	aliashdr_t *paliashdr;
	model_t *clmodel;
	maliasframedesc_t *oldframe, *frame;
	cvar_t *cv = NULL;
	byte *color32bit = NULL;
	qbool outline = false;
	float oldMatrix[16];
	extern	cvar_t r_viewmodelsize, cl_drawgun;
	qbool is_player_model = (ent->model->modhint == MOD_PLAYER || ent->renderfx & RF_PLAYERMODEL);

	// VULT NAILTRAIL - Hidenails
	if (amf_hidenails.value && currententity->model->modhint == MOD_SPIKE) {
		return;
	}

	// VULT ROCKETTRAILS - Hide rockets
	if (amf_hiderockets.value && currententity->model->flags & EF_ROCKET) {
		return;
	}

	// VULT CAMERAS - Show/Hide playermodel
	if (currententity->alpha == -1) {
		if (cameratype == C_NORMAL) {
			return;
		}
		else {
			currententity->alpha = 1;
		}
	}
	// VULT MOTION TRAILS
	if (currententity->alpha < 0) {
		return;
	}

	// Handle flame/flame0 model changes
	if (qmb_initialized) {
		if (!amf_part_fire.value && currententity->model->modhint == MOD_FLAME0) {
			currententity->model = cl.model_precache[cl_modelindices[mi_flame]];
		}
		else if (amf_part_fire.value) {
			if (currententity->model->modhint == MOD_FLAME0) {
				if (!ISPAUSED) {
					ParticleFire(currententity->origin);
				}
			}
			else if (!strcmp(currententity->model->name, "progs/flame.mdl") && cl_flame0_model) {
				// do we have progs/flame0.mdl?
				if (!ISPAUSED) {
					ParticleFire(currententity->origin);
				}
				currententity->model = cl_flame0_model;
			}
			else if (!strcmp(currententity->model->name, "progs/flame2.mdl") || !strcmp(currententity->model->name, "progs/flame3.mdl")) {
				if (!ISPAUSED) {
					ParticleFire(currententity->origin);
				}
				return;
			}
		}
	}

	local_skincolormode = r_skincolormode.integer;

	VectorCopy (ent->origin, r_entorigin);
	VectorSubtract (r_origin, r_entorigin, modelorg);

	//TODO: use modhints here? 
	//VULT CORONAS	
	if (amf_coronas.value && IsFlameModel(ent->model)) {
		//FIXME: This is slow and pathetic as hell, really we should just check the entity
		//alternativley add some kind of permanent client side TE for the torch
		NewStaticLightCorona(C_FIRE, ent->origin, ent);
	}

	if (ent->model->modhint == MOD_TELEPORTDESTINATION && amf_coronas.value) {
		NewStaticLightCorona (C_LIGHTNING, ent->origin, ent);
	}

	clmodel = ent->model;
	paliashdr = (aliashdr_t *) Mod_Extradata (ent->model);	//locate the proper data

	if (ent->frame >= paliashdr->numframes || ent->frame < 0) {
		if (ent->model->modhint != MOD_EYES) {
			Com_DPrintf("R_DrawAliasModel: no such frame %d\n", ent->frame);
		}

		ent->frame = 0;
	}
	if (ent->oldframe >= paliashdr->numframes || ent->oldframe < 0) {
		if (ent->model->modhint != MOD_EYES) {
			Com_DPrintf("R_DrawAliasModel: no such oldframe %d\n", ent->oldframe);
		}

		ent->oldframe = 0;
	}

	frame = &paliashdr->frames[ent->frame];
	oldframe = &paliashdr->frames[ent->oldframe];

	r_framelerp = min(ent->framelerp, 1);
	if (!r_lerpframes.value || ent->framelerp < 0 || ent->oldframe == ent->frame) {
		r_framelerp = 1.0;
	}

	//culling
	if (!(ent->renderfx & RF_WEAPONMODEL)) {
		if (ent->angles[0] || ent->angles[1] || ent->angles[2]) {
			if (R_CullSphere(ent->origin, max(oldframe->radius, frame->radius))) {
				return;
			}
		}
		else {
			if (r_framelerp == 1) {	
				VectorAdd(ent->origin, frame->bboxmin, mins);
				VectorAdd(ent->origin, frame->bboxmax, maxs);
			}
			else {
				for (i = 0; i < 3; i++) {
					mins[i] = ent->origin[i] + min (oldframe->bboxmin[i], frame->bboxmin[i]);
					maxs[i] = ent->origin[i] + max (oldframe->bboxmax[i], frame->bboxmax[i]);
				}
			}
			if (R_CullBox(mins, maxs)) {
				return;
			}
		}
	}

	GL_EnableFog();

	//get lighting information
	R_AliasSetupLighting(ent);
	shadedots = r_avertexnormal_dots[((int) (ent->angles[1] * (SHADEDOT_QUANT / 360.0))) & (SHADEDOT_QUANT - 1)];

	//draw all the triangles
	c_alias_polys += paliashdr->numtris;
	GL_PushMatrix(GL_MODELVIEW, oldMatrix);
	R_RotateForEntity (ent);

	if (clmodel->modhint == MOD_EYES) {
		GL_Translate(GL_MODELVIEW, paliashdr->scale_origin[0], paliashdr->scale_origin[1], paliashdr->scale_origin[2] - (22 + 8));
		// double size of eyes, since they are really hard to see in gl
		GL_Scale(GL_MODELVIEW, paliashdr->scale[0] * 2, paliashdr->scale[1] * 2, paliashdr->scale[2] * 2);
	}
	else if (ent->renderfx & RF_WEAPONMODEL) {
		scale = 0.5 + bound(0, r_viewmodelsize.value, 1) / 2;
		GL_Translate(GL_MODELVIEW, paliashdr->scale_origin[0], paliashdr->scale_origin[1], paliashdr->scale_origin[2]);
		GL_Scale(GL_MODELVIEW, paliashdr->scale[0] * scale, paliashdr->scale[1], paliashdr->scale[2]);
	}
	else {
		GL_Translate(GL_MODELVIEW, paliashdr->scale_origin[0], paliashdr->scale_origin[1], paliashdr->scale_origin[2]);
		GL_Scale(GL_MODELVIEW, paliashdr->scale[0], paliashdr->scale[1], paliashdr->scale[2]);
	}

	anim = (int) (r_refdef2.time * 10) & 3;
	skinnum = ent->skinnum;
	if (skinnum >= paliashdr->numskins || skinnum < 0) {
		Com_DPrintf ("R_DrawAliasModel: no such skin # %d\n", skinnum);
		skinnum = 0;
	}

	texture = paliashdr->gl_texturenum[skinnum][anim];
	fb_texture = paliashdr->fb_texturenum[skinnum][anim];

	r_modelalpha = ((ent->renderfx & RF_WEAPONMODEL) && gl_mtexable) ? bound(0, cl_drawgun.value, 1) : 1;
	//VULT MOTION TRAILS
	if (ent->alpha) {
		r_modelalpha = ent->alpha;
	}

	if (ent->scoreboard) {
		playernum = ent->scoreboard - cl.players;
	}

	// we can't dynamically colormap textures, so they are cached separately for the players.  Heads are just uncolored.
	if (!gl_nocolors.value) {
		if (playernum >= 0 && playernum < MAX_CLIENTS) {
			if (!ent->scoreboard->skin) {
				CL_NewTranslation(playernum);
			}
			texture    = playernmtextures[playernum];
			fb_texture = playerfbtextures[playernum];
		}
	}
	if (full_light || !gl_fb_models.value) {
		fb_texture = 0;
	}

	if (gl_smoothmodels.value) {
		glShadeModel(GL_SMOOTH);
	}

	if (gl_affinemodels.value) {
		glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
	}

	if (is_player_model && playernum >= 0 && playernum < MAX_CLIENTS) {
		if (cl.teamplay && strcmp(cl.players[playernum].team, TP_SkinForcingTeam()) == 0) {
			cv = &r_teamskincolor;
		}
		else {
			cv = &r_enemyskincolor;
		}

		if (ISDEAD(ent->frame) && r_skincolormodedead.integer != -1) {
			local_skincolormode = r_skincolormodedead.integer;
		}
	}

	if (cv && cv->string[0]) {
		color32bit = cv->color;
	}

	// Check for outline on models.
	// We don't support outline for transparent models,
	// and we also check for ruleset, since we don't want outline on eyes.
	outline = ((gl_outline.integer & 1) && r_modelalpha == 1 && !RuleSets_DisallowModelOutline(clmodel));

	R_RenderAliasModel(clmodel, paliashdr, color32bit, local_skincolormode, texture, fb_texture, oldframe, frame, outline);

	// FIXME: think need put it after caustics
	if (bound(0, gl_powerupshells.value, 1))
	{
		// always allow powerupshells for specs or demos.
		// do not allow powerupshells for eyes in other cases
		if ( ( cls.demoplayback || cl.spectator ) || ent->model->modhint != MOD_EYES )
		{
			if ((ent->effects & EF_RED) || (ent->effects & EF_GREEN) || (ent->effects & EF_BLUE)) {
				R_DrawPowerupShell(ent->effects, 0, gl_powerupshells_base1level.value,
					gl_powerupshells_effect1level.value, oldframe, frame, paliashdr);
				R_DrawPowerupShell(ent->effects, 1, gl_powerupshells_base2level.value,
					gl_powerupshells_effect2level.value, oldframe, frame, paliashdr);
			}

			memset(r_shellcolor, 0, sizeof(r_shellcolor));
		}
	}

	// Underwater caustics on alias models of QRACK -->
#define GL_RGB_SCALE 0x8573

	// MEAG: GLM-FIXME
	if (!GL_ShadersSupported() && (gl_caustics.value) && (underwatertexture && gl_mtexable && ISUNDERWATER(TruePointContents(ent->origin))))
	{
		GL_EnableMultitexture ();
		glBindTexture (GL_TEXTURE_2D, underwatertexture);

		glMatrixMode (GL_TEXTURE);
		glLoadIdentity ();
		glScalef (0.5, 0.5, 1);
		glRotatef (r_refdef2.time * 10, 1, 0, 0);
		glRotatef (r_refdef2.time * 10, 0, 1, 0);
		glMatrixMode (GL_MODELVIEW);

		GL_Bind (underwatertexture);

		GL_TextureEnvMode(GL_DECAL);        
		glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
		GL_AlphaBlendFlags(GL_BLEND_ENABLED);

		R_SetupAliasFrame (oldframe, frame, paliashdr, true, false, false);

		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		GL_AlphaBlendFlags(GL_BLEND_DISABLED);

		GL_SelectTexture(GL_TEXTURE1);
		//glTexEnvi (GL_TEXTURE_ENV, GL_RGB_SCALE, 1); FIXME
		GL_TextureEnvMode(GL_REPLACE);
		glDisable (GL_TEXTURE_2D);

		glMatrixMode (GL_TEXTURE);
		glLoadIdentity ();
		glMatrixMode (GL_MODELVIEW);

		GL_DisableMultitexture ();
	}
	// <-- Underwater caustics on alias models of QRACK

	glShadeModel (GL_FLAT);
	if (gl_affinemodels.value) {
		glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
	}

	GL_PopMatrix(GL_MODELVIEW, oldMatrix);

	// MEAG: TODO
	//VULT MOTION TRAILS - No shadows on motion trails
	if (!GL_ShadersSupported()) {
		if ((r_shadows.value && !full_light && !(ent->renderfx & RF_NOSHADOW)) && !ent->alpha) {
			float theta;
			static float shadescale = 0;

			if (!shadescale) {
				shadescale = 1 / sqrt(2);
			}
			theta = -ent->angles[1] / 180 * M_PI;

			VectorSet(shadevector, cos(theta) * shadescale, sin(theta) * shadescale, shadescale);

			GL_PushMatrix(GL_MODELVIEW, oldMatrix);
			glTranslatef(ent->origin[0], ent->origin[1], ent->origin[2]);
			glRotatef(ent->angles[1], 0, 0, 1);

			glDisable(GL_TEXTURE_2D);
			GL_AlphaBlendFlags(GL_BLEND_ENABLED);
			glColor4f(0, 0, 0, 0.5);
			GL_DrawAliasShadow(paliashdr, lastposenum);
			glEnable(GL_TEXTURE_2D);
			GL_AlphaBlendFlags(GL_BLEND_DISABLED);
			GL_PopMatrix(GL_MODELVIEW, oldMatrix);
		}
		glColor3ubv (color_white);
	}

	GL_DisableFog();
	return;
}

void GL_DrawPowerupShell(aliashdr_t* paliashdr, int pose, trivertx_t* verts1, trivertx_t* verts2, float lerpfrac, qbool scrolldir)
{
	int *order, count;
	float scroll[2];
	float v[3];
	float shell_size = bound(0, gl_powerupshells_size.value, 20);
	byte color[4];
	int vertIndex = paliashdr->vertsOffset + pose * paliashdr->vertsPerPose;

	// LordHavoc: set the state to what we need for rendering a shell
	if (!shelltexture) {
		shelltexture = GL_GenerateShellTexture();
	}
	GL_Bind(shelltexture);
	GL_AlphaBlendFlags(GL_BLEND_ENABLED);

	if (gl_powerupshells_style.integer) {
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	}
	else {
		glBlendFunc(GL_ONE, GL_ONE);
	}

	if (scrolldir) {
		scroll[0] = cos(cl.time * -0.5); // FIXME: cl.time ????
		scroll[1] = sin(cl.time * -0.5);
	}
	else {
		scroll[0] = cos(cl.time * 1.5);
		scroll[1] = sin(cl.time * 1.1);
	}

	if (GL_ShadersSupported()) {
		color[0] = r_shellcolor[0] * 255;
		color[1] = r_shellcolor[1] * 255;
		color[2] = r_shellcolor[2] * 255;
		color[3] = bound(0, gl_powerupshells.value, 1) * 255;
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

		if (GL_ShadersSupported()) {
			order += 2 * count;

			GLM_DrawShellPoly(drawMode, color, shell_size, paliashdr->vao, vertIndex, count);

			vertIndex += count;
		}
		else {
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
	}

	// LordHavoc: reset the state to what the rest of the renderer expects
	GL_AlphaBlendFlags(GL_BLEND_DISABLED);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void GL_DrawAliasFrame(aliashdr_t *paliashdr, int pose1, int pose2, qbool mtex, qbool scrolldir)
{
	int *order, count;
	vec3_t interpolated_verts;
	float l, lerpfrac;
	trivertx_t *verts1, *verts2;
	//VULT COLOURED MODEL LIGHTS
	int i;
	vec3_t lc;

	if (GL_ShadersSupported()) {
		GLM_DrawSimpleAliasFrame(paliashdr, (r_framelerp >= 0.5) ? pose2 : pose1, scrolldir);
		return;
	}

	lerpfrac = r_framelerp;
	lastposenum = (lerpfrac >= 0.5) ? pose2 : pose1;

	verts2 = verts1 = (trivertx_t *) ((byte *) paliashdr + paliashdr->posedata);

	verts1 += pose1 * paliashdr->poseverts;
	verts2 += pose2 * paliashdr->poseverts;

	order = (int *) ((byte *) paliashdr + paliashdr->commands);

	if (r_shellcolor[0] || r_shellcolor[1] || r_shellcolor[2]) {
		GL_DrawPowerupShell(paliashdr, pose1, verts1, verts2, lerpfrac, scrolldir);
	}
	else {
		if (r_modelalpha < 1) {
			GL_AlphaBlendFlags(GL_BLEND_ENABLED);
		}

		if (custom_model) {
			glDisable(GL_TEXTURE_2D);
			glColor4ub(custom_model->color_cvar.color[0], custom_model->color_cvar.color[1], custom_model->color_cvar.color[2], r_modelalpha * 255);
		}

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

		if (r_modelalpha < 1) {
			GL_AlphaBlendFlags(GL_BLEND_DISABLED);
		}

		if (custom_model) {
			glEnable(GL_TEXTURE_2D);
			custom_model = NULL;
		}
	}
}

void R_SetupAliasFrame(maliasframedesc_t *oldframe, maliasframedesc_t *frame, aliashdr_t *paliashdr, qbool mtex, qbool scrolldir, qbool outline)
{
	int oldpose, pose, numposes;
	float interval;

	oldpose = oldframe->firstpose;
	numposes = oldframe->numposes;
	if (numposes > 1) {
		interval = oldframe->interval;
		oldpose += (int) (r_refdef2.time / interval) % numposes;
	}

	pose = frame->firstpose;
	numposes = frame->numposes;
	if (numposes > 1) {
		interval = frame->interval;
		pose += (int) (r_refdef2.time / interval) % numposes;
	}

	GL_DrawAliasFrame (paliashdr, oldpose, pose, mtex, scrolldir);
	if (outline) {
		GL_DrawAliasOutlineFrame(paliashdr, oldpose, pose);
	}
}

static void GL_DrawAliasShadow(aliashdr_t *paliashdr, int posenum)
{
	int *order, count;
	vec3_t point;
	float lheight = currententity->origin[2] - lightspot[2], height = 1 - lheight;
	trivertx_t *verts;

	if (!GL_ShadersSupported()) {
		return;
	}

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

static void GL_DrawAliasOutlineFrame(aliashdr_t *paliashdr, int pose1, int pose2)
{
	int *order, count;
	vec3_t interpolated_verts;
	float lerpfrac;
	trivertx_t *verts1, *verts2;

	GL_PolygonOffset(1, 1);

	glCullFace(GL_BACK);
	glPolygonMode(GL_FRONT, GL_LINE);

	// limit outline width, since even width == 3 can be considered as cheat.
	glLineWidth(bound(0.1, gl_outline_width.value, 3.0));

	glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
	glEnable(GL_LINE_SMOOTH);
	glDisable(GL_TEXTURE_2D);

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

	glColor4f(1, 1, 1, 1);
	glPolygonMode(GL_FRONT, GL_FILL);
	glDisable(GL_LINE_SMOOTH);
	glCullFace(GL_FRONT);
	glEnable(GL_TEXTURE_2D);

	GL_PolygonOffset(0, 0);
}

void R_AliasSetupLighting(entity_t *ent)
{
	int minlight, lnum;
	float add, fbskins;
	unsigned int i;
	unsigned int j;
	unsigned int k;
	vec3_t dist;
	model_t *clmodel;

	//VULT COLOURED MODEL LIGHTING
	float radiusmax = 0;

	clmodel = ent->model;

	custom_model = NULL;
	for (i = 0; i < sizeof (custom_model_colors) / sizeof (custom_model_colors[0]); ++i) {
		custom_model_color_t* test = &custom_model_colors[i];
		if (test->model_hint == clmodel->modhint) {
			if (test->color_cvar.string[0] && (test->amf_cvar == NULL || test->amf_cvar->integer == 0)) {
				custom_model = &custom_model_colors[i];
			}
			break;
		}
	}

	if (custom_model && custom_model->fullbright_cvar.integer) {
		ambientlight = 4096;
		shadelight = 0;
		full_light = true;
		return;
	}

	// make thunderbolt and torches full light
	if (clmodel->modhint == MOD_THUNDERBOLT) {
		ambientlight = 60 + 150 * bound(0, gl_shaftlight.value, 1);
		shadelight = 0;
		full_light = true;
		return;
	} else if (clmodel->modhint == MOD_FLAME) {
		ambientlight = 255;
		shadelight = 0;
		full_light = true;
		return;
	}

	//normal lighting
	full_light = false;
	ambientlight = shadelight = R_LightPoint (ent->origin);

	/* FIXME: dimman... cache opt from fod */
	//VULT COLOURED MODEL LIGHTS
	if (amf_lighting_colour.value) {
		for (i = 0; i < MAX_DLIGHTS/32; i++) {
			if (cl_dlight_active[i]) {
				for (j = 0; j < 32; j++) {
					if ((cl_dlight_active[i]&(1<<j)) && i*32+j < MAX_DLIGHTS) {
						lnum = i*32 + j;

						VectorSubtract (ent->origin, cl_dlights[lnum].origin, dist);
						add = cl_dlights[lnum].radius - VectorLength(dist);

						if (add > 0) {
							//VULT VERTEX LIGHTING
							if (amf_lighting_vertex.value) {
								if (!radiusmax) {
									radiusmax = cl_dlights[lnum].radius;
									VectorCopy(cl_dlights[lnum].origin, vertexlight);
								}
								else if (cl_dlights[lnum].radius > radiusmax) {
									radiusmax = cl_dlights[lnum].radius;
									VectorCopy(cl_dlights[lnum].origin, vertexlight);
								}
							}

							if (cl_dlights[lnum].type == lt_custom) {
								VectorCopy(cl_dlights[lnum].color, dlight_color);
								VectorScale(dlight_color, (1.0 / 255), dlight_color); // convert color from byte to float
							}
							else {
								VectorCopy(bubblecolor[cl_dlights[lnum].type], dlight_color);
							}

							for (k = 0;k < 3;k++) {
								lightcolor[k] = lightcolor[k] + (dlight_color[k] * add) * 2;
								if (lightcolor[k] > 256) {
									switch (k) {
									case 0:
										lightcolor[1] = lightcolor[1] - (1 * lightcolor[1] / 3);
										lightcolor[2] = lightcolor[2] - (1 * lightcolor[2] / 3);
										break;
									case 1:
										lightcolor[0] = lightcolor[0] - (1 * lightcolor[0] / 3);
										lightcolor[2] = lightcolor[2] - (1 * lightcolor[2] / 3);
										break;
									case 2:
										lightcolor[1] = lightcolor[1] - (1 * lightcolor[1] / 3);
										lightcolor[0] = lightcolor[0] - (1 * lightcolor[0] / 3);
										break;
									}
								}
							}
						}
					}
				}
			}
		}
	}
	else {
		for (i = 0; i < MAX_DLIGHTS/32; i++) {
			if (cl_dlight_active[i]) {
				for (j = 0; j < 32; j++) {
					if ((cl_dlight_active[i]&(1<<j)) && i*32+j < MAX_DLIGHTS) {
						lnum = i*32 + j;

						VectorSubtract (ent->origin, cl_dlights[lnum].origin, dist);
						add = cl_dlights[lnum].radius - VectorLength(dist);

						if (add > 0)
						{
							//VULT VERTEX LIGHTING
							if (amf_lighting_vertex.value)
							{
								if (!radiusmax)
								{
									radiusmax = cl_dlights[lnum].radius;
									VectorCopy(cl_dlights[lnum].origin, vertexlight);
								}
								else if (cl_dlights[lnum].radius > radiusmax)
								{
									radiusmax = cl_dlights[lnum].radius;
									VectorCopy(cl_dlights[lnum].origin, vertexlight);
								}
							}
							ambientlight += add;
						}
					}
				}
			}
		}
	}
	//calculate pitch and yaw for vertex lighting
	if (amf_lighting_vertex.value) {
		vec3_t dist, ang;
		apitch = currententity->angles[0];
		ayaw = currententity->angles[1];

		if (!radiusmax) {
			vlight_pitch = 45;
			vlight_yaw = 45;
		}
		else {
			VectorSubtract(vertexlight, currententity->origin, dist);
			vectoangles(dist, ang);
			vlight_pitch = ang[0];
			vlight_yaw = ang[1];
		}
	}

	// clamp lighting so it doesn't overbright as much
	if (ambientlight > 128) {
		ambientlight = 128;
	}
	if (ambientlight + shadelight > 192) {
		shadelight = 192 - ambientlight;
	}

	// always give the gun some light
	if ((ent->renderfx & RF_WEAPONMODEL) && ambientlight < 24) {
		ambientlight = shadelight = 24;
	}

	// never allow players to go totally black
	if (clmodel->modhint == MOD_PLAYER || ent->renderfx & RF_PLAYERMODEL) {
		if (ambientlight < 8) {
			ambientlight = shadelight = 8;
		}
	}

	if (clmodel->modhint == MOD_PLAYER || ent->renderfx & RF_PLAYERMODEL) {
		fbskins = bound(0, r_fullbrightSkins.value, r_refdef2.max_fbskins);
		if (fbskins == 1 && gl_fb_models.value == 1) {
			ambientlight = shadelight = 4096;
			full_light = true;
		}
		else if (fbskins == 0) {
			ambientlight = max(ambientlight, 8);
			shadelight = max(shadelight, 8);
			full_light = false;
		}
		else if (fbskins) {
			ambientlight = max(ambientlight, 8 + fbskins * 120);
			shadelight = max(shadelight, 8 + fbskins * 120);
			full_light = true;
		}
	}
	else if (
		!((clmodel->modhint == MOD_EYES || clmodel->modhint == MOD_BACKPACK) && strncasecmp(Rulesets_Ruleset(), "default", 7)) &&
		(gl_fb_models.integer == 1 && clmodel->modhint != MOD_GIB && clmodel->modhint != MOD_VMODEL && !IsLocalSinglePlayerGame())
		) {
		ambientlight = shadelight = 4096;
	}

	minlight = cl.minlight;

	if (ambientlight < minlight) {
		ambientlight = shadelight = minlight;
	}
}

void R_DrawViewModel(void)
{
	centity_t *cent;
	static entity_t gun;

	//VULT CAMERA - Don't draw gun in external camera
	if (cameratype != C_NORMAL) {
		return;
	}

	if (!r_drawentities.value || !cl.viewent.current.modelindex) {
		return;
	}

	memset(&gun, 0, sizeof(gun));
	cent = &cl.viewent;
	currententity = &gun;

	if (!(gun.model = cl.model_precache[cent->current.modelindex])) {
		Host_Error("R_DrawViewModel: bad modelindex");
	}

	VectorCopy(cent->current.origin, gun.origin);
	VectorCopy(cent->current.angles, gun.angles);
	gun.colormap = vid.colormap;
	gun.renderfx = RF_WEAPONMODEL | RF_NOSHADOW;
	if (r_lerpmuzzlehack.value) {
		if (cent->current.modelindex != cl_modelindices[mi_vaxe] &&
			cent->current.modelindex != cl_modelindices[mi_vbio] &&
			cent->current.modelindex != cl_modelindices[mi_vgrap] &&
			cent->current.modelindex != cl_modelindices[mi_vknife] &&
			cent->current.modelindex != cl_modelindices[mi_vknife2] &&
			cent->current.modelindex != cl_modelindices[mi_vmedi] &&
			cent->current.modelindex != cl_modelindices[mi_vspan]) {
			gun.renderfx |= RF_LIMITLERP;
			r_lerpdistance = 135;
		}
	}

	gun.effects |= (cl.stats[STAT_ITEMS] & IT_QUAD) ? EF_BLUE : 0;
	gun.effects |= (cl.stats[STAT_ITEMS] & IT_INVULNERABILITY) ? EF_RED : 0;
	gun.effects |= (cl.stats[STAT_ITEMS] & IT_SUIT) ? EF_GREEN : 0;

	gun.frame = cent->current.frame;
	if (cent->frametime >= 0 && cent->frametime <= r_refdef2.time) {
		gun.oldframe = cent->oldframe;
		gun.framelerp = (r_refdef2.time - cent->frametime) * 10;
	}
	else {
		gun.oldframe = gun.frame;
		gun.framelerp = -1;
	}

	// hack the depth range to prevent view model from poking into walls
	glDepthRange(gldepthmin, gldepthmin + 0.3 * (gldepthmax - gldepthmin));

	switch (currententity->model->type) {
	case mod_alias:
		R_DrawAliasModel(currententity);
		break;
	case mod_alias3:
		R_DrawAlias3Model(currententity);
		break;
	default:
		Com_Printf("Not drawing view model of type %i\n", currententity->model->type);
		break;
	}

	glDepthRange(gldepthmin, gldepthmax);
}

void R_InitAliasModelCvars(void)
{
	int i;

	for (i = 0; i < sizeof(custom_model_colors) / sizeof(custom_model_colors[0]); ++i) {
		Cvar_Register(&custom_model_colors[i].color_cvar);
		Cvar_Register(&custom_model_colors[i].fullbright_cvar);
	}

	Cvar_Register(&r_lerpmuzzlehack);
	Cvar_Register(&gl_shaftlight);

	Cvar_Register (&gl_powerupshells_base1level);
	Cvar_Register (&gl_powerupshells_effect1level);
	Cvar_Register (&gl_powerupshells_base2level);
	Cvar_Register (&gl_powerupshells_effect2level);
}
