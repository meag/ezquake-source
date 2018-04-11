/*
Copyright (C) 2002-2003 A Nourai

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

// vid_common_gl.c -- Common code for vid_wgl.c and vid_glx.c

#include <SDL.h>

#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "tr_types.h"
#include "image.h"

static void GL_InitialiseDebugging(void);

#ifdef WITH_OPENGL_TRACE
#define DEBUG_FRAME_DEPTH_CHARS 2

static qbool dev_frame_debug_queued;
#endif

// <debug-functions (4.3)>
//typedef void (APIENTRY *DEBUGPROC)(GLenum source, GLenum type, GLuint id, GLenum severity,  GLsizei length, const GLchar *message, const void *userParam);
typedef void (APIENTRY *glDebugMessageCallback_t)(GLDEBUGPROC callback, void* userParam);
typedef void (APIENTRY *glPushDebugGroup_t)(GLenum source, GLuint id, GLsizei length, const char* message);
typedef void (APIENTRY *glPopDebugGroup_t)(void);

static glPushDebugGroup_t glPushDebugGroup;
static glPopDebugGroup_t glPopDebugGroup;
// </debug-functions>

void GL_BindBuffer(buffer_ref ref);

const char *gl_vendor;
const char *gl_renderer;
const char *gl_version;
const char *gl_extensions;

int anisotropy_ext = 0;

qbool gl_mtexable = false;
int gl_textureunits = 1;
glMultiTexCoord2f_t qglMultiTexCoord2f = NULL;
glActiveTexture_t qglActiveTexture = NULL;
glClientActiveTexture_t qglClientActiveTexture = NULL;

float vid_gamma = 1.0;
byte vid_gamma_table[256];

unsigned short d_8to16table[256];
unsigned d_8to24table[256];
unsigned d_8to24table2[256];

byte color_white[4] = {255, 255, 255, 255};
byte color_black[4] = {0, 0, 0, 255};

void OnChange_gl_ext_texture_compression(cvar_t *, char *, qbool *);
extern cvar_t vid_renderer;

cvar_t gl_strings                 = {"gl_strings", "", CVAR_ROM | CVAR_SILENT};
cvar_t gl_ext_texture_compression = {"gl_ext_texture_compression", "0", CVAR_SILENT, OnChange_gl_ext_texture_compression};
cvar_t gl_maxtmu2                 = {"gl_maxtmu2", "0", CVAR_LATCH};

// GL_ARB_texture_non_power_of_two
qbool gl_support_arb_texture_non_power_of_two = false;
cvar_t gl_ext_arb_texture_non_power_of_two = {"gl_ext_arb_texture_non_power_of_two", "1", CVAR_LATCH};

// Debugging
#ifdef _WIN32
void APIENTRY MessageCallback( GLenum source,
	GLenum type,
	GLuint id,
	GLenum severity,
	GLsizei length,
	const GLchar* message,
	const void* userParam
)
{
	if (source != GL_DEBUG_SOURCE_APPLICATION) {
		char buffer[1024] = { 0 };

		if (type == GL_DEBUG_TYPE_ERROR) {
			snprintf(buffer, sizeof(buffer) - 1,
					 "GL CALLBACK: ** GL ERROR ** type = 0x%x, severity = 0x%x, message = %s\n",
					 type, severity, message);
		}
		else {
			snprintf(buffer, sizeof(buffer) - 1,
					 "GL CALLBACK: type = 0x%x, severity = 0x%x, message = %s\n",
					 type, severity, message);
		}

		OutputDebugString(buffer);
	}
}
#endif

glObjectLabel_t glObjectLabel;
glGetObjectLabel_t glGetObjectLabel;

static qbool shaders_supported = false;

qbool GL_ShadersSupported(void)
{
	return vid_renderer.integer == 1 && shaders_supported;
}

#define OPENGL_LOAD_SHADER_FUNCTION(x) \
	if (shaders_supported) { \
		x = (x ## _t)SDL_GL_GetProcAddress(#x); \
		shaders_supported = (x != NULL); \
	}

/************************************* EXTENSIONS *************************************/

static void GL_CheckShaderExtensions(void)
{
	shaders_supported = false;

	GL_InitialiseBufferHandling();
	GL_InitialiseFramebufferHandling();

	if (GL_UseGLSL() && glConfig.majorVersion >= 2) {
		shaders_supported = GLM_LoadProgramFunctions();
		shaders_supported &= GLM_LoadStateFunctions();
		shaders_supported &= GLM_LoadTextureManagementFunctions();
		shaders_supported &= GLM_LoadDrawFunctions();
	}

	GL_LoadDrawFunctions();
	GL_InitialiseDebugging();

	if (GL_UseGLSL() && !shaders_supported) {
		Con_Printf("&cf00Error&r: GLSL not available, missing extensions.\n");
		Cvar_LatchedSetValue(&vid_renderer, 0);
		Cvar_SetFlags(&vid_renderer, CVAR_ROM);
	}

	if (GL_UseGLSL()) {
		Con_Printf("&c0f0Renderer&r: OpenGL (GLSL)\n");
	}
	else {
		Con_Printf("&c0f0Renderer&r: OpenGL (classic)\n");
	}
}

void GL_CheckExtensions (void)
{
	GL_CheckMultiTextureExtensions();

	if (SDL_GL_ExtensionSupported("GL_EXT_texture_filter_anisotropic")) {
		int gl_anisotropy_factor_max;

		anisotropy_ext = 1;

		glGetIntegerv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &gl_anisotropy_factor_max);

		Com_Printf_State(PRINT_OK, "Anisotropic Filtering Extension Found (%d max)\n",gl_anisotropy_factor_max);
	}

	if (SDL_GL_ExtensionSupported("GL_ARB_texture_compression")) {
		Com_Printf_State(PRINT_OK, "Texture compression extensions found\n");
		Cvar_SetCurrentGroup(CVAR_GROUP_TEXTURES);
		Cvar_Register(&gl_ext_texture_compression);
		Cvar_ResetCurrentGroup();
	}

	// GL_ARB_texture_non_power_of_two
	// NOTE: we always register cvar even if ext is not supported.
	// cvar added just to be able force OFF an extension.
	Cvar_SetCurrentGroup(CVAR_GROUP_TEXTURES);
	Cvar_Register(&gl_ext_arb_texture_non_power_of_two);
	Cvar_ResetCurrentGroup();

	gl_support_arb_texture_non_power_of_two =
		gl_ext_arb_texture_non_power_of_two.integer && SDL_GL_ExtensionSupported("GL_ARB_texture_non_power_of_two");
	Com_Printf_State(PRINT_OK, "GL_ARB_texture_non_power_of_two extension %s\n", 
		gl_support_arb_texture_non_power_of_two ? "found" : "not found");
}

void OnChange_gl_ext_texture_compression(cvar_t *var, char *string, qbool *cancel) {
	float newval = Q_atof(string);

	gl_alpha_format = newval ? GL_COMPRESSED_RGBA_ARB : GL_RGBA8;
	gl_solid_format = newval ? GL_COMPRESSED_RGB_ARB : GL_RGB8;
}

/************************************** GL INIT **************************************/

void GL_Init(void)
{
	gl_vendor = (const char*)glGetString(GL_VENDOR);
	gl_renderer = (const char*)glGetString(GL_RENDERER);
	gl_version = (const char*)glGetString(GL_VERSION);
	if (GL_UseGLSL()) {
		gl_extensions = "(using modern OpenGL)\n";
	}
	else {
		gl_extensions = (const char*)glGetString(GL_EXTENSIONS);
	}

#if !defined( _WIN32 ) && !defined( __linux__ ) /* we print this in different place on WIN and Linux */
	/* FIXME/TODO: FreeBSD too? */
	Com_Printf_State(PRINT_INFO, "GL_VENDOR: %s\n", gl_vendor);
	Com_Printf_State(PRINT_INFO, "GL_RENDERER: %s\n", gl_renderer);
	Com_Printf_State(PRINT_INFO, "GL_VERSION: %s\n", gl_version);
#endif

	if (COM_CheckParm("-gl_ext")) {
		Com_Printf_State(PRINT_INFO, "GL_EXTENSIONS: %s\n", gl_extensions);
	}

	Cvar_Register(&gl_strings);
	Cvar_ForceSet(&gl_strings,
		va("GL_VENDOR: %s\nGL_RENDERER: %s\n"
		   "GL_VERSION: %s\nGL_EXTENSIONS: %s", gl_vendor, gl_renderer, gl_version, gl_extensions));
	Cvar_Register(&gl_maxtmu2);

	GL_CheckShaderExtensions();

	GL_StateDefaultInit();

	GL_CheckExtensions();
}

/************************************* VID GAMMA *************************************/

void Check_Gamma (unsigned char *pal) {
	float inf;
	unsigned char palette[768];
	int i;

	// we do not need this after host initialized
	if (!host_initialized) {
		float old = v_gamma.value;
		char string = v_gamma.string[0];
		if ((i = COM_CheckParm("-gamma")) != 0 && i + 1 < COM_Argc()) {
			vid_gamma = bound(0.3, Q_atof(COM_Argv(i + 1)), 1);
		}
		else {
			vid_gamma = 1;
		}

		Cvar_SetDefault (&v_gamma, vid_gamma);
		// Cvar_SetDefault set not only default value, but also reset to default, fix that
		Cvar_SetValue(&v_gamma, old || string == '0' ? old : vid_gamma);
	}

	if (vid_gamma != 1) {
		for (i = 0; i < 256; i++) {
			inf = 255 * pow((i + 0.5) / 255.5, vid_gamma) + 0.5;
			if (inf > 255) {
				inf = 255;
			}
			vid_gamma_table[i] = inf;
		}
	}
	else {
		for (i = 0; i < 256; i++) {
			vid_gamma_table[i] = i;
		}
	}

	for (i = 0; i < 768; i++) {
		palette[i] = vid_gamma_table[pal[i]];
	}

	memcpy (pal, palette, sizeof(palette));
}

/************************************* HW GAMMA *************************************/

void VID_SetPalette (unsigned char *palette) {
	int i;
	byte *pal;
	unsigned r,g,b, v, *table;

	// 8 8 8 encoding
	// Macintosh has different byte order
	pal = palette;
	table = d_8to24table;
	for (i = 0; i < 256; i++) {
		r = pal[0];
		g = pal[1];
		b = pal[2];
		pal += 3;
		v = LittleLong ((255 << 24) + (r << 0) + (g << 8) + (b << 16));
		*table++ = v;
	}
	d_8to24table[255] = 0;		// 255 is transparent

	// Tonik: create a brighter palette for bmodel textures
	pal = palette;
	table = d_8to24table2;

	for (i = 0; i < 256; i++) {
		r = pal[0] * (2.0 / 1.5); if (r > 255) r = 255;
		g = pal[1] * (2.0 / 1.5); if (g > 255) g = 255;
		b = pal[2] * (2.0 / 1.5); if (b > 255) b = 255;
		pal += 3;
		*table++ = LittleLong ((255 << 24) + (r << 0) + (g << 8) + (b << 16));
	}
	d_8to24table2[255] = 0;	// 255 is transparent
}

#undef glDisable
#undef glEnable

void GL_AlphaFunc(GLenum func, GLclampf threshold)
{
	if (GL_UseImmediateMode()) {
		glAlphaFunc(func, threshold);
	}
}

#ifdef WITH_OPENGL_TRACE
void Dev_VidFrameTrace(void)
{
	dev_frame_debug_queued = true;
}
#endif

static void GL_InitialiseDebugging(void)
{
#ifdef _WIN32
	// During init, enable debug output
	if (IsDebuggerPresent()) {
		glDebugMessageCallback_t glDebugMessageCallback = (glDebugMessageCallback_t)SDL_GL_GetProcAddress("glDebugMessageCallback");

		if (glDebugMessageCallback) {
			glEnable(GL_DEBUG_OUTPUT);
			glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
			glDebugMessageCallback((GLDEBUGPROC)MessageCallback, 0);
		}
	}
#endif

	glObjectLabel = (glObjectLabel_t)SDL_GL_GetProcAddress("glObjectLabel");
	glGetObjectLabel = (glGetObjectLabel_t)SDL_GL_GetProcAddress("glGetObjectLabel");
	glPushDebugGroup = (glPushDebugGroup_t)SDL_GL_GetProcAddress("glPushDebugGroup");
	glPopDebugGroup = (glPopDebugGroup_t)SDL_GL_GetProcAddress("glPopDebugGroup");
}

#ifdef WITH_OPENGL_TRACE
static int debug_frame_depth = 0;
static unsigned long regions_trace_only;
FILE* debug_frame_out;

void GL_EnterTracedRegion(const char* regionName, qbool trace_only)
{
	if (GL_UseGLSL()) {
		if (!trace_only && glPushDebugGroup) {
			glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, regionName);
		}
	}
	else if (debug_frame_out) {
		fprintf(debug_frame_out, "Enter: %.*s %s {\n", debug_frame_depth, "                                                          ", regionName);
		debug_frame_depth += DEBUG_FRAME_DEPTH_CHARS;
	}

	regions_trace_only <<= 1;
	regions_trace_only &= (trace_only ? 1 : 0);
}

void GL_LeaveTracedRegion(qbool trace_only)
{
	if (GL_UseGLSL()) {
		if (!trace_only && glPopDebugGroup) {
			glPopDebugGroup();
		}
	}
	else if (debug_frame_out) {
		debug_frame_depth -= DEBUG_FRAME_DEPTH_CHARS;
		debug_frame_depth = max(debug_frame_depth, 0);
		fprintf(debug_frame_out, "Leave: %.*s }\n", debug_frame_depth, "                                                          ");
	}
}

void GL_MarkEvent(const char* format, ...)
{
	va_list argptr;
	char msg[4096];

	va_start(argptr, format);
	vsnprintf(msg, sizeof(msg), format, argptr);
	va_end(argptr);

	if (GL_UseGLSL()) {
		//nvtxMark(va(msg));
	}
	else if (debug_frame_out) {
		fprintf(debug_frame_out, "Event: %.*s %s\n", debug_frame_depth, "                                                          ", msg);
	}
}

qbool GL_LoggingEnabled(void)
{
	return debug_frame_out != NULL;
}

void GL_LogAPICall(const char* format, ...)
{
	if (GL_UseImmediateMode() && debug_frame_out) {
		va_list argptr;
		char msg[4096];

		va_start(argptr, format);
		vsnprintf(msg, sizeof(msg), format, argptr);
		va_end(argptr);

		fprintf(debug_frame_out, "API:   %.*s %s\n", debug_frame_depth, "                                                          ", msg);
	}
}

void GL_ResetRegion(qbool start)
{
	if (start && debug_frame_out) {
		fclose(debug_frame_out);
		debug_frame_out = NULL;
	}
	else if (start && dev_frame_debug_queued) {
		char fileName[MAX_PATH];
#ifndef _WIN32
		time_t t;
		struct tm date;
		t = time(NULL);
		localtime_r(&t, &date);

		snprintf(fileName, sizeof(fileName), "%s/qw/frame_%04d-%02d-%02d_%02d-%02d-%02d.txt",
				 com_basedir, date.tm_year, date.tm_mon, date.tm_mday, date.tm_hour, date.tm_min, date.tm_sec);
#else
		SYSTEMTIME date;
		GetLocalTime(&date);

		snprintf(fileName, sizeof(fileName), "%s/qw/frame_%04d-%02d-%02d_%02d-%02d-%02d.txt",
				 com_basedir, date.wYear, date.wMonth, date.wDay, date.wHour, date.wMinute, date.wSecond);
#endif

		debug_frame_out = fopen(fileName, "wt");
		dev_frame_debug_queued = false;
	}

	if (GL_UseImmediateMode() && debug_frame_out) {
		fprintf(debug_frame_out, "---Reset---\n");
		debug_frame_depth = 0;
	}
}

#endif
