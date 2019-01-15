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
#include "gl_local.h"
#include "r_local.h"
#include "r_state.h"
#include "gl_sprite3d.h"
#include "r_sprite3d_internal.h"
#include "glc_vao.h"
#include "tr_types.h"
#include "glc_local.h"
#include "r_renderer.h"

static void GLC_Create3DSpriteVAO(void)
{
	if (buffers.supported) {
		R_GenVertexArray(vao_3dsprites);
		R_Sprite3DCreateVBO();
		R_Sprite3DCreateIndexBuffer();

		GLC_VAOSetIndexBuffer(vao_3dsprites, sprite3dIndexes);
		GLC_VAOSetVertexBuffer(vao_3dsprites, sprite3dVBO);
		GLC_VAOEnableVertexPointer(vao_3dsprites, 3, GL_FLOAT, sizeof(r_sprite3d_vert_t), VBO_FIELDOFFSET(r_sprite3d_vert_t, position));
		GLC_VAOEnableColorPointer(vao_3dsprites, 4, GL_UNSIGNED_BYTE, sizeof(r_sprite3d_vert_t), VBO_FIELDOFFSET(r_sprite3d_vert_t, color));
		GLC_VAOEnableTextureCoordPointer(vao_3dsprites, 0, 2, GL_FLOAT, sizeof(r_sprite3d_vert_t), VBO_FIELDOFFSET(r_sprite3d_vert_t, tex));
	}
}

static void GLC_DrawSequentialBatch(gl_sprite3d_batch_t* batch, int index_offset, GLuint maximum_batch_size)
{
	if (R_TextureReferenceIsValid(batch->texture)) {
		if (!batch->textured_rendering_state) {
			assert(false);
			return;
		}
		R_ApplyRenderingState(batch->textured_rendering_state);
		renderer.TextureUnitBind(0, batch->texture);

		// All batches are the same texture, so no issues
		GL_DrawSequentialBatchImpl(batch, 0, batch->count, index_offset, maximum_batch_size);
	}
	else {
		// Group by texture usage
		int start = 0, end = 1;

		if (R_TextureReferenceIsValid(batch->textures[start])) {
			if (!batch->textured_rendering_state) {
				assert(false);
				return;
			}

			R_ApplyRenderingState(batch->textured_rendering_state);
			renderer.TextureUnitBind(0, batch->textures[start]);
		}
		else {
			if (!batch->untextured_rendering_state) {
				assert(false);
				return;
			}
			R_ApplyRenderingState(batch->untextured_rendering_state);
		}
		for (end = 1; end < batch->count; ++end) {
			if (!R_TextureReferenceEqual(batch->textures[start], batch->textures[end])) {
				GL_DrawSequentialBatchImpl(batch, start, end, index_offset, maximum_batch_size);

				if (R_TextureReferenceIsValid(batch->textures[end])) {
					if (!batch->textured_rendering_state) {
						assert(false);
						return;
					}
					renderer.TextureUnitBind(0, batch->textures[end]);
					R_ApplyRenderingState(batch->textured_rendering_state);
				}
				else {
					if (!batch->untextured_rendering_state) {
						assert(false);
						return;
					}
					R_ApplyRenderingState(batch->untextured_rendering_state);
				}
				start = end;
			}
		}

		if (end > start) {
			GL_DrawSequentialBatchImpl(batch, start, end, index_offset, maximum_batch_size);
		}
	}
}

void GLC_Draw3DSpritesInline(void)
{
	unsigned int i, j, k;

	if (!batchCount || !vertexCount || (batchCount == 1 && !batches[0].count)) {
		return;
	}

	if (buffers.supported) {
		if (!R_VertexArrayCreated(vao_3dsprites)) {
			GLC_Create3DSpriteVAO();
		}
		buffers.Update(sprite3dVBO, vertexCount * sizeof(verts[0]), verts);
	}

	for (i = 0; i < batchCount; ++i) {
		gl_sprite3d_batch_t* batch = &batches[i];

		if (!batch->count) {
			continue;
		}

		if (buffers.supported) {
			if (batch->count == 1) {
				if (R_TextureReferenceIsValid(batch->textures[0])) {
					renderer.TextureUnitBind(0, batch->textures[0]);
					R_ApplyRenderingState(batch->textured_rendering_state);
				}
				else if (R_TextureReferenceIsValid(batch->texture)) {
					renderer.TextureUnitBind(0, batch->texture);
					R_ApplyRenderingState(batch->textured_rendering_state);
				}
				else {
					R_ApplyRenderingState(batch->untextured_rendering_state);
				}
				GL_DrawArrays(glPrimitiveTypes[batch->primitive_id], batch->glFirstVertices[0], batch->numVertices[0]);
			}
			else if (GL_DrawElementsBaseVertexAvailable() && batch->allSameNumber && batch->numVertices[0] == 4 && batch->primitive_id == r_primitive_triangle_strip) {
				GLC_DrawSequentialBatch(batch, indexes_start_quads, INDEXES_MAX_QUADS);
			}
			else if (GL_DrawElementsBaseVertexAvailable() && batch->allSameNumber && batch->numVertices[0] == 9 && GL_Supported(R_SUPPORT_PRIMITIVERESTART)) {
				GLC_DrawSequentialBatch(batch, indexes_start_sparks, INDEXES_MAX_SPARKS);
			}
			else if (GL_DrawElementsBaseVertexAvailable() && batch->allSameNumber && batch->numVertices[0] == 18 && GL_Supported(R_SUPPORT_PRIMITIVERESTART)) {
				GLC_DrawSequentialBatch(batch, indexes_start_flashblend, INDEXES_MAX_FLASHBLEND);
			}
			else if (R_TextureReferenceIsValid(batch->texture)) {
				renderer.TextureUnitBind(0, batch->texture);
				R_ApplyRenderingState(batch->textured_rendering_state);

				GL_MultiDrawArrays(glPrimitiveTypes[batch->primitive_id], batch->glFirstVertices, batch->numVertices, batch->count);
			}
			else {
				int first = 0, last = 1;

				if (R_TextureReferenceIsValid(batch->textures[0])) {
					renderer.TextureUnitBind(0, batch->textures[0]);
					R_ApplyRenderingState(batch->textured_rendering_state);
				}
				else {
					R_ApplyRenderingState(batch->untextured_rendering_state);
				}

				while (last < batch->count) {
					if (!R_TextureReferenceEqual(batch->textures[first], batch->textures[last])) {
						GL_MultiDrawArrays(glPrimitiveTypes[batch->primitive_id], batch->glFirstVertices, batch->numVertices, last - first);

						if (R_TextureReferenceIsValid(batch->textures[last])) {
							renderer.TextureUnitBind(0, batch->textures[last]);
							R_ApplyRenderingState(batch->textured_rendering_state);
						}
						else {
							R_ApplyRenderingState(batch->untextured_rendering_state);
						}
						first = last;
					}
					++last;
				}

				GL_MultiDrawArrays(glPrimitiveTypes[batch->primitive_id], batch->glFirstVertices, batch->numVertices, last - first);
			}
		}
		else {
			for (j = 0; j < batch->count; ++j) {
				r_sprite3d_vert_t* v;

				if (R_TextureReferenceIsValid(batch->textures[j])) {
					renderer.TextureUnitBind(0, batch->textures[j]);
					R_ApplyRenderingState(batch->textured_rendering_state);
				}
				else if (R_TextureReferenceIsValid(batch->texture)) {
					renderer.TextureUnitBind(0, batch->texture);
					R_ApplyRenderingState(batch->textured_rendering_state);
				}
				else {
					R_ApplyRenderingState(batch->untextured_rendering_state);
				}

				// Immediate mode
				GLC_Begin(glPrimitiveTypes[batch->primitive_id]);
				v = &verts[batch->firstVertices[j]];
				for (k = 0; k < batch->numVertices[j]; ++k, ++v) {
					glTexCoord2fv(v->tex);
					R_CustomColor4ubv(v->color);
					GLC_Vertex3fv(v->position);
				}
				GLC_End();
			}
		}

		batch->count = 0;
	}

	batchCount = vertexCount = 0;
	memset(batchMapping, 0, sizeof(batchMapping));
}

void GLC_DrawSimpleItem(model_t* model, int skin, vec3_t org, float sprsize, vec3_t up, vec3_t right)
{
	texture_ref simpletexture = model->simpletexture[skin];
	r_sprite3d_vert_t* vert = R_Sprite3DAddEntrySpecific(SPRITE3D_ENTITIES, 4, simpletexture, 0);

	if (vert) {
		R_Sprite3DRender(vert, org, up, right, sprsize, -sprsize, -sprsize, sprsize, 1, 1, 0);
	}
}

void GLC_DrawSpriteModel(entity_t* e)
{
	vec3_t right, up;
	mspriteframe_t *frame;
	msprite2_t *psprite;
	r_sprite3d_vert_t* vert;

	// don't even bother culling, because it's just a single
	// polygon without a surface cache
	psprite = (msprite2_t*)Mod_Extradata(e->model);	//locate the proper data
	frame = R_GetSpriteFrame(e, psprite);

	if (!frame)
		return;

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

	vert = R_Sprite3DAddEntrySpecific(SPRITE3D_ENTITIES, 4, frame->gl_texturenum, 0);
	if (vert) {
		vec3_t points[4];

		VectorMA(e->origin, frame->up, up, points[0]);
		VectorMA(points[0], frame->left, right, points[0]);
		VectorMA(e->origin, frame->down, up, points[1]);
		VectorMA(points[1], frame->left, right, points[1]);
		VectorMA(e->origin, frame->up, up, points[2]);
		VectorMA(points[2], frame->right, right, points[2]);
		VectorMA(e->origin, frame->down, up, points[3]);
		VectorMA(points[3], frame->right, right, points[3]);

		R_Sprite3DSetVert(vert++, points[0][0], points[0][1], points[0][2], 0, 0, color_white, 0);
		R_Sprite3DSetVert(vert++, points[1][0], points[1][1], points[1][2], 0, 1, color_white, 0);
		R_Sprite3DSetVert(vert++, points[2][0], points[2][1], points[2][2], 1, 0, color_white, 0);
		R_Sprite3DSetVert(vert++, points[3][0], points[3][1], points[3][2], 1, 1, color_white, 0);
	}
}
