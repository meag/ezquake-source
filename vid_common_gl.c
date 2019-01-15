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

#ifdef WITH_NVTX
#include "nvToolsExt.h"
#endif

void GL_AlphaFunc(GLenum func, GLclampf threshold);

const char *gl_vendor;
const char *gl_renderer;
const char *gl_version;
const char *gl_extensions;

int anisotropy_ext = 0;

qbool gl_mtexable = false;
int gl_textureunits = 1;
lpMTexFUNC qglMultiTexCoord2f = NULL;
lpSelTexFUNC qglActiveTexture = NULL;

qbool gl_combine = false;

qbool gl_add_ext = false;

float vid_gamma = 1.0;
byte vid_gamma_table[256];

unsigned short d_8to16table[256];
unsigned d_8to24table[256];
unsigned d_8to24table2[256];

byte color_white[4] = {255, 255, 255, 255};
byte color_black[4] = {0, 0, 0, 255};

void OnChange_gl_ext_texture_compression(cvar_t *, char *, qbool *);

cvar_t	gl_strings = {"gl_strings", "", CVAR_ROM | CVAR_SILENT};
cvar_t	gl_ext_texture_compression = {"gl_ext_texture_compression", "0", CVAR_SILENT, OnChange_gl_ext_texture_compression};
cvar_t  gl_maxtmu2 = {"gl_maxtmu2", "0", CVAR_LATCH};

// GL_ARB_texture_non_power_of_two
qbool gl_support_arb_texture_non_power_of_two = false;
cvar_t gl_ext_arb_texture_non_power_of_two = {"gl_ext_arb_texture_non_power_of_two", "1", CVAR_LATCH};

// VBO functions
glBindBuffer_t     glBindBufferExt = NULL;
glBufferData_t     glBufferDataExt = NULL;
glBufferSubData_t  glBufferSubDataExt = NULL;
glGenBuffers_t     glGenBuffers = NULL;
glDeleteBuffers_t  glDeleteBuffers = NULL;

// VAO functions
glGenVertexArrays_t         glGenVertexArrays = NULL;
glBindVertexArray_t         glBindVertexArray = NULL;
glEnableVertexAttribArray_t glEnableVertexAttribArray = NULL;
glDeleteVertexArrays_t      glDeleteVertexArrays = NULL;
glVertexAttribPointer_t     glVertexAttribPointer = NULL;
glVertexAttribIPointer_t    glVertexAttribIPointer = NULL;
glVertexAttribDivisor_t     glVertexAttribDivisor = NULL;

// Shader functions
glCreateShader_t      glCreateShader = NULL;
glShaderSource_t      glShaderSource = NULL;
glCompileShader_t     glCompileShader = NULL;
glDeleteShader_t      glDeleteShader = NULL;
glGetShaderInfoLog_t  glGetShaderInfoLog = NULL;
glGetShaderiv_t       glGetShaderiv = NULL;

// Program functions
glCreateProgram_t     glCreateProgram = NULL;
glLinkProgram_t       glLinkProgram = NULL;
glDeleteProgram_t     glDeleteProgram = NULL;
glGetProgramiv_t      glGetProgramiv = NULL;
glGetProgramInfoLog_t glGetProgramInfoLog = NULL;
glUseProgram_t        glUseProgram = NULL;
glAttachShader_t      glAttachShader = NULL;
glDetachShader_t      glDetachShader = NULL;

// Uniforms
glGetUniformLocation_t   glGetUniformLocation = NULL;
glUniform1f_t            glUniform1f;
glUniform1fv_t           glUniform1fv;
glUniform2f_t            glUniform2f;
glUniform3f_t            glUniform3f;
glUniform3fv_t           glUniform3fv;
glUniform4f_t            glUniform4f;
glUniform1i_t            glUniform1i;
glUniformMatrix4fv_t     glUniformMatrix4fv;
glUniform4fv_t           glUniform4fv;
glUniform1iv_t           glUniform1iv;

// Texture functions 
glActiveTexture_t        glActiveTexture;
glTexSubImage3D_t        glTexSubImage3D;
glTexStorage3D_t         glTexStorage3D;
glTexImage3D_t           glTexImage3D;

// Draw functions
glMultiDrawArrays_t      glMultiDrawArrays;
glMultiDrawElements_t    glMultiDrawElements;
glDrawArraysInstanced_t  glDrawArraysInstanced;
glMultiDrawArraysIndirect_t glMultiDrawArraysIndirect;
glDrawArraysInstancedBaseInstance_t glDrawArraysInstancedBaseInstance;
glDrawElementsInstancedBaseInstance_t glDrawElementsInstancedBaseInstance;
glPrimitiveRestartIndex_t glPrimitiveRestartIndex;

static qbool vbo_supported = false;
static qbool shaders_supported = false;
static unsigned int vbo_number = 1;
static int modern_only = -1;

qbool GL_ShadersSupported(void)
{
	if (modern_only < 0) {
		modern_only = COM_CheckParm("-modern");
	}

	return modern_only || shaders_supported;
}

qbool GL_VBOsSupported(void)
{
	return vbo_supported;
}

#define OPENGL_LOAD_SHADER_FUNCTION(x) \
	if (shaders_supported) { \
		x = (x ## _t)SDL_GL_GetProcAddress(#x); \
		shaders_supported = (x != NULL); \
	}

/************************************* EXTENSIONS *************************************/

static void CheckMultiTextureExtensions(void)
{
	if (!COM_CheckParm("-nomtex") && SDL_GL_ExtensionSupported("GL_ARB_multitexture")) {
		if (strstr(gl_renderer, "Savage")) {
			return;
		}
		qglMultiTexCoord2f = SDL_GL_GetProcAddress("glMultiTexCoord2fARB");
		qglActiveTexture = SDL_GL_GetProcAddress("glActiveTextureARB");
		if (!qglMultiTexCoord2f || !qglActiveTexture) {
			return;
		}
		Com_Printf_State(PRINT_OK, "Multitexture extensions found\n");
		gl_mtexable = true;
	}

	glGetIntegerv(GL_MAX_TEXTURE_UNITS_ARB, (GLint *)&gl_textureunits);
	gl_textureunits = min(gl_textureunits, 4);

	if (COM_CheckParm("-maxtmu2") /*|| !strcmp(gl_vendor, "ATI Technologies Inc.")*/ || gl_maxtmu2.value) {
		gl_textureunits = min(gl_textureunits, 2);
	}

	if (gl_textureunits < 2) {
		gl_mtexable = false;
	}

	if (!gl_mtexable) {
		gl_textureunits = 1;
	}
	else {
		Com_Printf_State(PRINT_OK, "Enabled %i texture units on hardware\n", gl_textureunits);
	}
}

static void CheckShaderExtensions(void)
{
	int gl_version;

	shaders_supported = vbo_supported = false;
	glBindBufferExt = NULL;
	glBufferDataExt = NULL;
	glBufferSubDataExt = NULL;

	if (COM_CheckParm("-modern") && SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &gl_version) == 0) {
		if (gl_version >= 2) {
			glBindBufferExt = (glBindBuffer_t)SDL_GL_GetProcAddress("glBindBuffer");
			glBufferDataExt = (glBufferData_t)SDL_GL_GetProcAddress("glBufferData");
			glBufferSubDataExt = (glBufferSubData_t)SDL_GL_GetProcAddress("glBufferSubData");

			shaders_supported = true;
			OPENGL_LOAD_SHADER_FUNCTION(glCreateShader);
			OPENGL_LOAD_SHADER_FUNCTION(glShaderSource);
			OPENGL_LOAD_SHADER_FUNCTION(glCompileShader);
			OPENGL_LOAD_SHADER_FUNCTION(glDeleteShader);
			OPENGL_LOAD_SHADER_FUNCTION(glGetShaderInfoLog);
			OPENGL_LOAD_SHADER_FUNCTION(glGetShaderiv);

			OPENGL_LOAD_SHADER_FUNCTION(glGenBuffers);
			OPENGL_LOAD_SHADER_FUNCTION(glDeleteBuffers);

			OPENGL_LOAD_SHADER_FUNCTION(glGenVertexArrays);
			OPENGL_LOAD_SHADER_FUNCTION(glBindVertexArray);
			OPENGL_LOAD_SHADER_FUNCTION(glDeleteVertexArrays);
			OPENGL_LOAD_SHADER_FUNCTION(glEnableVertexAttribArray);
			OPENGL_LOAD_SHADER_FUNCTION(glVertexAttribPointer);
			OPENGL_LOAD_SHADER_FUNCTION(glVertexAttribIPointer);
			OPENGL_LOAD_SHADER_FUNCTION(glVertexAttribDivisor);

			OPENGL_LOAD_SHADER_FUNCTION(glCreateProgram);
			OPENGL_LOAD_SHADER_FUNCTION(glLinkProgram);
			OPENGL_LOAD_SHADER_FUNCTION(glDeleteProgram);
			OPENGL_LOAD_SHADER_FUNCTION(glUseProgram);
			OPENGL_LOAD_SHADER_FUNCTION(glAttachShader);
			OPENGL_LOAD_SHADER_FUNCTION(glDetachShader);
			OPENGL_LOAD_SHADER_FUNCTION(glGetProgramInfoLog);
			OPENGL_LOAD_SHADER_FUNCTION(glGetProgramiv);

			OPENGL_LOAD_SHADER_FUNCTION(glGetUniformLocation);
			OPENGL_LOAD_SHADER_FUNCTION(glUniform1f);
			OPENGL_LOAD_SHADER_FUNCTION(glUniform1fv);
			OPENGL_LOAD_SHADER_FUNCTION(glUniform2f);
			OPENGL_LOAD_SHADER_FUNCTION(glUniform3f);
			OPENGL_LOAD_SHADER_FUNCTION(glUniform3fv);
			OPENGL_LOAD_SHADER_FUNCTION(glUniform4f);
			OPENGL_LOAD_SHADER_FUNCTION(glUniform1i);
			OPENGL_LOAD_SHADER_FUNCTION(glUniform4fv);
			OPENGL_LOAD_SHADER_FUNCTION(glUniform1iv);
			OPENGL_LOAD_SHADER_FUNCTION(glUniformMatrix4fv);

			OPENGL_LOAD_SHADER_FUNCTION(glActiveTexture);
			OPENGL_LOAD_SHADER_FUNCTION(glTexSubImage3D);
			OPENGL_LOAD_SHADER_FUNCTION(glTexStorage3D);
			OPENGL_LOAD_SHADER_FUNCTION(glTexImage3D);

			OPENGL_LOAD_SHADER_FUNCTION(glMultiDrawArrays);
			OPENGL_LOAD_SHADER_FUNCTION(glMultiDrawElements);
			OPENGL_LOAD_SHADER_FUNCTION(glDrawArraysInstanced);
			OPENGL_LOAD_SHADER_FUNCTION(glMultiDrawArraysIndirect);
			OPENGL_LOAD_SHADER_FUNCTION(glDrawArraysInstancedBaseInstance);
			OPENGL_LOAD_SHADER_FUNCTION(glDrawElementsInstancedBaseInstance);
			OPENGL_LOAD_SHADER_FUNCTION(glPrimitiveRestartIndex);
		}
		else if (SDL_GL_ExtensionSupported("GL_ARB_vertex_buffer_object")) {
			glBindBufferExt = (glBindBuffer_t)SDL_GL_GetProcAddress("glBindBufferARB");
			glBufferDataExt = (glBufferData_t)SDL_GL_GetProcAddress("glBufferDataARB");
			glBufferSubDataExt = (glBufferSubData_t)SDL_GL_GetProcAddress("glBufferSubDataARB");
		}

		vbo_supported = glBindBufferExt && glBufferDataExt && glBufferSubDataExt;
	}
	vbo_number = 1;
}

void GL_CheckExtensions (void) {
	CheckMultiTextureExtensions ();
	CheckShaderExtensions();

	gl_combine = SDL_GL_ExtensionSupported("GL_ARB_texture_env_combine");
	gl_add_ext = SDL_GL_ExtensionSupported("GL_ARB_texture_env_add");

	if (SDL_GL_ExtensionSupported("GL_EXT_texture_filter_anisotropic")) {
		int gl_anisotropy_factor_max;

		anisotropy_ext = 1;

		glGetIntegerv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &gl_anisotropy_factor_max);

		Com_Printf_State(PRINT_OK, "Anisotropic Filtering Extension Found (%d max)\n",gl_anisotropy_factor_max);
	}

	if (SDL_GL_ExtensionSupported("GL_ARB_texture_compression")) {
		Com_Printf_State(PRINT_OK, "Texture compression extensions found\n");
		Cvar_SetCurrentGroup(CVAR_GROUP_TEXTURES);
		Cvar_Register (&gl_ext_texture_compression);
		Cvar_ResetCurrentGroup();
	}

	// GL_ARB_texture_non_power_of_two
	// NOTE: we always register cvar even if ext is not supported.
	// cvar added just to be able force OFF an extension.
	Cvar_SetCurrentGroup(CVAR_GROUP_TEXTURES);
	Cvar_Register (&gl_ext_arb_texture_non_power_of_two);
	Cvar_ResetCurrentGroup();

	gl_support_arb_texture_non_power_of_two =
		gl_ext_arb_texture_non_power_of_two.integer && SDL_GL_ExtensionSupported("GL_ARB_texture_non_power_of_two");
	Com_Printf_State(PRINT_OK, "GL_ARB_texture_non_power_of_two extension %s\n", 
		gl_support_arb_texture_non_power_of_two ? "found" : "not found");
}

void OnChange_gl_ext_texture_compression(cvar_t *var, char *string, qbool *cancel) {
	float newval = Q_atof(string);

	gl_alpha_format = newval ? GL_COMPRESSED_RGBA_ARB : GL_RGBA;
	gl_solid_format = newval ? GL_COMPRESSED_RGB_ARB : GL_RGB;
}

/************************************** GL INIT **************************************/

void GL_Init (void) {
	gl_vendor     = (const char*) glGetString (GL_VENDOR);
	gl_renderer   = (const char*) glGetString (GL_RENDERER);
	gl_version    = (const char*) glGetString (GL_VERSION);
	if (GL_ShadersSupported()) {
		gl_extensions = "(using modern OpenGL)\n";
	}
	else {
		gl_extensions = (const char*)glGetString(GL_EXTENSIONS);
	}

#if !defined( _WIN32 ) && !defined( __linux__ ) /* we print this in different place on WIN and Linux */
/* FIXME/TODO: FreeBSD too? */
	Com_Printf_State(PRINT_INFO, "GL_VENDOR: %s\n",   gl_vendor);
	Com_Printf_State(PRINT_INFO, "GL_RENDERER: %s\n", gl_renderer);
	Com_Printf_State(PRINT_INFO, "GL_VERSION: %s\n",  gl_version);
#endif

	if (COM_CheckParm("-gl_ext"))
		Com_Printf_State(PRINT_INFO, "GL_EXTENSIONS: %s\n", gl_extensions);

	Cvar_Register (&gl_strings);
	Cvar_ForceSet (&gl_strings, va("GL_VENDOR: %s\nGL_RENDERER: %s\n"
		"GL_VERSION: %s\nGL_EXTENSIONS: %s", gl_vendor, gl_renderer, gl_version, gl_extensions));
    Cvar_Register (&gl_maxtmu2);
#ifndef __APPLE__
	glClearColor (1,0,0,0);
#else
	glClearColor (0.2,0.2,0.2,1.0);
#endif

	GL_CullFace(GL_FRONT);
	glEnable(GL_TEXTURE_2D);

	GL_AlphaBlendFlags(GL_ALPHATEST_ENABLED);
	GL_AlphaFunc(GL_GREATER, 0.666);

	glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
	GL_ProcessErrors("PreInit");
	GL_ShadeModel(GL_FLAT);
	GL_ProcessErrors("PreInit2");

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	GL_ProcessErrors("PreInit3");

	GL_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GL_ProcessErrors("PreInit4");

	GL_TextureEnvMode(GL_REPLACE);
	GL_ProcessErrors("PreInit5");

	GL_CheckExtensions();
	GL_ProcessErrors("PostInit");
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

#define GLM_Enabled GL_ShadersSupported
void GLM_OrthographicProjection(float left, float right, float top, float bottom, float zNear, float zFar);

static GLfloat projectionMatrix[16];
static GLfloat modelMatrix[16];
//static GLfloat viewMatrix[16];
static GLfloat identityMatrix[16] = {
	1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 1, 0,
	0, 0, 0, 1
};

static const GLfloat* GL_OrthoMatrix(float left, float right, float top, float bottom, float zNear, float zFar);

void GLM_SetMatrix(float* target, const float* source)
{
	memcpy(target, source, sizeof(float) * 16);
}

float* GLM_ModelviewMatrix(void)
{
	return modelMatrix;
}

float* GLM_ProjectionMatrix(void)
{
	return projectionMatrix;
}

void GL_OrthographicProjection(float left, float right, float top, float bottom, float zNear, float zFar)
{
	if (GLM_Enabled()) {
		GLM_OrthographicProjection(left, right, top, bottom, zNear, zFar);
	}
	else {
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(left, right, top, bottom, zNear, zFar);
	}
}

void GLM_SetIdentityMatrix(float* matrix)
{
	GLM_SetMatrix(matrix, identityMatrix);
}

void GLM_Rotate(float* matrix, float angle, float x, float y, float z)
{
	vec3_t vec = { x, y, z };
	double s = sin(angle * M_PI / 180);
	double c = cos(angle * M_PI / 180);
	float rotation[16];
	float result[16];

	VectorNormalize(vec);

	// Taken from https://www.khronos.org/registry/OpenGL-Refpages/gl2.1/xhtml/glRotate.xml
	/*rotation[0] = x * x * (1 - c) + c;
	rotation[1] = x * y * (1 - c) - z * s;
	rotation[2] = x * z * (1 - c) + y * s;
	rotation[3] = 0;
	rotation[4] = y * x * (1 - c) + z * s;
	rotation[5] = y * y * (1 - c) + c;
	rotation[6] = y * z * (1 - c) - x * s;
	rotation[7] = 0;
	rotation[8] = x * z * (1 - c) - y * s;
	rotation[9] = y * z * (1 - c) + x * s;
	rotation[10] = z * z * (1 - c) + c;
	rotation[11] = 0;
	rotation[12] = rotation[13] = rotation[14] = 0;
	rotation[15] = 1;*/
	rotation[0] = x * x * (1 - c) + c;
	rotation[4] = x * y * (1 - c) - z * s;
	rotation[8] = x * z * (1 - c) + y * s;
	rotation[12] = 0;
	rotation[1] = y * x * (1 - c) + z * s;
	rotation[5] = y * y * (1 - c) + c;
	rotation[9] = y * z * (1 - c) - x * s;
	rotation[13] = 0;
	rotation[2] = x * z * (1 - c) - y * s;
	rotation[6] = y * z * (1 - c) + x * s;
	rotation[10] = z * z * (1 - c) + c;
	rotation[14] = 0;
	rotation[3] = rotation[7] = rotation[11] = 0;
	rotation[15] = 1;

	GLM_MultiplyMatrix(rotation, matrix, result);
	GLM_SetMatrix(matrix, result);
}

void GLM_TransformMatrix(float* matrix, float x, float y, float z)
{
	matrix[12] += matrix[0] * x + matrix[4] * y + matrix[8] * z;
	matrix[13] += matrix[1] * x + matrix[5] * y + matrix[9] * z;
	matrix[14] += matrix[2] * x + matrix[6] * y + matrix[10] * z;
	matrix[15] += matrix[3] * x + matrix[7] * y + matrix[11] * z;
}

void GLM_ScaleMatrix(float* matrix, float x_scale, float y_scale, float z_scale)
{
	matrix[0] *= x_scale;
	matrix[1] *= x_scale;
	matrix[2] *= x_scale;
	matrix[3] *= x_scale;
	matrix[4] *= y_scale;
	matrix[5] *= y_scale;
	matrix[6] *= y_scale;
	matrix[7] *= y_scale;
	matrix[8] *= z_scale;
	matrix[9] *= z_scale;
	matrix[10] *= z_scale;
	matrix[11] *= z_scale;
}

void GLM_MultiplyMatrix(const float* lhs, const float* rhs, float* target)
{
	target[0] = lhs[0] * rhs[0] + lhs[1] * rhs[4] + lhs[2] * rhs[8] + lhs[3] * rhs[12];
	target[1] = lhs[0] * rhs[1] + lhs[1] * rhs[5] + lhs[2] * rhs[9] + lhs[3] * rhs[13];
	target[2] = lhs[0] * rhs[2] + lhs[1] * rhs[6] + lhs[2] * rhs[10] + lhs[3] * rhs[14];
	target[3] = lhs[0] * rhs[3] + lhs[1] * rhs[7] + lhs[2] * rhs[11] + lhs[3] * rhs[15];

	target[4] = lhs[4] * rhs[0] + lhs[5] * rhs[4] + lhs[6] * rhs[8] + lhs[7] * rhs[12];
	target[5] = lhs[4] * rhs[1] + lhs[5] * rhs[5] + lhs[6] * rhs[9] + lhs[7] * rhs[13];
	target[6] = lhs[4] * rhs[2] + lhs[5] * rhs[6] + lhs[6] * rhs[10] + lhs[7] * rhs[14];
	target[7] = lhs[4] * rhs[3] + lhs[5] * rhs[7] + lhs[6] * rhs[11] + lhs[7] * rhs[15];

	target[8] = lhs[8] * rhs[0] + lhs[9] * rhs[4] + lhs[10] * rhs[8] + lhs[11] * rhs[12];
	target[9] = lhs[8] * rhs[1] + lhs[9] * rhs[5] + lhs[10] * rhs[9] + lhs[11] * rhs[13];
	target[10] = lhs[8] * rhs[2] + lhs[9] * rhs[6] + lhs[10] * rhs[10] + lhs[11] * rhs[14];
	target[11] = lhs[8] * rhs[3] + lhs[9] * rhs[7] + lhs[10] * rhs[11] + lhs[11] * rhs[15];

	target[12] = lhs[12] * rhs[0] + lhs[13] * rhs[4] + lhs[14] * rhs[8] + lhs[15] * rhs[12];
	target[13] = lhs[12] * rhs[1] + lhs[13] * rhs[5] + lhs[14] * rhs[9] + lhs[15] * rhs[13];
	target[14] = lhs[12] * rhs[2] + lhs[13] * rhs[6] + lhs[14] * rhs[10] + lhs[15] * rhs[14];
	target[15] = lhs[12] * rhs[3] + lhs[13] * rhs[7] + lhs[14] * rhs[11] + lhs[15] * rhs[15];
}

void GL_IdentityModelView(void)
{
	if (GLM_Enabled()) {
		GLM_SetIdentityMatrix(GLM_ModelviewMatrix());
	}
	else {
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
	}
}

void GL_GetMatrix(GLenum mode, GLfloat* matrix)
{
	if (GLM_Enabled()) {
		GLM_GetMatrix(mode, matrix);
	}
	else {
		glGetFloatv(mode, matrix);
	}
}

void GL_GetViewport(GLint* view)
{
	if (GLM_Enabled()) {
		glGetIntegerv(GL_VIEWPORT, (GLint *)view);
	}
	else {
		glGetIntegerv(GL_VIEWPORT, (GLint *)view);
	}
}

static GLenum lastTextureMode = GL_MODULATE;

void GL_TextureEnvMode(GLenum mode)
{
	if (GL_ShadersSupported()) {
		// Just store for now
		lastTextureMode = mode;
	}
	else if (mode != lastTextureMode) {
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, mode);
		lastTextureMode = mode;
	}
}

static int old_alphablend_flags = 0;

int GL_AlphaBlendFlags(int flags)
{
	if (!GL_ShadersSupported()) {
		if ((flags & GL_ALPHATEST_ENABLED) && !(old_alphablend_flags & GL_ALPHATEST_ENABLED)) {
			glEnable(GL_ALPHA_TEST);
		}
		else if ((flags & GL_ALPHATEST_DISABLED) && (old_alphablend_flags & GL_ALPHATEST_ENABLED)) {
			glDisable(GL_ALPHA_TEST);
		}
		else if (!(flags & (GL_ALPHATEST_ENABLED | GL_ALPHATEST_DISABLED))) {
			flags |= (old_alphablend_flags & GL_ALPHATEST_ENABLED);
		}
	}

	if ((flags & GL_BLEND_ENABLED) && !(old_alphablend_flags & GL_BLEND_ENABLED)) {
		glEnable(GL_BLEND);
	}
	else if (flags & GL_BLEND_DISABLED && (old_alphablend_flags & GL_BLEND_ENABLED)) {
		glDisable(GL_BLEND);
	}
	else if (!(flags & (GL_BLEND_ENABLED | GL_BLEND_DISABLED))) {
		flags |= (old_alphablend_flags & GL_BLEND_ENABLED);
	}

	old_alphablend_flags = flags;
	return old_alphablend_flags;
}

void GLM_OrthographicProjection(float left, float right, float top, float bottom, float zNear, float zFar)
{
	// Deliberately inverting top & bottom here...
	GLM_SetMatrix(projectionMatrix, GL_OrthoMatrix(left, right, bottom, top, zNear, zFar));
}

static float* GL_MatrixForMode(GLenum type)
{
	static float junk[16] = { 0 };

	if (type == GL_PROJECTION || type == GL_PROJECTION_MATRIX) {
		return projectionMatrix;
	}
	else if (type == GL_MODELVIEW || type == GL_MODELVIEW_MATRIX) {
		return modelMatrix;
	}
	else {
		return junk;
	}
}

void GLM_GetMatrix(GLenum type, float* matrix)
{
	GLM_SetMatrix(matrix, GL_MatrixForMode(type));
}

#undef glColor3f
#undef glColor4f
#undef glColor3fv
#undef glColor3ubv
#undef glColor4ubv
#undef glColor4ub

#undef glDisable
#undef glEnable

void GL_Color3f(float r, float g, float b)
{
	if (GL_ShadersSupported()) {
		// TODO
	}
	else {
		glColor3f(r, g, b);
	}
}

void GL_Color4f(float r, float g, float b, float a)
{
	if (GL_ShadersSupported()) {
		// TODO
	}
	else {
		glColor4f(r, g, b, a);
	}
}

void GL_Color3fv(const float* rgbVec)
{
	if (GL_ShadersSupported()) {
		// TODO
	}
	else {
		glColor3fv(rgbVec);
	}
}

void GL_Color3ubv(const GLubyte* rgbVec)
{
	if (GL_ShadersSupported()) {
		// TODO
	}
	else {
		glColor3ubv(rgbVec);
	}

}

void GL_Color4ubv(const GLubyte* rgbaVec)
{
	if (GL_ShadersSupported()) {
		// TODO
	}
	else {
		glColor4ubv(rgbaVec);
	}

}

void GL_Color4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a)
{
	if (GL_ShadersSupported()) {
		// TODO
	}
	else {
		glColor4ub(r, g, b, a);
	}
}

void GL_Rotate(GLenum matrix, float angle, float x, float y, float z)
{
	if (GL_ShadersSupported()) {
		GLM_Rotate(GL_MatrixForMode(matrix), angle, x, y, z);
	}
	else {
//		glMatrixMode(matrix);
		glRotatef(angle, x, y, z);
	}
}

void GL_Translate(GLenum matrix, float x, float y, float z)
{
	if (GL_ShadersSupported()) {
		GLM_TransformMatrix(GL_MatrixForMode(matrix), x, y, z);
	}
	else {
		glTranslatef(x, y, z);
	}
}

void GL_IdentityProjectionView(void)
{
	if (GL_ShadersSupported()) {
		GLM_SetIdentityMatrix(GLM_ProjectionMatrix());
	}
	else {
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
	}
}

// 
static const GLfloat* GL_OrthoMatrix(float left, float right, float top, float bottom, float zNear, float zFar)
{
	static GLfloat matrix[16];

	memset(matrix, 0, sizeof(matrix));
	matrix[0] = 2 / (right - left);
	matrix[5] = 2 / (top - bottom);
	matrix[10] = -2 / (zFar - zNear);
	matrix[12] = -(right + left) / (right - left);
	matrix[13] = -(top + bottom) / (top - bottom);
	matrix[14] = -(zFar + zNear) / (zFar - zNear);
	matrix[15] = 1;

	return matrix;
}

void GL_ProcessErrors(const char* message)
{
	GLenum error = glGetError();
	while (error != GL_NO_ERROR) {
		Con_Printf("%s> = %X\n", message, error);
		error = glGetError();
	}
}

void GL_PushMatrix(GLenum mode, float* matrix)
{
	if (GL_ShadersSupported()) {
		memcpy(matrix, GL_MatrixForMode(mode), sizeof(float) * 16);
	}
	else {
		glMatrixMode(mode);
		glPushMatrix();
	}
}

void GL_PopMatrix(GLenum mode, float* matrix)
{
	if (GL_ShadersSupported()) {
		memcpy(GL_MatrixForMode(mode), matrix, sizeof(float) * 16);
	}
	else {
		glMatrixMode(mode);
		glPopMatrix();
	}
}

void GL_Scale(GLenum matrix, float xScale, float yScale, float zScale)
{
	if (GL_ShadersSupported()) {
		GLM_ScaleMatrix(GL_MatrixForMode(matrix), xScale, yScale, zScale);
	}
	else {
		glScalef(xScale, yScale, zScale);
	}
}

void GL_Frustum(double left, double right, double bottom, double top, double zNear, double zFar)
{
	if (GL_ShadersSupported()) {
		float perspective[16] = { 0 };
		float projection[16];
		float new_projection[16];

		perspective[0] = (2 * zNear) / (right - left);
		perspective[8] = (right + left) / (right - left);
		perspective[5] = (2 * zNear) / (top - bottom);
		perspective[9] = (top + bottom) / (top - bottom);
		perspective[10] = -(zFar + zNear) / (zFar - zNear);
		perspective[11] = -1;
		perspective[14] = -2 * (zFar * zNear) / (zFar - zNear);

		GLM_GetMatrix(GL_PROJECTION, projection);
		GLM_MultiplyMatrix(perspective, projection, new_projection);
		GLM_SetMatrix(GL_MatrixForMode(GL_PROJECTION), new_projection);
	}
	else {
		glFrustum(left, right, bottom, top, zNear, zFar);
	}
}

void GLM_DebugMatrix(GLenum mode, const char* label)
{
	float matrix[16];
	int i;

	GL_GetMatrix(mode, matrix);

	Con_Printf("%s\n", label);
	for (i = 0; i < 4; ++i) {
		Con_Printf("  [%5.3f %5.3f %5.3f %5.3f]\n", matrix[i], matrix[i + 4], matrix[i + 8], matrix[i + 12]);
	}
}

void GL_EnableFog(void)
{
	if (!GLM_Enabled() && gl_fogenable.value) {
		glEnable(GL_FOG);
	}
}

void GL_DisableFog(void)
{
	if (!GLM_Enabled() && gl_fogenable.value) {
		glDisable(GL_FOG);
	}
}

void GL_ConfigureFog(void)
{
	vec3_t colors;

	if (GLM_Enabled()) {
		// TODO
		return;
	}

	// START shaman BUG fog was out of control when fogstart>fogend {
	if (gl_fogenable.value && gl_fogstart.value >= 0 && gl_fogstart.value < gl_fogend.value) {
		// } END shaman BUG fog was out of control when fogstart>fogend
		glFogi(GL_FOG_MODE, GL_LINEAR);
		colors[0] = gl_fogred.value;
		colors[1] = gl_foggreen.value;
		colors[2] = gl_fogblue.value;
		glFogfv(GL_FOG_COLOR, colors);
		glFogf(GL_FOG_START, gl_fogstart.value);
		glFogf(GL_FOG_END, gl_fogend.value);
		glEnable(GL_FOG);
	}
	else {
		glDisable(GL_FOG);
	}
}

void GL_EnableWaterFog(int contents)
{
	extern cvar_t gl_waterfog_color_water;
	extern cvar_t gl_waterfog_color_lava;
	extern cvar_t gl_waterfog_color_slime;

	float colors[4];

	// TODO
	if (!GL_ShadersSupported()) {
		return;
	}

	switch (contents) {
	case CONTENTS_LAVA:
		colors[0] = (float) gl_waterfog_color_lava.color[0] / 255.0;
		colors[1] = (float) gl_waterfog_color_lava.color[1] / 255.0;
		colors[2] = (float) gl_waterfog_color_lava.color[2] / 255.0;
		colors[3] = (float) gl_waterfog_color_lava.color[3] / 255.0;
		break;
	case CONTENTS_SLIME:
		colors[0] = (float) gl_waterfog_color_slime.color[0] / 255.0;
		colors[1] = (float) gl_waterfog_color_slime.color[1] / 255.0;
		colors[2] = (float) gl_waterfog_color_slime.color[2] / 255.0;
		colors[3] = (float) gl_waterfog_color_slime.color[3] / 255.0;
		break;
	default:
		colors[0] = (float) gl_waterfog_color_water.color[0] / 255.0;
		colors[1] = (float) gl_waterfog_color_water.color[1] / 255.0;
		colors[2] = (float) gl_waterfog_color_water.color[2] / 255.0;
		colors[3] = (float) gl_waterfog_color_water.color[3] / 255.0;
		break;
	}

	glFogfv(GL_FOG_COLOR, colors);
	if (((int)gl_waterfog.value) == 2) {
		glFogf(GL_FOG_DENSITY, 0.0002 + (0.0009 - 0.0002) * bound(0, gl_waterfog_density.value, 1));
		glFogi(GL_FOG_MODE, GL_EXP);
	}
	else {
		glFogi(GL_FOG_MODE, GL_LINEAR);
		glFogf(GL_FOG_START, 150.0f);
		glFogf(GL_FOG_END, 4250.0f - (4250.0f - 1536.0f) * bound(0, gl_waterfog_density.value, 1));
	}
	glEnable(GL_FOG);
}

void GLM_MultiplyMatrixVector(float* matrix, vec3_t vector, float* result)
{
	result[0] = matrix[0] * vector[0] + matrix[4] * vector[1] + matrix[8] * vector[2] + matrix[12] * vector[3];
	result[1] = matrix[1] * vector[0] + matrix[5] * vector[1] + matrix[9] * vector[2] + matrix[13] * vector[3];
	result[2] = matrix[2] * vector[0] + matrix[6] * vector[1] + matrix[10] * vector[2] + matrix[14] * vector[3];
	result[3] = matrix[3] * vector[0] + matrix[7] * vector[1] + matrix[11] * vector[2] + matrix[15] * vector[3];
}

#ifdef WITH_NVTX
void GL_EnterRegion(const char* regionName)
{
	nvtxRangePushA(regionName);
}

void GL_LeaveRegion(void)
{
	nvtxRangePop();
}
#endif

// Linked list of all vbo buffers
static glm_vbo_t* vbo_list = NULL;

// Linked list of all vao buffers
static glm_vao_t* vao_list = NULL;

void GL_GenBuffer(glm_vbo_t* vbo, const char* name)
{
	if (vbo->vbo) {
		glDeleteBuffers(1, &vbo->vbo);
	}
	else {
		vbo->next = vbo_list;
		vbo_list = vbo;
	}
	vbo->name = name;
	glGenBuffers(1, &vbo->vbo);
}

void GL_GenVertexArray(glm_vao_t* vao)
{
	if (vao->vao) {
		glDeleteVertexArrays(1, &vao->vao);
	}
	else {
		vao->next = vao_list;
		vao_list = vao;
	}
	glGenVertexArrays(1, &vao->vao);
}

void GL_DeleteBuffers(void)
{
	glm_vao_t* vao = vao_list;
	glm_vbo_t* vbo = vbo_list;

	if (glBindVertexArray) {
		glBindVertexArray(0);
	}
	while (vao) {
		glm_vao_t* prev = vao;

		if (vao->vao) {
			if (glDeleteVertexArrays) {
				glDeleteVertexArrays(1, &vao->vao);
			}
			vao->vao = 0;
		}

		vao = vao->next;
		prev->next = NULL;
	}

	while (vbo) {
		glm_vbo_t* prev = vbo;

		if (vbo->vbo) {
			if (glDeleteBuffers) {
				glDeleteBuffers(1, &vbo->vbo);
			}
			vbo->vbo = 0;
		}

		vbo = vbo->next;
		prev->next = NULL;
	}

	vbo_list = NULL;
	vao_list = NULL;
}

void GL_AlphaFunc(GLenum func, GLclampf threshold)
{
	if (!GL_ShadersSupported()) {
		glAlphaFunc(func, threshold);
	}
}
