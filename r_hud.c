/*
Copyright (C) 2017-2018 ezQuake team

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
#include "glm_draw.h"
#include "r_draw.h"
#include "tr_types.h"

#define MAX_2D_ELEMENTS 4096

typedef struct draw_hud_element_s {
	r_image_type_t type;
	texture_ref texture;
	int index;
} draw_hud_element_t;

typedef struct hud_api_s {
	struct {
		void(*Prepare)(void);
		void(*Draw)(texture_ref texture, int start, int end);
	} types[imagetype_count];

	void(*OnComplete)(void);

	draw_hud_element_t elements[MAX_2D_ELEMENTS];
	int count;
} hud_api_t;

static hud_api_t hud;

#define HudSetFunctionPointers(prefix) \
{ \
	extern void prefix ## _HudDrawCircles(texture_ref texture, int start, int end); \
	extern void prefix ## _HudDrawLines(texture_ref texture, int start, int end); \
	extern void prefix ## _HudDrawPolygons(texture_ref texture, int start, int end); \
	extern void prefix ## _HudDrawImages(texture_ref texture, int start, int length); \
	extern void prefix ## _HudPrepareCircles(void); \
	extern void prefix ## _HudPrepareLines(void); \
	extern void prefix ## _HudPreparePolygons(void); \
	extern void prefix ## _HudPrepareImages(void); \
	extern void prefix ## _HudDrawComplete(void); \
\
	hud.types[imagetype_image].Draw = prefix ## _HudDrawImages; \
	hud.types[imagetype_image].Prepare = prefix ## _HudPrepareImages; \
	hud.types[imagetype_circle].Draw = prefix ## _HudDrawCircles; \
	hud.types[imagetype_circle].Prepare = prefix ## _HudPrepareCircles; \
	hud.types[imagetype_line].Draw = prefix ## _HudDrawLines; \
	hud.types[imagetype_line].Prepare = prefix ## _HudPrepareLines; \
	hud.types[imagetype_polygon].Draw = prefix ## _HudDrawPolygons; \
	hud.types[imagetype_polygon].Prepare = prefix ## _HudPreparePolygons; \
	hud.OnComplete = prefix ## _HudDrawComplete; \
}

void R_Hud_Initialise(void)
{
	if (R_UseModernOpenGL()) {
		HudSetFunctionPointers(GLM);
	}
	else if (R_UseImmediateOpenGL()) {
		HudSetFunctionPointers(GLC);
	}
	else if (R_UseVulkan()) {
		HudSetFunctionPointers(VK);
	}
}

static void R_PrepareImageDraw(void)
{
	hud.types[imagetype_image].Prepare();
	hud.types[imagetype_polygon].Prepare();
	hud.types[imagetype_circle].Prepare();
	hud.types[imagetype_line].Prepare();
}

void R_DrawRectangle(float x, float y, float width, float height, byte* color);

void R_DrawAlphaRectangleRGB(int x, int y, int w, int h, float thickness, qbool fill, byte* bytecolor)
{
	if (fill) {
		R_DrawRectangle(x, y, w, h, bytecolor);
	}
	else {
		R_DrawRectangle(x, y, w, thickness, bytecolor);
		R_DrawRectangle(x, y + thickness, thickness, h - thickness, bytecolor);
		R_DrawRectangle(x + w - thickness, y + thickness, thickness, h - thickness, bytecolor);
		R_DrawRectangle(x, y + h, w, thickness, bytecolor);
	}
}

void R_Draw_SAlphaSubPic2(int x, int y, mpic_t *pic, int src_width, int src_height, float newsl, float newtl, float newsh, float newth, float scale_x, float scale_y, float alpha)
{
	byte color[] = { 255, 255, 255, alpha * 255 };

	R_DrawImage(x, y, scale_x * src_width, scale_y * src_height, newsl, newtl, newsh - newsl, newth - newtl, color, false, pic->texnum, false, false);
}

void R_Draw_FadeScreen(float alpha)
{
	Draw_AlphaRectangleRGB(0, 0, vid.width, vid.height, 0.0f, true, RGBA_TO_COLOR(0, 0, 0, (alpha < 1 ? alpha * 255 : 255)));
}

void R_EmptyImageQueue(void)
{
	hud.count = imageData.imageCount = circleData.circleCount = lineData.lineCount = polygonData.polygonCount = 0;
}

void R_FlushImageDraw(void)
{
	R_PrepareImageDraw();

	if (hud.count && glConfig.initialized) {
		texture_ref currentTexture = null_texture_reference;
		r_image_type_t type = imagetype_image;
		int start = 0;
		int i;

		for (i = 0; i < hud.count; ++i) {
			qbool texture_changed = (GL_TextureReferenceIsValid(currentTexture) && GL_TextureReferenceIsValid(hud.elements[i].texture) && !GL_TextureReferenceEqual(currentTexture, hud.elements[i].texture));

			if (i && (hud.elements[i].type != type || texture_changed)) {
				hud.types[type].Draw(currentTexture, start, i - 1);

				start = i;
			}

			if (GL_TextureReferenceIsValid(hud.elements[i].texture)) {
				currentTexture = hud.elements[i].texture;
			}
			type = hud.elements[i].type;
		}

		if (hud.count - start) {
			hud.types[type].Draw(currentTexture, start, hud.count - 1);
		}
	}

	R_EmptyImageQueue();

	hud.OnComplete();
}

qbool GLM_LogCustomImageType(r_image_type_t type, int index)
{
	if (hud.count >= MAX_2D_ELEMENTS) {
		return false;
	}

	hud.elements[hud.count].type = type;
	hud.elements[hud.count].index = index;
	hud.elements[hud.count].texture = null_texture_reference;
	hud.count++;
	return true;
}

qbool GLM_LogCustomImageTypeWithTexture(r_image_type_t type, int index, texture_ref texture)
{
	if (hud.count >= MAX_2D_ELEMENTS) {
		return false;
	}

	hud.elements[hud.count].type = type;
	hud.elements[hud.count].index = index;
	hud.elements[hud.count].texture = texture;
	hud.count++;
	return true;
}
