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
#include "gl_local.h"
#include "gl_aliasmodel.h"

extern texture_ref solidskytexture, alphaskytexture;

float GL_WaterAlpha(void)
{
	return bound((1 - r_refdef2.max_watervis), r_wateralpha.value, 1);
}

void GLC_StateBeginWaterSurfaces(void)
{
	float wateralpha = GL_WaterAlpha();

	if (wateralpha < 1.0) {
		GL_AlphaBlendFlags(GL_BLEND_ENABLED);
		glColor4f (1, 1, 1, wateralpha);
		GL_TextureEnvMode(GL_MODULATE);
		if (wateralpha < 0.9) {
			GL_DepthMask(GL_FALSE);
		}
	}
	GL_DisableMultitexture();
}

void GLC_StateEndWaterSurfaces(void)
{
	float wateralpha = GL_WaterAlpha();

	if (wateralpha < 1.0) {
		GL_TextureEnvMode(GL_REPLACE);

		glColor3ubv (color_white);
		GL_AlphaBlendFlags(GL_BLEND_DISABLED);
		if (wateralpha < 0.9) {
			GL_DepthMask(GL_TRUE);
		}
	}
}

void GL_StateBeginEntities(visentlist_t* vislist)
{
	// draw sprites separately, because of alpha_test
	GL_AlphaBlendFlags(
		(vislist->alpha ? GL_ALPHATEST_ENABLED : GL_ALPHATEST_DISABLED) |
		(vislist->alphablend ? GL_BLEND_ENABLED : GL_BLEND_DISABLED)
	);
	if (!GL_ShadersSupported()) {
		GL_EnableTMU(GL_TEXTURE0);
	}
}

void GL_StateEndEntities(visentlist_t* vislist)
{
	if (vislist->alpha) {
		GL_AlphaBlendFlags(GL_ALPHATEST_DISABLED);
	}
	if (!GL_ShadersSupported()) {
		if (gl_affinemodels.value) {
			GL_Hint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
		}
		if (gl_smoothmodels.value) {
			GL_ShadeModel(GL_FLAT);
		}
	}
}

void GL_StateBeginPolyBlend(void)
{
	GL_AlphaBlendFlags(GL_ALPHATEST_DISABLED | GL_BLEND_ENABLED);
}

void GL_StateEndPolyBlend(void)
{
	GL_AlphaBlendFlags(GL_ALPHATEST_ENABLED | GL_BLEND_DISABLED);
}

void GLC_StateBeginAlphaChain(void)
{
	GL_AlphaBlendFlags(GL_ALPHATEST_ENABLED);
}

void GLC_StateEndAlphaChain(void)
{
	GL_AlphaBlendFlags(GL_ALPHATEST_DISABLED);
	GL_DisableMultitexture();
	GL_TextureEnvMode(GL_REPLACE);
}

void GLC_StateBeginAlphaChainSurface(msurface_t* s)
{
	texture_t* t = s->texinfo->texture;

	//bind the world texture
	GL_DisableMultitexture();
	GL_BindTextureUnit(GL_TEXTURE0, t->gl_texturenum);

	if (gl_mtexable) {
		GL_EnableMultitexture();

		GLC_SetTextureLightmap(GL_TEXTURE1, s->lightmaptexturenum);
	}
}

void GLC_StateBeginAliasOutlineFrame(void)
{
	extern cvar_t gl_outline_width;

	GL_PolygonOffset(POLYGONOFFSET_OUTLINES);
	GL_CullFace(GL_BACK);
	glPolygonMode(GL_FRONT, GL_LINE);

	// limit outline width, since even width == 3 can be considered as cheat.
	glLineWidth(bound(0.1, gl_outline_width.value, 3.0));

	glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
	glEnable(GL_LINE_SMOOTH);
	glDisable(GL_TEXTURE_2D);
}

void GLC_StateEndAliasOutlineFrame(void)
{
	glColor4f(1, 1, 1, 1);
	glPolygonMode(GL_FRONT, GL_FILL);
	glDisable(GL_LINE_SMOOTH);
	GL_CullFace(GL_FRONT);
	glEnable(GL_TEXTURE_2D);

	GL_PolygonOffset(POLYGONOFFSET_DISABLED);
}

void GLC_StateBeginAliasPowerupShell(void)
{
	GL_BindTextureUnit(GL_TEXTURE0, shelltexture);
	GL_AlphaBlendFlags(GL_BLEND_ENABLED);

	if (gl_powerupshells_style.integer) {
		GL_BlendFunc(GL_SRC_ALPHA, GL_ONE);
	}
	else {
		GL_BlendFunc(GL_ONE, GL_ONE);
	}
}

void GLC_StateEndAliasPowerupShell(void)
{
	// LordHavoc: reset the state to what the rest of the renderer expects
	GL_AlphaBlendFlags(GL_BLEND_DISABLED);
	GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void GLC_StateBeginUnderwaterCaustics(void)
{
	GL_EnableMultitexture();
	GL_BindTextureUnit(GL_TEXTURE1, underwatertexture);

	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();
	glScalef(0.5, 0.5, 1);
	glRotatef(r_refdef2.time * 10, 1, 0, 0);
	glRotatef(r_refdef2.time * 10, 0, 1, 0);
	glMatrixMode(GL_MODELVIEW);

	GL_BlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
	GL_AlphaBlendFlags(GL_BLEND_ENABLED);
}

void GLC_StateEndUnderwaterCaustics(void)
{
	GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GL_AlphaBlendFlags(GL_BLEND_DISABLED);

	GL_SelectTexture(GL_TEXTURE1);
	//glTexEnvi (GL_TEXTURE_ENV, GL_RGB_SCALE, 1); FIXME
	GL_TextureEnvMode(GL_REPLACE);
	glDisable(GL_TEXTURE_2D);

	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);

	GL_DisableMultitexture();
}

void GLC_StateBeginDrawImage(qbool alpha, byte color[4])
{
	GL_TextureEnvMode(GL_MODULATE);
	glEnable(GL_TEXTURE_2D);
	GL_AlphaBlendFlags(GL_BLEND_ENABLED);

	GL_AlphaBlendFlags((alpha ? GL_ALPHATEST_ENABLED : GL_ALPHATEST_DISABLED));
	GL_Color4ubv(color);
}

void GLC_StateEndDrawImage(void)
{
	// FIXME: GL_ResetState
	glColor3ubv (color_white);
	GL_AlphaBlendFlags(GL_ALPHATEST_ENABLED | GL_BLEND_DISABLED);
	GL_TextureEnvMode(GL_REPLACE);
}

void GLC_StateBeginAlphaPic(float alpha)
{
	if (alpha < 1.0) {
		GL_AlphaBlendFlags(GL_ALPHATEST_DISABLED | GL_BLEND_ENABLED);
		GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		GL_TextureEnvMode(GL_MODULATE);
		GL_CullFace(GL_FRONT);
		glColor4f(1, 1, 1, alpha);
	}
}

void GLC_StateEndAlphaPic(float alpha)
{
	if (alpha < 1.0) {
		GL_AlphaBlendFlags(GL_ALPHATEST_ENABLED | GL_BLEND_DISABLED);
		GL_TextureEnvMode(GL_REPLACE);
		glColor4f(1, 1, 1, 1);
	}
}

void GLC_StateBeginAlphaRectangle(void)
{
	GL_AlphaBlendFlags(GL_ALPHATEST_DISABLED | GL_BLEND_ENABLED);
	glDisable (GL_TEXTURE_2D);
}

void GLC_StateEndAlphaRectangle(void)
{
	glColor4ubv (color_white);
	glEnable (GL_TEXTURE_2D);
	GL_AlphaBlendFlags(GL_ALPHATEST_ENABLED | GL_BLEND_DISABLED);
}

void GLC_StateBeginFadeScreen(float alpha)
{
	if (alpha < 1) {
		GL_AlphaBlendFlags(GL_ALPHATEST_DISABLED | GL_BLEND_ENABLED);
		glColor4f(0, 0, 0, alpha);
	}
	else {
		glColor3f(0, 0, 0);
	}
	glDisable(GL_TEXTURE_2D);
}

void GLC_StateEndFadeScreen(void)
{
	glColor3ubv(color_white);
	glEnable(GL_TEXTURE_2D);
	GL_AlphaBlendFlags(GL_ALPHATEST_ENABLED | GL_BLEND_DISABLED);
}

void GLC_StateBeginMD3Draw(float alpha)
{
	GL_DisableMultitexture();
	if (alpha < 1) {
		GL_AlphaBlendFlags(GL_BLEND_ENABLED);
	}
	GL_EnableFog();
	GL_TextureEnvMode(GL_MODULATE);
}

void GLC_StateEndMD3Draw(void)
{
	GL_AlphaBlendFlags(GL_BLEND_DISABLED);
	GL_TextureEnvMode(GL_REPLACE);
	glColor4f(1, 1, 1, 1);
	GL_ShadeModel(GL_FLAT);
	GL_Enable(GL_TEXTURE_2D);
	GL_DisableFog();
}

void GLC_StateBeginBrightenScreen(void)
{
	glDisable(GL_TEXTURE_2D);
	GL_AlphaBlendFlags(GL_BLEND_ENABLED);
	GL_BlendFunc(GL_DST_COLOR, GL_ONE);
}

void GLC_StateEndBrightenScreen(void)
{
	// FIXME: GL_ResetState()
	GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_TEXTURE_2D);
	GL_AlphaBlendFlags(GL_BLEND_DISABLED);
	glColor3ubv(color_white);
}

void GLC_StateBeginFastSky(void)
{
	GL_DisableMultitexture();
	if (gl_fogsky.value) {
		GL_EnableFog();
	}
	glDisable(GL_TEXTURE_2D);
	glColor3ubv(r_skycolor.color);
}

void GLC_StateEndFastSky(void)
{
	glEnable(GL_TEXTURE_2D);
	glColor3f(1, 1, 1);
	if (gl_fogsky.value) {
		GL_DisableFog();
	}
}

void GLC_StateBeginSky(void)
{
	GL_DisableMultitexture();
	glDisable(GL_DEPTH_TEST);
}

void GLC_StateBeginSkyZBufferPass(void)
{
	if (gl_fogenable.value && gl_fogsky.value) {
		glEnable(GL_DEPTH_TEST);
		GL_EnableFog();
		glColor4f(gl_fogred.value, gl_foggreen.value, gl_fogblue.value, 1);
		GL_BlendFunc(GL_ONE, GL_ZERO);
	}
	else {
		glEnable(GL_DEPTH_TEST);
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		GL_BlendFunc(GL_ZERO, GL_ONE);
	}
	glDisable(GL_TEXTURE_2D);
	GL_AlphaBlendFlags(GL_BLEND_ENABLED);
}

void GLC_StateEndSkyZBufferPass(void)
{
	// FIXME: GL_ResetState()
	if (gl_fogenable.value && gl_fogsky.value) {
		GL_DisableFog();
	}
	else {
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	}
	GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_TEXTURE_2D);
	GL_AlphaBlendFlags(GL_BLEND_DISABLED);
}

void GLC_StateEndSkyNoZBufferPass(void)
{
	glEnable(GL_DEPTH_TEST);
}

void GLC_StateBeginSkyDome(void)
{
	GL_DisableMultitexture();
	GL_BindTextureUnit(GL_TEXTURE0, solidskytexture);
	GL_AlphaBlendFlags(GL_BLEND_DISABLED);
}

void GLC_StateBeginSkyDomeCloudPass(void)
{
	GL_AlphaBlendFlags(GL_BLEND_ENABLED);
	GL_BindTextureUnit(GL_TEXTURE0, alphaskytexture);
}

void GLC_StateBeginMultiTextureSkyChain(void)
{
	GL_DisableMultitexture();
	if (gl_fogsky.value) {
		GL_EnableFog();
	}
	GL_TextureEnvMode(GL_MODULATE);
	GL_BindTextureUnit(GL_TEXTURE0, solidskytexture);

	GL_EnableMultitexture();
	GL_TextureEnvMode(GL_DECAL);
	GL_BindTextureUnit(GL_TEXTURE1, alphaskytexture);
}

void GLC_StateEndMultiTextureSkyChain(void)
{
	GL_DisableMultitexture();
	GL_TextureEnvMode(GL_REPLACE);
	if (gl_fogsky.value) {
		GL_DisableFog();
	}
}

void GLC_StateBeginSingleTextureSkyPass(void)
{
	GL_DisableMultitexture();
	if (gl_fogsky.value) {
		GL_EnableFog();
	}
	GL_BindTextureUnit(GL_TEXTURE0, solidskytexture);
}

void GLC_StateBeginSingleTextureCloudPass(void)
{
	GL_AlphaBlendFlags(GL_BLEND_ENABLED);
	GL_BindTextureUnit(GL_TEXTURE0, alphaskytexture);
}

void GLC_StateEndSingleTextureSky(void)
{
	GL_AlphaBlendFlags(GL_BLEND_DISABLED);
	if (gl_fogsky.value) {
		GL_DisableFog();
	}
}

void GLC_StateBeginRenderFullbrights(void)
{
	// don't bother writing Z
	GL_DepthMask(GL_FALSE);
	GL_AlphaBlendFlags(GL_ALPHATEST_ENABLED);
	GL_TextureEnvMode(GL_REPLACE);
}

void GLC_StateEndRenderFullbrights(void)
{
	GL_AlphaBlendFlags(GL_ALPHATEST_DISABLED);
	GL_DepthMask(GL_TRUE);
}

void GLC_StateBeginRenderLumas(void)
{
	GL_DepthMask(GL_FALSE);	// don't bother writing Z
	GL_AlphaBlendFlags(GL_BLEND_ENABLED);
	GL_BlendFunc(GL_ONE, GL_ONE);
	GL_TextureEnvMode(GL_REPLACE);
}

void GLC_StateEndRenderLumas(void)
{
	// FIXME: GL_ResetState()
	GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GL_DepthMask(GL_TRUE);
}

void GLC_StateBeginEmitDetailPolys(void)
{
	GL_BindTextureUnit(GL_TEXTURE0, detailtexture);
	GL_TextureEnvMode(GL_DECAL);
	GL_BlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
	GL_AlphaBlendFlags(GL_BLEND_ENABLED);
}

void GLC_StateEndEmitDetailPolys(void)
{
	GL_TextureEnvMode(GL_REPLACE);
	GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GL_AlphaBlendFlags(GL_BLEND_DISABLED);
}

void GLC_StateBeginDrawMapOutline(void)
{
	extern cvar_t gl_outline_width;

	GL_PolygonOffset(POLYGONOFFSET_OUTLINES);
	glColor3f(1.0f, 1.0f, 1.0f);
	glLineWidth(bound(0.1, gl_outline_width.value, 3.0));

	glPushAttrib(GL_ENABLE_BIT);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_TEXTURE_2D);
}

void GLC_StateBeginEndMapOutline(void)
{
	glPopAttrib();

	glColor3f(1.0f, 1.0f, 1.0f);
	GL_PolygonOffset(POLYGONOFFSET_DISABLED);
}

void GLM_StateBeginDrawBillboards(void)
{
	GL_DisableFog();
	GL_DepthMask(GL_FALSE);
	GL_AlphaBlendFlags(GL_BLEND_ENABLED | GL_ALPHATEST_DISABLED);
	GL_TextureEnvMode(GL_MODULATE);
	GL_ShadeModel(GL_SMOOTH);
}

void GLM_StateEndDrawBillboards(void)
{
	GL_DepthMask(GL_TRUE);
	GL_AlphaBlendFlags(GL_BLEND_DISABLED);
	GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GL_TextureEnvMode(GL_REPLACE);
	GL_ShadeModel(GL_FLAT);
	GL_EnableFog();
}

void GLM_StateBeginDrawWorldOutlines(void)
{
	extern cvar_t gl_outline_width;

	GL_PolygonOffset(POLYGONOFFSET_OUTLINES);
	GL_CullFace(GL_BACK);
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	GL_Disable(GL_DEPTH_TEST);
	GL_Disable(GL_CULL_FACE);

	// limit outline width, since even width == 3 can be considered as cheat.
	glLineWidth(bound(0.1, gl_outline_width.value, 3.0));
}

void GLM_StateEndDrawWorldOutlines(void)
{
	GL_Enable(GL_DEPTH_TEST);
	GL_Enable(GL_CULL_FACE);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	GL_CullFace(GL_FRONT);
	GL_PolygonOffset(POLYGONOFFSET_DISABLED);
}

void GL_BeginStateAlphaLineRGB(float thickness)
{
	glDisable (GL_TEXTURE_2D);
	GL_AlphaBlendFlags(GL_ALPHATEST_DISABLED | GL_BLEND_ENABLED);
	if (thickness > 0.0) {
		glLineWidth(thickness);
	}
}

void GL_EndStateAlphaLineRGB(void)
{
	glEnable(GL_TEXTURE_2D);
	GL_AlphaBlendFlags(GL_ALPHATEST_ENABLED | GL_BLEND_DISABLED);
}

void GLC_BeginStateDrawAliasFrame(GLenum textureEnvMode, texture_ref texture, texture_ref fb_texture, qbool mtex, float alpha, struct custom_model_color_s* custom_model)
{
	GL_DisableMultitexture();
	GL_EnableTMU(GL_TEXTURE0);
	GL_TextureEnvMode(textureEnvMode);
	if (GL_TextureReferenceIsValid(texture)) {
		GL_BindTextureUnit(GL_TEXTURE0, texture);
	}
	if (GL_TextureReferenceIsValid(fb_texture) && mtex) {
		GL_EnableMultitexture();
		GL_BindTextureUnit(GL_TEXTURE1, fb_texture);
		GL_TextureEnvMode(GL_DECAL);
	}

	if (alpha < 1) {
		GL_AlphaBlendFlags(GL_BLEND_ENABLED);
	}

	if (custom_model) {
		glDisable(GL_TEXTURE_2D);
		glColor4ub(custom_model->color_cvar.color[0], custom_model->color_cvar.color[1], custom_model->color_cvar.color[2], alpha * 255);
	}
}

void GLC_EndStateDrawAliasFrame(void)
{
	GL_AlphaBlendFlags(GL_BLEND_DISABLED);
	glEnable(GL_TEXTURE_2D);
}

void GLC_BeginStateDrawAliasCustomModel(void)
{
	glDisable(GL_TEXTURE_2D);
}

void GLC_EndStateDrawAliasCustomModel(void)
{
	glEnable(GL_TEXTURE_2D);
}

void GLC_BeginStateAliasModelShadow(void)
{
	glDisable(GL_TEXTURE_2D);
	GL_AlphaBlendFlags(GL_BLEND_ENABLED);
	glColor4f(0, 0, 0, 0.5);
}

void GLC_EndStateAliasModelShadow(void)
{
	GL_AlphaBlendFlags(GL_BLEND_DISABLED);
	glEnable(GL_TEXTURE_2D);
}

void GLC_StateBeginDrawFlatModel(void)
{
	GL_DisableMultitexture();
	GL_TextureEnvMode(GL_BLEND);
	GL_EnableTMU(GL_TEXTURE0);

	// START shaman BUG /fog not working with /r_drawflat {
	if (gl_fogenable.value) {
		glEnable(GL_FOG);
	}
	// } END shaman BUG /fog not working with /r_drawflat
}

void GLC_StateEndDrawFlatModel(void)
{
	if (gl_fogenable.value) {
		glDisable(GL_FOG);
	}

	glColor3f(1.0f, 1.0f, 1.0f);
}

void GLC_StateBeginDrawTextureChains(GLenum lightmapTextureUnit, GLenum fullbrightTextureUnit, GLenum fullbrightMode)
{
	if (gl_fogenable.value) {
		glEnable(GL_FOG);
	}

	GL_EnableTMU(GL_TEXTURE0);
	GL_TextureEnvMode(GL_REPLACE);

	if (lightmapTextureUnit) {
		GL_EnableTMU(lightmapTextureUnit);
		GLC_SetLightmapTextureEnvironment();
	}
	if (fullbrightTextureUnit) {
		GL_EnableTMU(fullbrightTextureUnit);
		GL_TextureEnvMode(fullbrightMode);
	}
	if (lightmapTextureUnit || fullbrightTextureUnit) {
		GL_EnableMultitexture();
	}
	else {
		GL_DisableMultitexture();
	}
}

void GLC_StateEndDrawTextureChainsFirstPass(GLenum lightmapTextureUnit, GLenum fullbrightTextureUnit)
{
	if (fullbrightTextureUnit) {
		GL_DisableTMU(fullbrightTextureUnit);
	}
	if (lightmapTextureUnit) {
		GL_DisableTMU(lightmapTextureUnit);
	}
	if (fullbrightTextureUnit || lightmapTextureUnit) {
		GL_SelectTexture(GL_TEXTURE0);
	}
}

void GLC_StateEndDrawTextureChains(void)
{
	if (gl_fogenable.value) {
		glDisable(GL_FOG);
	}
}

void GLC_StateBeginFastTurbPoly(byte color[4])
{
	float wateralpha = GL_WaterAlpha();

	GL_DisableMultitexture();
	GL_EnableFog();
	glDisable(GL_TEXTURE_2D);

	// START shaman FIX /gl_turbalpha + /r_fastturb {
	if (wateralpha < 1.0 && wateralpha >= 0) {
		GL_AlphaBlendFlags(GL_BLEND_ENABLED);
		color[3] = wateralpha * 255;
		glColor4ubv(color); // 1, 1, 1, wateralpha
		GL_TextureEnvMode(GL_MODULATE);
		if (wateralpha < 0.9) {
			GL_DepthMask(GL_FALSE);
		}
	}
	else {
		glColor3ubv(color);
	}
	// END shaman FIX /gl_turbalpha + /r_fastturb {
}

void GLC_StateEndFastTurbPoly(void)
{
	// START shaman FIX /gl_turbalpha + /r_fastturb {
	GL_TextureEnvMode(GL_REPLACE);
	GL_AlphaBlendFlags(GL_BLEND_DISABLED);
	GL_DepthMask(GL_TRUE);
	// END shaman FIX /gl_turbalpha + /r_fastturb {

	GL_DisableFog();
	glEnable(GL_TEXTURE_2D);
	glColor3ubv(color_white);
	// END shaman RFE 1022504
}

void GLC_StateBeginTurbPoly(void)
{
	GL_DisableMultitexture();
	GL_EnableFog();
	glEnable(GL_TEXTURE_2D);
}

void GLC_StateEndTurbPoly(void)
{
	GL_DisableFog();
}

void GLC_StateBeginBlendLightmaps(void)
{
	extern qbool gl_invlightmaps;

	GL_DepthMask(GL_FALSE);		// don't bother writing Z
	if (gl_invlightmaps) {
		GL_BlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
	}
	else {
		GL_BlendFunc(GL_ZERO, GL_SRC_COLOR);
	}

	if (!(r_lightmap.value && r_refdef2.allow_cheats)) {
		GL_AlphaBlendFlags(GL_BLEND_ENABLED);
	}
}

void GLC_StateEndBlendLightmaps(void)
{
	GL_AlphaBlendFlags(GL_BLEND_DISABLED);
	GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GL_DepthMask(GL_TRUE);		// back to normal Z buffering
}

void GLC_StateBeginDrawSimpleItem(void)
{
	glDisable(GL_CULL_FACE);
	GL_AlphaBlendFlags(GL_ALPHATEST_ENABLED | GL_BLEND_DISABLED);
	GL_EnableTMU(GL_TEXTURE0);
}

void GLC_StateEndDrawSimpleItem(int oldFlags)
{
	glEnable(GL_CULL_FACE);
	GL_AlphaBlendFlags(oldFlags);
}

void GLC_StateBeginDrawAlphaPieSliceRGB(float thickness)
{
	glDisable(GL_TEXTURE_2D);
	GL_AlphaBlendFlags(GL_ALPHATEST_DISABLED | GL_BLEND_ENABLED);
	if (thickness > 0.0) {
		glLineWidth(thickness);
	}
}

void GLC_StateEndDrawAlphaPieSliceRGB(float thickness)
{
	// FIXME: thickness is not reset
	glEnable(GL_TEXTURE_2D);
	GL_AlphaBlendFlags(GL_ALPHATEST_ENABLED | GL_BLEND_DISABLED);
	glColor4ubv(color_white);
}

void GLC_StateBeginDrawCharacterBase(qbool apply_overall_alpha, byte color[4])
{
	extern cvar_t gl_alphafont;
	extern cvar_t scr_coloredText;
	extern float overall_alpha;

	// Turn on alpha transparency.
	if ((gl_alphafont.value || apply_overall_alpha)) {
		GL_AlphaBlendFlags(GL_ALPHATEST_DISABLED);
	}
	GL_AlphaBlendFlags(GL_BLEND_ENABLED);

	if (scr_coloredText.integer) {
		GL_TextureEnvMode(GL_MODULATE);
	}
	else {
		GL_TextureEnvMode(GL_REPLACE);
	}

	// Set the overall alpha.
	glColor4ub(color[0], color[1], color[2], color[3] * overall_alpha);
}

void GLC_StateResetCharGLState(void)
{
	GL_AlphaBlendFlags(GL_ALPHATEST_ENABLED | GL_BLEND_DISABLED);
	GL_TextureEnvMode(GL_REPLACE);
	glColor4ubv(color_white);
}

void GLC_StateBeginStringDraw(void)
{
	extern cvar_t gl_alphafont;
	extern cvar_t scr_coloredText;
	extern float overall_alpha;

	// Turn on alpha transparency.
	if (gl_alphafont.value || (overall_alpha < 1.0)) {
		GL_AlphaBlendFlags(GL_ALPHATEST_DISABLED);
	}
	GL_AlphaBlendFlags(GL_BLEND_ENABLED);

	if (scr_coloredText.integer) {
		GL_TextureEnvMode(GL_MODULATE);
	}
	else {
		GL_TextureEnvMode(GL_REPLACE);
	}
}

void GLC_StateBeginSceneBlur(void)
{
	// Remember all attributes.
	glPushAttrib(GL_ALL_ATTRIB_BITS);
	GL_Viewport(0, 0, glwidth, glheight);
	GL_OrthographicProjection(0, glwidth, 0, glheight, -99999, 99999);
	GL_IdentityModelView();
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	GL_AlphaBlendFlags(GL_ALPHATEST_DISABLED | GL_BLEND_ENABLED);
}

void GLC_StateEndSceneBlur(void)
{
	// FIXME: Yup, this is the same

	// Restore attributes.
	glPopAttrib();
}

void GLC_StateBeginCausticsPolys(void)
{
	GL_TextureEnvMode(GL_DECAL);
	GL_BlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
	GL_AlphaBlendFlags(GL_BLEND_ENABLED);
}

void GLC_StateEndCausticsPolys(void)
{
	GL_TextureEnvMode(GL_REPLACE);
	GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GL_AlphaBlendFlags(GL_BLEND_DISABLED);
}

void GL_StateBeginDrawViewModel(float alpha)
{
	// hack the depth range to prevent view model from poking into walls
	GL_DepthRange(gldepthmin, gldepthmin + 0.3 * (gldepthmax - gldepthmin));
	if (gl_mtexable) {
		if (alpha < 1) {
			GL_AlphaBlendFlags(GL_BLEND_ENABLED);
			GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}
	}
	if (gl_affinemodels.value) {
		GL_Hint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
	}
}

void GL_StateEndDrawViewModel(void)
{
	if (gl_affinemodels.value) {
		GL_Hint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
	}
	GL_DepthRange(gldepthmin, gldepthmax);
}

void GL_StateBeginDrawBrushModel(entity_t* e, qbool polygonOffset)
{
	if (e->alpha) {
		GL_AlphaBlendFlags(GL_BLEND_ENABLED);
		GL_TextureEnvMode(GL_MODULATE);
		glColor4f (1, 1, 1, e->alpha);
	}
	else {
		glColor3f (1,1,1);
	}

	if (!GL_ShadersSupported() && polygonOffset) {
		GL_PolygonOffset(POLYGONOFFSET_STANDARD);
	}
}

void GL_StateEndDrawBrushModel(void)
{
	GL_PolygonOffset(POLYGONOFFSET_DISABLED);
}

void GL_StateDefault2D(void)
{
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	GL_AlphaBlendFlags(GL_ALPHATEST_ENABLED | GL_BLEND_DISABLED);
	GL_TextureEnvMode(GL_REPLACE);
	GL_Color3ubv(color_white);
}

void GL_StateDefault3D(void)
{
	// set drawing parms
	GL_CullFace(GL_FRONT);
	if (gl_cull.value) {
		glEnable(GL_CULL_FACE);
	}
	else {
		glDisable(GL_CULL_FACE);
	}

	if (CL_MultiviewEnabled()) {
		glClear(GL_DEPTH_BUFFER_BIT);
		gldepthmin = 0;
		gldepthmax = 1;
		GL_DepthFunc(GL_LEQUAL);
	}

	GL_DepthRange(gldepthmin, gldepthmax);

	GL_AlphaBlendFlags(GL_ALPHATEST_DISABLED | GL_BLEND_DISABLED);

	glEnable(GL_DEPTH_TEST);

	if (gl_gammacorrection.integer) {
		glEnable(GL_FRAMEBUFFER_SRGB);
	}
	else {
		glDisable(GL_FRAMEBUFFER_SRGB);
	}
}

void GL_StateDefaultInit(void)
{
#ifndef __APPLE__
	glClearColor(1, 0, 0, 0);
#else
	glClearColor(0.2, 0.2, 0.2, 1.0);
#endif

	GL_CullFace(GL_FRONT);
	if (!GL_ShadersSupported()) {
		glEnable(GL_TEXTURE_2D);
	}

	GL_AlphaBlendFlags(GL_ALPHATEST_ENABLED);
	GL_AlphaFunc(GL_GREATER, 0.666);

	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	GL_ShadeModel(GL_FLAT);

	GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	GL_TextureEnvMode(GL_REPLACE);
}

void GL_StateBeginSCRTeamInfo(void)
{
	GL_TextureEnvMode(GL_MODULATE);
	GL_Color4f(1, 1, 1, 1);
	GL_AlphaBlendFlags(GL_ALPHATEST_DISABLED | GL_BLEND_ENABLED);
}

void GL_StateEndSCRTeamInfo(void)
{
	GL_AlphaBlendFlags(GL_ALPHATEST_ENABLED | GL_BLEND_DISABLED);
	GL_TextureEnvMode(GL_REPLACE);
	GL_Color4f(1, 1, 1, 1);
}

void GL_StateBeginSCRShowNick(void)
{
	GL_Color4f(1, 1, 1, 1);
	GL_TextureEnvMode(GL_MODULATE);
	GL_AlphaBlendFlags(GL_ALPHATEST_DISABLED | GL_BLEND_ENABLED);
}

void GL_StateEndSCRShowNick(void)
{
	GL_AlphaBlendFlags(GL_ALPHATEST_ENABLED | GL_BLEND_DISABLED);
	GL_TextureEnvMode(GL_REPLACE);
	GL_Color4f(1, 1, 1, 1);
}

void GLC_StateBeginDrawPolygon(void)
{
	GL_AlphaBlendFlags(GL_ALPHATEST_DISABLED | GL_BLEND_ENABLED);
	glDisable(GL_TEXTURE_2D);
	// (color also set)
}

void GLC_StateEndDrawPolygon(int oldFlags)
{
	GL_AlphaBlendFlags(oldFlags);
	glEnable(GL_TEXTURE_2D);
}

