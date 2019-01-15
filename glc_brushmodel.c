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

$Id: gl_model.c,v 1.41 2007-10-07 08:06:33 tonik Exp $
*/
// gl_model.c  -- model loading and caching

// models are the only shared resource between a client and server running on the same machine.

#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "rulesets.h"
#include "r_framestats.h"
#include "r_texture.h"
#include "glc_state.h"
#include "glc_vao.h"
#include "r_brushmodel.h"
#include "r_brushmodel_sky.h"
#include "glc_local.h"
#include "tr_types.h"
#include "r_renderer.h"
#include "glsl/constants.glsl"
#include "r_lightmaps.h"
#include "r_trace.h"

extern buffer_ref brushModel_vbo;

extern glpoly_t *fullbright_polys[MAX_GLTEXTURES];
extern glpoly_t *luma_polys[MAX_GLTEXTURES];

glpoly_t *caustics_polys = NULL;
glpoly_t *detail_polys = NULL;

void GLC_EnsureVAOCreated(r_vao_id vao)
{
	if (R_VertexArrayCreated(vao)) {
		return;
	}

	if (!R_BufferReferenceIsValid(brushModel_vbo)) {
		// TODO: vbo data in client memory
		return;
	}

	R_GenVertexArray(vao);
	GLC_VAOSetVertexBuffer(vao, brushModel_vbo);
	// TODO: index data _not_ in client memory

	switch (vao) {
		case vao_brushmodel:
		{
			// tmus: [material, material2, lightmap]
			GLC_VAOEnableVertexPointer(vao, 3, GL_FLOAT, sizeof(glc_vbo_world_vert_t), VBO_FIELDOFFSET(glc_vbo_world_vert_t, position));
			GLC_VAOEnableTextureCoordPointer(vao, 0, 2, GL_FLOAT, sizeof(glc_vbo_world_vert_t), VBO_FIELDOFFSET(glc_vbo_world_vert_t, material_coords));
			GLC_VAOEnableTextureCoordPointer(vao, 1, 2, GL_FLOAT, sizeof(glc_vbo_world_vert_t), VBO_FIELDOFFSET(glc_vbo_world_vert_t, material_coords));
			GLC_VAOEnableTextureCoordPointer(vao, 2, 2, GL_FLOAT, sizeof(glc_vbo_world_vert_t), VBO_FIELDOFFSET(glc_vbo_world_vert_t, lightmap_coords));
			break;
		}
		case vao_brushmodel_lm_unit1:
		{
			// tmus: [material, lightmap, material2]
			GLC_VAOEnableVertexPointer(vao, 3, GL_FLOAT, sizeof(glc_vbo_world_vert_t), VBO_FIELDOFFSET(glc_vbo_world_vert_t, position));
			GLC_VAOEnableTextureCoordPointer(vao, 0, 2, GL_FLOAT, sizeof(glc_vbo_world_vert_t), VBO_FIELDOFFSET(glc_vbo_world_vert_t, material_coords));
			GLC_VAOEnableTextureCoordPointer(vao, 1, 2, GL_FLOAT, sizeof(glc_vbo_world_vert_t), VBO_FIELDOFFSET(glc_vbo_world_vert_t, lightmap_coords));
			GLC_VAOEnableTextureCoordPointer(vao, 2, 2, GL_FLOAT, sizeof(glc_vbo_world_vert_t), VBO_FIELDOFFSET(glc_vbo_world_vert_t, material_coords));
			break;
		}
		case vao_brushmodel_details:
		{
			// tmus: [details]
			GLC_VAOEnableVertexPointer(vao, 3, GL_FLOAT, sizeof(glc_vbo_world_vert_t), VBO_FIELDOFFSET(glc_vbo_world_vert_t, position));
			GLC_VAOEnableTextureCoordPointer(vao, 0, 2, GL_FLOAT, sizeof(glc_vbo_world_vert_t), VBO_FIELDOFFSET(glc_vbo_world_vert_t, detail_coords));
			break;
		}
		case vao_brushmodel_lightmap_pass:
		{
			// tmus: [lightmap]
			GLC_VAOEnableVertexPointer(vao, 3, GL_FLOAT, sizeof(glc_vbo_world_vert_t), VBO_FIELDOFFSET(glc_vbo_world_vert_t, position));
			GLC_VAOEnableTextureCoordPointer(vao, 0, 2, GL_FLOAT, sizeof(glc_vbo_world_vert_t), VBO_FIELDOFFSET(glc_vbo_world_vert_t, lightmap_coords));
			GLC_VAOEnableCustomAttribute(vao, 0, r_program_attribute_world_drawflat_style, 1, GL_FLOAT, GL_FALSE, sizeof(glc_vbo_world_vert_t), VBO_FIELDOFFSET(glc_vbo_world_vert_t, flatstyle));
			break;
		}
		case vao_brushmodel_simpletex:
		{
			// tmus: [material]
			GLC_VAOEnableVertexPointer(vao, 3, GL_FLOAT, sizeof(glc_vbo_world_vert_t), VBO_FIELDOFFSET(glc_vbo_world_vert_t, position));
			GLC_VAOEnableTextureCoordPointer(vao, 0, 2, GL_FLOAT, sizeof(glc_vbo_world_vert_t), VBO_FIELDOFFSET(glc_vbo_world_vert_t, material_coords));
			break;
		}
		default:
		{
			assert(false);
			break;
		}
	}
}

static void GLC_BlendLightmaps(void);
void GLC_RenderFullbrights(void);
void GLC_RenderLumas(void);

static qbool GLC_DrawflatProgramCompile(void)
{
	if (R_ProgramRecompileNeeded(r_program_world_drawflat_glc, 0)) {
		R_ProgramCompile(r_program_world_drawflat_glc);
		R_ProgramSetCustomOptions(r_program_world_drawflat_glc, 0);
	}

	return R_ProgramReady(r_program_world_drawflat_glc);
}

static void GLC_SurfaceColor(const msurface_t* s, byte* desired)
{
	if (s->flags & SURF_DRAWSKY) {
		memcpy(desired, r_skycolor.color, 3);
	}
	else if (s->flags & SURF_DRAWTURB) {
		memcpy(desired, SurfaceFlatTurbColor(s->texinfo->texture), 3);
	}
	else if (s->flags & SURF_DRAWFLAT_FLOOR) {
		memcpy(desired, r_floorcolor.color, 3);
	}
	else {
		memcpy(desired, r_wallcolor.color, 3);
	}
}

static void GLC_DrawFlat(model_t *model, qbool polygonOffset)
{
	int index_count = 0;

	msurface_t *s, *prev;
	int k;
	float *v;
	qbool draw_caustics = R_TextureReferenceIsValid(underwatertexture) && gl_caustics.value;
	qbool first_surf = true;
	qbool use_vbo = buffers.supported && modelIndexes;
	int last_lightmap = -2;
	extern cvar_t gl_program_world;
	int i;
	qbool first_lightmap_surf = true;
	unsigned int lightmap_count = R_LightmapCount();

	R_TraceEnterFunctionRegion;

	if (use_vbo && gl_program_world.integer && GL_Supported(R_SUPPORT_RENDERING_SHADERS) && GLC_DrawflatProgramCompile()) {
		extern cvar_t r_watercolor, r_slimecolor, r_lavacolor, r_telecolor;
		float color[4];

		color[3] = 1.0f;

		R_ProgramUse(r_program_world_drawflat_glc);
		VectorScale(r_wallcolor.color, 1.0f / 255.0f, color);
		R_ProgramUniform4fv(r_program_uniform_world_drawflat_glc_wallcolor, color);
		VectorScale(r_floorcolor.color, 1.0f / 255.0f, color);
		R_ProgramUniform4fv(r_program_uniform_world_drawflat_glc_floorcolor, color);
		VectorScale(r_skycolor.color, 1.0f / 255.0f, color);
		R_ProgramUniform4fv(r_program_uniform_world_drawflat_glc_skycolor, color);
		VectorScale(r_watercolor.color, 1.0f / 255.0f, color);
		R_ProgramUniform4fv(r_program_uniform_world_drawflat_glc_watercolor, color);
		VectorScale(r_slimecolor.color, 1.0f / 255.0f, color);
		R_ProgramUniform4fv(r_program_uniform_world_drawflat_glc_slimecolor, color);
		VectorScale(r_lavacolor.color, 1.0f / 255.0f, color);
		R_ProgramUniform4fv(r_program_uniform_world_drawflat_glc_lavacolor, color);
		VectorScale(r_telecolor.color, 1.0f / 255.0f, color);
		R_ProgramUniform4fv(r_program_uniform_world_drawflat_glc_telecolor, color);

		if (model->drawflat_chain) {
			R_ApplyRenderingState(r_state_drawflat_without_lightmaps_glc);
			R_CustomPolygonOffset(polygonOffset ? r_polygonoffset_standard : r_polygonoffset_disabled);

			// drawflat_chain has no lightmaps
			s = model->drawflat_chain;
			while (s) {
				glpoly_t *p;

				for (p = s->polys; p; p = p->next) {
					index_count = GLC_DrawIndexedPoly(p, modelIndexes, modelIndexMaximum, index_count);
				}

				// START shaman FIX /r_drawflat + /gl_caustics {
				if ((s->flags & SURF_UNDERWATER) && draw_caustics) {
					s->polys->caustics_chain = caustics_polys;
					caustics_polys = s->polys;
				}
				// } END shaman FIX /r_drawflat + /gl_caustics

				prev = s;
				s = s->drawflatchain;
				prev->drawflatchain = NULL;
			}
			if (index_count) {
				GL_DrawElements(GL_TRIANGLE_STRIP, index_count, GL_UNSIGNED_INT, modelIndexes);
				index_count = 0;
			}

			model->drawflat_chain = NULL;
		}

		// go through lightmap chains
		for (i = 0; i < lightmap_count; ++i) {
			msurface_t* surf = R_DrawflatLightmapChain(i);

			if (surf) {
				if (first_lightmap_surf) {
					R_ApplyRenderingState(r_state_drawflat_with_lightmaps_glc);
					R_CustomPolygonOffset(polygonOffset ? r_polygonoffset_standard : r_polygonoffset_disabled);
				}

				GLC_SetTextureLightmap(0, i);
				while (surf) {
					glpoly_t* p;

					for (p = surf->polys; p; p = p->next) {
						index_count = GLC_DrawIndexedPoly(p, modelIndexes, modelIndexMaximum, index_count);
					}

					prev = surf;
					surf = surf->drawflatchain;
					prev->drawflatchain = NULL;
				}
				R_ClearDrawflatLightmapChain(i);

				if (index_count) {
					GL_DrawElements(GL_TRIANGLE_STRIP, index_count, GL_UNSIGNED_INT, modelIndexes);
					index_count = 0;
				}
			}
		}
		R_ProgramUse(r_program_none);
	}
	else {
		byte current[3] = { 255, 255, 255 }, desired[4] = { 255, 255, 255, 255 };

		s = model->drawflat_chain;
		while (s) {
			GLC_SurfaceColor(s, desired);

			if (first_surf || (use_vbo && (desired[0] != current[0] || desired[1] != current[1] || desired[2] != current[2]))) {
				if (index_count) {
					GL_DrawElements(GL_TRIANGLE_STRIP, index_count, GL_UNSIGNED_INT, modelIndexes);
					index_count = 0;
				}
				if (first_surf) {
					R_ApplyRenderingState(r_state_drawflat_without_lightmaps_glc);
					R_CustomPolygonOffset(polygonOffset ? r_polygonoffset_standard : r_polygonoffset_disabled);
				}
				R_CustomColor4ubv(desired);
			}

			VectorCopy(desired, current);
			first_surf = false;

			{
				glpoly_t *p;
				for (p = s->polys; p; p = p->next) {
					v = p->verts[0];

					if (use_vbo) {
						index_count = GLC_DrawIndexedPoly(p, modelIndexes, modelIndexMaximum, index_count);
					}
					else {
						R_CustomColor4ubv(desired);
						GLC_Begin(GL_TRIANGLE_STRIP);
						for (k = 0; k < p->numverts; k++, v += VERTEXSIZE) {
							GLC_Vertex3fv(v);
						}
						GLC_End();
					}
				}
			}

			// START shaman FIX /r_drawflat + /gl_caustics {
			if ((s->flags & SURF_UNDERWATER) && draw_caustics) {
				s->polys->caustics_chain = caustics_polys;
				caustics_polys = s->polys;
			}
			// } END shaman FIX /r_drawflat + /gl_caustics

			prev = s;
			s = s->drawflatchain;
			prev->drawflatchain = NULL;
		}

		if (index_count) {
			GL_DrawElements(GL_TRIANGLE_STRIP, index_count, GL_UNSIGNED_INT, modelIndexes);
			index_count = 0;
		}

		first_surf = true;
		for (i = 0; i < lightmap_count; ++i) {
			msurface_t* surf = R_DrawflatLightmapChain(i);

			if (surf) {
				GLC_SetTextureLightmap(0, i);
				while (surf) {
					glpoly_t* p;

					GLC_SurfaceColor(surf, desired);

					if (first_surf || (use_vbo && (desired[0] != current[0] || desired[1] != current[1] || desired[2] != current[2]))) {
						if (index_count) {
							GL_DrawElements(GL_TRIANGLE_STRIP, index_count, GL_UNSIGNED_INT, modelIndexes);
							index_count = 0;
						}
						if (first_surf) {
							R_ApplyRenderingState(r_state_drawflat_with_lightmaps_glc);
							R_CustomPolygonOffset(polygonOffset ? r_polygonoffset_standard : r_polygonoffset_disabled);
						}
						R_CustomColor4ubv(desired);
					}
					VectorCopy(desired, current);
					first_surf = false;

					for (p = surf->polys; p; p = p->next) {
						v = p->verts[0];

						if (use_vbo) {
							index_count = GLC_DrawIndexedPoly(p, modelIndexes, modelIndexMaximum, index_count);
						}
						else {
							R_CustomColor4ubv(desired);
							GLC_Begin(GL_TRIANGLE_STRIP);
							for (k = 0; k < p->numverts; k++, v += VERTEXSIZE) {
								glTexCoord2f(v[5], v[6]);
								GLC_Vertex3fv(v);
							}
							GLC_End();
						}
					}

					prev = surf;
					surf = surf->drawflatchain;
					prev->drawflatchain = NULL;
				}
				R_ClearDrawflatLightmapChain(i);

				if (index_count) {
					GL_DrawElements(GL_TRIANGLE_STRIP, index_count, GL_UNSIGNED_INT, modelIndexes);
					index_count = 0;
				}
			}
		}

		if (index_count) {
			GL_DrawElements(GL_TRIANGLE_STRIP, index_count, GL_UNSIGNED_INT, modelIndexes);
		}
	}

	model->drawflat_chain = NULL;
	model->drawflat_todo = false;

	// START shaman FIX /r_drawflat + /gl_caustics {
	if (gl_caustics.integer && caustics_polys && R_TextureReferenceIsValid(underwatertexture)) {
		GLC_EmitCausticsPolys(use_vbo);
	}
	caustics_polys = NULL;
	// } END shaman FIX /r_drawflat + /gl_caustics

	R_TraceLeaveFunctionRegion;
}

static void GLC_DrawTextureChains(entity_t* ent, model_t *model, qbool caustics, qbool polygonOffset)
{
	extern cvar_t gl_lumatextures;
	extern cvar_t gl_textureless;
	int index_count = 0;
	r_state_id state = r_state_world_singletexture_glc;
	texture_ref fb_texturenum = null_texture_reference;
	int i, k;
	msurface_t *s, *prev;
	float *v;

	qbool draw_caustics = R_TextureReferenceIsValid(underwatertexture) && gl_caustics.integer;
	qbool draw_details = R_TextureReferenceIsValid(detailtexture) && gl_detail.integer;
	qbool isLumaTexture;
	qbool use_vbo = buffers.supported && modelIndexes;

	qbool drawfullbrights = false;
	qbool drawlumas = false;
	qbool useLumaTextures = gl_lumatextures.integer && r_refdef2.allow_lumas;

	qbool texture_change;
	texture_ref current_material = null_texture_reference;
	texture_ref current_material_fb = null_texture_reference;
	texture_ref null_fb_texture = null_texture_reference;
	int current_lightmap = -1;
	int fbTextureUnit = -1;
	int lmTextureUnit = -1;

	texture_ref desired_textures[4];
	int texture_unit_count = 1;

	qbool draw_textureless = gl_textureless.integer && model->isworldmodel;

	// if (!gl_fb_bmodels)
	//   (material + fullbright) * lightmap
	// else
	//   material * lightmap + fullbright

	R_TraceEnterFunctionRegion;

	// If there are no luma textures in the chain, disable luma textures (saves enabling 3rd unit etc)
	useLumaTextures &= model->texturechains_have_lumas;

	// For each texture we have to draw, it may or may not have a luma, so the old code changed
	//   up the bindings/allocations with each texture (binding lightmap to each unit with luma textures on/off)
	// Instead we keep 0 for material, 1 for (fullbright/lightmap), 2 for (fullbright/lightmap)
	//   and if fullbright/luma texture not available, we enable/disable texture unit 1 or 2
	// Default: no multi-texturing, each pass done at the bottom
	if (gl_mtexable) {
		texture_unit_count = gl_textureunits >= 3 ? 3 : 2;

		if (useLumaTextures && !gl_fb_bmodels.integer) {
			// blend(material + fb, lightmap)
			state = r_state_world_material_fb_lightmap;
			null_fb_texture = solidblack_texture;
			fbTextureUnit = 1;
			lmTextureUnit = (gl_textureunits >= 3 ? 2 : -1);
		}
		else if (useLumaTextures) {
			// blend(material, lightmap) + luma
			state = r_state_world_material_lightmap_luma;
			lmTextureUnit = 1;
			fbTextureUnit = (gl_textureunits >= 3 ? 2 : -1);
			null_fb_texture = solidblack_texture; // GL_ADD adds colors, multiplies alphas
		}
		else {
			// blend(material, lightmap) 
			state = r_state_world_material_lightmap;
			lmTextureUnit = 1;
			fbTextureUnit = -1;
			null_fb_texture = solidblack_texture; // GL_ADD adds colors, multiplies alphas
			texture_unit_count = 2;
		}
	}
	else {
		state = r_state_world_singletexture_glc;
	}

	R_ApplyRenderingState(state);
	if (ent && ent->alpha) {
		R_CustomColor(ent->alpha, ent->alpha, ent->alpha, ent->alpha);
	}
	if (polygonOffset) {
		R_CustomPolygonOffset(r_polygonoffset_standard);
	}

	//Tei: textureless for the world brush models (Qrack)
	if (draw_textureless) {
		if (use_vbo) {
			// meag: better to have different states for this
			R_GLC_DisableTexturePointer(0);
			if (fbTextureUnit >= 0) {
				R_GLC_DisableTexturePointer(fbTextureUnit);
			}
		}

		if (qglMultiTexCoord2f) {
			qglMultiTexCoord2f(GL_TEXTURE0, 0, 0);

			if (fbTextureUnit >= 0) {
				qglMultiTexCoord2f(GL_TEXTURE0 + fbTextureUnit, 0, 0);
			}
		}
		else {
			glTexCoord2f(0, 0);
		}
	}

	for (i = 0; i < model->numtextures; i++) {
		texture_t* t;

		if (!model->textures[i] || !model->textures[i]->texturechain) {
			continue;
		}

		t = R_TextureAnimation(ent, model->textures[i]);
		if (t->isLumaTexture) {
			isLumaTexture = useLumaTextures;
			fb_texturenum = isLumaTexture ? t->fb_texturenum : null_fb_texture;
		}
		else {
			isLumaTexture = false;
			fb_texturenum = null_fb_texture;
		}

		//bind the world texture
		texture_change = !R_TextureReferenceEqual(t->gl_texturenum, current_material);
		texture_change |= !R_TextureReferenceEqual(fb_texturenum, current_material_fb);

		current_material = t->gl_texturenum;
		current_material_fb = fb_texturenum;

		desired_textures[0] = current_material;
		if (fbTextureUnit >= 0) {
			desired_textures[fbTextureUnit] = current_material_fb;
		}

		s = model->textures[i]->texturechain;
		while (s) {
			if (!(s->texinfo->flags & TEX_SPECIAL)) {
				if (lmTextureUnit >= 0) {
					texture_change |= (s->lightmaptexturenum != current_lightmap);

					desired_textures[lmTextureUnit] = GLC_LightmapTexture(s->lightmaptexturenum);
					current_lightmap = s->lightmaptexturenum;
				}
				else {
					GLC_AddToLightmapChain(s);
				}

				if (texture_change) {
					if (index_count) {
						GL_DrawElements(GL_TRIANGLE_STRIP, index_count, GL_UNSIGNED_INT, modelIndexes);
						index_count = 0;
					}

					renderer.TextureUnitMultiBind(0, texture_unit_count, desired_textures);
					if (r_lightmap_lateupload.integer && lmTextureUnit >= 0) {
						R_UploadLightMap(lmTextureUnit, current_lightmap);
					}

					texture_change = false;
				}

				if (use_vbo) {
					index_count = GLC_DrawIndexedPoly(s->polys, modelIndexes, modelIndexMaximum, index_count);
				}
				else {
					v = s->polys->verts[0];

					GLC_Begin(GL_TRIANGLE_STRIP);
					for (k = 0; k < s->polys->numverts; k++, v += VERTEXSIZE) {
						if (lmTextureUnit >= 0) {
							qglMultiTexCoord2f(GL_TEXTURE0 + lmTextureUnit, v[5], v[6]);
						}

						if (!draw_textureless) {
							if (qglMultiTexCoord2f) {
								qglMultiTexCoord2f(GL_TEXTURE0, v[3], v[4]);

								if (fbTextureUnit >= 0) {
									qglMultiTexCoord2f(GL_TEXTURE0 + fbTextureUnit, v[3], v[4]);
								}
							}
							else {
								glTexCoord2f(v[3], v[4]);
							}
						}
						GLC_Vertex3fv(v);
					}
					GLC_End();
				}
			}

			if (draw_caustics && ((s->flags & SURF_UNDERWATER) || caustics)) {
				s->polys->caustics_chain = caustics_polys;
				caustics_polys = s->polys;
			}

			if (!(s->flags & SURF_UNDERWATER) && draw_details) {
				s->polys->detail_chain = detail_polys;
				detail_polys = s->polys;
			}

			if (!R_TextureReferenceEqual(fb_texturenum, null_fb_texture) && gl_fb_bmodels.integer && fbTextureUnit < 0) {
				if (isLumaTexture) {
					s->polys->luma_chain = luma_polys[fb_texturenum.index];
					luma_polys[fb_texturenum.index] = s->polys;
					drawlumas = true;
				}
				else {
					s->polys->fb_chain = fullbright_polys[fb_texturenum.index];
					fullbright_polys[fb_texturenum.index] = s->polys;
					drawfullbrights = true;
				}
			}

			prev = s;
			s = s->texturechain;
			prev->texturechain = NULL;
		}
	}

	if (index_count) {
		GL_DrawElements(GL_TRIANGLE_STRIP, index_count, GL_UNSIGNED_INT, modelIndexes);
	}

	if (gl_fb_bmodels.integer) {
		if (lmTextureUnit < 0) {
			GLC_BlendLightmaps();
		}
		if (drawfullbrights) {
			GLC_RenderFullbrights();
			drawfullbrights = false;
		}
		if (drawlumas) {
			GLC_RenderLumas();
			drawlumas = false;
		}
	}
	else {
		if (drawlumas) {
			GLC_RenderLumas();
			drawlumas = false;
		}
		if (lmTextureUnit < 0) {
			GLC_BlendLightmaps();
		}
		if (drawfullbrights) {
			GLC_RenderFullbrights();
			drawfullbrights = false;
		}
	}

	GLC_EmitCausticsPolys(use_vbo);
	GLC_EmitDetailPolys(use_vbo);

	R_TraceLeaveFunctionRegion;
}

void GLC_DrawWorld(void)
{
	extern msurface_t* alphachain;

	R_TraceEnterFunctionRegion;

	if (r_drawflat.integer != 1) {
		GLC_DrawTextureChains(NULL, cl.worldmodel, false, false);
	}
	if (cl.worldmodel->drawflat_todo) {
		GLC_DrawFlat(cl.worldmodel, false);
	}

	if (R_DrawWorldOutlines()) {
		GLC_DrawMapOutline(cl.worldmodel);
	}

	//draw the world alpha textures
	GLC_DrawAlphaChain(alphachain, polyTypeWorldModel);

	R_TraceLeaveFunctionRegion;
}

void GLC_DrawBrushModel(entity_t* e, qbool polygonOffset, qbool caustics)
{
	extern msurface_t* alphachain;
	model_t* clmodel = e->model;

	if (r_drawflat.integer && clmodel->isworldmodel) {
		if (r_drawflat.integer == 1) {
			GLC_DrawFlat(clmodel, polygonOffset);
		}
		else {
			GLC_DrawTextureChains(e, clmodel, caustics, polygonOffset);
			GLC_DrawFlat(clmodel, polygonOffset);
		}
	}
	else {
		GLC_DrawTextureChains(e, clmodel, caustics, polygonOffset);
	}

	if (clmodel->isworldmodel && R_DrawWorldOutlines()) {
		GLC_DrawMapOutline(clmodel);
	}

	GLC_SkyDrawChainedSurfaces();
	GLC_DrawAlphaChain(alphachain, polyTypeBrushModel);
}

/*
// This populates VBO, splitting up by lightmap for efficient
//   rendering when not using texture arrays
int GLC_PopulateVBOForBrushModel(model_t* m, float* vbo_buffer, int vbo_pos)
{
	int i, j;
	int combinations = 0;
	int original_pos = vbo_pos;

	for (i = 0; i < m->numtextures; ++i) {
		if (m->textures[i]) {
			m->textures[i]->gl_first_lightmap = -1;
			for (j = 0; j < MAX_LIGHTMAPS; ++j) {
				m->textures[i]->gl_next_lightmap[j] = -1;
			}
		}
	}

	// Order vertices in the VBO by texture & lightmap
	for (i = 0; i < m->numtextures; ++i) {
		int lightmap = -1;
		int length = 0;
		int surface_count = 0;
		int tex_vbo_start = vbo_pos;

		if (!m->textures[i]) {
			continue;
		}

		// Find first lightmap for this texture
		for (j = 0; j < m->numsurfaces; ++j) {
			msurface_t* surf = m->surfaces + j;

			if (surf->texinfo->miptex != i) {
				continue;
			}

			if (!(surf->flags & (SURF_DRAWTURB | SURF_DRAWSKY))) {
				if (surf->texinfo->flags & TEX_SPECIAL) {
					continue;
				}
			}

			if (surf->lightmaptexturenum >= 0 && (lightmap < 0 || surf->lightmaptexturenum < lightmap)) {
				lightmap = surf->lightmaptexturenum;
			}
		}

		m->textures[i]->gl_first_lightmap = lightmap;

		// Build the VBO in order of lightmaps...
		while (lightmap >= 0) {
			int next_lightmap = -1;

			length = 0;
			m->textures[i]->gl_vbo_start[lightmap] = vbo_pos / VERTEXSIZE;
			++combinations;

			for (j = 0; j < m->numsurfaces; ++j) {
				msurface_t* surf = m->surfaces + j;
				glpoly_t* poly;

				if (surf->texinfo->miptex != i) {
					continue;
				}
				if (surf->lightmaptexturenum > lightmap && (next_lightmap < 0 || surf->lightmaptexturenum < next_lightmap)) {
					next_lightmap = surf->lightmaptexturenum;
				}

				if (surf->lightmaptexturenum == lightmap) {
					// copy verts into buffer (alternate to turn fan into triangle strip)
					for (poly = surf->polys; poly; poly = poly->next) {
						int end_vert = 0;
						int start_vert = 1;
						int output = 0;
						int material = m->textures[i]->gl_texture_index;
						float scaleS = m->textures[i]->gl_texture_scaleS;
						float scaleT = m->textures[i]->gl_texture_scaleT;

						if (!poly->numverts) {
							continue;
						}

						// Store position for drawing individual polys
						poly->vbo_start = vbo_pos / VERTEXSIZE;
						vbo_pos = CopyVertToBuffer(vbo_buffer, vbo_pos, poly->verts[0], surf->lightmaptexturenum, material, scaleS, scaleT);
						++output;

						start_vert = 1;
						end_vert = poly->numverts - 1;

						while (start_vert <= end_vert) {
							vbo_pos = CopyVertToBuffer(vbo_buffer, vbo_pos, poly->verts[start_vert], surf->lightmaptexturenum, material, scaleS, scaleT);
							++output;

							if (start_vert < end_vert) {
								vbo_pos = CopyVertToBuffer(vbo_buffer, vbo_pos, poly->verts[end_vert], surf->lightmaptexturenum, material, scaleS, scaleT);
								++output;
							}

							++start_vert;
							--end_vert;
						}

						length += poly->numverts;
						++surface_count;
					}
				}
			}

			m->textures[i]->gl_vbo_length[lightmap] = length;
			m->textures[i]->gl_next_lightmap[lightmap] = next_lightmap;
			lightmap = next_lightmap;
		}
	}

	Con_Printf("%s = %d verts, reserved %d\n", m->name, (vbo_pos - original_pos) / VERTEXSIZE, GLM_MeasureVBOSizeForBrushModel(m));
	return vbo_pos;
}
*/

static void GLC_BlendLightmaps(void)
{
	int i, j;
	glpoly_t *p;
	float *v;
	qbool use_vbo = buffers.supported && modelIndexes;

	GLC_StateBeginBlendLightmaps(use_vbo);
	
	for (i = 0; i < GLC_LightmapCount(); i++) {
		if (!(p = GLC_LightmapChain(i))) {
			continue;
		}

		GLC_LightmapUpdate(i);
		renderer.TextureUnitBind(0, GLC_LightmapTexture(i));
		if (use_vbo) {
			GLuint index_count = 0;

			for (; p; p = p->chain) {
				index_count = GLC_DrawIndexedPoly(p, modelIndexes, modelIndexMaximum, index_count);
			}

			if (index_count) {
				GL_DrawElements(GL_TRIANGLE_STRIP, index_count, GL_UNSIGNED_INT, modelIndexes);
			}
		}
		else {
			for (; p; p = p->chain) {
				GLC_Begin(GL_TRIANGLE_STRIP);
				v = p->verts[0];
				for (j = 0; j < p->numverts; j++, v += VERTEXSIZE) {
					glTexCoord2f(v[5], v[6]);
					GLC_Vertex3fv(v);
				}
				GLC_End();
			}
		}
	}
	GLC_ClearLightmapPolys();
}

//draws transparent textures for HL world and nonworld models
void GLC_DrawAlphaChain(msurface_t* alphachain, frameStatsPolyType polyType)
{
	int k;
	msurface_t *s;
	float *v;

	if (!alphachain) {
		return;
	}

	GLC_StateBeginAlphaChain();
	for (s = alphachain; s; s = s->texturechain) {
		++frameStats.classic.polycount[polyType];
		R_RenderDynamicLightmaps(s, false);

		GLC_StateBeginAlphaChainSurface(s);

		GLC_Begin(GL_TRIANGLE_STRIP);
		v = s->polys->verts[0];
		for (k = 0; k < s->polys->numverts; k++, v += VERTEXSIZE) {
			if (gl_mtexable) {
				qglMultiTexCoord2f(GL_TEXTURE0, v[3], v[4]);
				qglMultiTexCoord2f(GL_TEXTURE1, v[5], v[6]);
			}
			else {
				glTexCoord2f(v[3], v[4]);
			}
			GLC_Vertex3fv(v);
		}
		GLC_End();
	}
	alphachain = NULL;
}

int GLC_BrushModelCopyVertToBuffer(model_t* mod, void* vbo_buffer_, int position, float* source, int lightmap, int material, float scaleS, float scaleT, msurface_t* surf, qbool has_luma_texture)
{
	glc_vbo_world_vert_t* target = (glc_vbo_world_vert_t*)vbo_buffer_ + position;

	VectorCopy(source, target->position);
	target->material_coords[0] = source[3];
	target->material_coords[1] = source[4];
	target->lightmap_coords[0] = source[5];
	target->lightmap_coords[1] = source[6];
	target->detail_coords[0] = source[7];
	target->detail_coords[1] = source[8];
	if (scaleS) {
		target->material_coords[0] *= scaleS;
	}
	if (scaleT) {
		target->material_coords[1] *= scaleT;
	}

	target->flatstyle = 0;
	if (surf->flags & SURF_DRAWSKY) {
		target->flatstyle = 1;
	}
	else if (surf->flags & SURF_DRAWTURB) {
		if (surf->texinfo->texture->turbType == TEXTURE_TURB_WATER) {
			target->flatstyle = 2;
		}
		else if (surf->texinfo->texture->turbType == TEXTURE_TURB_SLIME) {
			target->flatstyle = 4;
		}
		else if (surf->texinfo->texture->turbType == TEXTURE_TURB_LAVA) {
			target->flatstyle = 8;
		}
		else if (surf->texinfo->texture->turbType == TEXTURE_TURB_TELE) {
			target->flatstyle = 16;
		}
		else if (surf->texinfo->texture->turbType == TEXTURE_TURB_SKY) {
			target->flatstyle = 32;
		}
	}
	else if (surf->flags & SURF_DRAWFLAT_FLOOR) {
		target->flatstyle = 64;
	}
	else {
		target->flatstyle = 128;
	}

	return position + 1;
}

void GLC_ChainBrushModelSurfaces(model_t* clmodel, entity_t* ent)
{
	extern void GLC_EmitWaterPoly(msurface_t* fa);
	qbool glc_first_water_poly = true;
	msurface_t* psurf;
	int i;
	qbool drawFlatFloors = clmodel->isworldmodel && (r_drawflat.integer == 2 || r_drawflat.integer == 1);
	qbool drawFlatWalls = clmodel->isworldmodel && (r_drawflat.integer == 3 || r_drawflat.integer == 1);
	extern msurface_t* skychain;
	extern msurface_t* alphachain;

	psurf = &clmodel->surfaces[clmodel->firstmodelsurface];
	for (i = 0; i < clmodel->nummodelsurfaces; i++, psurf++) {
		// find which side of the node we are on
		mplane_t* pplane = psurf->plane;
		float dot = PlaneDiff(modelorg, pplane);

		//draw the water surfaces now, and setup sky/normal chains
		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON))) {
			if (psurf->flags & SURF_DRAWSKY) {
				CHAIN_SURF_B2F(psurf, skychain);
			}
			else if (psurf->flags & SURF_DRAWTURB) {
				if (glc_first_water_poly) {
					GLC_StateBeginWaterSurfaces();
					glc_first_water_poly = false;
				}
				GLC_EmitWaterPoly(psurf);
			}
			else if (psurf->flags & SURF_DRAWALPHA) {
				CHAIN_SURF_B2F(psurf, alphachain);
			}
			else {
				if (drawFlatFloors && (psurf->flags & SURF_DRAWFLAT_FLOOR)) {
					chain_surfaces_drawflat(&clmodel->drawflat_chain, psurf);
					clmodel->drawflat_todo = true;
				}
				else if (drawFlatWalls && !(psurf->flags & SURF_DRAWFLAT_FLOOR)) {
					chain_surfaces_drawflat(&clmodel->drawflat_chain, psurf);
					clmodel->drawflat_todo = true;
				}
				else {
					chain_surfaces_by_lightmap(&psurf->texinfo->texture->texturechain, psurf);

					clmodel->texturechains_have_lumas |= R_TextureAnimation(ent, psurf->texinfo->texture)->isLumaTexture;
					clmodel->first_texture_chained = min(clmodel->first_texture_chained, psurf->texinfo->miptex);
					clmodel->last_texture_chained = max(clmodel->last_texture_chained, psurf->texinfo->miptex);
				}
			}
		}
	}
}
