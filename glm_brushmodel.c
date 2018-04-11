
#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "rulesets.h"
#include "utils.h"
#include "glsl/constants.glsl"

typedef struct vbo_world_surface_s {
	float normal[4];
	float tex_vecs0[4];
	float tex_vecs1[4];
} vbo_world_surface_t;

GLuint* modelIndexes;
GLuint modelIndexMaximum;

buffer_ref brushModel_vbo;
buffer_ref worldModel_surfaces_ssbo;
buffer_ref vbo_brushElements;
glm_vao_t brushModel_vao;

// Sets tex->next_same_size to link up all textures of common size
int R_ChainTexturesBySize(model_t* m)
{
	texture_t* tx;
	int i, j;
	int num_sizes = 0;

	// Initialise chain
	for (i = 0; i < m->numtextures; ++i) {
		tx = m->textures[i];
		if (!tx || !tx->loaded) {
			continue;
		}

		tx->next_same_size = -1;
		tx->size_start = false;
	}

	for (i = 0; i < m->numtextures; ++i) {
		tx = m->textures[i];
		if (!tx || !tx->loaded || tx->next_same_size >= 0 || !tx->gl_width || !tx->gl_height || !GL_TextureReferenceIsValid(tx->gl_texturenum)) {
			continue; // not loaded or already processed
		}

		++num_sizes;
		tx->size_start = true;
		for (j = i + 1; j < m->numtextures; ++j) {
			texture_t* next = m->textures[j];
			if (!next || !next->loaded || next->next_same_size >= 0) {
				continue; // not loaded or already processed
			}

			if (tx->gl_width == next->gl_width && tx->gl_height == next->gl_height) {
				tx->next_same_size = j;
				tx = next;
				next->next_same_size = m->numtextures;
			}
		}
	}

	return num_sizes;
}

// 'source' is from GLC's float[VERTEXSIZE]
static int CopyVertToBuffer(model_t* mod, vbo_world_vert_t* vbo_buffer, int position, float* source, int lightmap, int material, float scaleS, float scaleT, msurface_t* surf, qbool has_luma_texture)
{
	vbo_world_vert_t* target = vbo_buffer + position;

	VectorCopy(source, target->position);
	target->material_coords[0] = source[3];
	target->material_coords[1] = source[4];
	target->lightmap_coords[0] = source[5] * 65535;
	target->lightmap_coords[1] = source[6] * 65535;
	target->detail_coords[0] = source[7];
	target->detail_coords[1] = source[8];
	if (scaleS) {
		target->material_coords[0] *= scaleS;
	}
	if (scaleT) {
		target->material_coords[1] *= scaleT;
	}
	target->lightmap_index = lightmap;
	target->material_index = material;

	if (surf->flags & SURF_DRAWSKY) {
		target->flags = TEXTURE_TURB_SKY;
	}
	else {
		target->flags = surf->texinfo->texture->turbType & EZQ_SURFACE_TYPE;
	}

	target->flags |=
		(surf->flags & SURF_UNDERWATER ? EZQ_SURFACE_UNDERWATER : 0) |
		(surf->flags & SURF_DRAWFLAT_FLOOR ? EZQ_SURFACE_IS_FLOOR : 0) |
		//(has_luma_texture ? EZQ_SURFACE_HAS_LUMA : 0) |
		(surf->flags & SURF_DRAWALPHA ? EZQ_SURFACE_ALPHATEST : 0);
	if (mod->isworldmodel) {
		target->flags |= EZQ_SURFACE_DETAIL;
	}
	memcpy(target->flatcolor, &surf->texinfo->texture->flatcolor3ub, sizeof(target->flatcolor));

	target->surface_num = mod->isworldmodel ? surf - mod->surfaces : 0;

	return position + 1;
}

int GL_MeasureVBOSizeForBrushModel(model_t* m)
{
	int j, total_surf_verts = 0, total_surfaces = 0;

	for (j = 0; j < m->nummodelsurfaces; ++j) {
		msurface_t* surf = m->surfaces + m->firstmodelsurface + j;
		glpoly_t* poly;

		if (!(surf->flags & (SURF_DRAWTURB | SURF_DRAWSKY))) {
			if (surf->texinfo->flags & TEX_SPECIAL) {
				continue;
			}
		}
		if (!m->textures[surf->texinfo->miptex]) {
			continue;
		}

		for (poly = surf->polys; poly; poly = poly->next) {
			total_surf_verts += poly->numverts;
			++total_surfaces;
		}
	}

	if (total_surf_verts <= 0 || total_surfaces < 1) {
		return 0;
	}

	return (total_surf_verts);// +2 * (total_surfaces - 1));
}

int GL_MeasureIndexSizeForBrushModel(model_t* m)
{
	int j, total_surf_verts = 0, total_surfaces = 0;

	for (j = 0; j < m->nummodelsurfaces; ++j) {
		msurface_t* surf = m->surfaces + m->firstmodelsurface + j;
		glpoly_t* poly;

		for (poly = surf->polys; poly; poly = poly->next) {
			total_surf_verts += poly->numverts;
			++total_surfaces;
		}
	}

	if (total_surf_verts <= 0 || total_surfaces < 1) {
		return 0;
	}

	// Every vert in every surface, + surface terminator
	return (total_surf_verts) + (total_surfaces - 1);
}

// Create VBO, ordering by texture array
int GL_PopulateVBOForBrushModel(model_t* m, vbo_world_vert_t* vbo_buffer, int vbo_pos)
{
	int i, j;
	int combinations = 0;
	int original_pos = vbo_pos;

	// Order vertices in the VBO by texture & lightmap
	for (i = 0; i < m->numtextures; ++i) {
		int lightmap = -1;
		int length = 0;
		int surface_count = 0;
		int tex_vbo_start = vbo_pos;
		qbool has_luma = false;

		if (!m->textures[i]) {
			continue;
		}

		has_luma = GL_TextureReferenceIsValid(m->textures[i]->fb_texturenum);
		for (j = 0; j < m->numsurfaces; ++j) {
			msurface_t* surf = m->surfaces + j;
			int lightmap = surf->flags & (SURF_DRAWTURB | SURF_DRAWSKY) ? -1 : surf->lightmaptexturenum;
			glpoly_t* poly;

			if (surf->texinfo->miptex != i) {
				continue;
			}

			// copy verts into buffer (alternate to turn fan into triangle strip)
			for (poly = surf->polys; poly; poly = poly->next) {
				int end_vert = 0;
				int start_vert = 1;
				int output = 0;
				int material = i;
				float scaleS = m->textures[i]->gl_texture_scaleS;
				float scaleT = m->textures[i]->gl_texture_scaleT;

				if (!poly->numverts) {
					continue;
				}

				// Store position for drawing individual polys
				poly->vbo_start = vbo_pos;
				vbo_pos = CopyVertToBuffer(m, vbo_buffer, vbo_pos, poly->verts[0], lightmap, material, scaleS, scaleT, surf, has_luma);
				++output;

				start_vert = 1;
				end_vert = poly->numverts - 1;

				while (start_vert <= end_vert) {
					vbo_pos = CopyVertToBuffer(m, vbo_buffer, vbo_pos, poly->verts[start_vert], lightmap, material, scaleS, scaleT, surf, has_luma);
					++output;

					if (start_vert < end_vert) {
						vbo_pos = CopyVertToBuffer(m, vbo_buffer, vbo_pos, poly->verts[end_vert], lightmap, material, scaleS, scaleT, surf, has_luma);
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

	return vbo_pos;
}

void GL_CreateBrushModelVAO(buffer_ref instance_vbo)
{
	int i;
	int size = 0;
	int position = 0;
	int indexes = 0;
	vbo_world_vert_t* buffer = NULL;

	for (i = 1; i < MAX_MODELS; ++i) {
		model_t* mod = cl.model_precache[i];
		if (mod && mod->type == mod_brush) {
			size += GL_MeasureVBOSizeForBrushModel(mod);
			indexes += GL_MeasureIndexSizeForBrushModel(mod);
		}
	}

	for (i = 0; i < MAX_VWEP_MODELS; i++) {
		model_t* mod = cl.vw_model_precache[i];
		if (mod && mod->type == mod_brush) {
			if (mod == cl.worldmodel || !mod->isworldmodel) {
				size += GL_MeasureVBOSizeForBrushModel(mod);
				indexes += GL_MeasureIndexSizeForBrushModel(mod);
			}
		}
	}

	// Create vbo buffer
	buffer = Q_malloc(size * sizeof(vbo_world_vert_t));

	// Create vao
	GL_GenVertexArray(&brushModel_vao, "brushmodel-vao");

	if (indexes > modelIndexMaximum) {
		Q_free(modelIndexes);
		modelIndexMaximum = indexes;
		modelIndexes = Q_malloc(sizeof(*modelIndexes) * modelIndexMaximum);

		if (GL_BufferReferenceIsValid(vbo_brushElements)) {
			GL_ResizeBuffer(vbo_brushElements, modelIndexMaximum * sizeof(modelIndexes[0]), NULL);
		}
	}

	if (!GL_BufferReferenceIsValid(vbo_brushElements)) {
		vbo_brushElements = GL_GenFixedBuffer(GL_ELEMENT_ARRAY_BUFFER, "brushmodel-elements", modelIndexMaximum * sizeof(modelIndexes[0]), NULL, GL_STREAM_DRAW);
	}
	GL_BindBuffer(vbo_brushElements);

	// Copy data into buffer
	for (i = 1; i < MAX_MODELS; ++i) {
		model_t* mod = cl.model_precache[i];
		if (mod && mod->type == mod_brush) {
			if (mod == cl.worldmodel || !mod->isworldmodel) {
				position = GL_PopulateVBOForBrushModel(mod, buffer, position);
			}
		}
	}

	for (i = 0; i < MAX_VWEP_MODELS; i++) {
		model_t* mod = cl.vw_model_precache[i];
		if (mod && mod->type == mod_brush) {
			position = GL_PopulateVBOForBrushModel(mod, buffer, position);
		}
	}

	// Copy VBO buffer across
	brushModel_vbo = GL_GenFixedBuffer(GL_ARRAY_BUFFER, "brushmodel-vbo", size * sizeof(vbo_world_vert_t), buffer, GL_STATIC_DRAW);

	GL_ConfigureVertexAttribPointer(&brushModel_vao, brushModel_vbo, 0, 3, GL_FLOAT, GL_FALSE, sizeof(vbo_world_vert_t), VBO_FIELDOFFSET(vbo_world_vert_t, position));
	GL_ConfigureVertexAttribPointer(&brushModel_vao, brushModel_vbo, 1, 2, GL_FLOAT, GL_FALSE, sizeof(vbo_world_vert_t), VBO_FIELDOFFSET(vbo_world_vert_t, material_coords));
	GL_ConfigureVertexAttribPointer(&brushModel_vao, brushModel_vbo, 2, 2, GL_UNSIGNED_SHORT, GL_TRUE, sizeof(vbo_world_vert_t), VBO_FIELDOFFSET(vbo_world_vert_t, lightmap_coords));
	GL_ConfigureVertexAttribPointer(&brushModel_vao, brushModel_vbo, 3, 2, GL_FLOAT, GL_FALSE, sizeof(vbo_world_vert_t), VBO_FIELDOFFSET(vbo_world_vert_t, detail_coords));
	GL_ConfigureVertexAttribIPointer(&brushModel_vao, brushModel_vbo, 4, 1, GL_SHORT, sizeof(vbo_world_vert_t), VBO_FIELDOFFSET(vbo_world_vert_t, lightmap_index));
	GL_ConfigureVertexAttribIPointer(&brushModel_vao, brushModel_vbo, 5, 1, GL_SHORT, sizeof(vbo_world_vert_t), VBO_FIELDOFFSET(vbo_world_vert_t, material_index));
	// 
	GL_ConfigureVertexAttribIPointer(&brushModel_vao, instance_vbo, 6, 1, GL_UNSIGNED_INT, sizeof(GLuint), 0);
	GL_ConfigureVertexAttribIPointer(&brushModel_vao, brushModel_vbo, 7, 1, GL_UNSIGNED_BYTE, sizeof(vbo_world_vert_t), VBO_FIELDOFFSET(vbo_world_vert_t, flags));
	GL_ConfigureVertexAttribIPointer(&brushModel_vao, brushModel_vbo, 8, 3, GL_UNSIGNED_BYTE, sizeof(vbo_world_vert_t), VBO_FIELDOFFSET(vbo_world_vert_t, flatcolor));
	GL_ConfigureVertexAttribIPointer(&brushModel_vao, brushModel_vbo, 9, 1, GL_UNSIGNED_INT, sizeof(vbo_world_vert_t), VBO_FIELDOFFSET(vbo_world_vert_t, surface_num));

	glVertexAttribDivisor(6, 1);

	Q_free(buffer);

	// Create surface information SSBO
	if (cl.worldmodel) {
		worldModel_surfaces_ssbo = GL_GenFixedBuffer(GL_SHADER_STORAGE_BUFFER, "brushmodel-surfs", cl.worldmodel->numsurfaces * sizeof(vbo_world_surface_t), NULL, GL_STATIC_COPY);
		for (i = 0; i < cl.worldmodel->numsurfaces; ++i) {
			msurface_t* surf = cl.worldmodel->surfaces + i;
			vbo_world_surface_t vboSurface;

			VectorCopy(surf->plane->normal, vboSurface.normal);
			vboSurface.normal[3] = surf->plane->dist;
			memcpy(vboSurface.tex_vecs0, surf->texinfo->vecs[0], sizeof(surf->texinfo->vecs[0]));
			memcpy(vboSurface.tex_vecs1, surf->texinfo->vecs[1], sizeof(surf->texinfo->vecs[1]));

			GL_UpdateBufferSection(worldModel_surfaces_ssbo, i * sizeof(vbo_world_surface_t), sizeof(vbo_world_surface_t), &vboSurface);
		}
		GL_BindBufferBase(worldModel_surfaces_ssbo, GL_BINDINGPOINT_WORLDMODEL_SURFACES);
	}
}

