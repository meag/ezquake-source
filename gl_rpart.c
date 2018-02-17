/*
Copyright (C) 2002-2003, Dr Labman, A. Nourai

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

#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "vx_stuff.h"
#include "pmove.h"
#include "utils.h"
#include "qsound.h"
#include "tr_types.h"

//VULT
static float alphatrail_s;
static float alphatrail_l;
static float varray_vertex[16];

#define ABSOLUTE_MIN_PARTICLES				256
#define ABSOLUTE_MAX_PARTICLES				32768

//Better rand nums
#define lhrandom(MIN,MAX) ((rand() & 32767) * (((MAX)-(MIN)) * (1.0f / 32767.0f)) + (MIN))

typedef byte col_t[4];

//VULT PARTICLES
void RainSplash(vec3_t org);
void ParticleStats (int change);
void VX_ParticleTrail (vec3_t start, vec3_t end, float size, float time, col_t color);
void R_PreCalcBeamVerts(vec3_t org1, vec3_t org2, vec3_t right1, vec3_t right2);
void R_CalcBeamVerts(float *vert, const vec3_t org1, const vec3_t org2, const vec3_t right1, const vec3_t right2, float width);

typedef enum {
	p_spark, p_smoke, p_fire, p_bubble, p_lavasplash, p_gunblast, p_chunk, p_shockwave,
	p_inferno_flame, p_inferno_trail, p_sparkray, p_staticbubble, p_trailpart,
	p_dpsmoke, p_dpfire, p_teleflare, p_blood1, p_blood2, p_blood3,
	//VULT PARTICLES
	p_rain,
	p_alphatrail,
	p_railtrail,
	p_streak,
	p_streaktrail,
	p_streakwave,
	p_lightningbeam,
	p_vxblood,
	p_lavatrail,
	p_vxsmoke,
	p_vxsmoke_red,
	p_muzzleflash,
	p_inferno, //VULT - NOT TO BE CONFUSED WITH THE 0.36 FIREBALL
	p_2dshockwave,
	p_vxrocketsmoke,
	p_trailbleed, 
	p_bleedspike, 
	p_flame,
	p_bubble2,
	p_bloodcloud,
	p_chunkdir,
	p_smallspark,
	//[HyperNewbie] - particles!
	p_slimeglow, //slime glow
	p_slimebubble, //slime yellowish growing popping bubble
	p_blacklavasmoke,
	num_particletypes,
} part_type_t;

typedef enum {
	pm_static, pm_normal, pm_bounce, pm_die, pm_nophysics, pm_float,
	//VULT PARTICLES
	pm_rain,
	pm_streak,
	pm_streakwave
} part_move_t;

typedef enum {
	ptex_none, ptex_smoke, ptex_bubble, ptex_generic, ptex_dpsmoke, ptex_lava,
	ptex_blueflare, ptex_blood1, ptex_blood2, ptex_blood3,
	//VULT PARTICLES
	ptex_shockwave,
	ptex_lightning,
	ptex_spark,
	num_particletextures,
} part_tex_t;

typedef enum {
	pd_spark, pd_sparkray, pd_billboard, pd_billboard_vel,
	//VULT PARTICLES
	pd_beam,
	pd_hide,
	pd_normal,
} part_draw_t;

typedef enum {
	BLEND_GL_SRC_ALPHA_GL_ONE_MINUS_SRC_ALPHA,
	BLEND_GL_SRC_ALPHA_GL_ONE,
	BLEND_GL_ONE_GL_ONE,
	BLEND_GL_ZERO_GL_ONE_MINUS_SRC_COLOR_CONSTANT,
	BLEND_GL_ZERO_GL_ONE_MINUS_SRC_COLOR,

	NUMBER_OF_BLEND_TYPES
} part_blend_id;

typedef struct part_blend_info_s {
	//GLenum glSourceFactorOld;
	//GLenum glDestFactorOld;

	GLenum glSourceFactor;
	GLenum glDestFactor;

	qbool premultiply_alpha;
	int fixed_alpha; // -1 = no adjustment
	qbool zero_color;
} part_blend_info_t;

static part_blend_info_t blend_options[NUMBER_OF_BLEND_TYPES] = {
	// BLEND_GL_SRC_ALPHA_GL_ONE_MINUS_SRC_ALPHA
	{ GL_ONE, GL_ONE_MINUS_SRC_ALPHA, true, -1, false },
	// BLEND_GL_SRC_ALPHA_GL_ONE (additive)
	{ GL_ONE, GL_ONE_MINUS_SRC_ALPHA, true, 0, false },
	// BLEND_GL_ONE_GL_ONE,
	{ GL_ONE, GL_ONE_MINUS_SRC_ALPHA, false, 0, false },
	// BLEND_GL_ZERO_GL_ONE_MINUS_SRC_COLOR_CONSTANT
	{ GL_ONE, GL_ONE_MINUS_SRC_ALPHA, false, -1, true },
	// BLEND_GL_ZERO_GL_ONE_MINUS_SRC_COLOR, (this is now for varying color only, can't convert?)
	{ GL_ZERO, GL_ONE_MINUS_SRC_COLOR, false, -1, true }
};

typedef struct particle_s {
	struct particle_s *next;
	vec3_t		org, endorg;
	col_t		color;
	float		growth;		
	vec3_t		vel;
	float		rotangle;
	float		rotspeed;	
	float		size;
	float		start;		
	float		die;		
	byte		hit;		
	byte		texindex;	
	byte		bounces;	
} particle_t;

typedef struct particle_tree_s {
	particle_t	  *start;
	part_type_t	  id;
	part_draw_t	  drawtype;
	part_blend_id blendtype;
	part_tex_t	  texture;
	float		  startalpha;
	float		  grav;
	float		  accel;
	part_move_t	  move;
	float		  custom;

	int           verts_per_primitive;
	billboard_batch_id billboard_type;
} particle_type_t;

typedef struct qmb_particle_vertex_s {
	float position[3];
	float tex[2];
	GLubyte color[4];
} qmb_particle_vertex_t;

#define MAX_BEAM_TRAIL 10

#define	MAX_PTEX_COMPONENTS		8
typedef struct particle_texture_s {
	texture_ref texnum;
	int			components;
	float		coords[MAX_PTEX_COMPONENTS][4];		
} particle_texture_t;

static float sint[7] = {0.000000, 0.781832, 0.974928, 0.433884, -0.433884, -0.974928, -0.781832};
static float cost[7] = {1.000000, 0.623490, -0.222521, -0.900969, -0.900969, -0.222521, 0.623490};

static particle_t *particles, *free_particles;
static particle_type_t particle_types[num_particletypes];
static int particle_type_index[num_particletypes];	
static particle_texture_t particle_textures[num_particletextures];

static int r_numparticles;		
static vec3_t zerodir = {22, 22, 22};
static int particle_count = 0;
static float particle_time;		
static vec3_t trail_stop;

qbool qmb_initialized = false;

static cvar_t gl_clipparticles = {"gl_clipparticles", "1"};
static cvar_t amf_part_fulldetail = {"gl_particle_fulldetail", "0", CVAR_LATCH};
cvar_t gl_bounceparticles = {"gl_bounceparticles", "1"};

static qbool TraceLineN (vec3_t start, vec3_t end, vec3_t impact, vec3_t normal)
{
	trace_t trace = PM_TraceLine (start, end);

	VectorCopy (trace.endpos, impact);

	if (normal)
		VectorCopy (trace.plane.normal, normal);

	if (!trace.allsolid)
		return false;

	if (trace.startsolid)
		return false;

	return true;
}

static byte *ColorForParticle(part_type_t type) {
	int lambda;
	static col_t color;

	switch (type) {
	case p_spark:
		color[0] = 224 + (rand() & 31); color[1] = 100 + (rand() & 31); color[2] = 0;
		break;
	case p_smoke:
		color[0] = color[1] = color[2] = color[3] = 255;
		break;
	case p_fire:
		color[0] = 255; color[1] = 142; color[2] = 62;
		break;
	case p_slimebubble:
	case p_bubble:
	case p_staticbubble:
		color[0] = color[1] = color[2] = 192 + (rand() & 63);
		break;
	case p_teleflare:
	case p_slimeglow:
	case p_lavasplash:
		color[0] = color[1] = color[2] = 128 + (rand() & 127);
		break;
	case p_gunblast:
		color[0]= 224 + (rand() & 31); color[1] = 170 + (rand() & 31); color[2] = 0;
		break;
	case p_chunk:
		color[0] = color[1] = color[2] = (32 + (rand() & 127));
		break;
	case p_shockwave:
		color[0] = color[1] = color[2] = 64 + (rand() & 31);
		break;
	case p_inferno_flame:
	case p_inferno_trail:
		color[0] = 255; color[1] = 77; color[2] = 13;
		break;
	case p_sparkray:
		color[0] = 255; color[1] = 102; color[2] = 25;
		break;
	case p_dpsmoke:
		color[0] = color[1] = color[2] = 48 + (((rand() & 0xFF) * 48) >> 8);
		break;
	case p_dpfire:
		lambda = rand() & 0xFF;
		color[0] = 160 + ((lambda * 48) >> 8); color[1] = 16 + ((lambda * 148) >> 8); color[2] = 16 + ((lambda * 16) >> 8);
		break;
	case p_blood1:
	case p_blood2:
		color[0] = color[1] = color[2] = 180 + (rand() & 63);
		break;
	case p_blood3:
		color[0] = (100 + (rand() & 31)); color[1] = color[2] = 0;
		break;
	case p_smallspark:
		color[0] = color[1] = color[2] = color[3] = 255;	
	default:
		//assert(!"ColorForParticle: unexpected type"); -> hexum - FIXME not all types are handled, seems to work ok though
		break;
	}
	return color;
}

#define ADD_PARTICLE_TEXTURE(_ptex, _texnum, _texindex, _components, _s1, _t1, _s2, _t2)	\
do {																						\
	particle_textures[_ptex].texnum = _texnum;												\
	particle_textures[_ptex].components = _components;										\
	particle_textures[_ptex].coords[_texindex][0] = (_s1 + 1) / 256.0;						\
	particle_textures[_ptex].coords[_texindex][1] = (_t1 + 1) / 512.0;						\
	particle_textures[_ptex].coords[_texindex][2] = (_s2 - 1) / 256.0;						\
	particle_textures[_ptex].coords[_texindex][3] = (_t2 - 1) / 512.0;						\
} while(0);

#define ADD_PARTICLE_TYPE(_id, _drawtype, _blend_type, _texture, _startalpha, _grav, _accel, _move, _custom, _verts_per_primitive) \
do { \
	particle_types[count].id = (_id);                                              \
	particle_types[count].drawtype = (_drawtype);                                  \
	particle_types[count].blendtype = (_blend_type);                               \
	particle_types[count].texture = (_texture);                                    \
	particle_types[count].startalpha = (_startalpha);                              \
	particle_types[count].grav = 9.8 * (_grav);                                    \
	particle_types[count].accel = (_accel);                                        \
	particle_types[count].move = (_move);                                          \
	particle_types[count].custom = (_custom);                                      \
	particle_types[count].verts_per_primitive = (_verts_per_primitive);            \
	particle_types[count].billboard_type = (BILLBOARD_PARTICLES_NEW_ ## _id);      \
	particle_type_index[_id] = count;                                              \
	count++;                                                                       \
} while(0);

static int QMB_CompareParticleType(const void* lhs_, const void* rhs_)
{
	const particle_type_t* lhs = (const particle_type_t*)lhs_;
	const particle_type_t* rhs = (const particle_type_t*)rhs_;
	int comparison;

	comparison = lhs->blendtype - rhs->blendtype;
	comparison = comparison ? comparison : lhs->texture - rhs->texture;

	return comparison;
}

static void QMB_SortParticleTypes(void)
{
	int i;

	// To reduce state changes: Sort by blend mode, then texture
	qsort(particle_types, num_particletypes, sizeof(particle_types[0]), QMB_CompareParticleType);

	// Fix up references
	for (i = 0; i < num_particletypes; ++i) {
		particle_type_index[particle_types[i].id] = i;
	}
}

void QMB_AllocParticles(void)
{
	extern cvar_t r_particles_count;

	r_numparticles = bound(ABSOLUTE_MIN_PARTICLES, r_particles_count.integer, ABSOLUTE_MAX_PARTICLES);

	if (particles || r_numparticles < 1) {
		// seems QMB_AllocParticles() called from wrong place
		Sys_Error("QMB_AllocParticles: internal error");
	}

	// can't alloc on Hunk, using native memory
	particles = (particle_t *)Q_malloc(r_numparticles * sizeof(particle_t));
}

static void QMB_DuplicateForPreMultipliedAlpha(byte* data, int width, int height)
{
	int x, y;

	memcpy(data + width * height * 4, data, width * height * 4);

	// Adjust particle font as we simplified the blending rules...
	for (x = 0; x < width; ++x) {
		for (y = height; y < height * 2; ++y) {
			byte* base = data + (x + y * width) * 4;

			// Pre-multiply alpha
			base[0] = (byte)(((int)base[0] * (int)base[3]) / 255.0);
			base[1] = (byte)(((int)base[1] * (int)base[3]) / 255.0);
			base[2] = (byte)(((int)base[2] * (int)base[3]) / 255.0);
		}
	}
}

static void QMB_LoadTextureSubImage(part_tex_t tex, const char* id, const byte* pixels, byte* temp_buffer, int full_width, int full_height, int texIndex, int components, int sub_x, int sub_y, int sub_x2, int sub_y2)
{
	const int mode = TEX_ALPHA | TEX_COMPLAIN | TEX_NOSCALE | TEX_MIPMAP;
	texture_ref tex_ref;
	int y;
	int width = sub_x2 - sub_x;
	int height = sub_y2 - sub_y;

	width = (width * full_width) / 256;
	height = (height * full_height) / 256;
	sub_x = (sub_x * full_width) / 256;
	sub_y = (sub_y * full_height) / 256;

	for (y = 0; y < height; ++y) {
		memcpy(temp_buffer + y * width * 4, pixels + ((sub_y + y) * full_width + sub_x) * 4, width * 4);
	}

	QMB_DuplicateForPreMultipliedAlpha(temp_buffer, width, height);

	tex_ref = GL_LoadTexture(id, width, height * 2, temp_buffer, mode, 4);

	ADD_PARTICLE_TEXTURE(tex, tex_ref, texIndex, components, 0, 0, 256, 256);
}

static texture_ref QMB_LoadTextureImage(const char* path)
{
	const int mode = TEX_ALPHA | TEX_COMPLAIN | TEX_NOSCALE | TEX_MIPMAP;

	int real_width, real_height;
	byte* original = GL_LoadImagePixels(path, 0, 0, mode, &real_width, &real_height);
	byte* data;
	texture_ref tex;

	if (original == NULL) {
		return null_texture_reference;
	}

	data = Q_malloc(4 * real_width * real_height * 2);
	if (data == NULL) {
		Q_free(original);
		return null_texture_reference;
	}

	// Copy the data twice, once for pre-multiplied and one without
	memcpy(data, original, real_width * real_height * 4);

	QMB_DuplicateForPreMultipliedAlpha(data, real_width, real_height);
	Q_free(original);

	tex = GL_LoadTexture(path, real_width, real_height * 2, data, mode, 4);
	Q_free(data);
	return tex;
}

void QMB_InitParticles (void)
{
	int	i, count = 0;
	texture_ref shockwave_texture, lightning_texture, spark_texture;

    Cvar_Register (&amf_part_fulldetail);
	if (!host_initialized && COM_CheckParm("-detailtrails")) {
		Cvar_LatchedSetValue(&amf_part_fulldetail, 1);
	}

	if (!qmb_initialized) {
		if (!host_initialized) {
			Cvar_SetCurrentGroup(CVAR_GROUP_PARTICLES);
			Cvar_Register(&gl_clipparticles);
			Cvar_Register(&gl_bounceparticles);
			Cvar_ResetCurrentGroup();
		}

		Q_free(particles); // yeah, shit happens, work around
		QMB_AllocParticles();
	}
	else {
		QMB_ClearParticles (); // also re-allocc particles
		qmb_initialized = false; // so QMB particle system will be turned off if we fail to load some texture
	}

	ADD_PARTICLE_TEXTURE(ptex_none, null_texture_reference, 0, 1, 0, 0, 0, 0);

	// Move particle fonts off atlas and into their own textures...
	{
		int real_width, real_height;
		byte* original = GL_LoadImagePixels("textures/particles/particlefont", 0, 0, TEX_ALPHA | TEX_COMPLAIN | TEX_NOSCALE | TEX_MIPMAP, &real_width, &real_height);
		byte* temp_buffer;
		if (!original) {
			return;
		}

		temp_buffer = Q_malloc(real_width * real_height * 2 * 4);  // double height for pre-multiplied alpha version
		QMB_LoadTextureSubImage(ptex_blood1, "qmb:blood1", original, temp_buffer, real_width, real_height, 0, 1, 0, 0, 64, 64);
		QMB_LoadTextureSubImage(ptex_blood2, "qmb:blood2", original, temp_buffer, real_width, real_height, 0, 1, 64, 0, 128, 64);
		QMB_LoadTextureSubImage(ptex_lava, "qmb:lava", original, temp_buffer, real_width, real_height, 0, 1, 128, 0, 192, 64);
		QMB_LoadTextureSubImage(ptex_blueflare, "qmb:blueflare", original, temp_buffer, real_width, real_height, 0, 1, 192, 0, 256, 64);
		QMB_LoadTextureSubImage(ptex_generic, "qmb:generic", original, temp_buffer, real_width, real_height, 0, 1, 0, 96, 96, 192);
		QMB_LoadTextureSubImage(ptex_smoke, "qmb:smoke", original, temp_buffer, real_width, real_height, 0, 1, 96, 96, 192, 192);
		QMB_LoadTextureSubImage(ptex_blood3, "qmb:blood3", original, temp_buffer, real_width, real_height, 0, 1, 192, 96, 256, 160);
		QMB_LoadTextureSubImage(ptex_bubble, "qmb:bubble", original, temp_buffer, real_width, real_height, 0, 1, 192, 160, 224, 192);
		for (i = 0; i < 8; i++) {
			QMB_LoadTextureSubImage(ptex_dpsmoke, va("qmb:smoke%d", i), original, temp_buffer, real_width, real_height, i, 8, i * 32, 64, (i + 1) * 32, 96);
		}
		Q_free(temp_buffer);
		Q_free(original);
	}

	//VULT PARTICLES
	shockwave_texture = QMB_LoadTextureImage("textures/shockwavetex");
	if (!GL_TextureReferenceIsValid(shockwave_texture)) {
		return;
	}
	lightning_texture = QMB_LoadTextureImage("textures/zing1");
	if (!GL_TextureReferenceIsValid(lightning_texture)) {
		return;
	}
	spark_texture = QMB_LoadTextureImage("textures/sparktex");
	if (!GL_TextureReferenceIsValid(spark_texture)) {
		return;
	}
	ADD_PARTICLE_TEXTURE(ptex_shockwave, shockwave_texture, 0, 1, 0, 0, 128, 128);
	ADD_PARTICLE_TEXTURE(ptex_lightning, lightning_texture, 0, 1, 0, 0, 256, 256);
	ADD_PARTICLE_TEXTURE(ptex_spark, spark_texture, 0, 1, 0, 0, 32, 32);

	ADD_PARTICLE_TYPE(p_spark, pd_spark, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_none, 255, -32, 0, pm_bounce, 1.3, 9);
	ADD_PARTICLE_TYPE(p_sparkray, pd_sparkray, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_none, 255, -0, 0, pm_nophysics, 0, 9);
	ADD_PARTICLE_TYPE(p_gunblast, pd_spark, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_none, 255, -16, 0, pm_bounce, 1.3, 9);

	ADD_PARTICLE_TYPE(p_fire, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_generic, 204, 0, -2.95, pm_die, 0, 4);
	ADD_PARTICLE_TYPE(p_chunk, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_generic, 255, -16, 0, pm_bounce, 1.475, 4);
	ADD_PARTICLE_TYPE(p_shockwave, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_generic, 255, 0, -4.85, pm_nophysics, 0, 4);
	ADD_PARTICLE_TYPE(p_inferno_flame, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_generic, 153, 0, 0, pm_static, 0, 4);
	ADD_PARTICLE_TYPE(p_inferno_trail, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_generic, 204, 0, 0, pm_die, 0, 4);
	ADD_PARTICLE_TYPE(p_trailpart, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_generic, 230, 0, 0, pm_static, 0, 4);
	ADD_PARTICLE_TYPE(p_smoke, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_smoke, 140, 3, 0, pm_normal, 0, 4);
	ADD_PARTICLE_TYPE(p_dpfire, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_dpsmoke, 144, 0, 0, pm_die, 0, 4);
	ADD_PARTICLE_TYPE(p_dpsmoke, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_dpsmoke, 85, 3, 0, pm_die, 0, 4);

	ADD_PARTICLE_TYPE(p_teleflare, pd_billboard, BLEND_GL_ONE_GL_ONE, ptex_blueflare, 255, 0, 0, pm_die, 0, 4);
	ADD_PARTICLE_TYPE(p_blood1, pd_billboard, BLEND_GL_ZERO_GL_ONE_MINUS_SRC_COLOR, ptex_blood1, 255, -20, 0, pm_die, 0, 4);
	ADD_PARTICLE_TYPE(p_blood2, pd_billboard_vel, BLEND_GL_ZERO_GL_ONE_MINUS_SRC_COLOR, ptex_blood2, 255, -25, 0, pm_die, 0.018, 4);

	ADD_PARTICLE_TYPE(p_lavasplash, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE_MINUS_SRC_ALPHA, ptex_lava, 170, 0, 0, pm_nophysics, 0, 4);
	ADD_PARTICLE_TYPE(p_blood3, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE_MINUS_SRC_ALPHA, ptex_blood3, 255, -20, 0, pm_normal, 0, 4);
	ADD_PARTICLE_TYPE(p_bubble, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE_MINUS_SRC_ALPHA, ptex_bubble, 204, 8, 0, pm_float, 0, 4);
	ADD_PARTICLE_TYPE(p_staticbubble, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE_MINUS_SRC_ALPHA, ptex_bubble, 204, 0, 0, pm_static, 0, 4);

	//VULT PARTICLES
	ADD_PARTICLE_TYPE(p_rain, pd_hide, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_none, 100, 0, 0, pm_rain, 0, 4);
	ADD_PARTICLE_TYPE(p_alphatrail, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_generic, 100, 0, 0, pm_static, 0, 4);
	ADD_PARTICLE_TYPE(p_railtrail, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_generic, 255, 0, 0, pm_die, 0, 4);

	ADD_PARTICLE_TYPE(p_vxblood, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE_MINUS_SRC_ALPHA, ptex_blood3, 255, -38, 0, pm_normal, 0, 4); //HyperNewbie - Blood does NOT glow like fairy light
	ADD_PARTICLE_TYPE(p_streak, pd_hide, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_none, 255, -64, 0, pm_streak, 1.5, 4); //grav was -64
	ADD_PARTICLE_TYPE(p_streakwave, pd_hide, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_none, 255, 0, 0, pm_streakwave, 0, 4);
	ADD_PARTICLE_TYPE(p_lavatrail, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_generic, 255, 3, 0, pm_normal, 0, 4);
	ADD_PARTICLE_TYPE(p_vxsmoke, pd_billboard, BLEND_GL_ZERO_GL_ONE_MINUS_SRC_COLOR_CONSTANT, ptex_smoke, 140, 3, 0, pm_normal, 0, 4);
	ADD_PARTICLE_TYPE(p_vxsmoke_red, pd_billboard, BLEND_GL_ZERO_GL_ONE_MINUS_SRC_COLOR, ptex_smoke, 140, 3, 0, pm_normal, 0, 4);
	ADD_PARTICLE_TYPE(p_muzzleflash, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_generic, 128, 0, 0, pm_die, 0, 4);
	ADD_PARTICLE_TYPE(p_2dshockwave, pd_normal, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_shockwave, 255, 0, 0, pm_static, 0, 4);
	ADD_PARTICLE_TYPE(p_vxrocketsmoke, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_generic, 128, 0, 0, pm_normal, 0, 4);
	ADD_PARTICLE_TYPE(p_flame, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_generic, 200, 10, 0, pm_die, 0, 4);
	ADD_PARTICLE_TYPE(p_trailbleed, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_generic, 200, 0, 0, pm_static, 0, 4);
	ADD_PARTICLE_TYPE(p_bleedspike, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_generic, 200, 0, 0, pm_static, 0, 4);
	ADD_PARTICLE_TYPE(p_lightningbeam, pd_beam, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_lightning, 128, 0, 0, pm_static, 0, 4);
	ADD_PARTICLE_TYPE(p_bubble2, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE_MINUS_SRC_ALPHA, ptex_bubble, 204, 1, 0, pm_float, 0, 4);
	ADD_PARTICLE_TYPE(p_bloodcloud, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE_MINUS_SRC_ALPHA, ptex_generic, 255, -3, 0, pm_normal, 0, 4);
	ADD_PARTICLE_TYPE(p_chunkdir, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_generic, 255, -32, 0, pm_bounce, 1.475, 4);
	ADD_PARTICLE_TYPE(p_smallspark, pd_beam, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_spark, 255, -64, 0, pm_bounce, 1.5, 4); //grav was -64

	//HyperNewbie particles
	ADD_PARTICLE_TYPE(p_slimeglow, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_lava, 72, 0, 0, pm_nophysics, 0, 4); //Glow
	ADD_PARTICLE_TYPE(p_slimebubble, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE_MINUS_SRC_ALPHA, ptex_bubble, 204, 0, 0, pm_static, 0, 4);
	ADD_PARTICLE_TYPE(p_blacklavasmoke, pd_billboard, BLEND_GL_ZERO_GL_ONE_MINUS_SRC_COLOR_CONSTANT, ptex_smoke, 140, 3, 0, pm_normal, 0, 4);

	//Allow overkill trails
	if (amf_part_fulldetail.integer) {
		ADD_PARTICLE_TYPE(p_streaktrail, pd_billboard, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_generic, 128, 0, 0, pm_static, 0, 4);
	}
	else {
		ADD_PARTICLE_TYPE(p_streaktrail, pd_beam, BLEND_GL_SRC_ALPHA_GL_ONE, ptex_none, 128, 0, 0, pm_die, 0, 4);
	}

	QMB_SortParticleTypes();

	qmb_initialized = true;
}

void QMB_ClearParticles (void)
{
	int	i;

	if (!qmb_initialized) {
		return;
	}

	Q_free (particles);		// free
	QMB_AllocParticles ();	// and alloc again

	particle_count = 0;
	memset(particles, 0, r_numparticles * sizeof(particle_t));
	free_particles = &particles[0];

	for (i = 0; i + 1 < r_numparticles; i++) {
		particles[i].next = &particles[i + 1];
	}
	particles[r_numparticles - 1].next = NULL;

	for (i = 0; i < num_particletypes; i++)
		particle_types[i].start = NULL;

	//VULT STATS
	ParticleCount = 0;
	ParticleCountHigh = 0;
}
/*
static void QMB_SetParticleVertex(int pos, float x, float y, float z, float s, float t, col_t colour)
{
	VectorSet(vertices[pos].position, x, y, z);
	vertices[pos].tex[0] = s;
	vertices[pos].tex[1] = t;
	memcpy(vertices[pos].color, colour, sizeof(vertices[pos].color));
}*/

static void QMB_AdjustColor(col_t input, part_blend_info_t* blending, col_t output)
{
	if (blending->zero_color) {
		//output[0] = output[1] = output[2] = 0;
		output[0] = output[1] = output[2] = 0;
		output[3] = input[3];
	}
	else if (blending->premultiply_alpha) {
		output[0] = (byte)(((int)input[0] * (int)input[3]) / 255.0f);
		output[1] = (byte)(((int)input[1] * (int)input[3]) / 255.0f);
		output[2] = (byte)(((int)input[2] * (int)input[3]) / 255.0f);
		output[3] = input[3];
	}
	else {
		memcpy(output, input, sizeof(output));
	}

	if (blending->fixed_alpha == 0) {
		output[3] = 0;
	}
	else if (blending->fixed_alpha == 1) {
		output[3] = 255;
	}
}

static void QMB_BillboardAddVert(particle_type_t* type, float x, float y, float z, float s, float t, col_t color)
{
	part_blend_info_t* blend = &blend_options[type->blendtype];
	col_t new_color;

	if (blend->premultiply_alpha) {
		t += 0.5;
	}

	QMB_AdjustColor(color, blend, new_color);

	GL_BillboardAddVert(type->billboard_type, x, y, z, s, t, new_color);
}

__inline static void CALCULATE_PARTICLE_BILLBOARD(particle_texture_t * ptex, particle_type_t* type, particle_t * p, vec3_t coord[4], int pos)
{
	vec3_t verts[4];
	float scale = p->size;

	if (!GL_BillboardAddEntry(type->billboard_type, 4)) {
		return;
	}

	if (p->rotspeed)
	{
		matrix3x3_t rotate_matrix;
		Matrix3x3_CreateRotate(rotate_matrix, DEG2RAD(p->rotangle), vpn);

		Matrix3x3_MultiplyByVector(verts[0], (const vec_t (*)[3]) rotate_matrix, coord[0]);
		Matrix3x3_MultiplyByVector(verts[1], (const vec_t (*)[3]) rotate_matrix, coord[1]);
		// do some fast math for verts[2] and verts[3].
		VectorNegate(verts[0], verts[2]);
		VectorNegate(verts[1], verts[3]);

		VectorMA(p->org, scale, verts[0], verts[0]);
		VectorMA(p->org, scale, verts[1], verts[1]);
		VectorMA(p->org, scale, verts[2], verts[2]);
		VectorMA(p->org, scale, verts[3], verts[3]);
	}
	else
	{
		VectorMA(p->org, scale, coord[0], verts[0]);
		VectorMA(p->org, scale, coord[1], verts[1]);
		VectorMA(p->org, scale, coord[2], verts[2]);
		VectorMA(p->org, scale, coord[3], verts[3]);
	}

	QMB_BillboardAddVert(type, verts[0][0], verts[0][1], verts[0][2], ptex->coords[p->texindex][0], ptex->coords[p->texindex][3], p->color);
	QMB_BillboardAddVert(type, verts[1][0], verts[1][1], verts[1][2], ptex->coords[p->texindex][0], ptex->coords[p->texindex][1], p->color);
	QMB_BillboardAddVert(type, verts[2][0], verts[2][1], verts[2][2], ptex->coords[p->texindex][2], ptex->coords[p->texindex][1], p->color);
	QMB_BillboardAddVert(type, verts[3][0], verts[3][1], verts[3][2], ptex->coords[p->texindex][2], ptex->coords[p->texindex][3], p->color);

	return;
}

static void QMB_FillParticleVertexBuffer(void)
{
	vec3_t billboard[4], velcoord[4];
	particle_type_t* pt;
	particle_t* p;
	float oldMatrix[16];
	int i, j, k;
	int l;
	int pos = 0;

	GL_GetMatrix(GL_MODELVIEW, oldMatrix);

	VectorAdd(vup, vright, billboard[2]);
	VectorSubtract(vright, vup, billboard[3]);
	VectorNegate(billboard[2], billboard[0]);
	VectorNegate(billboard[3], billboard[1]);

	for (i = 0; i < num_particletypes; i++) {
		qbool first = true;

		pt = &particle_types[i];

		if (!pt->start) {
			continue;
		}
		if (pt->drawtype == pd_hide) {
			continue;
		}

		//VULT PARTICLES
		switch (pt->drawtype) {
		case pd_beam:
			for (p = pt->start; p; p = p->next) {
				particle_texture_t* ptex = &particle_textures[ptex_lightning];
				vec3_t right1, right2;

				if (particle_time < p->start || particle_time >= p->die) {
					continue;
				}

				if (first) {
					GL_BillboardInitialiseBatch(pt->billboard_type, blend_options[pt->blendtype].glSourceFactor, blend_options[pt->blendtype].glDestFactor, ptex->texnum, GL_TRIANGLE_FAN, true);
					first = false;
				}

				R_PreCalcBeamVerts(p->org, p->endorg, right1, right2);
				for (l = min(amf_part_traildetail.integer, MAX_BEAM_TRAIL); l > 0; l--) {
					if (GL_BillboardAddEntry(pt->billboard_type, 4)) {
						R_CalcBeamVerts(varray_vertex, p->org, p->endorg, right1, right2, p->size / (l * amf_part_trailwidth.value));

						QMB_BillboardAddVert(pt, varray_vertex[0], varray_vertex[1], varray_vertex[2], ptex->coords[p->texindex][2], ptex->coords[p->texindex][1], p->color);
						QMB_BillboardAddVert(pt, varray_vertex[4], varray_vertex[5], varray_vertex[6], ptex->coords[p->texindex][2], ptex->coords[p->texindex][3], p->color);
						QMB_BillboardAddVert(pt, varray_vertex[8], varray_vertex[9], varray_vertex[10], ptex->coords[p->texindex][0], ptex->coords[p->texindex][3], p->color);
						QMB_BillboardAddVert(pt, varray_vertex[12], varray_vertex[13], varray_vertex[14], ptex->coords[p->texindex][0], ptex->coords[p->texindex][1], p->color);
					}
				}
			}
			break;
		case pd_spark:
		case pd_sparkray:
			for (p = pt->start; p; p = p->next) {
				vec3_t neworg;
				float* point;
				GLubyte farColor[4];
				particle_texture_t* ptex = &particle_textures[ptex_none];

				if (particle_time < p->start || particle_time >= p->die) {
					continue;
				}

				if (first) {
					GL_BillboardInitialiseBatch(pt->billboard_type, blend_options[pt->blendtype].glSourceFactor, blend_options[pt->blendtype].glDestFactor, ptex->texnum, GL_TRIANGLE_FAN, true);
					first = false;
				}

				if (GL_BillboardAddEntry(pt->billboard_type, pt->verts_per_primitive)) {
					if (!TraceLineN(p->endorg, p->org, neworg, NULL)) {
						VectorCopy(p->org, neworg);
					}

					point = (pt->drawtype == pd_spark ? p->org : p->endorg);
					farColor[0] = p->color[0] >> 1;
					farColor[1] = p->color[1] >> 1;
					farColor[2] = p->color[2] >> 1;
					farColor[3] = 0;

					QMB_BillboardAddVert(pt, point[0], point[1], point[2], ptex->coords[0][0], ptex->coords[0][1], p->color);
					for (j = 7; j >= 0; j--) {
						vec3_t v;
						for (k = 0; k < 3; k++) {
							if (pt->drawtype == pd_spark) {
								v[k] = p->org[k] - p->vel[k] / 8 + vright[k] * cost[j % 7] * p->size + vup[k] * sint[j % 7] * p->size;
							}
							else {
								v[k] = neworg[k] + vright[k] * cost[j % 7] * p->size + vup[k] * sint[j % 7] * p->size;
							}
						}
						QMB_BillboardAddVert(pt, v[0], v[1], v[2], ptex->coords[0][0], ptex->coords[0][1], farColor);
					}
				}
			}
			break;
		case pd_billboard:
		case pd_billboard_vel:
			{
				int drawncount = 0;
				particle_texture_t* ptex = &particle_textures[pt->texture];

				for (p = pt->start; p; p = p->next) {
					if (particle_time < p->start || particle_time >= p->die) {
						continue;
					}

					if (first) {
						GL_BillboardInitialiseBatch(pt->billboard_type, blend_options[pt->blendtype].glSourceFactor, blend_options[pt->blendtype].glDestFactor, ptex->texnum, GL_TRIANGLE_FAN, true);
						first = false;
					}

					if (pt->drawtype == pd_billboard) {
						if (gl_clipparticles.value) {
							if (drawncount >= 3 && VectorSupCompare(p->org, r_origin, 30)) {
								continue;
							}
							drawncount++;
						}

						CALCULATE_PARTICLE_BILLBOARD(ptex, pt, p, billboard, pos);
					}
					else if (pt->drawtype == pd_billboard_vel) {
						vec3_t up, right;

						VectorCopy(p->vel, up);
						CrossProduct(vpn, up, right);
						VectorNormalizeFast(right);
						VectorScale(up, pt->custom, up);

						VectorAdd(up, right, velcoord[2]);
						VectorSubtract(right, up, velcoord[3]);
						VectorNegate(velcoord[2], velcoord[0]);
						VectorNegate(velcoord[3], velcoord[1]);

						CALCULATE_PARTICLE_BILLBOARD(ptex, pt, p, velcoord, pos);
					}
				}
			}
			break;

			//VULT PARTICLES - This produces the shockwave effect
			//Mind you it can be used for more than that... Decals come to mind first
		case pd_normal:
			{
				particle_texture_t* ptex = &particle_textures[pt->texture];

				for (p = pt->start; p; p = p->next) {
					float vector[4];

					if (particle_time < p->start || particle_time >= p->die) {
						continue;
					}

					if (first) {
						GL_BillboardInitialiseBatch(pt->billboard_type, blend_options[pt->blendtype].glSourceFactor, blend_options[pt->blendtype].glDestFactor, ptex->texnum, GL_TRIANGLE_FAN, true);
						first = false;
					}

					if (GL_BillboardAddEntry(pt->billboard_type, 4)) {
						GLM_TransformMatrix(oldMatrix, p->org[0], p->org[1], p->org[2]);
						GLM_ScaleMatrix(oldMatrix, p->size, p->size, p->size);
						GLM_RotateMatrix(oldMatrix, p->endorg[0], 0, 1, 0);
						GLM_RotateMatrix(oldMatrix, p->endorg[1], 0, 0, 1);
						GLM_RotateMatrix(oldMatrix, p->endorg[2], 1, 0, 0);

						GLM_MultiplyVector3f(oldMatrix, -p->size, -p->size, 0, vector);
						QMB_BillboardAddVert(pt, vector[0], vector[1], vector[2], ptex->coords[0][0], ptex->coords[0][1], p->color);
						GLM_MultiplyVector3f(oldMatrix, p->size, -p->size, 0, vector);
						QMB_BillboardAddVert(pt, vector[0], vector[1], vector[2], ptex->coords[0][2], ptex->coords[0][1], p->color);
						GLM_MultiplyVector3f(oldMatrix, p->size, p->size, 0, vector);
						QMB_BillboardAddVert(pt, vector[0], vector[1], vector[2], ptex->coords[0][2], ptex->coords[0][3], p->color);
						GLM_MultiplyVector3f(oldMatrix, -p->size, p->size, 0, vector);
						QMB_BillboardAddVert(pt, vector[0], vector[1], vector[2], ptex->coords[0][0], ptex->coords[0][3], p->color);

						if (GL_BillboardAddEntry(pt->billboard_type, 4)) {
							GLM_RotateMatrix(oldMatrix, 180, 1, 0, 0);

							GLM_MultiplyVector3f(oldMatrix, -p->size, -p->size, 0, vector);
							QMB_BillboardAddVert(pt, vector[0], vector[1], vector[2], ptex->coords[0][0], ptex->coords[0][1], p->color);
							GLM_MultiplyVector3f(oldMatrix, p->size, -p->size, 0, vector);
							QMB_BillboardAddVert(pt, vector[0], vector[1], vector[2], ptex->coords[0][2], ptex->coords[0][1], p->color);
							GLM_MultiplyVector3f(oldMatrix, p->size, p->size, 0, vector);
							QMB_BillboardAddVert(pt, vector[0], vector[1], vector[2], ptex->coords[0][2], ptex->coords[0][3], p->color);
							GLM_MultiplyVector3f(oldMatrix, -p->size, p->size, 0, vector);
							QMB_BillboardAddVert(pt, vector[0], vector[1], vector[2], ptex->coords[0][0], ptex->coords[0][3], p->color);
						}
					}
				}
			}
			break;
		default:
			assert(!"QMB_DrawParticles: unexpected drawtype");
			break;
		}
	}
}

// TODO: Split up
static void QMB_UpdateParticles(void)
{
	int i, contents;
	float grav, bounce;
	vec3_t oldorg, stop, normal;
	particle_type_t *pt;
	particle_t *p, *kill;

	if (!qmb_initialized)
		return;

	particle_count = 0;
	grav = movevars.gravity / 800.0;

	//VULT PARTICLES
	WeatherEffect();

	for (i = 0; i < num_particletypes; i++) {
		pt = &particle_types[i];

#ifdef _WIN32
		if (pt && ((uintptr_t)pt->start == 1)) {
			/* hack! fixme!
			 * for some reason in some occasions - MS VS 2005 compiler
			 * this address doesn't point to 0 as other unitialized do, but to 0x00000001 */
			pt->start = NULL;
			Com_DPrintf("ERROR: particle_type[%i].start == 1\n", i);
		}
#endif // _WIN32

		if (pt->start) {
			for (p = pt->start; p && p->next; ) {
				kill = p->next;
				if (kill->die <= particle_time) {
					p->next = kill->next;
					kill->next = free_particles;
					free_particles = kill;
					//VULT STATS
					ParticleStats(-1);
				}
				else {
					p = p->next;
				}
			}

			if (pt->start->die <= particle_time) {
				kill = pt->start;
				pt->start = kill->next;
				kill->next = free_particles;
				free_particles = kill;
				//VULT STATS
				ParticleStats(-1);
			}
		}

		for (p = pt->start; p; p = p->next) {
			if (particle_time < p->start)
				continue;

			particle_count++;

			p->size += p->growth * cls.frametime;

			if (p->size <= 0) {
				p->die = 0;
				continue;
			}

			//VULT PARTICLE
			if (pt->id == p_streaktrail || pt->id == p_lightningbeam) {
				p->color[3] = p->bounces * ((p->die - particle_time) / (p->die - p->start));
			}
			else {
				p->color[3] = pt->startalpha * ((p->die - particle_time) / (p->die - p->start));
			}

			p->rotangle += p->rotspeed * cls.frametime;

			if (p->hit) {
				continue;
			}

			//VULT - switched these around so velocity is scaled before gravity is applied
			VectorScale(p->vel, 1 + pt->accel * cls.frametime, p->vel);
			p->vel[2] += pt->grav * grav * cls.frametime;

			switch (pt->move) {
			case pm_static:
				break;
			case pm_normal:
				VectorCopy(p->org, oldorg);
				VectorMA(p->org, cls.frametime, p->vel, p->org);
				if (CONTENTS_SOLID == TruePointContents(p->org)) {
					p->hit = 1;
					VectorCopy(oldorg, p->org);
					VectorClear(p->vel);
				}
				break;
			case pm_float:
				VectorMA(p->org, cls.frametime, p->vel, p->org);
				p->org[2] += p->size + 1;
				contents = TruePointContents(p->org);
				if (!ISUNDERWATER(contents)) {
					p->die = 0;
				}
				p->org[2] -= p->size + 1;
				break;
			case pm_nophysics:
				VectorMA(p->org, cls.frametime, p->vel, p->org);
				break;
			case pm_die:
				VectorMA(p->org, cls.frametime, p->vel, p->org);
				if (CONTENTS_SOLID == TruePointContents(p->org)) {
					p->die = 0;
				}
				break;
			case pm_bounce:
				if (!gl_bounceparticles.value || p->bounces) {
					if (pt->id == p_smallspark)
						VectorCopy(p->org, p->endorg);

					VectorMA(p->org, cls.frametime, p->vel, p->org);
					if (CONTENTS_SOLID == TruePointContents(p->org))
						p->die = 0;
				}
				else {
					VectorCopy(p->org, oldorg);
					if (pt->id == p_smallspark)
						VectorCopy(oldorg, p->endorg);
					VectorMA(p->org, cls.frametime, p->vel, p->org);
					if (CONTENTS_SOLID == TruePointContents(p->org)) {
						if (TraceLineN(oldorg, p->org, stop, normal)) {
							VectorCopy(stop, p->org);
							bounce = -pt->custom * DotProduct(p->vel, normal);
							VectorMA(p->vel, bounce, normal, p->vel);
							p->bounces++;
							if (pt->id == p_smallspark) {
								VectorCopy(stop, p->endorg);
							}
						}
					}
				}
				break;
				//VULT PARTICLES
			case pm_rain:
				VectorCopy(p->org, oldorg);
				VectorMA(p->org, cls.frametime, p->vel, p->org);
				contents = TruePointContents(p->org);
				if (ISUNDERWATER(contents) || contents == CONTENTS_SOLID) {
					if (!amf_weather_rain_fast.value || amf_weather_rain_fast.value == 2) {
						vec3_t rorg;
						VectorCopy(oldorg, rorg);
						//Find out where the rain should actually hit
						//This is a slow way of doing it, I'll fix it later maybe...
						while (1) {
							rorg[2] = rorg[2] - 0.5f;
							contents = TruePointContents(rorg);
							if (contents == CONTENTS_WATER) {
								if (amf_weather_rain_fast.value == 2) {
									break;
								}
								RainSplash(rorg);
								break;
							}
							else if (contents == CONTENTS_SOLID) {
								byte col[3] = { 128,128,128 };
								SparkGen(rorg, col, 3, 50, 0.15);
								break;
							}
						}
						VectorCopy(rorg, p->org);
						VX_ParticleTrail(oldorg, p->org, p->size, 0.2, p->color);
					}
					p->die = 0;
				}
				else {
					VX_ParticleTrail(oldorg, p->org, p->size, 0.2, p->color);
				}
				break;
				//VULT PARTICLES
			case pm_streak:
				VectorCopy(p->org, oldorg);
				VectorMA(p->org, cls.frametime, p->vel, p->org);
				if (CONTENTS_SOLID == TruePointContents(p->org)) {
					if (TraceLineN(oldorg, p->org, stop, normal)) {
						VectorCopy(stop, p->org);
						bounce = -pt->custom * DotProduct(p->vel, normal);
						VectorMA(p->vel, bounce, normal, p->vel);
					}
				}
				VX_ParticleTrail(oldorg, p->org, p->size, 0.2, p->color);
				if (VectorLength(p->vel) == 0) {
					p->die = 0;
				}
				break;
			case pm_streakwave:
				VectorCopy(p->org, oldorg);
				VectorMA(p->org, cls.frametime, p->vel, p->org);
				VX_ParticleTrail(oldorg, p->org, p->size, 0.5, p->color);
				p->vel[0] = 19 * p->vel[0] / 20;
				p->vel[1] = 19 * p->vel[1] / 20;
				p->vel[2] = 19 * p->vel[2] / 20;
				break;
			default:
				assert(!"QMB_UpdateParticles: unexpected pt->move");
				break;
			}
		}
	}
}

void QMB_CalculateParticles(void)
{
	if (!qmb_initialized) {
		return;
	}

	particle_time = r_refdef2.time;

	if (!ISPAUSED) {
		QMB_UpdateParticles();
	}
}
void QMB_DrawParticles(void)
{
	if (!qmb_initialized) {
		return;
	}

	QMB_FillParticleVertexBuffer();
}

#define	INIT_NEW_PARTICLE(_pt, _p, _color, _size, _time)	\
		_p = free_particles;								\
		free_particles = _p->next;							\
		_p->next = _pt->start;								\
		_pt->start = _p;									\
		_p->size = _size;									\
		_p->hit = 0;										\
		_p->start = r_refdef2.time;							\
		_p->die = _p->start + _time;						\
		_p->growth = 0;										\
		_p->rotspeed = 0;									\
		_p->texindex = (rand() % particle_textures[_pt->texture].components);	\
		_p->bounces = 0;									\
		memcpy(_p->color, _color, sizeof(_p->color));		\
		ParticleStats(1);		//VULT PARTICLES


__inline static void AddParticle(part_type_t type, vec3_t org, int count, float size, float time, col_t col, vec3_t dir) {
	byte *color;
	int i, j;
	float tempSize;
	particle_t *p;
	particle_type_t *pt;

	if (!qmb_initialized)
		Sys_Error("QMB particle added without initialization");

	if (size <= 0 || time <= 0)
	{
		Con_DPrintf("AddParticle() failed, type %d, size %f, time %f\n", type, size, time);
		return;
	}

	if (type >= num_particletypes)
		Sys_Error("AddParticle: Invalid type (%d)", type);

	pt = &particle_types[particle_type_index[type]];

	for (i = 0; i < count && free_particles; i++) {
		color = col ? col : ColorForParticle(type);
		INIT_NEW_PARTICLE(pt, p, color, size, time);

		switch (type) {
		case p_spark:
			p->size = 1.175;
			VectorCopy(org, p->org);
			tempSize = size * 2;
			p->vel[0] = (rand() % (int) tempSize) - ((int) tempSize / 2);
			p->vel[1] = (rand() % (int) tempSize) - ((int) tempSize / 2);
			p->vel[2] = (rand() % (int) tempSize) - ((int) tempSize / 3);
			break;
		case p_smoke:
			for (j = 0; j < 3; j++)
				p->org[j] = org[j] + ((rand() & 31) - 16) / 2.0;
			for (j = 0; j < 3; j++)
				p->vel[j] = ((rand() % 10) - 5) / 20.0;
			break;
		case p_fire:
			VectorCopy(org, p->org);
			for (j = 0; j < 3; j++)
				p->vel[j] = ((rand() % 160) - 80) * (size / 25.0);
			break;
		case p_bubble:
			//VULT PARTICLES
			VectorCopy(org, p->org);
			for (j = 0; j < 2; j++)
				p->vel[j] = (rand() % 150)-75;
			p->vel[2] = (rand() % 64) + 24;
			break;
		case p_lavasplash:
			VectorCopy(org, p->org);
			VectorCopy(dir, p->vel);	
			break;
		case p_slimeglow:
			VectorCopy(org, p->org);
			VectorCopy(dir, p->vel);	
			break;
		case p_gunblast:
			p->size = 1;
			VectorCopy(org, p->org);
			p->vel[0] = (rand() & 127) - 64;
			p->vel[1] = (rand() & 127) - 64;
			p->vel[2] = (rand() & 127) - 10;
			break;
		case p_chunk:
			VectorCopy(org, p->org);
			p->vel[0] = (rand() % 40) - 20;
			p->vel[1] = (rand() % 40) - 20;
			p->vel[2] = (rand() % 40) - 5;			
			break;
		case p_shockwave:
			VectorCopy(org, p->org);
			VectorCopy(dir, p->vel);	
			break;
		case p_inferno_trail:
			for (j = 0; j < 3; j++)
				p->org[j] = org[j] + (rand() & 15) - 8;
			for (j = 0; j < 3; j++)
				p->vel[j] = (rand() & 3) - 2;
			p->growth = -1.5;
			break;
		case p_inferno_flame:
			VectorCopy(org, p->org);
			VectorClear(p->vel);
			p->growth = -30;
			break;
		case p_sparkray:
			VectorCopy(org, p->endorg);
			VectorCopy(dir, p->org);	
			for (j = 0; j < 3; j++)
				p->vel[j] = (rand() & 127) - 64;
			p->growth = -16;
			break;
		case p_staticbubble:
			VectorCopy(org, p->org);
			VectorClear(p->vel);
			break;
		case p_slimebubble:
			//HyperNewbie - Rising, popping bubbles
			VectorCopy(org, p->org);
			VectorClear(p->vel);
			p->growth = 7.5;
			break;
		case p_teleflare:
			VectorCopy(org, p->org);
			VectorCopy(dir, p->vel);	
			p->growth = 1.75;
			break;
		case p_blood1:
		case p_blood2:
			for (j = 0; j < 3; j++)
				p->org[j] = org[j] + (rand() & 15) - 8;
			for (j = 0; j < 3; j++)
				p->vel[j] = (rand() & 63) - 32;
			break;
		case p_blood3:
			p->size = size * (rand() % 20) / 5.0;
			VectorCopy(org, p->org);
			for (j = 0; j < 3; j++)
				p->vel[j] = (rand() % 40) - 20;
			break;
		//VULT PARTICLES
		case p_rain:
			p->size = 1;
			VectorCopy(org, p->org);
			p->vel[0] = (rand() % 10 + 85)*size;
			p->vel[1] = (rand() % 10 + 85)*size;
			p->vel[2] = (rand() % -100 - 2000)*(size/3);
			break;
		//VULT PARTICLES
		case p_streaktrail:
		case p_lightningbeam:
			VectorCopy(org, p->org);
			VectorCopy(dir, p->endorg);
			if (amf_part_fulldetail.integer && type == p_streaktrail)
				p->size = size * 3;
			VectorClear(p->vel);
			p->growth = -p->size/time;
			p->bounces = color[3];
			break;
		//VULT PARTICLES
		case p_streak:
		case p_inferno:
		case p_streakwave:
		case p_smallspark:
			VectorCopy(org, p->org);
			VectorCopy(dir, p->vel);
			break;
		//VULT PARTICLES
		case p_vxblood:
			VectorCopy(org, p->org);
			for (j = 0; j < 2; j++)
				p->vel[j] = (rand()%80)-40;
			p->vel[2] = (rand()%65)-15;
			break;
		//VULT PARTICLES
		case p_vxsmoke:
		case p_vxsmoke_red:
			VectorCopy(org, p->org);
			VectorCopy(dir, p->vel);
			p->growth = 4.5;
			p->rotspeed = (rand() & 63) + 96;
			break;
		//VULT PARTICLES
		case p_muzzleflash:
			VectorCopy (org, p->org);
			VectorCopy(dir, p->vel);
			p->growth = -size / time;
			break;
		//VULT PARTICLES
		case p_2dshockwave:
			VectorCopy (org, p->org);
			VectorCopy (dir, p->endorg);
			VectorClear(p->vel);
			p->size = 1;
			p->growth = size;
			break;
		//VULT PARTICLES
		case p_flame:
			VectorCopy(org, p->org);
			p->org[2] += 4;
			p->growth = -p->size/2;
			VectorClear(p->vel);
			for (j = 0; j < 2; j++)
				p->vel[j] = (rand() % 6) - 3;
			break;
		//VULT PARTICLES
		case p_bloodcloud:
			for (j = 0; j < 3; j++)
				p->org[j] = org[j] + (rand() & 30) - 15;
			VectorClear(p->vel);
			p->size = (rand() % (int)size)+10;
			break;
		//HyperNewbie Particles
		case p_blacklavasmoke:
			VectorCopy(org, p->org);
			VectorCopy(dir, p->vel);
			p->growth = 7.5;
			p->rotspeed = (rand() & 63) + 16;
			break;
		case p_chunkdir:
			VectorCopy(org, p->org);
			VectorCopy(dir, p->vel);
			break;
		default:
			assert(!"AddParticle: unexpected type");
			break;
		}
	}
}

__inline static void AddParticleTrail(part_type_t type, vec3_t start, vec3_t end, float size, float time, col_t col) {
	byte *color;
	int i, j,  num_particles;
	float count = 0.0, length, theta = 0.0;
	vec3_t point, delta;
	particle_t *p;
	particle_type_t *pt;
	//VULT PARTICLES - for railtrail
	int loops = 0;
	vec3_t vf, vr, radial;

	if (!qmb_initialized)
		Sys_Error("QMB particle added without initialization");

	if (time == 0 || size == 0.0f) {
		return;
	}

	if (type >= num_particletypes)
		Sys_Error("AddParticle: Invalid type (%d)", type);

	pt = &particle_types[particle_type_index[type]];

	VectorCopy(start, point);
	VectorSubtract(end, start, delta);
	if (!(length = VectorLength(delta)))
		goto done;

	switch(type) {
	case p_alphatrail:
	case p_lavatrail:
	case p_bleedspike:
	case p_bubble:
		count = length / 1.1;
		break;
	case p_bubble2:
		count = length / 5;
		break;
	case p_trailpart:
		count = length / 1.1;
		break; 
	case p_blood3:
		count = length / 8;
		break;
	case p_smoke:
	case p_vxrocketsmoke:
		count = length / 3.8;
		break;
	case p_dpsmoke:
		count = length / 2.5;
		break;
	case p_dpfire:
		count = length / 2.8;
		break;
	//VULT PARTICLES
	case p_railtrail:
		count = length * 1.6;
		if (!(loops = (int) length / 13.0))
			goto done;
		VectorScale(delta, 1 / length, vf);
		VectorVectors(vf, vr, vup);
		break;
	case p_trailbleed:
		count = length / 1.1;
		break;
	default:
		Com_DPrintf("AddParticleTrail: unexpected type %d\n", type);
		break;
	}

	if (!(num_particles = (int) count))
		goto done;

	VectorScale(delta, 1.0 / num_particles, delta);

	for (i = 0; i < num_particles && free_particles; i++) {
		color = col ? col : ColorForParticle(type);
		INIT_NEW_PARTICLE(pt, p, color, size, time);

		switch (type) {
		//VULT PARTICLES
		case p_trailpart:
		case p_alphatrail:
			VectorCopy (point, p->org);
			VectorClear(p->vel);
			p->growth = -size / time;
			break;
		case p_blood3:
			VectorCopy (point, p->org);
			for (j = 0; j < 3; j++)
				p->org[j] += ((rand() & 15) - 8) / 8.0;
			for (j = 0; j < 3; j++)
				p->vel[j] = ((rand() & 15) - 8) / 2.0;
			p->size = size * (rand() % 20) / 10.0;
			p->growth = 6;
			break;
		case p_smoke:
			VectorCopy (point, p->org);
			for (j = 0; j < 3; j++)
				p->org[j] += ((rand() & 7) - 4) / 8.0;
			p->vel[0] = p->vel[1] = 0;
			p->vel[2] = rand() & 3;
			p->growth = 4.5;
			p->rotspeed = (rand() & 63) + 96;
			break;
		case p_dpsmoke:
			VectorCopy (point, p->org);
			for (j = 0; j < 3; j++)
				p->vel[j] = (rand() % 10) - 5;
			p->growth = 3;
			p->rotspeed = (rand() & 63) + 96;
			break;
		case p_dpfire:
			VectorCopy (point, p->org);
			for (j = 0; j < 3; j++)
				p->vel[j] = (rand() % 40) - 20;
			break;
		//VULT PARTICLES
		case p_railtrail:
			theta += loops * 2 * M_PI / count;
			for (j = 0; j < 3; j++)
				radial[j] = vr[j] * cos(theta) + vup[j] * sin(theta);
			VectorMA(point, 2.6, radial, p->org);
			for (j = 0; j < 3; j++)
				p->vel[j] = radial[j]*5;
			break;
		//VULT PARTICLES
		case p_bubble:
		case p_bubble2:
			VectorCopy(point, p->org);
			for (j = 0; j < 3; j++)
				p->vel[j] = (rand() % 10)-5;
			break;
		//VULT PARTICLES
		case p_lavatrail:
			VectorCopy (point, p->org);
			for (j = 0; j < 3; j++)
				p->org[j] += ((rand() & 7) - 4);
			p->vel[0] = p->vel[1] = 0;
			p->vel[2] = rand() & 3;
			break;
		//VULT PARTICLES
		case p_vxrocketsmoke:
			VectorCopy (point, p->org);
			for (j = 0; j < 3; j++)
				p->vel[j] = (rand() % 8)-4;
			break;
		//VULT PARTICLES
		case p_trailbleed:
			p->size = (rand() % (int)size);
			p->growth = -rand()%5;
			VectorCopy (point, p->org);
			break;
		//VULT PARTICLES
		case p_bleedspike:
			size = 9*size/10 ;
			VectorCopy (point, p->org);
			break;
		default:
			//assert(!"AddParticleTrail: unexpected type"); -> hexum - FIXME not all types are handled, seems to work ok though
			break;
		}

		VectorAdd(point, delta, point);
	}
done:
	VectorCopy (point, trail_stop);
}

#define ONE_FRAME_ONLY	(0.0001)

void QMB_ParticleExplosion (vec3_t org) {
	int contents;

	contents = TruePointContents(org);
	if (ISUNDERWATER(contents)) {
		if (r_explosiontype.value != 9) {
			AddParticle(p_fire, org, 12, 14, 0.8, NULL, zerodir);
		}
		AddParticle(p_bubble, org, 6, 3.0, 2.5, NULL, zerodir);
		AddParticle(p_bubble, org, 4, 2.35, 2.5, NULL, zerodir);
		if (r_explosiontype.value != 1) {
			AddParticle(p_spark, org, 50, 100, 0.75, NULL, zerodir);
			AddParticle(p_spark, org, 25, 60, 0.75, NULL, zerodir);
		}
	}
	else {
		if (r_explosiontype.value != 9) { 	
			AddParticle(p_fire, org, 16, 18, 1, NULL, zerodir);
		}
		if (r_explosiontype.value != 1) {
			AddParticle(p_spark, org, 50, 250, 0.925f, NULL, zerodir);
			AddParticle(p_spark,org, 25, 150, 0.925f,  NULL, zerodir);
		}
	}
}

void QMB_RunParticleEffect (vec3_t org, vec3_t dir, int col, int count) {
	col_t color;
	vec3_t neworg;
	int i, blastcount, blastsize, chunkcount, particlecount, bloodcount, z;
	float scale;
	float blasttime;

	count = max(1, count);

	if (col == 73) {			
		bloodcount = Q_rint(count / 28.0);
		bloodcount = bound(2, bloodcount, 8);
		for (i = 0; i < bloodcount; i++) {
			AddParticle(p_blood1, org, 1, 4.5, 2 + (rand() & 15) / 16.0, NULL, zerodir);
			AddParticle(p_blood2, org, 1, 4.5, 2 + (rand() & 15) / 16.0, NULL, zerodir);
		}
		return;
	} else if (col == 225) {	
		VectorClear(color);
		scale = (count > 130) ? 3 : (count > 20) ? 2  : 1;
		scale *= 0.758;
		for (i = 0; i < (count >> 1); i++) {
			neworg[0] = org[0] + scale * ((rand() & 15) - 8);
			neworg[1] = org[1] + scale * ((rand() & 15) - 8);
			neworg[2] = org[2] + scale * ((rand() & 15) - 8);
			color[0] = 50 + (rand() & 63);
			AddParticle(p_blood3, org, 1, 1, 2.3, NULL, zerodir);
		}
		return;
	} else if (col == 20 && count == 30) {	
		color[0] = 51; color[2] = 51; color[1] = 255; color[3] = 255;
		AddParticle(p_chunk, org, 1, 1, 0.75, color, zerodir);  
		AddParticle(p_spark, org, 12 , 75, 0.4, color, zerodir);
		return;
	} else if (col == 226 && count == 20) {	
		color[0] = 230; color[1] = 204; color[2] = 26; color[3] = 255;
		AddParticle(p_chunk, org, 1, 1, 0.75, color, zerodir);  
		AddParticle(p_spark, org, 12 , 75, 0.4, color, zerodir);
		return;
	}

	switch (count) {
	case 10:	
		AddParticle(p_spark, org, 4, 70, 0.9, NULL, zerodir);
		break;
	case 30:	
		AddParticle(p_chunk, org, 10, 1, 4, NULL, zerodir);
		AddParticle(p_spark, org, 8, 105, 0.9, NULL, zerodir);
		break;
	case 20:	
		if (col != 256) {
			AddParticle(p_spark, org, 8, 85, 0.9, NULL, zerodir);
			break;
		}
		/* fall through */
	default:	
		if (count > 130) {
			scale = 2.274;
			blastcount = 50; blastsize = 50; blasttime = 0.4;
			chunkcount = 14;
		} else if (count > 20) {
			scale = 1.516;
			blastcount = 30; blastsize = 35; blasttime = 0.25;
			chunkcount = 7;
		} else {
			scale = 0.758;
			blastcount = 15; blastsize = 5;	blasttime = 0.15;
			chunkcount = 3;
		}
		particlecount = max(1, count >> 1);
		AddParticle(p_gunblast, org, blastcount, blastsize, blasttime, NULL, zerodir);
		for (i = 0; i < particlecount; i++) {
			neworg[0] = org[0] + scale * ((rand() & 15) - 8);
			neworg[1] = org[1] + scale * ((rand() & 15) - 8);
			neworg[2] = org[2] + scale * ((rand() & 15) - 8);

			// col == 256 means it gun shot.
			// So we always USE smoke when it NOT a gun shot, in case of gun shot we DON'T use smoke when gl_part_gunshots == 2
			if (col != 256 || gl_part_gunshots.integer != 2)
				AddParticle(p_smoke, neworg, 1, 4, 0.825f + ((rand() % 10) - 5) / 40.0, NULL, zerodir);

			z = particlecount / chunkcount;
			if (!z || (i % z == 0))
				AddParticle(p_chunk, neworg, 1, 0.75, 3.75, NULL, zerodir);
		}
	}
}

void QMB_ParticleTrail (vec3_t start, vec3_t end, vec3_t *trail_origin, trail_type_t type) {
	col_t color;
	extern cvar_t gl_part_tracer1_color, gl_part_tracer1_size, gl_part_tracer1_time,
                  gl_part_tracer2_color, gl_part_tracer2_size, gl_part_tracer2_time;

	switch (type) {
		case GRENADE_TRAIL:
			//VULT PARTICLES
			if (amf_underwater_trails.value && R_PointIsUnderwater(start)) {
				AddParticleTrail(p_bubble, start, end, 1.8, 0.825, NULL);
			}
			else {
				AddParticleTrail(p_smoke, start, end, 1.45, 0.825, NULL);
			}
			break;
		case BLOOD_TRAIL:
		case BIG_BLOOD_TRAIL:
			AddParticleTrail(p_blood3, start, end, type == BLOOD_TRAIL ? 1.35 : 2.4, 2, NULL);
			break;
		case TRACER1_TRAIL:
			AddParticleTrail (p_trailpart, start, end, gl_part_tracer1_size.value, gl_part_tracer1_time.value, gl_part_tracer1_color.color);
			break;
		case TRACER2_TRAIL:
			AddParticleTrail (p_trailpart, start, end, gl_part_tracer2_size.value, gl_part_tracer2_time.value, gl_part_tracer2_color.color);
			break;
		case VOOR_TRAIL:
			color[0] = 77; color[1] = 0; color[2] = 255; color[3] = 255;
			AddParticleTrail (p_trailpart, start, end, 3.75, 0.5, color);
			break;
		case ALT_ROCKET_TRAIL:
			if (amf_underwater_trails.value && R_PointIsUnderwater(start)) {
				AddParticleTrail(p_bubble, start, end, 1.8, 0.825, NULL);
			}
			else {
				AddParticleTrail(p_dpfire, start, end, 3, 0.26, NULL);
				AddParticleTrail(p_dpsmoke, start, end, 3, 0.825, NULL);
			}
			break;
		//VULT TRAILS
		case RAIL_TRAIL:
		case RAIL_TRAIL2:
			color[0] = 255; color[1] = 255; color[2] = 255; color[3] = 255;
			//VULT PARTICLES
			AddParticleTrail (p_alphatrail, start, end, 2, 0.525, color);
			if (type == RAIL_TRAIL2)
			{
				color[0] = 255; 
				color[1] = 0; 
				color[2] = 0;
			}
			else
			{
				color[0] = 0; 
				color[1] = 0; 
				color[2] = 255;
			}
			AddParticleTrail (p_railtrail, start, end, 1.3, 0.525, color);
			break;
		//VULT TRAILS
		case TF_TRAIL:
			if (amf_underwater_trails.value && R_PointIsUnderwater(start)) {
				if (amf_nailtrail_water.value) {
					AddParticleTrail(p_bubble2, start, end, 1.5, 0.825, NULL);
				}
				else {
					AddParticleTrail(p_bubble, start, end, 1.5, 0.825, NULL);
				}
				break;
			}
			color[0] = 10; color[1] = 10; color[2] = 10;
			AddParticleTrail (p_alphatrail, start, end, alphatrail_s, alphatrail_l, color);
			break;
		//VULT TRAILS
		case LAVA_TRAIL:
			if (amf_underwater_trails.value && R_PointIsUnderwater(start)) {
				AddParticleTrail(p_bubble, start, end, 1.8, 0.825, NULL);
				color[0] = 25;
				color[1] = 102;
				color[2] = 255;
				color[3] = 255;
			}
			else {
				color[0] = 255;
				color[1] = 102;
				color[2] = 25;
				color[3] = 255;
			}
			AddParticleTrail(p_lavatrail, start, end, 5, 1, color);
			break;
		//VULT TRAILS
		case AMF_ROCKET_TRAIL:
			if (amf_underwater_trails.value && R_PointIsUnderwater(start)) {
				AddParticleTrail(p_bubble, start, end, 1.8, 0.825, NULL);
			}
			else {
				color[0] = 128; color[1] = 128; color[2] = 128; color[3] = 255;
				AddParticleTrail (p_alphatrail, start, end, 2, 0.6, color);
				AddParticleTrail (p_vxrocketsmoke, start, end, 3, 0.5, color);
				
				color[0] = 128; color[1] = 56; color[2] = 9; color[3] = 255;
				AddParticleTrail(p_trailpart, start, end, 4, 0.2, color);
			}
			break;
		//VULT PARTICLES
		case BLEEDING_TRAIL:
		case BLEEDING_TRAIL2:
			if (amf_part_blood_color.value == 2) {
				color[0]=55;color[1]=102;color[2]=255;
			}
			//green
			else if (amf_part_blood_color.value == 3) {
				color[0]=80;color[1]=255;color[2]=80;
			}
			//red
			else {
				color[0]=128;color[1]=0;color[2]=0;
			}
			color[3] = 255;
			AddParticleTrail(type == BLEEDING_TRAIL ? p_trailbleed : p_bleedspike, start, end, 4, type == BLEEDING_TRAIL ? 0.5 : 0.2 , color);
			break;
		case ROCKET_TRAIL:
		default:
			color[0] = 255; color[1] = 56; color[2] = 9; color[3] = 255;
			AddParticleTrail(p_trailpart, start, end, 6.2, 0.31, color);
			AddParticleTrail(p_smoke, start, end, 1.8, 0.825, NULL);
			break;
	}

	VectorCopy(trail_stop, *trail_origin);
}


// deurk: QMB version of ported zquake rail trail
void QMB_ParticleRailTrail (vec3_t start, vec3_t end, int color_num) {
	col_t		color;

	color[0] = 255; color[1] = 255; color[2] = 255; color[3] = 255;
	AddParticleTrail (p_alphatrail, start, end, 0.5, 0.5, color);
	switch (color_num) {
	case 180:
        color[0] = 91; color[1] = 74; color[2] = 56;
		break;
	case 35:
        color[0] = 121; color[1] = 118; color[2] = 185;
		break;
	case 224:
        color[0] = 206; color[1] = 0; color[2] = 0;
		break;
	case 133:
        color[0] = 107; color[1] = 70; color[2] = 88;
		break;
	case 192:
        color[0] = 255; color[1] = 242; color[2] = 32;
		break;
	case 6:
        color[0] = 200; color[1] = 200; color[2] = 200;
		break;
	default:
		color[0] = 0; color[1] = 0; color[2] = 255;
		break;
	}
	AddParticleTrail (p_alphatrail, start, end, 2, 1, color);
}

void QMB_BlobExplosion (vec3_t org) {
	float theta;
	col_t color;
	vec3_t neworg, vel;

	color[0] = 60; color[1] = 100; color[2] = 240; color[3] = 255;
	AddParticle (p_spark, org, 44, 250, 1.15, color, zerodir);

	color[0] = 90; color[1] = 47; color[2] = 207; color[3] = 255;
	AddParticle(p_fire, org, 15, 30, 1.4, color, zerodir);

	vel[2] = 0;
	//VULT PARTICLES
	if (amf_part_shockwaves.value)
	{
		//VULT Get 3 rings to fly out
		if (amf_part_2dshockwaves.value)
		{
			vec3_t shockdir;
			int i;
			color[0] = (60 + (rand() & 15)); color[1] = (65 + (rand() & 15)); color[2] = (200 + (rand() & 15));
			color[3] = 255;
			for (i=0;i<3;i++)
				shockdir[i] = rand() % 360;
			AddParticle(p_2dshockwave, org, 1, 30, 0.5, color, shockdir);
			for (i=0;i<3;i++)
				shockdir[i] = rand() % 360;
			AddParticle(p_2dshockwave, org, 1, 30, 0.5, color, shockdir);
			for (i=0;i<3;i++)
				shockdir[i] = rand() % 360;
			AddParticle(p_2dshockwave, org, 1, 30, 0.5, color, shockdir);
		}
		else
		{
			for (theta = 0; theta < 2 * M_PI; theta += 2 * M_PI / 70) 
			{
				color[0] = (60 + (rand() & 15)); color[1] = (65 + (rand() & 15)); color[2] = (200 + (rand() & 15));
				color[3] = 255;
				
				vel[0] = cos(theta) * 125;
				vel[1] = sin(theta) * 125;
				neworg[0] = org[0] + cos(theta) * 6;
				neworg[1] = org[1] + sin(theta) * 6;
				neworg[2] = org[2] + 0 - 10;
				AddParticle(p_shockwave, neworg, 1, 4, 0.8, color, vel);
				neworg[2] = org[2] + 0 + 10;
				AddParticle(p_shockwave, neworg, 1, 4, 0.8, color, vel);

				
				vel[0] *= 1.15;
				vel[1] *= 1.15;
				neworg[0] = org[0] + cos(theta) * 13;
				neworg[1] = org[1] + sin(theta) * 13;
				neworg[2] = org[2] + 0;
				AddParticle(p_shockwave, neworg, 1, 6, 1.0, color, vel);
			}
		}
	}
}

void QMB_LavaSplash (vec3_t org) {
	int	i, j;
	float vel;
	vec3_t dir, neworg;

	for (i = -16; i < 16; i++) {
		for (j = -16; j < 16; j++) {
			dir[0] = j * 8 + (rand() & 7);
			dir[1] = i * 8 + (rand() & 7);
			dir[2] = 256;

			neworg[0] = org[0] + dir[0];
			neworg[1] = org[1] + dir[1];
			neworg[2] = org[2] + (rand() & 63);

			VectorNormalizeFast (dir);
			vel = 50 + (rand() & 63);
			VectorScale (dir, vel, dir);

			AddParticle(p_lavasplash, neworg, 1, 4.5, 2.6 + (rand() & 31) * 0.02, NULL, dir);
		}
	}
}

void QMB_TeleportSplash (vec3_t org) {
	int i, j, k;
	vec3_t neworg, angle;
	col_t color;

	
	for (i = -12; i <= 12; i += 6) {
		for (j = -12; j <= 12; j += 6) {
			for (k = -24; k <= 32; k += 8) {
				neworg[0] = org[0] + i + (rand() & 3) - 1;
				neworg[1] = org[1] + j + (rand() & 3) - 1;
				neworg[2] = org[2] + k + (rand() & 3) - 1;
				angle[0] = (rand() & 15) - 7;
				angle[1] = (rand() & 15) - 7;
				angle[2] = (rand() % 160) - 80;
				AddParticle(p_teleflare, neworg, 1, 1.8, 0.30 + (rand() & 7) * 0.02, NULL, angle);
			}
		}
	}

	if (gl_part_telesplash.integer != 2)
	{
		VectorSet(color, 140, 140, 255);
		color[3] = 255;
		VectorClear(angle);
		for (i = 0; i < 5; i++) {
			angle[2] = 0;
			for (j = 0; j < 5; j++) {
				AngleVectors(angle, NULL, NULL, neworg);
				VectorMA(org, 70, neworg, neworg);	
				AddParticle(p_sparkray, org, 1, 6 + (i & 3), 5,  color, neworg);
				angle[2] += 360 / 5;
			}
			angle[0] += 180 / 5;
		}
	}
}

void QMB_DetpackExplosion (vec3_t org) {
	int i, j, contents;
	float theta;
	vec3_t neworg, angle = {0, 0, 0};

	byte *f_color = (gl_part_detpackexplosion_fire_color.string[0] ? gl_part_detpackexplosion_fire_color.color : ColorForParticle(p_inferno_flame));
	byte *r_color = (gl_part_detpackexplosion_ray_color.string[0] ? gl_part_detpackexplosion_ray_color.color : NULL);
	
	contents = TruePointContents(org);
	if (ISUNDERWATER(contents)) {
		AddParticle(p_bubble, org, 8, 2.8, 2.5, NULL, zerodir);
		AddParticle(p_bubble, org, 6, 2.2, 2.5, NULL, zerodir);
		AddParticle(p_fire, org, 10, 25, 0.75, f_color, zerodir);
	} else {
		AddParticle(p_fire, org, 14, 33, 0.75, f_color, zerodir);
	}
	
	for (i = 0; i < 5; i++) {
		angle[2] = 0;
		for (j = 0; j < 5; j++) {
			AngleVectors(angle, NULL, NULL, neworg);
			VectorMA(org, 90, neworg, neworg);	
			AddParticle(p_sparkray, org, 1, 9 + (i & 3), 0.75,  r_color, neworg);
			angle[2] += 360 / 5;
		}
		angle[0] += 180 / 5;
	}

	//VULT PARTICLES
	if (amf_part_shockwaves.value)
	{
		if (amf_part_2dshockwaves.value)
		{
			AddParticle(p_2dshockwave, org, 1, 30, 0.5, NULL, vec3_origin);
		}
		else
		{
			angle[2] = 0;
			for (theta = 0; theta < 2 * M_PI; theta += 2 * M_PI / 90) {
				angle[0] = cos(theta) * 350;
				angle[1] = sin(theta) * 350;
				AddParticle(p_shockwave, org, 1, 14, 0.75, NULL, angle);
			}
		}
	}
}

void QMB_InfernoFlame (vec3_t org) {
	if (cls.frametime) {
		AddParticle(p_inferno_flame, org, 1, 30, 13.125 * cls.frametime,  NULL, zerodir);
		AddParticle(p_inferno_trail, org, 2, 1.75, 45.0 * cls.frametime, NULL, zerodir);
		AddParticle(p_inferno_trail, org, 2, 1.0, 52.5 * cls.frametime, NULL, zerodir);
	}
}

void QMB_StaticBubble (entity_t *ent) {
	AddParticle(p_staticbubble, ent->origin, 1, ent->frame == 1 ? 1.85 : 2.9, ONE_FRAME_ONLY, NULL, zerodir);
}

//VULT PARTICLES
//This WAS supposed to do more than just rain but I haven't really got around to
//writing anything else for it.
void ParticleFirePool (vec3_t);
extern cvar_t tei_lavafire;

void WeatherEffect (void)
{
	vec3_t org, start, impact, normal;
	int i;
	col_t colour = {128, 128, 128, 75};

	if ((int) amf_weather_rain.value) {
		for (i = 0; i <= (int) amf_weather_rain.value; i++) {
			VectorCopy(r_refdef.vieworg, org);
			org[0] = org[0] + (rand() % 3000) - 1500;
			org[1] = org[1] + (rand() % 3000) - 1500;
			VectorCopy(org, start);
			org[2] = org[2] + 15000;

			//Trace a line straight up, get the impact location

			//trace up slowly until we are in sky
			TraceLineN (start, org, impact, normal);

			//fixme: see is surface above has SURF_DRAWSKY (we'll come back to that, when the important stuff is done fist, eh?)
			//if (TruePointContents (impact) == CONTENTS_SKY && trace) {
			if (Mod_PointInLeaf(impact, cl.worldmodel)->contents == CONTENTS_SKY) {
				VectorCopy (impact, org);
				org[2] = org[2] - 1;
				AddParticle (p_rain, org, 1, 1, 15, colour, zerodir);
			}
		}

	}

	//Tei, lavafire on 2 or superior 
	// this can be more better for some users than "eshaders"
	if (tei_lavafire.value > 2) {
		for (i = 0; i < (int) tei_lavafire.value; i++) {
			VectorCopy( r_refdef.vieworg, org);
			org[0] = org[0] + (rand() % 3000) - 1500;
			org[1] = org[1] + (rand() % 3000) - 1500;
			VectorCopy(org, start);
			org[2] = org[2] - 15000;

			TraceLineN (start, org, impact, normal);

			//if (TruePointContents (impact) == CONTENTS_LAVA && trace) {
			if (Mod_PointInLeaf(impact, cl.worldmodel)->contents == CONTENTS_LAVA) {
				ParticleFirePool (impact);
			}
		}
	}
}

//VULT PARTICLES
void RainSplash (vec3_t org)
{
	vec3_t pos;
	col_t colour = {255,255,255,255};
	
	VectorCopy (org, pos);
	pos[2] = pos[2] + 1; //To get above the water on servers that don't use watervis

	//VULT - Sometimes the shockwave effect isn't noticable enough?
	AddParticle (p_2dshockwave, pos, 1, 5, 0.5, colour, vec3_origin);
}

//VULT PARTICLES
//Cheap function which will generate some sparks for me to be called elsewhere
void SparkGen (vec3_t org, byte col[3], float count, float size, float life)
{
	col_t color;
	vec3_t dir;
	int i,a;

	color[0] = col[0];
	color[1] = col[1];
	color[2] = col[2];
	color[3] = 255;

	if (amf_part_sparks.value)
	{
		for (a=0;a<count;a++)
		{
			for (i=0;i<3;i++)
				dir[i] = (rand() % (int)size*2) - ((int)size*2/3);
			if (amf_part_trailtype.value == 2)
				AddParticle(p_smallspark, org, 1, 1, life, color, dir);
			else
				AddParticle(p_streak, org, 1, 1, life, color, dir);
			
		}
	}
	else
		AddParticle(p_spark, org, count, size, life, color, zerodir);
}

//VULT STATS
void ParticleStats (int change)
{
	if (ParticleCount > ParticleCountHigh)
		ParticleCountHigh = ParticleCount;
	ParticleCount+=change;
}

//VULT PARTICLES
//VULT - Draw a thin white trail that could be used to represent motion for just about anything
void ParticleAlphaTrail (vec3_t start, vec3_t end, vec3_t *trail_origin, float size, float life)
{
	if (amf_part_fasttrails.value)
	{
		col_t color={255,255,255,128};
		AddParticle(p_streaktrail, start, 1, size*5, life, color, end);
		VectorCopy(end, *trail_origin);
	}
	else
	{
		alphatrail_s = size;
		alphatrail_l = life;
		QMB_ParticleTrail(start, end, trail_origin, TF_TRAIL);
	}
}

//VULT - Some of the following functions might be the same as above, thats because I intended to change them but never
//got around to it.

//VULT PARTICLES
//VULT - These trails were my initial motivation behind AMFQUAKE.
void VX_ParticleTrail (vec3_t start, vec3_t end, float size, float time, col_t color)
{
	vec3_t		vec;
	float		len;
	time *= amf_part_traillen.value;

	if (!time) {
		return;
	}
	
	if (amf_part_fulldetail.integer) {
		VectorSubtract(end, start, vec);
		len = VectorNormalize(vec);

		while (len > 0) {
			//ADD PARTICLE HERE
			AddParticle(p_streaktrail, start, 1, size, time, color, zerodir);
			len--;
			VectorAdd(start, vec, start);
		}
	}
	AddParticle(p_streaktrail, start, 1, size, time, color, end);

}

//VULT PARTICLES
//VULT - Does the deed when someone shoots a wall
void VXGunshot(vec3_t org, float count)
{
	col_t color = { 255,80,10, 128 };
	vec3_t dir, neworg;
	int a, i;
	int contents;

	if (amf_part_gunshot_type.value == 2 || amf_part_gunshot_type.value == 3) {
		color[0] = 255;
		color[1] = 242;
		color[2] = 153;
		VectorClear(dir);
		for (i = 0; i < 5; i++) {
			dir[2] = 0;
			for (a = 0; a < 5; a++) {
				AngleVectors(dir, NULL, NULL, neworg);
				VectorMA(org, 40, neworg, neworg);
				AddParticle(p_sparkray, org, 1, 3, 1.5, color, neworg);
				dir[2] += 360 / 5;
			}
			dir[0] += 180 / 5;
		}
		if (amf_coronas.value) {
			NewCorona(C_WHITELIGHT, org);
		}
		if (amf_part_gunshot_type.value == 3) {
			return;
		}
	}
	else if (amf_part_gunshot_type.value == 4) {
		color[0] = 255;
		color[1] = 80;
		color[2] = 10;
		color[3] = 128;
		if (amf_coronas.value) {
			NewCorona(C_GUNFLASH, org);
		}
		for (a = 0;a < count*1.5;a++) {
			for (i = 0;i < 3;i++) {
				dir[i] = (rand() % 700) - 300;
			}
			AddParticle(p_streakwave, org, 1, 1, 0.066*amf_part_trailtime.value, color, dir);
		}
		return;
	}
	else if (amf_coronas.value) {
		NewCorona(C_GUNFLASH, org);
	}

	for (a = 0;a < count;a++) {
		for (i = 0;i < 3; ++i) {
			dir[i] = (rand() % 700) - 350;
		}
		if (amf_part_trailtype.value == 2) {
			AddParticle(p_smallspark, org, 1, 1, 1 * amf_part_trailtime.value, NULL, dir);
		}
		else {
			AddParticle(p_streak, org, 1, 1, 1 * amf_part_trailtime.value, color, dir);
		}
	}

	contents = TruePointContents(org);
	if (ISUNDERWATER(contents)) {
		AddParticle(p_bubble, org, count / 5, 2.35, 2.5, NULL, zerodir);
	}
}

//VULT PARTICLES
void VXNailhit (vec3_t org, float count)
{
	col_t color;
	vec3_t dir, neworg;
	int a, i;
	int contents;
	if (amf_nailtrail_plasma.value)
	{
		color[0]=10;
		color[1]=80;
		color[2]=255;
		color[3]=128;
		AddParticle(p_fire, org, 15, 5, 0.5, color, zerodir);
	}
	else
	{
		if (amf_part_spikes_type.value == 2 || amf_part_spikes_type.value == 3)
		{
			color[0] = 255;
			color[1] = 242;
			color[2] = 153;
			color[3] = 255;
			VectorClear(dir);
			for (i = 0; i < 5; i++) 
			{
				dir[2] = 0;
				for (a = 0; a < 5; a++) 
				{
					AngleVectors(dir, NULL, NULL, neworg);
					VectorMA(org, 40, neworg, neworg);	
					AddParticle(p_sparkray, org, 1, 3, 1.5,  color, neworg);
					dir[2] += 360 / 5;
				}
				dir[0] += 180 / 5;
			}
			if (amf_coronas.value) {
				NewCorona(C_WHITELIGHT, org);
			}
			if (amf_part_spikes_type.value == 3) {
				return;
			}
		}
		else if (amf_part_spikes_type.value == 4)
		{
			color[0]=255;
			color[1]=80;
			color[2]=10;
			color[3]=128;
			if (amf_coronas.value) {
				NewCorona(C_GUNFLASH, org);
			}
			for (a=0;a<count*1.5;a++)
			{
				for (i = 0;i < 3;i++) {
					dir[i] = (rand() % 700) - 300;
				}
				AddParticle(p_streakwave, org, 1, 1, 0.066*amf_part_trailtime.value, color, dir);
			}
			return;
		}
		else
		{
			color[0]=255;
			color[1]=80;
			color[2]=10;
			color[3]=128;
			if (amf_coronas.value) {
				NewCorona(C_GUNFLASH, org);
			}
		}
	}
	
	for (a = 0; a < count; ++a) {
		for (i = 0;i < 3;i++) {
			dir[i] = (rand() % 700) - 350;
		}
		if (amf_part_trailtype.value == 2) {
			if (amf_nailtrail_plasma.value) {
				AddParticle(p_smallspark, org, 1, 1, 1 * amf_part_trailtime.value, color, dir);
			}
			else {
				AddParticle(p_smallspark, org, 1, 1, 1 * amf_part_trailtime.value, NULL, dir);
			}
		}
		else {
			AddParticle(p_streak, org, 1, 1, 1 * amf_part_trailtime.value, color, dir);
		}
	}

	contents = TruePointContents(org);
	if (ISUNDERWATER(contents)) {
		AddParticle(p_bubble, org, count / 5, 2.35, 2.5, NULL, zerodir);
	}

}

//VULT PARTICLES
void VXExplosion(vec3_t org)
{
	col_t color = { 255,100,25, 128 };
	vec3_t dir;
	int a, i;
	int contents;
	float theta;
	vec3_t angle;

	contents = TruePointContents(org);
	if (ISUNDERWATER(contents)) {
		if (r_explosiontype.value != 9) {
			AddParticle(p_fire, org, 12, 14, 0.8, NULL, zerodir);
		}
		AddParticle(p_bubble, org, 12, 3.0, 2.5, NULL, zerodir);
		AddParticle(p_bubble, org, 8, 2.35, 2.5, NULL, zerodir);
	}
	else if (r_explosiontype.value != 9) {
		AddParticle(p_fire, org, 16, 18, 1, NULL, zerodir);
	}

	for (a = 0;a < 120 * amf_part_explosion.value;a++) {
		for (i = 0;i < 3;i++) {
			dir[i] = (rand() % 1500) - 750;
		}
		if (amf_part_trailtype.value == 2) {
			AddParticle(p_smallspark, org, 1, 1, 1 * amf_part_trailtime.value, NULL, dir);
		}
		else {
			AddParticle(p_streak, org, 1, 1, 1 * amf_part_trailtime.value, color, dir);
		}
	}

	if (amf_part_shockwaves.value) {
		if (amf_part_2dshockwaves.value) {
			AddParticle(p_2dshockwave, org, 1, 30, 0.5, NULL, vec3_origin);
		}
		else {
			angle[2] = 0;
			for (theta = 0; theta < 2 * M_PI; theta += 2 * M_PI / 90) {
				angle[0] = cos(theta) * 500;
				angle[1] = sin(theta) * 500;
				AddParticle(p_shockwave, org, 1, 10, 0.5, NULL, angle);
			}
		}
	}
}

//VULT PARTICLES
void VXBlobExplosion(vec3_t org)
{
	float theta;
	col_t color;
	vec3_t neworg, vel, dir;
	int a, i;
	color[0] = 60; color[1] = 100; color[2] = 240; color[3] = 128;
	for (a = 0;a < 200 * amf_part_blobexplosion.value;a++) {
		for (i = 0;i < 3;i++) {
			dir[i] = (rand() % 1500) - 750;
		}
		AddParticle(p_streakwave, org, 1, 1, 1 * amf_part_trailtime.value, color, dir);
	}

	color[0] = 90; color[1] = 47; color[2] = 207;
	AddParticle(p_fire, org, 15, 30, 1.4, color, zerodir);

	vel[2] = 0;
	//VULT PARTICLES
	if (amf_part_shockwaves.value) {
		if (amf_part_2dshockwaves.value) {
			vec3_t shockdir;
			int i;
			color[0] = (60 + (rand() & 15)); color[1] = (65 + (rand() & 15)); color[2] = (200 + (rand() & 15));
			for (i = 0;i < 3;i++) {
				shockdir[i] = rand() % 360;
			}
			AddParticle(p_2dshockwave, org, 1, 30, 0.5, color, shockdir);
			for (i = 0;i < 3;i++) {
				shockdir[i] = rand() % 360;
			}
			AddParticle(p_2dshockwave, org, 1, 30, 0.5, color, shockdir);
			for (i = 0;i < 3;i++) {
				shockdir[i] = rand() % 360;
			}
			AddParticle(p_2dshockwave, org, 1, 30, 0.5, color, shockdir);
		}
		else {
			for (theta = 0; theta < 2 * M_PI; theta += 2 * M_PI / 70) {
				color[0] = (60 + (rand() & 15)); color[1] = (65 + (rand() & 15)); color[2] = (200 + (rand() & 15));

				vel[0] = cos(theta) * 125;
				vel[1] = sin(theta) * 125;
				neworg[0] = org[0] + cos(theta) * 6;
				neworg[1] = org[1] + sin(theta) * 6;
				neworg[2] = org[2] + 0 - 10;

				AddParticle(p_shockwave, neworg, 1, 4, 0.8, color, vel);
				neworg[2] = org[2] + 0 + 10;
				AddParticle(p_shockwave, neworg, 1, 4, 0.8, color, vel);

				vel[0] *= 1.15;
				vel[1] *= 1.15;
				neworg[0] = org[0] + cos(theta) * 13;
				neworg[1] = org[1] + sin(theta) * 13;
				neworg[2] = org[2] + 0;
				AddParticle(p_shockwave, neworg, 1, 6, 1.0, color, vel);
			}
		}
	}
}

//VULT PARTICLES
void VXTeleport (vec3_t org) 
{
	int i, j, a;
	vec3_t neworg, angle, dir;
	col_t color;

	VectorSet(color, 140, 140, 255);
	VectorClear(angle);
	for (i = 0; i < 5; i++) {
		angle[2] = 0;
		for (j = 0; j < 5; j++) {
			AngleVectors(angle, NULL, NULL, neworg);
			VectorMA(org, 70, neworg, neworg);	
			AddParticle(p_sparkray, org, 1, 6 + (i & 3), 5,  color, neworg);
			angle[2] += 360 / 5;
		}
		angle[0] += 180 / 5;
	}

	color[0]=color[1]=color[2] = 255;
	color[3]=128;
	for (a = 0; a < 200 * amf_part_teleport.value; a++) {
		for (i = 0;i < 3;i++)
			dir[i] = (rand() % 1500) - 750;
		AddParticle(p_streakwave, org, 1, 1, 1 * amf_part_trailtime.value, color, dir);
	}
}

//VULT - We need some kind of splatter effect, here it is
void VXBlood(vec3_t org, float count)
{
	col_t color;
	int a, i;
	vec3_t trail, start, end;

	if (amf_part_blood_type.value) {
		VectorCopy(org, start);

		for (i = 0;i < 15;i++) {
			VectorCopy(start, end);
			end[0] += rand() % 30 - 15;
			end[1] += rand() % 30 - 15;
			end[2] += rand() % 30 - 15;

			VectorCopy(end, trail);

			QMB_ParticleTrail(start, end, &trail, BLEEDING_TRAIL2);
		}
	}


	//blue
	if (amf_part_blood_color.value == 2) {
		color[0] = 55;color[1] = 102;color[2] = 255;
	}
	//green
	else if (amf_part_blood_color.value == 3) {
		color[0] = 80;color[1] = 255;color[2] = 80;
	}
	//red
	else {
		color[0] = 255;color[1] = 0;color[2] = 0;
	}
	color[3] = 255;

	for (a = 0;a < count;a++) {
		AddParticle(p_vxblood, org, 1, 2.5, 2, color, 0);
	}
}

//from darkplaces engine - finds which corner of a particle goes where, so I don't have to :D
void R_PreCalcBeamVerts(vec3_t org1, vec3_t org2, vec3_t right1, vec3_t right2)
{
	vec3_t diff, normal;

	VectorSubtract (org2, org1, normal);
	VectorNormalize (normal);

	//width = width / 2;
	// calculate 'right' vector for start
	VectorSubtract (r_origin, org1, diff);
	VectorNormalize (diff);
	CrossProduct (normal, diff, right1);

	// calculate 'right' vector for end
	VectorSubtract (r_origin, org2, diff);
	VectorNormalize (diff);
	CrossProduct (normal, diff, right2);
}

void R_CalcBeamVerts(float *vert, const vec3_t org1, const vec3_t org2, const vec3_t right1, const vec3_t right2, float width)
{
	vert[ 0] = org1[0] + width * right1[0];
	vert[ 1] = org1[1] + width * right1[1];
	vert[ 2] = org1[2] + width * right1[2];
	vert[ 4] = org1[0] - width * right1[0];
	vert[ 5] = org1[1] - width * right1[1];
	vert[ 6] = org1[2] - width * right1[2];
	vert[ 8] = org2[0] - width * right2[0];
	vert[ 9] = org2[1] - width * right2[1];
	vert[10] = org2[2] - width * right2[2];
	vert[12] = org2[0] + width * right2[0];
	vert[13] = org2[1] + width * right2[1];
	vert[14] = org2[2] + width * right2[2];
}

//VULT PARTICLES
void FireballTrail (vec3_t start, vec3_t end, vec3_t *trail_origin, byte col[3], float size, float life)
{
	col_t color;
	color[0] = col[0];
	color[1] = col[1];
	color[2] = col[2];
	color[3] = 255;

	//head
	AddParticleTrail (p_trailpart, start, end, size*7, 0.15, color);

	//head-white part
	color[0] = 255; color[1] = 255; color[2] = 255;
	AddParticleTrail (p_trailpart, start, end, size*5, 0.15, color);

	//medium trail
	color[0] = col[0];
	color[1] = col[1];
	color[2] = col[2];
	AddParticleTrail (p_trailpart, start, end, size*3, life, color);

	VectorCopy(trail_stop, *trail_origin);
}

//VULT PARTICLES
void FireballTrailWave(vec3_t start, vec3_t end, vec3_t *trail_origin, byte col[3], float size, float life, vec3_t angle)
{
	int i, j;
	vec3_t dir, vec;
	col_t color;
	color[0] = col[0];
	color[1] = col[1];
	color[2] = col[2];
	color[3] = 255;

	AngleVectors(angle, vec, NULL, NULL);
	vec[2] *= -1;
	FireballTrail(start, end, trail_origin, col, size, life);
	if (!ISPAUSED) {
		for (i = 0; i < 3; i++) {
			for (j = 0; j < 3; j++) {
				dir[j] = vec[j] * -600 + ((rand() % 100) - 50);
			}
			AddParticle(p_streakwave, end, 1, 1, 1, color, dir);
		}
	}
}

//VULT PARTICLES
void FuelRodGunTrail (vec3_t start, vec3_t end, vec3_t angle, vec3_t *trail_origin)
{
	col_t color;
	int i, j;
	vec3_t dir, vec;
	color[3] = 255;

	color[0] = 0; color[1] = 255; color[2] = 0;
	AddParticleTrail (p_trailpart, start, end, 15, 0.2, color);
	color[0] = 255; color[1] = 255; color[2] = 255;
	AddParticleTrail (p_trailpart, start, end, 10, 0.2, color);
	color[0] = 0; color[1] = 128; color[2] = 0;
	AddParticleTrail (p_trailpart, start, end, 10, 0.5, color);
	color[0] = 0; color[1] = 22; color[2] = 0;
	AddParticleTrail (p_trailpart, start, end, 2, 3, color);
	if (!ISPAUSED)
	{
		AngleVectors(angle, vec, NULL, NULL);
		color[0] = 75; color[1] = 255; color[2] = 75;
		for (i=0;i<3;i++)
		{
			for (j=0;j<3;j++)
				dir[j] = vec[j] * -300 + ((rand() % 100) - 50);
			dir[2] = dir[2] + 60;
			if (amf_part_trailtype.value == 2)
				AddParticle(p_smallspark, end, 1, 1, 2, color, dir);
			else
				AddParticle(p_streak, end, 1, 1, 2, color, dir);
		}
	}
	VectorCopy(trail_stop, *trail_origin);
}

//VULT PARTICLES
void DrawMuzzleflash (vec3_t start, vec3_t angle, vec3_t vel)
{
	vec3_t dir, forward, up, right;
	col_t color;
	int i;
	color[0] = 55;
	color[1] = 55;
	color[2] = 55;
	color[3] = 55;
	AngleVectors(angle, forward, right, up);
	VectorClear(dir);
	VectorMA(dir, 20, forward, dir);
	dir[2] += rand() & 5;
	AddParticle(p_vxsmoke, start, 1, 8, 1, color, dir);

	color[0] = 255;
	color[1] = 180;
	color[2] = 55;
	color[3] = 255;
	VectorCopy(start, dir);
	for (i=60; i > 0; i--) {
		//VULT - i here is basically the length of the gun flame
		VectorMA(dir, 0.33, forward, dir);
		AddParticle(p_muzzleflash, dir, 1, i/3, 0.1, color, vel);
	}
}

//VULT PARTICLES
void FuelRodExplosion(vec3_t org)
{
	col_t color;
	vec3_t dir, org2, angle;
	int a, i;
	float theta;

	color[0] = 75; color[1] = 255; color[2] = 75;
	color[3] = 255;
	AddParticle(p_fire, org, 10, 20, 0.5, color, zerodir);
	angle[2] = 0;
	if (amf_part_2dshockwaves.value) {
		AddParticle(p_2dshockwave, org, 1, 30, 0.5, NULL, vec3_origin);
	}
	else {
		for (theta = 0; theta < 2 * M_PI; theta += 2 * M_PI / 90) {
			angle[0] = cos(theta) * 900;
			angle[1] = sin(theta) * 900;
			AddParticle(p_shockwave, org, 1, 15, 1, NULL, angle);
		}
	}

	for (a = 0;a < 60 * amf_part_explosion.value;a++) {
		for (i = 0;i < 3;i++) {
			dir[i] = (rand() % 1500) - 750;
		}

		if (amf_part_trailtype.value == 2) {
			AddParticle(p_smallspark, org, 1, 1, 0.5*amf_part_trailtime.value, color, dir);
		}
		else {
			AddParticle(p_streak, org, 1, 1, 0.5*amf_part_trailtime.value, color, dir);
		}
	}

	for (i = 0;i < 5;i++) {
		VectorCopy(org, org2);
		for (a = 0;a < 60;a = a + 10) {
			org2[0] = org2[0] + (rand() % 15) - 7;
			org2[1] = org2[1] + (rand() % 15) - 7;
			org2[2] = org2[2] + 10;
			AddParticle(p_fire, org2, 5, 10, (0.5 + a) / 60, color, zerodir);
		}
	}
	for (i = 0;i < 15;i++) {
		VectorCopy(org, org2);
		org2[2] = org2[2] + 60;
		org2[0] = org2[0] + (rand() % 100) - 50;
		org2[1] = org2[1] + (rand() % 100) - 50;
		AddParticle(p_fire, org2, 5, 10, 1, color, zerodir);
	}

	for (i = 0;i < 5;i++) {
		VectorCopy(org, org2);
		org2[2] = org2[2] + 70;
		org2[0] = org2[0] + (rand() % 70) - 35;
		org2[1] = org2[1] + (rand() % 70) - 35;
		AddParticle(p_fire, org2, 5, 10, 1.05, color, zerodir);
	}
	for (i = 0;i < 5;i++) {
		VectorCopy(org, org2);
		org2[2] = org2[2] + 80;
		org2[0] = org2[0] + (rand() % 50) - 25;
		org2[1] = org2[1] + (rand() % 50) - 25;
		AddParticle(p_fire, org2, 5, 10, 1.1, color, zerodir);
	}
	for (i = 0;i < 5;i++) {
		VectorCopy(org, org2);
		org2[2] = org2[2] + 90;
		org2[0] = org2[0] + (rand() % 25) - 12;
		org2[1] = org2[1] + (rand() % 25) - 12;
		AddParticle(p_fire, org2, 5, 10, 1.15, color, zerodir);
	}
}

//VULT PARTICLES
//Not much, but anything is better than that alias torch
void ParticleFire(vec3_t org)
{
	col_t color = { 255,100,25, 128 };
	int	contents = TruePointContents(org);
	if (ISUNDERWATER(contents)) {
		AddParticle(p_bubble, org, 1, 2.8, 2.5, NULL, zerodir);
	}
	else {
		vec3_t end;
		trace_t trace;

		VectorCopy(org, end);
		end[2] += 32;
		trace = PM_TraceLine(org, end);

		AddParticle(p_flame, org, 1, 7, 0.8 * trace.fraction, amf_part_firecolor.string[0] ? StringToRGB(amf_part_firecolor.string) : color, zerodir);
	}
}

//TEI PARTICLES
//Idea: lavapool fire, Result: slighty lame
void ParticleFirePool(vec3_t org)
{
	col_t color = { 255,100,25, 128 };

	AddParticle(p_flame, org, 1, lhrandom(1, 32), lhrandom(1, 1), color, zerodir);
}

//TEI PARTICLES
//Idea: slimepool fire, Result: slighty lame
//4 - fantastic
//11 - fantastic
//28 - blood pool ULTRA FANTASTIC (movies?)
void ParticleSlimeHarcore(vec3_t org)
{
	col_t color = { 0,200,150, 240 };
	vec3_t dir = { 0,0,80 };

	AddParticle(p_lavasplash, org, 1, lhrandom(1, 32), lhrandom(1, 3), color, dir);//zerodir);
	AddParticle(p_staticbubble, org, 1, lhrandom(1, 128), lhrandom(1, 10), color, dir);//zerodir);
}

void ParticleSlime(vec3_t org)
{
	col_t color = { 0,200,150, 30 };
	vec3_t dir = { 0,0,80 };

	AddParticle(p_lavasplash, org, 1, lhrandom(1, 32), lhrandom(1, 3), color, dir);//zerodir);
	AddParticle(p_staticbubble, org, 1, lhrandom(1, 32), lhrandom(1, 10), color, dir);//zerodir);
}

//HyperNewbie Particles
void ParticleSmallerFirePool(vec3_t org)
{
	col_t color = { 255,100,25, 78 };

	AddParticle(p_flame, org, 1, lhrandom(4, 7), 0.8, color, zerodir);
}

void ParticleSlimeBubbles(vec3_t org)
{
	col_t color = { 32,32,1, 250 };
	AddParticle(p_slimebubble, org, 1, lhrandom(4, 6), lhrandom(1, 5), color, zerodir);
}

void ParticleSlimeGlow(vec3_t org)
{
	col_t color = { 0,128,128, 200 };
	vec3_t dir = { 0,0,5 };
	AddParticle(p_slimeglow, org, 1, lhrandom(32, 64), lhrandom(7, 8), color, dir);
}

void ParticleLavaSmokePool(vec3_t org)
{
	col_t color = { 15,15,15,15 };
	vec3_t dir = { 0,0,lhrandom(0,1) };

	AddParticle(p_blacklavasmoke, org, 1, lhrandom(14, 23), lhrandom(1, 3), color, dir);
}

//TEI PARTICLES
//Idea: bloodpool 
void ParticleBloodPool(vec3_t org)
{
	col_t color = { 30,100,150, 240 };
	vec3_t dir = { 0,0,80 };

	AddParticle(p_vxsmoke_red, org, 1, lhrandom(1, 11), lhrandom(1, 3), color, dir);
}


//VULT PARTICLES
//This looks quite good with detailtrails on
void VX_TeslaCharge(vec3_t org)
{
	vec3_t pos, vec, dir;
	col_t col = { 60,100,240, 128 };
	float time, len;
	int i;

	for (i = 0;i < 5;i++) {
		VectorClear(vec);
		VectorClear(dir);

		VectorCopy(org, pos);
		pos[0] += (rand() % 200) - 100;
		pos[1] += (rand() % 200) - 100;
		pos[2] += (rand() % 200) - 100;

		VectorSubtract(pos, org, vec);
		len = VectorLength(vec);
		VectorNormalize(vec);
		VectorMA(dir, -200, vec, dir);
		time = len / 200; //

		AddParticle(p_streakwave, pos, 1, 3, time, col, dir);
	}
}

//VULT PARTICLES
//Adds a beam type particle with the lightning texture
void VX_LightningBeam(vec3_t start, vec3_t end)
{
	//col_t color={120,140,255,255};
	AddParticle(p_lightningbeam, start, 1, 100, cls.frametime ? cls.frametime * 2 : 0.013, amf_lightning_color.color, end);
}

//VULT PARTICLES
//Effect for when someone dies
void VX_DeathEffect(vec3_t org)
{
	int a, i;
	vec3_t dir;
	col_t color;
	color[0] = 255;
	color[1] = 225;
	color[2] = 128;
	color[3] = 128;

	for (a = 0;a < 60;a++) {
		for (i = 0;i < 3;i++)
			dir[i] = (rand() % 700) - 300;
		AddParticle(p_streakwave, org, 1, 1, 0.5, color, dir);
	}
}

//VULT PARTICLES
//Effect for when someone dies violently
void VX_GibEffect(vec3_t org)
{
	col_t color;
	color[0] = 55;
	color[1] = 0;
	color[2] = 0;
	color[3] = 128;

	AddParticle(p_bloodcloud, org, 10, 30, 1, color, zerodir);
}

//VULT PARTICLES
//Prototype detpack explosion, yeah, I know, it sucks. Thats why its never actually used
void VX_DetpackExplosion(vec3_t org)
{
	vec3_t angle, neworg;
	float theta;
	col_t color = { 255,102,25,255 };
	int i, j;

	angle[2] = 0;
	for (theta = 0; theta < 2 * M_PI; theta += 2 * M_PI / 90) {
		angle[0] = cos(theta) * 750;
		angle[1] = sin(theta) * 750;
		AddParticle(p_shockwave, org, 1, 20, 1, NULL, angle);
		angle[0] = cos(theta) * 200 + ((rand() % 100) - 50);
		angle[1] = sin(theta) * 200 + ((rand() % 100) - 50);
		angle[2] = rand() % 300;
		AddParticle(p_chunkdir, org, 1, 8, 1, color, angle);
		angle[2] = rand() % 100 - 50;
	}
	AddParticle(p_fire, org, 20, 80, 1, color, zerodir);
	for (i = 0; i < 5; i++) {
		angle[2] = 0;
		for (j = 0; j < 5; j++) {
			AngleVectors(angle, NULL, NULL, neworg);
			VectorMA(org, 180, neworg, neworg);
			AddParticle(p_sparkray, org, 1, 16 + (i & 3), 1, NULL, neworg);
			angle[2] += 360 / 5;
		}
		angle[0] += 180 / 5;
	}
}


//TEI PARTICLES
//Teleport dimensional implosion
//unfinished :(
// testing cvs 
void VX_Implosion (vec3_t org)
{
	//TODO
}

//VULT PARTICLES
//This just adds a little lightning trail to the laser beams
void VX_LightningTrail(vec3_t start, vec3_t end)
{
	col_t color = { 255,77,0,255 };
	AddParticle(p_lightningbeam, start, 1, 50, 0.75, color, end);
}
