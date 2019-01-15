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

// 3D sprites
#include "quakedef.h"
#include "gl_model.h"
#include "r_sprite3d.h"
#include "r_sprite3d_internal.h"
#include "r_state.h"
#include "r_buffers.h"
#include "tr_types.h"
#include "r_renderer.h"

int indexes_start_quads, indexes_start_flashblend, indexes_start_sparks;

static const char* batch_type_names[] = {
	"ENTITIES",
	"PARTICLES_CLASSIC",
	"PARTICLES_NEW_p_spark",
	"PARTICLES_NEW_p_smoke",
	"PARTICLES_NEW_p_fire",
	"PARTICLES_NEW_p_bubble",
	"PARTICLES_NEW_p_lavasplash",
	"PARTICLES_NEW_p_gunblast",
	"PARTICLES_NEW_p_chunk",
	"PARTICLES_NEW_p_shockwave",
	"PARTICLES_NEW_p_inferno_flame",
	"PARTICLES_NEW_p_inferno_trail",
	"PARTICLES_NEW_p_sparkray",
	"PARTICLES_NEW_p_staticbubble",
	"PARTICLES_NEW_p_trailpart",
	"PARTICLES_NEW_p_dpsmoke",
	"PARTICLES_NEW_p_dpfire",
	"PARTICLES_NEW_p_teleflare",
	"PARTICLES_NEW_p_blood1",
	"PARTICLES_NEW_p_blood2",
	"PARTICLES_NEW_p_blood3",

	//VULT PARTICLES
	"PARTICLES_NEW_p_rain",
	"PARTICLES_NEW_p_alphatrail",
	"PARTICLES_NEW_p_railtrail",
	"PARTICLES_NEW_p_streak",
	"PARTICLES_NEW_p_streaktrail",
	"PARTICLES_NEW_p_streakwave",
	"PARTICLES_NEW_p_lightningbeam",
	"PARTICLES_NEW_p_vxblood",
	"PARTICLES_NEW_p_lavatrail",
	"PARTICLES_NEW_p_vxsmoke",
	"PARTICLES_NEW_p_vxsmoke_red",
	"PARTICLES_NEW_p_muzzleflash",
	"PARTICLES_NEW_p_inferno",
	"PARTICLES_NEW_p_2dshockwave",
	"PARTICLES_NEW_p_vxrocketsmoke",
	"PARTICLES_NEW_p_trailbleed",
	"PARTICLES_NEW_p_bleedspike",
	"PARTICLES_NEW_p_flame",
	"PARTICLES_NEW_p_bubble2",
	"PARTICLES_NEW_p_bloodcloud",
	"PARTICLES_NEW_p_chunkdir",
	"PARTICLES_NEW_p_smallspark",
	"PARTICLES_NEW_p_slimeglow",
	"PARTICLES_NEW_p_slimebubble",
	"PARTICLES_NEW_p_blacklavasmoke",
	"PARTICLES_NEW_p_entitytrail",
	"FLASHBLEND_LIGHTS",
	"CORONATEX_STANDARD",
	"CORONATEX_GUNFLASH",
	"CORONATEX_EXPLOSIONFLASH1",
	"CORONATEX_EXPLOSIONFLASH2",
	"CORONATEX_EXPLOSIONFLASH3",
	"CORONATEX_EXPLOSIONFLASH4",
	"CORONATEX_EXPLOSIONFLASH5",
	"CORONATEX_EXPLOSIONFLASH6",
	"CORONATEX_EXPLOSIONFLASH7",
	"CHATICON_AFK_CHAT",
	"CHATICON_CHAT",
	"CHATICON_AFK"
};

#ifdef C_ASSERT
C_ASSERT(sizeof(batch_type_names) / sizeof(batch_type_names[0]) == MAX_SPRITE3D_BATCHES);
#endif

r_sprite3d_vert_t verts[MAX_VERTS_PER_SCENE];
gl_sprite3d_batch_t batches[MAX_SPRITE3D_BATCHES];
unsigned int batchMapping[MAX_SPRITE3D_BATCHES];
unsigned int batchCount;
unsigned int vertexCount;
static unsigned int indexData[INDEXES_MAX_QUADS * 4 + INDEXES_MAX_SPARKS * 9 + INDEXES_MAX_FLASHBLEND * 18 + (INDEXES_MAX_QUADS + INDEXES_MAX_SPARKS + INDEXES_MAX_FLASHBLEND) * 3];

buffer_ref sprite3dVBO;
buffer_ref sprite3dIndexes;

static gl_sprite3d_batch_t* BatchForType(sprite3d_batch_id type, qbool allocate)
{
	unsigned int index = batchMapping[type];

	if (index == 0) {
		if (!allocate || batchCount >= sizeof(batches) / sizeof(batches[0])) {
			return NULL;
		}
		batchMapping[type] = index = ++batchCount;
	}

	return &batches[index - 1];
}

void R_Sprite3DInitialiseBatch(sprite3d_batch_id type, r_state_id textured_state, r_state_id untextured_state, texture_ref texture, int index, r_primitive_id primitive_type)
{
	gl_sprite3d_batch_t* batch = BatchForType(type, true);

	assert(textured_state != r_state_null);
	assert(untextured_state != r_state_null);

	batch->textured_rendering_state = textured_state;
	batch->untextured_rendering_state = untextured_state;
	batch->texture = texture;
	batch->count = 0;
	batch->primitive_id = primitive_type;
	batch->allSameNumber = true;
	batch->texture_index = index;
	batch->name = batch_type_names[type];
	batch->id = type;
}

r_sprite3d_vert_t* R_Sprite3DAddEntrySpecific(sprite3d_batch_id type, int verts_required, texture_ref texture, int texture_index)
{
	gl_sprite3d_batch_t* batch = BatchForType(type, false);
	int start = vertexCount;

	if (!batch || batch->count >= MAX_3DSPRITES_PER_BATCH || (batch->count + 1) * verts_required >= MAX_VERTS_PER_BATCH) {
		return NULL;
	}
	if (!R_TextureReferenceIsValid(texture) && !R_TextureReferenceIsValid(batch->texture) && !batch->untextured_rendering_state) {
		assert(false);
		return NULL;
	}

	if (start + verts_required >= MAX_VERTS_PER_SCENE) {
		return NULL;
	}
	batch->firstVertices[batch->count] = start;
	batch->glFirstVertices[batch->count] = start + buffers.BufferOffset(sprite3dVBO) / sizeof(verts[0]);
	batch->numVertices[batch->count] = verts_required;
	batch->textures[batch->count] = texture;
	batch->textureIndexes[batch->count] = texture_index;

	if (batch->count) {
		batch->allSameNumber &= verts_required == batch->numVertices[0];
	}
	++batch->count;
	vertexCount += verts_required;
	return &verts[start];
}

r_sprite3d_vert_t* R_Sprite3DAddEntryFixed(sprite3d_batch_id type, int verts_required)
{
	return R_Sprite3DAddEntrySpecific(type, verts_required, null_texture_reference, 0);
}

r_sprite3d_vert_t* R_Sprite3DAddEntry(sprite3d_batch_id type, int verts_required)
{
	return R_Sprite3DAddEntrySpecific(type, verts_required, null_texture_reference, 0);
}

void R_Sprite3DSetVert(r_sprite3d_vert_t* vert, float x, float y, float z, float s, float t, byte color[4], int texture_index)
{
	extern int particletexture_array_index;

	vert->color[0] = color[0];
	vert->color[1] = color[1];
	vert->color[2] = color[2];
	vert->color[3] = color[3];
	VectorSet(vert->position, x, y, z);
	if (texture_index < 0) {
		vert->tex[0] = 0.99;
		vert->tex[1] = 0.99;
		vert->tex[2] = particletexture_array_index;
	}
	else {
		vert->tex[0] = s;
		vert->tex[1] = t;
		vert->tex[2] = texture_index;
	}
}

void R_Sprite3DCreateVBO(void)
{
	if (!R_BufferReferenceIsValid(sprite3dVBO)) {
		sprite3dVBO = buffers.Create(buffertype_vertex, "sprite3d-vbo", sizeof(verts), verts, bufferusage_once_per_frame);
	}
}

void R_Sprite3DCreateIndexBuffer(void)
{
	if (!R_BufferReferenceIsValid(sprite3dIndexes)) {
		// Meag: *3 is for case of primitive restart not being supported
		int i, j;
		int pos = 0;
		int vbo_pos;

		indexes_start_quads = pos;
		for (i = 0, vbo_pos = 0; i < INDEXES_MAX_QUADS; ++i) {
			if (i) {
				if (glConfig.supported_features & R_SUPPORT_PRIMITIVERESTART) {
					indexData[pos++] = ~(unsigned int)0;
				}
				else {
					indexData[pos++] = vbo_pos - 1;
					indexData[pos++] = vbo_pos;
				}
			}
			for (j = 0; j < 4; ++j) {
				indexData[pos++] = vbo_pos++;
			}
		}

		// These are currently only supported/used if primitive restart supported, as they're rendered as triangle fans
		indexes_start_flashblend = pos;
		for (i = 0, vbo_pos = 0; i < INDEXES_MAX_FLASHBLEND; ++i) {
			if (i) {
				if (glConfig.supported_features & R_SUPPORT_PRIMITIVERESTART) {
					indexData[pos++] = ~(unsigned int)0;
				}
				else {
					indexData[pos++] = vbo_pos - 1;
					indexData[pos++] = vbo_pos;
				}
			}
			for (j = 0; j < 18; ++j) {
				indexData[pos++] = vbo_pos++;
			}
		}

		// These are currently only supported/used if primitive restart supported, as they're rendered as triangle fans
		indexes_start_sparks = pos;
		for (i = 0, vbo_pos = 0; i < INDEXES_MAX_SPARKS; ++i) {
			if (i) {
				if (glConfig.supported_features & R_SUPPORT_PRIMITIVERESTART) {
					indexData[pos++] = ~(unsigned int)0;
				}
				else {
					indexData[pos++] = vbo_pos - 1;
					indexData[pos++] = vbo_pos - 1;
					indexData[pos++] = vbo_pos;
				}
			}
			for (j = 0; j < 9; ++j) {
				indexData[pos++] = vbo_pos++;
			}
		}

		sprite3dIndexes = buffers.Create(buffertype_index, "3dsprite-indexes", sizeof(indexData), indexData, bufferusage_constant_data);
	}
}

void R_Sprite3DRender(r_sprite3d_vert_t* vert, vec3_t origin, vec3_t up, vec3_t right, float scale_up, float scale_down, float scale_left, float scale_right, float s, float t, int index)
{
	static byte color_white[4] = { 255, 255, 255, 255 };
	vec3_t points[4];


	VectorMA(origin, scale_up, up, points[0]);
	VectorMA(points[0], scale_left, right, points[0]);
	VectorMA(origin, scale_down, up, points[1]);
	VectorMA(points[1], scale_left, right, points[1]);
	VectorMA(origin, scale_up, up, points[2]);
	VectorMA(points[2], scale_right, right, points[2]);
	VectorMA(origin, scale_down, up, points[3]);
	VectorMA(points[3], scale_right, right, points[3]);

	R_Sprite3DSetVert(vert++, points[0][0], points[0][1], points[0][2], 0, 0, color_white, index);
	R_Sprite3DSetVert(vert++, points[1][0], points[1][1], points[1][2], 0, t, color_white, index);
	R_Sprite3DSetVert(vert++, points[2][0], points[2][1], points[2][2], s, 0, color_white, index);
	R_Sprite3DSetVert(vert++, points[3][0], points[3][1], points[3][2], s, t, color_white, index);
}
