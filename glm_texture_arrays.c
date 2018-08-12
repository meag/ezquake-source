
#include "quakedef.h"
#include "vx_stuff.h"
#include "vx_tracker.h"
#include "gl_model.h"
#include "gl_local.h"
#include "rulesets.h"
#ifndef  __APPLE__
#include "tr_types.h"
#endif

static unsigned int model_vao = 0;
static unsigned int instance_vbo = 0;

void GL_BuildCommonTextureArrays(void);
static void GLM_CreatePowerupShellTexture(GLuint texture_array, int maxWidth, int maxHeight, int slice);
static void GLM_CreateBrushModelVAO(void);

static qbool BrushModelIsAnySize(model_t* mod)
{
	return false;
}

#define MAX_INSTANCES 64

static void GLM_CreateModelVAO(GLuint model_vbo, GLuint required_vbo_length, float* new_vbo_buffer);
static void GLM_CreateInstanceVBO(void)
{
	unsigned int values[MAX_INSTANCES];
	int i;

	glGenBuffers(1, &instance_vbo);
	glBindBufferExt(GL_ARRAY_BUFFER, instance_vbo);

	for (i = 0; i < MAX_INSTANCES; ++i) {
		values[i] = i;
	}

	glBufferDataExt(GL_ARRAY_BUFFER, sizeof(values), values, GL_STATIC_DRAW);
}

typedef struct common_texture_s {
	int width;
	int height;
	int count;
	int any_size_count;
	GLuint gl_texturenum;
	int gl_width;
	int gl_height;

	int allocated;

	struct common_texture_s* next;
} common_texture_t;

void GL_RegisterCommonTextureSize(common_texture_t* list, GLint texture, qbool any_size)
{
	GLint width, height;

	if (!texture) {
		return;
	}

	GL_Bind(texture);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);

	while (list) {
		if (list->width == width && list->height == height) {
			list->count++;
			if (any_size) {
				list->any_size_count++;
			}
			break;
		}
		else if (!list->next) {
			list->next = Q_malloc(sizeof(common_texture_t));
			list->next->width = width;
			list->next->height = height;
		}

		list = list->next;
	}
}

GLuint GL_CreateTextureArray(int width, int height, int depth)
{
	GLuint gl_texturenum;
	int max_miplevels = 0;
	int min_dimension = min(width, height);

	// 
	while (min_dimension > 0) {
		max_miplevels++;
		min_dimension /= 2;
	}

	glGenTextures(1, &gl_texturenum);
	glBindTexture(GL_TEXTURE_2D_ARRAY, gl_texturenum);
	glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
	glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexStorage3D(GL_TEXTURE_2D_ARRAY, max_miplevels, GL_RGBA8, width, height, depth);

	return gl_texturenum;
}

void GL_AddTextureToArray(GLuint arrayTexture, int width, int height, int index)
{
	int level = 0;
	GLubyte* buffer;

	buffer = Q_malloc(width * height * 4 * sizeof(GLubyte));

	glBindTexture(GL_TEXTURE_2D_ARRAY, arrayTexture);
	for (level = 0; width && height; ++level, width /= 2, height /= 2) {
		glGetTexImage(GL_TEXTURE_2D, level, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
		glTexSubImage3D(GL_TEXTURE_2D_ARRAY, level, 0, 0, index, width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
	}

	Q_free(buffer);
}

common_texture_t* GL_FindTextureBySize(common_texture_t* list, int width, int height)
{
	while (list) {
		if (list->width == width && list->height == height) {
			return list;
		}

		list = list->next;
	}

	return NULL;
}

void GL_CopyToTextureArraySize(common_texture_t* list, GLuint stdTexture, qbool anySize, float* scaleS, float* scaleT, GLuint* texture_array, GLuint* texture_array_index)
{
	GLint width, height;
	common_texture_t* tex;

	if (!stdTexture) {
		if (scaleS && scaleT) {
			*scaleS = *scaleT = 0;
		}
		if (texture_array_index) {
			*texture_array_index = -1;
		}
		if (texture_array) {
			*texture_array = -1;
		}
		return;
	}

	GL_Bind(stdTexture);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);

	if (anySize) {
		tex = GL_FindTextureBySize(list, 0, 0);
	}
	else {
		tex = GL_FindTextureBySize(list, width, height);
		if (!tex->gl_texturenum) {
			tex->gl_texturenum = GL_CreateTextureArray(width, height, tex->count - tex->any_size_count);
			tex->gl_width = width;
			tex->gl_height = height;
		}
	}
	if (scaleS && scaleT) {
		*scaleS = width * 1.0f / tex->gl_width;
		*scaleT = height * 1.0f / tex->gl_height;
	}

	GL_AddTextureToArray(tex->gl_texturenum, width, height, tex->allocated);
	if (texture_array) {
		*texture_array = tex->gl_texturenum;
	}
	if (texture_array_index) {
		*texture_array_index = tex->allocated;
	}
	tex->allocated++;
}

void GL_FreeTextureSizeList(common_texture_t* tex)
{
	common_texture_t* next = tex->next;
	while (next) {
		Q_free(tex);
		tex = next;
		next = tex->next;
	}
	Q_free(tex);
}

void GL_PrintTextureSizes(common_texture_t* list)
{
	common_texture_t* tex;

	for (tex = list; tex; tex = tex->next) {
		if (tex->count) {
			Con_Printf("%dx%d = %d (%d anysize)\n", tex->width, tex->height, tex->count, tex->any_size_count);
		}
	}
}

void GL_SortTextureSizes(common_texture_t** first)
{
	qbool changed = true;

	while (changed) {
		common_texture_t** link = first;

		changed = false;
		while (*link && (*link)->next) {
			common_texture_t* current = *link;
			common_texture_t* next = current->next;

			if (next && current->any_size_count < next->any_size_count) {
				*link = next;
				current->next = next->next;
				next->next = current;
				changed = true;
				link = &next->next;
			}
			else {
				link = &current->next;
			}
		}
	}
}

static void GL_SetModelTextureArray(model_t* mod, GLuint array_num, float widthRatio, float heightRatio)
{
	if (!mod->texture_arrays) {
		mod->texture_arrays = Q_malloc(sizeof(GLuint));
		mod->texture_array_count = 1;
		mod->texture_arrays[0] = array_num;
		mod->texture_arrays_scale_s = Q_malloc(sizeof(GLuint));
		mod->texture_arrays_scale_t = Q_malloc(sizeof(GLuint));
		mod->texture_arrays_scale_s[0] = widthRatio;
		mod->texture_arrays_scale_t[0] = heightRatio;
	}
}

static void GL_MeasureTexturesForModel(model_t* mod, common_texture_t* common, int* required_vbo_length)
{
	int j;

	switch (mod->type) {
	case mod_alias:
	{
		aliashdr_t* paliashdr = (aliashdr_t *)Mod_Extradata(mod);
		qbool any_size = mod->max_tex[0] <= 1.0 && mod->max_tex[1] <= 1.0 && mod->min_tex[0] >= 0 && mod->min_tex[1] >= 0;

		for (j = 0; j < paliashdr->numskins; ++j) {
			int anim;
			for (anim = 0; anim < 4; ++anim) {
				if (anim == 0 || paliashdr->gl_texturenum[j][anim] != paliashdr->gl_texturenum[j][anim - 1]) {
					GL_RegisterCommonTextureSize(common, paliashdr->gl_texturenum[j][anim], any_size);
				}
				if (anim == 0 || paliashdr->fb_texturenum[j][anim] != paliashdr->fb_texturenum[j][anim - 1]) {
					GL_RegisterCommonTextureSize(common, paliashdr->fb_texturenum[j][anim], any_size);
				}
			}
		}

		for (j = 0; j < MAX_SIMPLE_TEXTURES; ++j) {
			if (mod->simpletexture[j]) {
				GL_RegisterCommonTextureSize(common, mod->simpletexture[j], true);
			}
		}

		*required_vbo_length += mod->vertsInVBO;
		break;
	}
	case mod_sprite:
	{
		msprite2_t* psprite = (msprite2_t*)Mod_Extradata(mod);
		int count = 0;

		for (j = 0; j < psprite->numframes; ++j) {
			int offset    = psprite->frames[j].offset;
			int numframes = psprite->frames[j].numframes;

			if (offset < (int)sizeof(msprite2_t) || numframes < 1) {
				continue;
			}

			GL_RegisterCommonTextureSize(common, ((mspriteframe_t* )((byte*)psprite + offset))->gl_texturenum, true);
			++count;
		}
		break;
	}
	case mod_brush:
	{
		int i;

		// Ammo-boxes etc can be replaced with simple textures
		for (j = 0; j < MAX_SIMPLE_TEXTURES; ++j) {
			if (mod->simpletexture[j]) {
				GL_RegisterCommonTextureSize(common, mod->simpletexture[j], true);
			}
		}

		// Brush models can be boxes (ammo, health), static world or moving platforms
		for (i = 0; i < mod->numtextures; i++) {
			texture_t* tx = mod->textures[i];
			if (!tx || !tx->loaded) {
				continue;
			}

			GL_RegisterCommonTextureSize(common, tx->gl_texturenum, BrushModelIsAnySize(mod));
		}
		break;
	}
	}
}

static int GLM_CountTextureArrays(model_t* mod)
{
	int i, j;
	int num_arrays = 0;

	for (i = 0; i < mod->numtextures; ++i) {
		texture_t* tex = mod->textures[i];
		qbool seen_prev = false;

		if (!tex || !tex->loaded || tex->gl_texture_array == 0) {
			continue;
		}

		for (j = 0; j < i; ++j) {
			texture_t* prev_tex = mod->textures[j];
			if (prev_tex && prev_tex->gl_texture_array == tex->gl_texture_array) {
				seen_prev = true;
				break;
			}
		}

		if (!seen_prev) {
			++num_arrays;
		}
	}

	return num_arrays;
}

static void GLM_SetTextureArrays(model_t* mod)
{
	int i, j;
	int num_arrays = 0;

	for (i = 0; i < mod->numtextures; ++i) {
		texture_t* tex = mod->textures[i];
		qbool seen_prev = false;

		if (!tex || !tex->loaded || tex->gl_texture_array == 0) {
			continue;
		}

		tex->next_same_size = -1;
		for (j = i - 1; j >= 0; --j) {
			texture_t* prev_tex = mod->textures[j];
			if (prev_tex && prev_tex->gl_texture_array == tex->gl_texture_array) {
				seen_prev = true;
				prev_tex->next_same_size = i;
				break;
			}
		}

		if (!seen_prev) {
			mod->texture_array_first[num_arrays] = i;
			mod->texture_arrays[num_arrays] = tex->gl_texture_array;
			mod->textures[i]->size_start = true;
			++num_arrays;
		}
	}
}

void GL_ImportTexturesForModel(model_t* mod, common_texture_t* common, common_texture_t* commonTex, int maxWidth, int maxHeight, GLuint model_vbo, float* new_vbo_buffer, int* new_vbo_position)
{
	int count = 0;
	int j;

	if (mod->type == mod_alias) {
		aliashdr_t* paliashdr = (aliashdr_t *)Mod_Extradata(mod);
		qbool any_size = mod->max_tex[0] <= 1.0 && mod->max_tex[1] <= 1.0 && mod->min_tex[0] >= 0 && mod->min_tex[1] >= 0;

		for (j = 0; j < paliashdr->numskins; ++j) {
			int anim;
			for (anim = 0; anim < 4; ++anim) {
				if (anim == 0 || paliashdr->gl_texturenum[j][anim] != paliashdr->gl_texturenum[j][anim - 1]) {
					GL_CopyToTextureArraySize(common, paliashdr->gl_texturenum[j][anim], any_size, &paliashdr->gl_scalingS[j][anim], &paliashdr->gl_scalingT[j][anim], NULL, &paliashdr->gl_arrayindex[j][anim]);
				}
				else {
					paliashdr->gl_arrayindex[j][anim] = paliashdr->gl_arrayindex[j][anim - 1];
					paliashdr->gl_scalingS[j][anim] = paliashdr->gl_scalingS[j][anim - 1];
					paliashdr->gl_scalingT[j][anim] = paliashdr->gl_scalingT[j][anim - 1];
				}

				if (anim == 0 || paliashdr->fb_texturenum[j][anim] != paliashdr->fb_texturenum[j][anim - 1]) {
					float fb_s, fb_t;

					GL_CopyToTextureArraySize(common, paliashdr->fb_texturenum[j][anim], any_size, &fb_s, &fb_t, NULL, &paliashdr->gl_fb_arrayindex[j][anim]);
				}
				else {
					paliashdr->gl_fb_arrayindex[j][anim] = paliashdr->gl_fb_arrayindex[j][anim - 1];
				}
			}
		}

		for (j = 0; j < MAX_SIMPLE_TEXTURES; ++j) {
			if (mod->simpletexture[j]) {
				GL_CopyToTextureArraySize(common, mod->simpletexture[j], true, &mod->simpletexture_scalingS[j], &mod->simpletexture_scalingT[j], &mod->simpletexture_array, &mod->simpletexture_indexes[j]);
			}
		}

		GL_SetModelTextureArray(mod, commonTex->gl_texturenum, commonTex->width * 1.0f / maxWidth, commonTex->height * 1.0f / maxHeight);

		// Copy VBO info to buffer (FIXME: Free the memory?  but is cached.  But CacheAlloc() fails... argh)
		memcpy(&new_vbo_buffer[(*new_vbo_position) * MODELVERTEXSIZE], mod->temp_vbo_buffer, mod->vertsInVBO * MODELVERTEXSIZE * sizeof(float));
		//Q_free(mod->temp_vbo_buffer);

		mod->vao_simple = mod->vao = model_vao;
		mod->vbo = model_vbo;
		mod->vbo_start = *new_vbo_position;

		paliashdr->vbo = model_vbo;
		paliashdr->vao = model_vao;
		paliashdr->vertsOffset = *new_vbo_position;

		*new_vbo_position += mod->vertsInVBO;
	}
	else if (mod->type == mod_sprite) {
		msprite2_t* psprite = (msprite2_t*)Mod_Extradata(mod);

		for (j = 0; j < psprite->numframes; ++j) {
			int offset    = psprite->frames[j].offset;
			int numframes = psprite->frames[j].numframes;
			mspriteframe_t* frame;

			if (offset < (int)sizeof(msprite2_t) || numframes < 1) {
				continue;
			}

			frame = ((mspriteframe_t*)((byte*)psprite + offset));
			GL_CopyToTextureArraySize(common, frame->gl_texturenum, true, &frame->gl_scalingS, &frame->gl_scalingT, NULL, &frame->gl_texturenum);
		}

		mod->vao_simple = model_vao;
		// FIXME: scaling factors
		GL_SetModelTextureArray(mod, commonTex->gl_texturenum, 0.5f, 0.5f);
		mod->vbo = model_vbo;
		mod->vbo_start = 0;
	}
	else if (mod->type == mod_brush) {
		for (j = 0; j < MAX_SIMPLE_TEXTURES; ++j) {
			if (mod->simpletexture[j]) {
				GL_CopyToTextureArraySize(common, mod->simpletexture[j], true, &mod->simpletexture_scalingS[j], &mod->simpletexture_scalingT[j], &mod->simpletexture_array, &mod->simpletexture_indexes[j]);
			}
		}
		mod->vao_simple = model_vao;
		mod->vbo_start = 0;

		for (j = 0; j < mod->numtextures; ++j) {
			texture_t* tex = mod->textures[j];
			if (tex && tex->loaded && !tex->gl_texture_array) {
				GL_CopyToTextureArraySize(common, tex->gl_texturenum, BrushModelIsAnySize(mod), &tex->gl_texture_scaleS, &tex->gl_texture_scaleT, &tex->gl_texture_array, &tex->gl_texture_index);
			}
		}

		mod->texture_array_count = GLM_CountTextureArrays(mod);
		mod->texture_arrays = Hunk_Alloc(sizeof(GLuint) * mod->texture_array_count);
		mod->texture_array_first = Hunk_Alloc(sizeof(int) * mod->texture_array_count);

		GLM_SetTextureArrays(mod);
	}
	else {
		//Con_Printf("***: type %d (%s)\n", mod->type, mod->name);
	}
}

static void GLM_CreateSpriteVBO(float* new_vbo_buffer)
{
	float* vert;

	vert = new_vbo_buffer;
	VectorSet(vert, 0, -1, -1);
	vert[3] = 1;
	vert[4] = 1;

	vert = new_vbo_buffer + MODELVERTEXSIZE;
	VectorSet(vert, 0, -1, 1);
	vert[3] = 1;
	vert[4] = 0;

	vert = new_vbo_buffer + MODELVERTEXSIZE * 2;
	VectorSet(vert, 0, 1, 1);
	vert[3] = 0;
	vert[4] = 0;

	vert = new_vbo_buffer + MODELVERTEXSIZE * 3;
	VectorSet(vert, 0, 1, -1);
	vert[3] = 0;
	vert[4] = 1;
}

void GL_BuildCommonTextureArrays(void)
{
	common_texture_t* common = Q_malloc(sizeof(common_texture_t));
	int required_vbo_length = 4;
	unsigned int model_vbo = 0;
	int i;

	glGenBuffers(1, &model_vbo);
	glGenVertexArrays(1, &model_vao);

	for (i = 1; i < MAX_MODELS; ++i) {
		model_t* mod = cl.model_precache[i];

		if (mod && (mod == cl.worldmodel || !mod->isworldmodel)) {
			GL_MeasureTexturesForModel(mod, common, &required_vbo_length);
		}
	}

	for (i = 0; i < MAX_VWEP_MODELS; i++) {
		model_t* mod = cl.vw_model_precache[i];

		if (mod) {
			GL_MeasureTexturesForModel(mod, common, &required_vbo_length);
		}
	}

	{
		// Find highest dimensions, stick everything in there for the moment unless texture is tiling
		// FIXME: this is a memory vs texture-switch tradeoff
		int maxWidth = 0;
		int maxHeight = 0;
		common_texture_t* tex;
		common_texture_t* commonTex = NULL;
		int anySizeCount = 0;
		float* new_vbo_buffer = Q_malloc(required_vbo_length * MODELVERTEXSIZE * sizeof(float));
		int new_vbo_position = 0;

		for (tex = common; tex; tex = tex->next) {
			if (tex->width == 0 && tex->height == 0) {
				commonTex = tex;
			}
			else {
				maxWidth = max(maxWidth, tex->width);
				maxHeight = max(maxHeight, tex->height);
			}
			anySizeCount += tex->any_size_count;
		}

		// Create non-specific array to fit everything that doesn't require tiling
		commonTex->gl_texturenum = GL_CreateTextureArray(maxWidth, maxHeight, anySizeCount + 1);
		commonTex->gl_width = maxWidth;
		commonTex->gl_height = maxHeight;

		// VBO starts with simple-model/sprite vertices
		GLM_CreateSpriteVBO(new_vbo_buffer);
		new_vbo_position = 4;

		// First texture is the powerup shell
		GLM_CreatePowerupShellTexture(commonTex->gl_texturenum, maxWidth, maxHeight, commonTex->allocated++);

		// Go back through all models, importing textures into arrays and creating new VBO
		for (i = 1; i < MAX_MODELS; ++i) {
			model_t* mod = cl.model_precache[i];

			if (mod) {
				GL_ImportTexturesForModel(mod, common, commonTex, maxWidth, maxHeight, model_vbo, new_vbo_buffer, &new_vbo_position);
			}
		}

		for (i = 0; i < MAX_VWEP_MODELS; i++) {
			model_t* mod = cl.vw_model_precache[i];

			if (mod) {
				GL_ImportTexturesForModel(mod, common, commonTex, maxWidth, maxHeight, model_vbo, new_vbo_buffer, &new_vbo_position);
			}
		}

		GLM_CreateInstanceVBO();
		GLM_CreateModelVAO(model_vbo, required_vbo_length, new_vbo_buffer);
		GLM_CreateBrushModelVAO();
	}

	GL_FreeTextureSizeList(common);
}

static void GLM_CreateModelVAO(GLuint model_vbo, GLuint required_vbo_length, float* new_vbo_buffer)
{
	glBindBufferExt(GL_ARRAY_BUFFER, model_vbo);
	glBufferDataExt(GL_ARRAY_BUFFER, required_vbo_length * MODELVERTEXSIZE * sizeof(float), new_vbo_buffer, GL_STATIC_DRAW);
	Q_free(new_vbo_buffer);

	GL_BindVertexArray(model_vao);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);
	glEnableVertexAttribArray(3);
	glBindBufferExt(GL_ARRAY_BUFFER, model_vbo);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * MODELVERTEXSIZE, (void*)0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * MODELVERTEXSIZE, (void*)(sizeof(float) * 3));
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(float) * MODELVERTEXSIZE, (void*)(sizeof(float) * 5));
	glBindBufferExt(GL_ARRAY_BUFFER, instance_vbo);
	glVertexAttribIPointer(3, 1, GL_UNSIGNED_INT, sizeof(GLuint), 0);
	glVertexAttribDivisor(3, 1);
}

static void GLM_CreatePowerupShellTexture(GLuint texture_array, int maxWidth, int maxHeight, int slice)
{
	float x, y, d;
	int level = 0;
	int height = maxHeight;
	int width = maxWidth;
	int minDimensions = min(maxWidth, maxHeight);
	byte* data = Q_malloc(4 * maxWidth * maxHeight);
	extern GLuint shelltexture2;

	while (width && height) {
		memset(data, 0, 4 * maxWidth * maxHeight);
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				int base = (x * height + y) * 4;

				d = (sin(4 * x * M_PI / minDimensions) + cos(4 * y * M_PI / minDimensions)) * 64 + 64;
				d = bound(0, d, 255);

				data[base] = data[base + 1] = data[base + 2] = d;
				data[base + 3] = 255;
			}
		}
		glTexSubImage3D(GL_TEXTURE_2D_ARRAY, level, 0, 0, 0, width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, data);

		++level;
		width /= 2;
		height /= 2;
		minDimensions /= 2;
	}

	//GL_LoadTexture("shelltexture", 32, 32, &data[0][0][0], TEX_MIPMAP, 4);
	Q_free(data);
}

static GLuint brushModel_vbo;
static GLuint brushModel_vao;

static void GLM_CreateBrushModelVAO(void)
{
	int i;
	int size = 0;
	int position = 0;
	float* buffer = NULL;

	for (i = 1; i < MAX_MODELS; ++i) {
		model_t* mod = cl.model_precache[i];
		if (mod && mod->type == mod_brush) {
			size += GLM_MeasureVBOSizeForBrushModel(mod);
		}
	}

	for (i = 0; i < MAX_VWEP_MODELS; i++) {
		model_t* mod = cl.vw_model_precache[i];
		if (mod && mod->type == mod_brush) {
			if (mod == cl.worldmodel || !mod->isworldmodel) {
				size += GLM_MeasureVBOSizeForBrushModel(mod);
			}
		}
	}

	// Create vbo buffer
	buffer = Q_malloc(size * VERTEXSIZE * sizeof(float));
	glGenBuffers(1, &brushModel_vbo);
	glBindBufferExt(GL_ARRAY_BUFFER, brushModel_vbo);

	// Create vao
	glGenVertexArrays(1, &brushModel_vao);
	GL_BindVertexArray(brushModel_vao);

	// Copy data into buffer
	for (i = 1; i < MAX_MODELS; ++i) {
		model_t* mod = cl.model_precache[i];
		if (mod && mod->type == mod_brush) {
			if (mod == cl.worldmodel || !mod->isworldmodel) {
				position = GLM_PopulateVBOForBrushModel(mod, buffer, position);
			}
			mod->vao = brushModel_vao;
		}
	}

	for (i = 0; i < MAX_VWEP_MODELS; i++) {
		model_t* mod = cl.vw_model_precache[i];
		if (mod && mod->type == mod_brush) {
			position = GLM_PopulateVBOForBrushModel(mod, buffer, position);
			mod->vao = brushModel_vao;
		}
	}

	// Copy VBO buffer across
	glBindBufferExt(GL_ARRAY_BUFFER, brushModel_vbo);
	glBufferDataExt(GL_ARRAY_BUFFER, size * VERTEXSIZE * sizeof(float), buffer, GL_STATIC_DRAW);

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);
	glEnableVertexAttribArray(3);
	glEnableVertexAttribArray(4);
	glEnableVertexAttribArray(5);
	glEnableVertexAttribArray(6);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * VERTEXSIZE, (void*) 0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * VERTEXSIZE, (void*) (sizeof(float) * 3));
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float) * VERTEXSIZE, (void*) (sizeof(float) * 5));
	glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(float) * VERTEXSIZE, (void*) (sizeof(float) * 7));
	glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(float) * VERTEXSIZE, (void*) (sizeof(float) * 9));
	glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(float) * VERTEXSIZE, (void*) (sizeof(float) * 10));
	glBindBufferExt(GL_ARRAY_BUFFER, instance_vbo);
	glVertexAttribIPointer(6, 1, GL_UNSIGNED_INT, sizeof(GLuint), 0);
	glVertexAttribDivisor(6, 1);

	Q_free(buffer);
}
