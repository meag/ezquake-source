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

// gl_state_resets.c
// moving state init/reset to here with intention of getting rid of all resets

#include "quakedef.h"
#include "gl_model.h"
#include "r_matrix.h"
#include "r_state.h"

static rendering_state_t default2DState;
static rendering_state_t brightenScreenState;
static rendering_state_t lineState;
static rendering_state_t sceneBlurState;
static rendering_state_t glcImageDrawState;
static rendering_state_t glcAlphaTestedImageDrawState;
static rendering_state_t glmImageDrawState;
static rendering_state_t glcBloomState;
static rendering_state_t polyBlendState;

void R_Initialise2DStates(void)
{
	R_InitRenderingState(&default2DState, true, "default2DState", vao_none);
	default2DState.depth.test_enabled = false;
	default2DState.cullface.enabled = false;

	R_InitRenderingState(&brightenScreenState, true, "brightenScreenState", vao_postprocess);
	brightenScreenState.depth.test_enabled = false;
	brightenScreenState.cullface.enabled = false;
	brightenScreenState.alphaTesting.enabled = true; // really?
	brightenScreenState.blendingEnabled = true;
	brightenScreenState.blendFunc = r_blendfunc_src_dst_color_dest_one;

	R_InitRenderingState(&lineState, true, "lineState", vao_hud_lines);
	lineState.depth.test_enabled = false;
	lineState.cullface.enabled = false;
	lineState.blendingEnabled = true;
	lineState.blendFunc = r_blendfunc_premultiplied_alpha;
	lineState.line.flexible_width = true;

	R_InitRenderingState(&sceneBlurState, true, "sceneBlurState", vao_postprocess);
	sceneBlurState.depth.test_enabled = false;
	sceneBlurState.cullface.enabled = false;
	//GL_Viewport(0, 0, glwidth, glheight);
	sceneBlurState.alphaTesting.enabled = false;
	sceneBlurState.blendingEnabled = true;
	sceneBlurState.blendFunc = r_blendfunc_premultiplied_alpha;
	sceneBlurState.textureUnits[0].enabled = true;
	sceneBlurState.textureUnits[0].mode = r_texunit_mode_replace;

	R_InitRenderingState(&glcImageDrawState, true, "glcImageDrawState", vao_hud_images);
	glcImageDrawState.depth.test_enabled = false;
	glcImageDrawState.cullface.enabled = false;
	glcImageDrawState.textureUnits[0].enabled = true;
	glcImageDrawState.textureUnits[0].mode = r_texunit_mode_modulate;
	glcImageDrawState.blendingEnabled = true;
	glcImageDrawState.blendFunc = r_blendfunc_premultiplied_alpha;

	R_CopyRenderingState(&glcAlphaTestedImageDrawState, &glcImageDrawState, "glcAlphaTestedImageDrawState");
	glcAlphaTestedImageDrawState.alphaTesting.enabled = true;

#ifdef BLOOM_SUPPORTED
	R_InitRenderingState(&glcBloomState, true, "glcBloomState", vao_postprocess);
	glcBloomState.depth.test_enabled = false;
	glcBloomState.cullface.enabled = false;
	glcBloomState.alphaTesting.enabled = true;
	glcBloomState.blendingEnabled = true;
	glcBloomState.blendFunc = r_blendfunc_additive_blending;
	glcBloomState.color[0] = glcBloomState.color[1] = glcBloomState.color[2] = r_bloom_alpha.value;
	glcBloomState.color[3] = 1.0f;
	glcBloomState.textureUnits[0].enabled = true;
	glcBloomState.textureUnits[0].mode = r_texunit_mode_modulate;
#endif

	R_InitRenderingState(&polyBlendState, true, "polyBlendState", vao_postprocess);
	polyBlendState.depth.test_enabled = false;
	polyBlendState.cullface.enabled = false;
	polyBlendState.blendingEnabled = true;
	polyBlendState.blendFunc = r_blendfunc_premultiplied_alpha;
	polyBlendState.color[0] = v_blend[0] * v_blend[3];
	polyBlendState.color[1] = v_blend[1] * v_blend[3];
	polyBlendState.color[2] = v_blend[2] * v_blend[3];
	polyBlendState.color[3] = v_blend[3];

	R_InitRenderingState(&glmImageDrawState, true, "glmImageDrawState", vao_hud_images);
	glmImageDrawState.depth.test_enabled = false;
	glmImageDrawState.cullface.enabled = false;
	glmImageDrawState.alphaTesting.enabled = false;
	glmImageDrawState.blendingEnabled = r_blendfunc_premultiplied_alpha;
}

void GLC_StateBeginBrightenScreen(void)
{
	R_ApplyRenderingState(&brightenScreenState);
}

void GL_StateBeginAlphaLineRGB(float thickness)
{
	R_ApplyRenderingState(&lineState);
	if (thickness > 0.0) {
		R_CustomLineWidth(thickness);
	}
}

void GLC_StateBeginDrawAlphaPieSliceRGB(float thickness)
{
	// Same as lineState
	R_ApplyRenderingState(&lineState);
	if (thickness > 0.0) {
		R_CustomLineWidth(thickness);
	}
}

void GLC_StateBeginSceneBlur(void)
{
	R_ApplyRenderingState(&sceneBlurState);

	GL_IdentityModelView();
	GL_OrthographicProjection(0, glwidth, 0, glheight, -99999, 99999);
}

void GLC_StateBeginDrawPolygon(void)
{
	R_ApplyRenderingState(&lineState);
}

void GLC_StateBeginBloomDraw(texture_ref texture)
{
	R_ApplyRenderingState(&glcBloomState);
	R_TextureUnitBind(0, texture);
}

void GLC_StateBeginPolyBlend(float v_blend[4])
{
	R_ApplyRenderingState(&polyBlendState);
}

void GLC_StateBeginImageDraw(qbool is_text)
{
	extern cvar_t gl_alphafont;

	if (is_text && !gl_alphafont.integer) {
		R_ApplyRenderingState(&glcAlphaTestedImageDrawState);
	}
	else {
		R_ApplyRenderingState(&glcImageDrawState);
	}
}

void GLM_StateBeginPolyBlend(void)
{
	R_ApplyRenderingState(&polyBlendState);
}

void GLM_StateBeginImageDraw(void)
{
	R_ApplyRenderingState(&glmImageDrawState);
}

void GLM_StateBeginPolygonDraw(void)
{
	R_ApplyRenderingState(&glmImageDrawState);
}
