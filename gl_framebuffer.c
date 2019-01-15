/*
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

// Loads and does all the framebuffer stuff

// Seems the original was for camquake & ported across to draw the hud on, but not used
//   Re-using file for deferred rendering & post-processing in modern

#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "gl_framebuffer.h"
#include "tr_types.h"
#include "r_texture.h"
#include "gl_texture_internal.h"
#include "r_renderer.h"

// OpenGL functionality from elsewhere
GLuint GL_TextureNameFromReference(texture_ref ref);

// OpenGL wrapper functions
static void GL_RenderBufferStorage(GLuint renderBuffer, GLenum internalformat, GLsizei width, GLsizei height);
static void GL_FramebufferRenderbuffer(GLuint fbref, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
static GLenum GL_CheckFramebufferStatus(GLuint fbref);
static void GL_FramebufferTexture(GLuint framebuffer, GLenum attachment, GLuint texture, GLint level);
static void GL_GenRenderBuffers(GLsizei n, GLuint* buffers);
static void GL_GenFramebuffers(GLsizei n, GLuint* buffers);

typedef struct framebuffer_data_s {
	GLuint glref;
	texture_ref texture[fbtex_count];
	GLuint depthBuffer;
	GLenum depthFormat;

	GLsizei width;
	GLsizei height;
	GLenum status;
} framebuffer_data_t;

static const char* framebuffer_names[] = {
	"none", // framebuffer_none
	"std", // framebuffer_std
	"hud", // framebuffer_hud
};
static const char* framebuffer_texture_names[] = {
	"std", // fbtex_standard,
	"env", // fbtex_bloom,
};
static qbool framebuffer_depth_buffer[] = {
	false, // framebuffer_none
	true, // framebuffer_std
	false, // framebuffer_hud
};

static framebuffer_data_t framebuffer_data[framebuffer_count];
static int framebuffers;

// 
typedef void (APIENTRY *glGenFramebuffers_t)(GLsizei n, GLuint* ids);
typedef void (APIENTRY *glDeleteFramebuffers_t)(GLsizei n, GLuint* ids);
typedef void (APIENTRY *glBindFramebuffer_t)(GLenum target, GLuint framebuffer);

typedef void (APIENTRY *glGenRenderbuffers_t)(GLsizei n, GLuint* ids);
typedef void (APIENTRY *glDeleteRenderbuffers_t)(GLsizei n, GLuint* ids);
typedef void (APIENTRY *glBindRenderbuffer_t)(GLenum target, GLuint renderbuffer);
typedef void (APIENTRY *glRenderbufferStorage_t)(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
typedef void (APIENTRY *glNamedRenderbufferStorage_t)(GLuint renderbuffer, GLenum internalformat, GLsizei width, GLsizei height);
typedef void (APIENTRY *glFramebufferRenderbuffer_t)(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
typedef void (APIENTRY *glNamedFramebufferRenderbuffer_t)(GLuint framebuffer, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);

typedef void (APIENTRY *glFramebufferTexture_t)(GLenum target, GLenum attachment, GLuint texture, GLint level);
typedef void (APIENTRY *glNamedFramebufferTexture_t)(GLuint framebuffer, GLenum attachment, GLuint texture, GLint level);
typedef GLenum (APIENTRY *glCheckFramebufferStatus_t)(GLenum target);
typedef GLenum (APIENTRY *glCheckNamedFramebufferStatus_t)(GLuint framebuffer, GLenum target);
typedef void (APIENTRY *glBlitFramebuffer_t)(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);
typedef void (APIENTRY *glBlitNamedFramebuffer_t)(GLuint readFramebuffer, GLuint drawFramebuffer, GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);

typedef void (APIENTRY *glClipControl_t)(GLenum origin, GLenum depth);

static glGenFramebuffers_t qglGenFramebuffers;
static glDeleteFramebuffers_t qglDeleteFramebuffers;
static glBindFramebuffer_t qglBindFramebuffer;

static glGenRenderbuffers_t qglGenRenderbuffers;
static glDeleteRenderbuffers_t qglDeleteRenderbuffers;
static glBindRenderbuffer_t qglBindRenderbuffer;
static glRenderbufferStorage_t qglRenderbufferStorage;
static glNamedRenderbufferStorage_t qglNamedRenderbufferStorage;
static glFramebufferRenderbuffer_t qglFramebufferRenderbuffer;
static glNamedFramebufferRenderbuffer_t qglNamedFramebufferRenderbuffer;

static glFramebufferTexture_t qglFramebufferTexture;
static glNamedFramebufferTexture_t qglNamedFramebufferTexture;
static glCheckFramebufferStatus_t qglCheckFramebufferStatus;
static glCheckNamedFramebufferStatus_t qglCheckNamedFramebufferStatus;

static glBlitFramebuffer_t qglBlitFramebuffer;
static glBlitNamedFramebuffer_t qglBlitNamedFramebuffer;

static glClipControl_t qglClipControl;

static GLenum glDepthFormats[] = { 0, GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT32, GL_DEPTH_COMPONENT32F };
typedef enum { r_depthformat_best, r_depthformat_16bit, r_depthformat_24bit, r_depthformat_32bit, r_depthformat_32bit_float, r_depthformat_count } r_depthformat;

#ifdef C_ASSERT
C_ASSERT(sizeof(framebuffer_names) / sizeof(framebuffer_names[0]) == framebuffer_count);
C_ASSERT(sizeof(framebuffer_texture_names) / sizeof(framebuffer_texture_names[0]) == fbtex_count);
C_ASSERT(sizeof(framebuffer_depth_buffer) / sizeof(framebuffer_depth_buffer[0]) == framebuffer_count);
C_ASSERT(sizeof(glDepthFormats) / sizeof(glDepthFormats[0]) == r_depthformat_count);
#endif

extern cvar_t vid_framebuffer_depthformat;
extern cvar_t vid_framebuffer_hdr;

//
// Initialize framebuffer stuff, Loads procadresses and such.
//
void GL_InitialiseFramebufferHandling(void)
{
	qbool framebuffers_supported = true;

	glConfig.supported_features &= ~(R_SUPPORT_FRAMEBUFFERS | R_SUPPORT_FRAMEBUFFERS_BLIT);

	if (GL_VersionAtLeast(3,0) || SDL_GL_ExtensionSupported("GL_ARB_framebuffer_object")) {
		GL_LoadMandatoryFunction(glGenFramebuffers, framebuffers_supported);
		GL_LoadMandatoryFunction(glDeleteFramebuffers, framebuffers_supported);
		GL_LoadMandatoryFunction(glBindFramebuffer, framebuffers_supported);

		GL_LoadMandatoryFunction(glGenRenderbuffers, framebuffers_supported);
		GL_LoadMandatoryFunction(glDeleteRenderbuffers, framebuffers_supported);
		GL_LoadMandatoryFunction(glBindRenderbuffer, framebuffers_supported);
		GL_LoadMandatoryFunction(glRenderbufferStorage, framebuffers_supported);
		GL_LoadMandatoryFunction(glFramebufferRenderbuffer, framebuffers_supported);
		GL_LoadMandatoryFunction(glBlitFramebuffer, framebuffers_supported);

		GL_LoadMandatoryFunction(glFramebufferTexture, framebuffers_supported);
		GL_LoadMandatoryFunction(glCheckFramebufferStatus, framebuffers_supported);

		glConfig.supported_features |= (framebuffers_supported ? (R_SUPPORT_FRAMEBUFFERS | R_SUPPORT_FRAMEBUFFERS_BLIT) : 0);
		if (GL_VersionAtLeast(3, 0) || SDL_GL_ExtensionSupported("GL_ARB_depth_buffer_float")) {
			glConfig.supported_features |= R_SUPPORT_DEPTH32F;
		}
	}
	else if (SDL_GL_ExtensionSupported("GL_EXT_framebuffer_object") && SDL_GL_ExtensionSupported("GL_ARB_depth_texture")) {
		GL_LoadMandatoryFunctionEXT(glGenFramebuffers, framebuffers_supported);
		GL_LoadMandatoryFunctionEXT(glDeleteFramebuffers, framebuffers_supported);
		GL_LoadMandatoryFunctionEXT(glBindFramebuffer, framebuffers_supported);

		GL_LoadMandatoryFunctionEXT(glGenRenderbuffers, framebuffers_supported);
		GL_LoadMandatoryFunctionEXT(glDeleteRenderbuffers, framebuffers_supported);
		GL_LoadMandatoryFunctionEXT(glBindRenderbuffer, framebuffers_supported);
		GL_LoadMandatoryFunctionEXT(glRenderbufferStorage, framebuffers_supported);
		GL_LoadMandatoryFunctionEXT(glFramebufferRenderbuffer, framebuffers_supported);
		if (SDL_GL_ExtensionSupported("GL_EXT_framebuffer_blit")) {
			GL_LoadOptionalFunctionEXT(glBlitFramebuffer);
		}

		GL_LoadMandatoryFunctionEXT(glFramebufferTexture, framebuffers_supported);
		GL_LoadMandatoryFunctionEXT(glCheckFramebufferStatus, framebuffers_supported);

		glConfig.supported_features |= (framebuffers_supported ? (R_SUPPORT_FRAMEBUFFERS) : 0);
		glConfig.supported_features |= (framebuffers_supported && (qglBlitFramebuffer != NULL) ? (R_SUPPORT_FRAMEBUFFERS_BLIT) : 0);
		glConfig.supported_features |= SDL_GL_ExtensionSupported("GL_ARB_depth_buffer_float") ? R_SUPPORT_DEPTH32F : 0;
	}

	if (GL_UseDirectStateAccess()) {
		GL_LoadOptionalFunction(glNamedRenderbufferStorage);
		GL_LoadOptionalFunction(glNamedFramebufferRenderbuffer);
		GL_LoadOptionalFunction(glBlitNamedFramebuffer);
		GL_LoadOptionalFunction(glNamedFramebufferTexture);
		GL_LoadOptionalFunction(glCheckNamedFramebufferStatus);
	}

	qglClipControl = NULL;
	// meag: disabled (classic needs glFrustum replaced, modern needs non-rubbish viewweapon
	//                 depth hack, and near-plane clipping issues when the player is gibbed)
	/*if (GL_VersionAtLeast(4, 5) || SDL_GL_ExtensionSupported("GL_ARB_clip_control")) {
		GL_LoadOptionalFunction(glClipControl);
	}*/

	framebuffers = 1;
	memset(framebuffer_data, 0, sizeof(framebuffer_data));
}

qbool GL_FramebufferCreate(framebuffer_id id, int width, int height)
{
	framebuffer_data_t* fb = NULL;
	char label[128];
	qbool hdr = (vid_framebuffer_hdr.integer && GL_VersionAtLeast(3, 0) && id == framebuffer_std);

	if (!GL_Supported(R_SUPPORT_FRAMEBUFFERS)) {
		return false;
	}

	fb = &framebuffer_data[id];
	if (fb->glref) {
		return false;
	}

	memset(fb, 0, sizeof(*fb));

	// Render to texture
	strlcpy(label, framebuffer_names[id], sizeof(label));
	strlcat(label, "/", sizeof(label));
	strlcat(label, framebuffer_texture_names[fbtex_standard], sizeof(label));

	//R_AllocateTextureReferences(texture_type_2d, width, height, TEX_NOSCALE | (id == framebuffer_std ? 0 : TEX_ALPHA), 1, &fb->texture[fbtex_standard]);
	GL_CreateTexturesWithIdentifier(texture_type_2d, 1, &fb->texture[fbtex_standard], label);
	GL_TexStorage2D(fb->texture[fbtex_standard], 1, hdr ? GL_RGB16F : (id == framebuffer_std ? GL_RGB8 : GL_RGBA8), width, height, false);
	renderer.TextureSetFiltering(fb->texture[fbtex_standard], texture_minification_linear, texture_minification_linear);
	renderer.TextureWrapModeClamp(fb->texture[fbtex_standard]);

	/*if (id == framebuffer_std) {
		strlcpy(label, framebuffer_names[id], sizeof(label));
		strlcat(label, "/", sizeof(label));
		strlcat(label, framebuffer_texture_names[fbtex_bloom], sizeof(label));

		R_AllocateTextureReferences(texture_type_2d, width, height, TEX_NOSCALE | TEX_MIPMAP, 1, &fb->texture[fbtex_bloom]);
		renderer.TextureLabelSet(fb->texture[fbtex_bloom], label);
		renderer.TextureSetFiltering(fb->texture[fbtex_bloom], texture_minification_linear, texture_minification_linear);
		renderer.TextureWrapModeClamp(fb->texture[fbtex_bloom]);
	}*/

	// Create frame buffer with texture & depth
	GL_GenFramebuffers(1, &fb->glref);
	GL_TraceObjectLabelSet(GL_FRAMEBUFFER, fb->glref, -1, framebuffer_names[id]);

	// Depth buffer
	if (framebuffer_depth_buffer[id]) {
		GLenum depthFormat = glDepthFormats[bound(0, vid_framebuffer_depthformat.integer, r_depthformat_count - 1)];
		if (depthFormat == 0) {
			depthFormat = qglClipControl ? GL_DEPTH_COMPONENT32F : GL_DEPTH_COMPONENT32;
		}
		if (depthFormat == GL_DEPTH_COMPONENT32F && !GL_Supported(R_SUPPORT_DEPTH32F)) {
			depthFormat = GL_DEPTH_COMPONENT32;
		}

		GL_GenRenderBuffers(1, &fb->depthBuffer);
		GL_TraceObjectLabelSet(GL_RENDERBUFFER, fb->depthBuffer, -1, "depth-buffer");
		GL_RenderBufferStorage(fb->depthBuffer, fb->depthFormat = depthFormat, width, height);
		GL_FramebufferRenderbuffer(fb->glref, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fb->depthBuffer);

		if (qglClipControl) {
			if (depthFormat == GL_DEPTH_COMPONENT32F) {
				qglClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
				glClearDepth(0.0f);
				glConfig.reversed_depth = true;
			}
			else {
				qglClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
				glClearDepth(1.0f);
				glConfig.reversed_depth = false;
			}
		}
		else {
			glConfig.reversed_depth = false;
			glClearDepth(1.0f);
		}
	}

	GL_FramebufferTexture(fb->glref, GL_COLOR_ATTACHMENT0, GL_TextureNameFromReference(fb->texture[fbtex_standard]), 0);

	fb->status = GL_CheckFramebufferStatus(fb->glref);
	fb->width = width;
	fb->height = height;

	qglBindFramebuffer(GL_FRAMEBUFFER, 0);

	return fb->status == GL_FRAMEBUFFER_COMPLETE;
}

void GL_FramebufferDelete(framebuffer_id id)
{
	int i;
	framebuffer_data_t* fb = &framebuffer_data[id];

	if (id != framebuffer_none && fb->glref) {
		if (fb->depthBuffer) {
			qglDeleteRenderbuffers(1, &fb->depthBuffer);
		}
		for (i = 0; i < sizeof(fb->texture) / sizeof(fb->texture[0]); ++i) {
			if (R_TextureReferenceIsValid(fb->texture[i])) {
				R_DeleteTexture(&fb->texture[i]);
			}
		}
		if (fb->glref) {
			qglDeleteFramebuffers(1, &fb->glref);
		}

		memset(fb, 0, sizeof(*fb));
	}
}

void GL_FramebufferStartUsing(framebuffer_id id)
{
	qglBindFramebuffer(GL_FRAMEBUFFER, framebuffer_data[id].glref);
}

void GL_FramebufferStartUsingScreen(void)
{
	qglBindFramebuffer(GL_FRAMEBUFFER, 0);
}

texture_ref GL_FramebufferTextureReference(framebuffer_id id, fbtex_id tex_id)
{
	return framebuffer_data[id].texture[tex_id];
}

// OpenGL wrapper functions
static void GL_RenderBufferStorage(GLuint renderBuffer, GLenum internalformat, GLsizei width, GLsizei height)
{
	if (qglNamedRenderbufferStorage) {
		qglNamedRenderbufferStorage(renderBuffer, internalformat, width, height);
	}
	else if (qglBindRenderbuffer && qglRenderbufferStorage) {
		qglBindRenderbuffer(GL_RENDERBUFFER, renderBuffer);
		qglRenderbufferStorage(GL_RENDERBUFFER, internalformat, width, height);
	}
	else {
		Sys_Error("ERROR: %s called without driver support", __FUNCTION__);
	}
}

static void GL_FramebufferRenderbuffer(GLuint fbref, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer)
{
	if (qglNamedFramebufferRenderbuffer) {
		qglNamedFramebufferRenderbuffer(fbref, attachment, renderbuffertarget, renderbuffer);
	}
	else if (qglBindFramebuffer && qglFramebufferRenderbuffer) {
		qglBindFramebuffer(GL_FRAMEBUFFER, fbref);
		qglFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, renderbuffertarget, renderbuffer);
	}
	else {
		Sys_Error("ERROR: %s called without driver support", __FUNCTION__);
	}
}

static GLenum GL_CheckFramebufferStatus(GLuint fbref)
{
	if (qglCheckNamedFramebufferStatus) {
		return qglCheckNamedFramebufferStatus(fbref, GL_FRAMEBUFFER);
	}
	else if (qglBindFramebuffer && qglCheckFramebufferStatus) {
		qglBindFramebuffer(GL_FRAMEBUFFER, fbref);
		return qglCheckFramebufferStatus(GL_FRAMEBUFFER);
	}
	else {
		Sys_Error("ERROR: %s called without driver support", __FUNCTION__);
		return 0;
	}
}

static void GL_FramebufferTexture(GLuint framebuffer, GLenum attachment, GLuint texture, GLint level)
{
	if (qglNamedFramebufferTexture) {
		qglNamedFramebufferTexture(framebuffer, attachment, texture, level);
	}
	else if (qglBindFramebuffer && qglFramebufferTexture) {
		qglBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
		qglFramebufferTexture(GL_FRAMEBUFFER, attachment, texture, level);
	}
	else {
		Sys_Error("ERROR: %s called without driver support", __FUNCTION__);
	}
}

static void GL_GenRenderBuffers(GLsizei n, GLuint* buffers)
{
	int i;

	qglGenRenderbuffers(n, buffers);

	for (i = 0; i < n; ++i) {
		qglBindRenderbuffer(GL_RENDERBUFFER, buffers[i]);
	}
}

static void GL_GenFramebuffers(GLsizei n, GLuint* buffers)
{
	int i;

	qglGenFramebuffers(n, buffers);

	for (i = 0; i < n; ++i) {
		qglBindFramebuffer(GL_FRAMEBUFFER, buffers[i]);
	}
}

int GL_FrameBufferWidth(framebuffer_id id)
{
	return framebuffer_data[id].width;
}

int GL_FrameBufferHeight(framebuffer_id id)
{
	return framebuffer_data[id].height;
}

void GL_FramebufferBlitSimple(framebuffer_id source_id, framebuffer_id destination_id)
{
	framebuffer_data_t* src = &framebuffer_data[source_id];
	framebuffer_data_t* dest = &framebuffer_data[destination_id];
	GLint srcTL[2] = { 0, 0 };
	GLint srcSize[2];
	GLint destTL[2] = { 0, 0 };
	GLint destSize[2];
	GLenum filter = GL_NEAREST;

	srcSize[0] = src->glref ? src->width : glConfig.vidWidth;
	srcSize[1] = src->glref ? src->height : glConfig.vidHeight;
	destSize[0] = destination_id ? dest->width : glConfig.vidWidth;
	destSize[1] = destination_id ? dest->height : glConfig.vidHeight;
	if (srcSize[0] != destSize[0] || srcSize[1] != destSize[1]) {
		filter = GL_LINEAR;
	}

	if (qglBlitNamedFramebuffer) {
		qglBlitNamedFramebuffer(
			src->glref, dest->glref,
			srcTL[0], srcTL[1], srcTL[0] + srcSize[0], srcTL[1] + srcSize[1],
			destTL[0], destTL[1], destTL[0] + destSize[0], destTL[1] + destSize[1],
			GL_COLOR_BUFFER_BIT, filter
		);
	}
	else if (qglBlitFramebuffer) {
		// ARB_framebuffer
		qglBindFramebuffer(GL_READ_FRAMEBUFFER, src->glref);
		qglBindFramebuffer(GL_DRAW_FRAMEBUFFER, dest->glref);

		qglBlitFramebuffer(
			srcTL[0], srcTL[1], srcTL[0] + srcSize[0], srcTL[1] + srcSize[1],
			destTL[0], destTL[1], destTL[0] + destSize[0], destTL[1] + destSize[1],
			GL_COLOR_BUFFER_BIT, filter
		);
	}
	else {
		// Shouldn't have been called, not supported
	}
}

// --- Wrapper functionality over, rendering logic below ---
extern cvar_t vid_framebuffer;
extern cvar_t vid_framebuffer_depthformat;
extern cvar_t vid_framebuffer_palette;

qbool GL_FramebufferEnabled3D(void)
{
	framebuffer_data_t* fb = &framebuffer_data[framebuffer_std];

	return vid_framebuffer.integer && fb->glref && R_TextureReferenceIsValid(fb->texture[fbtex_standard]);
}

qbool GL_FramebufferEnabled2D(void)
{
	framebuffer_data_t* fb = &framebuffer_data[framebuffer_hud];

	return vid_framebuffer.integer && fb->glref && R_TextureReferenceIsValid(fb->texture[fbtex_standard]);
}

static void VID_FramebufferFlip(void)
{
	qbool flip3d = vid_framebuffer.integer && GL_FramebufferEnabled3D();
	qbool flip2d = vid_framebuffer.integer == USE_FRAMEBUFFER_3DONLY && GL_FramebufferEnabled2D();

	if (flip3d || flip2d) {
		extern cvar_t vid_framebuffer_blit;

		// Screen-wide framebuffer without any processing required, so we can just blit
		qbool should_blit = (
			vid_framebuffer_palette.integer == 0 &&
			vid_framebuffer.integer != USE_FRAMEBUFFER_3DONLY &&
			vid_framebuffer_blit.integer &&
			(glConfig.supported_features & R_SUPPORT_FRAMEBUFFERS_BLIT)
		);

		// render to screen from now on
		GL_FramebufferStartUsingScreen();

		if (should_blit && flip3d) {
			// Blit to screen
			GL_FramebufferBlitSimple(framebuffer_std, framebuffer_none);
		}
		else {
			renderer.RenderFramebuffers();
		}
	}
}

static qbool VID_FramebufferInit(framebuffer_id id, int effective_width, int effective_height)
{
	framebuffer_data_t* fb = &framebuffer_data[id];

	if (effective_width && effective_height) {
		if (!fb->glref) {
			GL_FramebufferCreate(id, effective_width, effective_height);
		}
		else if (fb->width != effective_width || fb->height != effective_height) {
			GL_FramebufferDelete(id);

			GL_FramebufferCreate(id, effective_width, effective_height);
		}

		if (fb->glref) {
			GL_FramebufferStartUsing(id);
			return true;
		}
	}
	else if (fb->glref) {
		GL_FramebufferDelete(id);
	}

	return false;
}

void GL_FramebufferScreenDrawStart(void)
{
	if (vid_framebuffer.integer) {
		VID_FramebufferInit(framebuffer_std, VID_ScaledWidth3D(), VID_ScaledHeight3D());
	}
}

qbool GL_Framebuffer2DSwitch(void)
{
	if (vid_framebuffer.integer == USE_FRAMEBUFFER_3DONLY) {

		if (VID_FramebufferInit(framebuffer_hud, glConfig.vidWidth, glConfig.vidHeight)) {
			R_Viewport(0, 0, glConfig.vidWidth, glConfig.vidHeight);
			glClear(GL_COLOR_BUFFER_BIT);
			return true;
		}
	}

	R_Viewport(glx, gly, glwidth, glheight);
	return false;
}

void GL_FramebufferPostProcessScreen(void)
{
	extern cvar_t vid_framebuffer, vid_framebuffer_palette;
	qbool framebuffer_active = GL_FramebufferEnabled3D() || GL_FramebufferEnabled2D();

	if (framebuffer_active) {
		R_Viewport(glx, gly, glConfig.vidWidth, glConfig.vidHeight);

		VID_FramebufferFlip();

		if (!vid_framebuffer_palette.integer) {
			// Hardware palette changes
			V_UpdatePalette();
		}
	}
	else {
		// Hardware palette changes
		V_UpdatePalette();
	}
}

const char* GL_FramebufferZBufferString(framebuffer_id ref)
{
	if (framebuffer_data[ref].depthFormat == GL_DEPTH_COMPONENT16) {
		return "16-bit float z-buffer";
	}
	else if (framebuffer_data[ref].depthFormat == GL_DEPTH_COMPONENT24) {
		return "24-bit float z-buffer";
	}
	else if (framebuffer_data[ref].depthFormat == GL_DEPTH_COMPONENT32) {
		return "32-bit z-buffer";
	}
	else if (framebuffer_data[ref].depthFormat == GL_DEPTH_COMPONENT32F) {
		return "32-bit float z-buffer";
	}
	else {
		return "unknown z-buffer";
	}
}
