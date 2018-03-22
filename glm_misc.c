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

#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "gl_framebuffer.h"
#include "tr_types.h"

static framebuffer_ref framebuffer;
static glm_program_t post_process_program;
static buffer_ref post_process_vbo;
static glm_vao_t post_process_vao;

static qbool GLM_CompilePostProcessProgram(void);

// TODO: !?  Called as first step in 2D.  Include in frame-buffer at end of 3D scene rendering?
void GLM_PolyBlend(float v_blend[4])
{
	color_t v_blend_color = RGBA_TO_COLOR(
		bound(0, v_blend[0], 1) * 255,
		bound(0, v_blend[1], 1) * 255,
		bound(0, v_blend[2], 1) * 255,
		bound(0, v_blend[3], 1) * 255
	);

	Draw_AlphaRectangleRGB(r_refdef.vrect.x, r_refdef.vrect.y, r_refdef.vrect.width, r_refdef.vrect.height, 0.0f, true, v_blend_color);
}

void GLM_DrawVelocity3D(void)
{
	// MEAG: TODO
}

void GLM_RenderSceneBlurDo(float alpha)
{
	// MEAG: TODO
}

static buffer_ref ubo_refdef;
static buffer_ref ubo_common2d;
static uniform_block_refdef_t refdef;
static uniform_block_common2d_t common2d;

void GLM_PreRenderView(void)
{
	extern cvar_t gl_alphafont;

	common2d.gamma2d = v_gamma.value;
	common2d.r_alphafont = gl_alphafont.value;

	if (!GL_BufferReferenceIsValid(ubo_common2d)) {
		ubo_common2d = GL_GenUniformBuffer("common2d", &common2d, sizeof(common2d));
		GL_BindBufferBase(ubo_common2d, GL_BINDINGPOINT_COMMON2D_CVARS);
	}

	GL_UpdateVBO(ubo_common2d, sizeof(common2d), &common2d);
}

void GLM_SetupGL(void)
{
	extern cvar_t gl_textureless;

	GLM_GetMatrix(GL_MODELVIEW, refdef.modelViewMatrix);
	GLM_GetMatrix(GL_PROJECTION, refdef.projectionMatrix);
	VectorCopy(r_refdef.vieworg, refdef.position);
	refdef.time = cl.time;
	refdef.gamma3d = v_gamma.value;

	refdef.r_textureless = gl_textureless.integer || gl_max_size.integer == 1;

	if (!GL_BufferReferenceIsValid(ubo_refdef)) {
		ubo_refdef = GL_GenUniformBuffer("refdef", &refdef, sizeof(refdef));
		GL_BindBufferBase(ubo_refdef, GL_BINDINGPOINT_REFDEF_CVARS);
	}

	GL_UpdateVBO(ubo_refdef, sizeof(refdef), &refdef);
}

void GLM_ScreenDrawStart(void)
{
	extern cvar_t gl_postprocess_gamma;

	if (gl_postprocess_gamma.integer) {
		if (!GL_FramebufferReferenceIsValid(framebuffer)) {
			framebuffer = GL_FramebufferCreate(glConfig.vidWidth, glConfig.vidHeight, true);
		}

		if (GL_FramebufferReferenceIsValid(framebuffer)) {
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			GL_FramebufferStartUsing(framebuffer);
		}
	}
	else {
		if (GL_FramebufferReferenceIsValid(framebuffer)) {
			GL_FramebufferDelete(&framebuffer);
		}
	}
}

void GLM_PostProcessScene(void)
{
}

void GLM_PostProcessScreen(void)
{
	GLM_FlushImageDraw();

	if (GL_FramebufferReferenceIsValid(framebuffer)) {
		GL_FramebufferStopUsing(framebuffer);

		if (GLM_CompilePostProcessProgram()) {
			GL_UseProgram(post_process_program.program);
			GL_BindVertexArray(&post_process_vao);

			GL_BindTextureUnit(GL_TEXTURE0, GL_FramebufferTextureReference(framebuffer, 0));
			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		}
	}
}

#define POSTPROCESS_GAMMA 1

static qbool GLM_CompilePostProcessProgram(void)
{
	int options = (v_gamma.value != 1 ? POSTPROCESS_GAMMA : 0);

	if (GLM_ProgramRecompileNeeded(&post_process_program, options)) {
		static char included_definitions[512];
		GL_VFDeclare(post_process_screen);

		memset(included_definitions, 0, sizeof(included_definitions));
		if (options & POSTPROCESS_GAMMA) {
			strlcat(included_definitions, "#define POSTPROCESS_GAMMA\n", sizeof(included_definitions));
		}

		// Initialise program for drawing image
		GLM_CreateVFProgramWithInclude("post-process-screen", GL_VFParams(post_process_screen), &post_process_program, included_definitions);

		post_process_program.custom_options = options;
	}

	if (post_process_program.program && !post_process_program.uniforms_found) {
		GLint common2d_block = glGetUniformBlockIndex(post_process_program.program, "Common2d");

		glUniformBlockBinding(post_process_program.program, common2d_block, GL_BINDINGPOINT_COMMON2D_CVARS);

		post_process_program.uniforms_found = true;
	}

	if (!GL_BufferReferenceIsValid(post_process_vbo)) {
		float verts[4][5] = { { 0 } };

		VectorSet(verts[0], -1, -1, 0);
		verts[0][3] = 0;
		verts[0][4] = 0;

		VectorSet(verts[1], -1, 1, 0);
		verts[1][3] = 0;
		verts[1][4] = 1;

		VectorSet(verts[2], 1, -1, 0);
		verts[2][3] = 1;
		verts[2][4] = 0;

		VectorSet(verts[3], 1, 1, 0);
		verts[3][3] = 1;
		verts[3][4] = 1;

		post_process_vbo = GL_GenFixedBuffer(GL_ARRAY_BUFFER, "post-process-screen", sizeof(verts), verts, GL_STATIC_DRAW);
	}

	if (GL_BufferReferenceIsValid(post_process_vbo) && !post_process_vao.vao) {
		GL_GenVertexArray(&post_process_vao);

		GL_ConfigureVertexAttribPointer(&post_process_vao, post_process_vbo, 0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 5, (void*) 0);
		GL_ConfigureVertexAttribPointer(&post_process_vao, post_process_vbo, 1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 5, (void*) (sizeof(float) * 3));
	}

	return post_process_program.program && post_process_vao.vao;
}
