/*
Module for creating 2d atlas for UI elements

Copyright (C) 2011 ezQuake team

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

#include "quakedef.h"
#include "common_draw.h"
#include "gl_model.h"
#include "gl_local.h"

#define ATLAS_TEXTURE_WIDTH  4096
#define ATLAS_TEXTURE_HEIGHT 4096
#define ATLAS_CHUNK 64
#define ATLAS_WIDTH (ATLAS_TEXTURE_WIDTH / ATLAS_CHUNK)
#define ATLAS_HEIGHT (ATLAS_TEXTURE_HEIGHT / ATLAS_CHUNK)

static byte buffer[ATLAS_TEXTURE_WIDTH * ATLAS_TEXTURE_HEIGHT * 4];
static texture_ref atlas_deletable_textures[WADPIC_PIC_COUNT + NUMCROSSHAIRS + 2 + MAX_CHARSETS] = { { 0 } };
static int atlas_delete_count = 0;
static float solid_s;
static float solid_t;

static void AddToDeleteList(mpic_t* src)
{
	if (src->sl == 0 && src->tl == 0 && src->th == 1 && src->sh == 1) {
		if (atlas_delete_count < sizeof(atlas_deletable_textures) / sizeof(atlas_deletable_textures[0])) {
			atlas_deletable_textures[atlas_delete_count++] = src->texnum;
		}
	}
}

static void DeleteOldTextures(void)
{
	int i;
	for (i = 0; i < atlas_delete_count; ++i) {
		GL_DeleteTexture(&atlas_deletable_textures[i]);
	}
}

static cachepic_node_t wadpics[WADPIC_PIC_COUNT];
static cachepic_node_t charsetpics[MAX_CHARSETS];
static cachepic_node_t crosshairpics[NUMCROSSHAIRS + 2];

static int  atlas_allocated[ATLAS_WIDTH];
static byte atlas_texels[ATLAS_TEXTURE_WIDTH * ATLAS_TEXTURE_HEIGHT * 4];
static byte prev_atlas_texels[ATLAS_TEXTURE_WIDTH * ATLAS_TEXTURE_HEIGHT * 4];
static qbool atlas_dirty;
static texture_ref atlas_texnum;
static qbool atlas_refresh = false;

static cvar_t gfx_atlasautoupload = { "gfx_atlasautoupload", "1" };

void Atlas_SolidTextureCoordinates(texture_ref* ref, float* s, float* t)
{
	*ref = atlas_texnum;
	*s = solid_s;
	*t = solid_t;
}

// Returns false if allocation failed.
static qbool CachePics_AllocBlock(int w, int h, int *x, int *y)
{
	int i, j, best, best2;

	w = (w + (ATLAS_CHUNK - 1)) / ATLAS_CHUNK;
	h = (h + (ATLAS_CHUNK - 1)) / ATLAS_CHUNK;
	best = ATLAS_HEIGHT;

	for (i = 0; i < ATLAS_WIDTH - w; i++) {
		best2 = 0;

		for (j = 0; j < w; j++) {
			if (atlas_allocated[i + j] >= best) {
				break;
			}
			if (atlas_allocated[i + j] > best2) {
				best2 = atlas_allocated[i + j];
			}
		}

		if (j == w) {
			// This is a valid spot.
			*x = i;
			*y = best = best2;
		}
	}

	if (best + h > ATLAS_HEIGHT) {
		return false;
	}

	for (i = 0; i < w; i++) {
		atlas_allocated[*x + i] = best + h;
	}

	atlas_dirty = true;
	*x *= ATLAS_CHUNK;
	*y *= ATLAS_CHUNK;

	return true;
}

static void CachePics_AllocateSolidTexture(void)
{
	int x_pos, y_pos, y;

	CachePics_AllocBlock(ATLAS_CHUNK, ATLAS_CHUNK, &x_pos, &y_pos);

	for (y = 0; y < ATLAS_CHUNK; ++y) {
		memset(atlas_texels + (x_pos + (y_pos + y) * ATLAS_TEXTURE_WIDTH) * 4, 0xFF, ATLAS_CHUNK * 4);
	}

	solid_s = (x_pos + ATLAS_CHUNK / 2) * 1.0f / ATLAS_TEXTURE_WIDTH;
	solid_t = (y_pos + ATLAS_CHUNK / 2) * 1.0f / ATLAS_TEXTURE_HEIGHT;

	Con_Printf("SolidTexture(%d,%d => %f, %f)\n", x_pos, y_pos, solid_s, solid_t);
}

static int CachePics_AddToAtlas(mpic_t* pic)
{
	int width = pic->width, height = pic->height;
	int texWidth = 0, texHeight = 0;
	int x_pos, y_pos;
	int padding = 1;

	// Find size of the source
	texWidth = GL_TextureWidth(pic->texnum);
	texHeight = GL_TextureHeight(pic->texnum);

	width = (pic->sh - pic->sl) * texWidth;
	height = (pic->th - pic->tl) * texHeight;

	if (width > ATLAS_TEXTURE_WIDTH || height > ATLAS_TEXTURE_HEIGHT) {
		return -1;
	}

	// Allocate space in an atlas texture
	if (CachePics_AllocBlock(width + (width == ATLAS_TEXTURE_WIDTH ? 0 : padding), height + (height == ATLAS_TEXTURE_HEIGHT ? 0 : padding), &x_pos, &y_pos)) {
		int yOffset;
		byte* input_image = NULL;

		// Copy texture image
		if (GL_TextureReferenceEqual(pic->texnum, atlas_texnum)) {
			input_image = prev_atlas_texels;
		}
		else {
			GL_GetTexImage(GL_TEXTURE0, pic->texnum, 0, GL_RGBA, GL_UNSIGNED_BYTE, sizeof(buffer), buffer);

			input_image = buffer;
		}

		for (yOffset = 0; yOffset < height; ++yOffset) {
			int y = y_pos + yOffset;
			int base = (x_pos + y * ATLAS_TEXTURE_WIDTH) * 4;
			int srcBase = (pic->sl * texWidth + (yOffset + pic->tl * texHeight) * texWidth) * 4;

			memcpy(atlas_texels + base, input_image + srcBase, 4 * width);
		}

		pic->sl = (x_pos) / (float)ATLAS_TEXTURE_WIDTH;
		pic->sh = (x_pos + width) / (float)ATLAS_TEXTURE_WIDTH;
		pic->tl = (y_pos) / (float)ATLAS_TEXTURE_HEIGHT;
		pic->th = (y_pos + height) / (float)ATLAS_TEXTURE_HEIGHT;
		pic->texnum = atlas_texnum;

		return 0;
	}
	else {
		Con_DPrintf("&cf00atlas&r FAILED TO PLACE: %d (%d x %d)\n", pic->texnum, width, height);
	}

	return -1;
}

void CachePics_AtlasFrame(void)
{
	if (gfx_atlasautoupload.integer) {
		if (atlas_refresh) {
			CachePics_CreateAtlas();
		}
	}
}

void CachePics_AtlasUpload(void)
{
	if (atlas_dirty) {
		atlas_texnum = GL_LoadTexture("cachepics:atlas", ATLAS_TEXTURE_WIDTH, ATLAS_TEXTURE_HEIGHT, atlas_texels, TEX_ALPHA | TEX_NOSCALE, 4);
	}
	atlas_dirty = 0;
}

void CachePics_Init(void)
{
	atlas_dirty = ~0;

	CachePics_AtlasUpload();

	Cvar_Register(&gfx_atlasautoupload);
	Cmd_AddCommand("gfx_atlasupload", CachePics_CreateAtlas);
}

void CachePics_InsertBySize(cachepic_node_t** sized_list, cachepic_node_t* node)
{
	int size_node;
	cachepic_node_t* current = *sized_list;
	int size_this;

	node->width = GL_TextureWidth(node->data.pic->texnum);
	node->height = GL_TextureHeight(node->data.pic->texnum);

	node->width *= (node->data.pic->sh - node->data.pic->sl);
	node->height *= (node->data.pic->th - node->data.pic->tl);

	size_node = node->width * node->height;

	while (current) {
		size_this = current->width * current->height;
		if (size_this > size_node) {
			sized_list = &current->size_order;
			current = *sized_list;
			continue;
		}

		break;
	}

	node->size_order = current;
	*sized_list = node;
}

void CachePics_LoadAmmoPics(mpic_t* ibar)
{
	extern mpic_t sb_ib_ammo[4];
	mpic_t* targPic;
	int i;
	GLint texWidth, texHeight;
	float sRatio = (ibar->sh - ibar->sl) / (float)ibar->width;
	float tRatio = (ibar->th - ibar->tl) / (float)ibar->height;
	byte* source;
	byte* target;
	int realwidth, realheight;
	float newsh, newsl;
	float newth, newtl;

	// Find size of the source
	texWidth = GL_TextureWidth(ibar->texnum);
	texHeight = GL_TextureHeight(ibar->texnum);

	source = Q_malloc(texWidth * texHeight * 4);
	target = Q_malloc(texWidth * texHeight * 4);

	GL_GetTexImage(GL_TEXTURE0, ibar->texnum, 0, GL_RGBA, GL_UNSIGNED_BYTE, texWidth * texHeight * 4, source);
	for (i = WADPIC_SB_IBAR_AMMO1; i <= WADPIC_SB_IBAR_AMMO4; ++i) {
		int num = i - WADPIC_SB_IBAR_AMMO1;
		int y;
		int x_src, y_src;
		char name[16];
		int xcoord = (3 + num * 48);
		int xwidth = 42;
		int ycoord = 0;
		int yheight = 11;

		newsl = ibar->sl + xcoord * sRatio;
		newsh = newsl + xwidth * sRatio;
		newtl = ibar->tl + ycoord * tRatio;
		newth = newtl + yheight * tRatio;

		x_src = texWidth * newsl;
		y_src = texHeight * newtl;

		realwidth = (newsh - newsl) * texWidth;
		realheight = (newth - newtl) * texHeight;

		snprintf(name, sizeof(name), "hud_ammo_%d", i - WADPIC_SB_IBAR_AMMO1);

		for (y = 0; y < realheight; ++y) {
			memset(target + (y * realwidth) * 4, source[(x_src + (y_src + y) * texWidth) * 4], realwidth * 4);
		}

		targPic = wad_pictures[i].pic = &sb_ib_ammo[num];
		targPic->texnum = GL_LoadTexture(name, realwidth, realheight, target, TEX_NOCOMPRESS | TEX_ALPHA | TEX_NOSCALE | TEX_NO_TEXTUREMODE, 4);
		targPic->sl = 0;
		targPic->sh = 1;
		targPic->tl = 0;
		targPic->th = 1;
		targPic->width = 42;
		targPic->height = 11;
	}

	Q_free(target);
	Q_free(source);
}

void CachePics_CreateAtlas(void)
{
	cachepic_node_t* sized_list = NULL;
	cachepic_node_t* cur;
	cachepic_node_t simple_items[MOD_NUMBER_HINTS * MAX_SIMPLE_TEXTURES];
	int i, j;
	double start_time = Sys_DoubleTime();

	// Delete old atlas textures
	memcpy(prev_atlas_texels, atlas_texels, sizeof(prev_atlas_texels));
	memset(atlas_texels, 0, sizeof(atlas_texels));
	memset(atlas_allocated, 0, sizeof(atlas_allocated));
	memset(wadpics, 0, sizeof(wadpics));
	atlas_delete_count = 0;

	// Copy wadpic data over
	for (i = 0; i < WADPIC_PIC_COUNT; ++i) {
		extern wadpic_t wad_pictures[WADPIC_PIC_COUNT];
		mpic_t* src = wad_pictures[i].pic;

		// This tiles, so don't include in atlas
		if (i == WADPIC_BACKTILE) {
			continue;
		}

		if (src) {
			wadpics[i].data.pic = src;

			if (i != WADPIC_SB_IBAR) {
				CachePics_InsertBySize(&sized_list, &wadpics[i]);

				AddToDeleteList(src);
			}
		}
	}

	// Copy text images over
	for (i = 0; i < MAX_CHARSETS; ++i) {
		extern mpic_t char_textures[MAX_CHARSETS];
		mpic_t* src = &char_textures[i];

		if (GL_TextureReferenceIsValid(src->texnum)) {
			charsetpics[i].data.pic = src;

			CachePics_InsertBySize(&sized_list, &charsetpics[i]);

			AddToDeleteList(src);
		}
	}

	// Copy crosshairs
	{
		extern mpic_t crosshairtexture_txt;
		extern mpic_t crosshairpic;
		extern mpic_t crosshairs_builtin[NUMCROSSHAIRS];

		if (GL_TextureReferenceIsValid(crosshairtexture_txt.texnum)) {
			crosshairpics[0].data.pic = &crosshairtexture_txt;
			CachePics_InsertBySize(&sized_list, &crosshairpics[0]);

			AddToDeleteList(&crosshairtexture_txt);
		}

		if (GL_TextureReferenceIsValid(crosshairpic.texnum)) {
			crosshairpics[1].data.pic = &crosshairpic;
			CachePics_InsertBySize(&sized_list, &crosshairpics[1]);

			AddToDeleteList(&crosshairpic);
		}

		for (i = 0; i < NUMCROSSHAIRS; ++i) {
			if (GL_TextureReferenceIsValid(crosshairs_builtin[i].texnum)) {
				crosshairpics[i + 2].data.pic = &crosshairs_builtin[i];
				CachePics_InsertBySize(&sized_list, &crosshairpics[i + 2]);

				AddToDeleteList(&crosshairs_builtin[i]);
			}
		}
	}

	// Add simple item textures
	for (i = 0; i < MOD_NUMBER_HINTS; ++i) {
		for (j = 0; j < MAX_SIMPLE_TEXTURES; ++j) {
			mpic_t* pic = Mod_SimpleTextureForHint(i, j);
			cachepic_node_t* node = &simple_items[i * MAX_SIMPLE_TEXTURES + j];

			node->data.pic = pic;

			if (pic && GL_TextureReferenceIsValid(pic->texnum)) {
				CachePics_InsertBySize(&sized_list, node);

				// Don't delete these, used for 3d sprites
			}
		}
	}

	// Add cached picture images
	for (i = 0; i < CACHED_PICS_HDSIZE; ++i) {
		extern cachepic_node_t *cachepics[CACHED_PICS_HDSIZE];
		cachepic_node_t *cur;

		for (cur = cachepics[i]; cur; cur = cur->next) {
			if (!Draw_IsConsoleBackground(cur->data.pic)) {
				CachePics_InsertBySize(&sized_list, cur);

				AddToDeleteList(cur->data.pic);
			}
		}
	}

	for (cur = sized_list; cur; cur = cur->size_order) {
		CachePics_AddToAtlas(cur->data.pic);
	}
	CachePics_AllocateSolidTexture();

	// Upload atlas textures
	CachePics_AtlasUpload();

	DeleteOldTextures();

	Con_DPrintf("Atlas build time: %f\n", Sys_DoubleTime() - start_time);

	// Make sure we don't reference any old textures
	GL_EmptyImageQueue();

	atlas_refresh = false;
}

void CachePics_MarkAtlasDirty(void)
{
	atlas_refresh = true;
}
