/*
Copyright (C) 2018 ezQuake team

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

// GLM_FrameBuffer.c
// Framebuffer support in OpenGL core

// For the moment, also contains general framebuffer code as immediate-mode isn't supported

#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "gl_framebuffer.h"
#include "tr_types.h"
#include "glm_vao.h"
#include "r_buffers.h"
#include "glm_local.h"
#include "r_state.h"
#include "r_program.h"
#include "r_renderer.h"

static buffer_ref post_process_vbo;

#define POST_PROCESS_PALETTE    1
#define POST_PROCESS_3DONLY     2
#define POST_PROCESS_TONEMAP    4

qbool GLM_CompilePostProcessVAO(void)
{
	if (!R_BufferReferenceIsValid(post_process_vbo)) {
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

		post_process_vbo = buffers.Create(buffertype_vertex, "post-process-screen", sizeof(verts), verts, bufferusage_constant_data);
	}

	if (R_BufferReferenceIsValid(post_process_vbo) && !R_VertexArrayCreated(vao_postprocess)) {
		R_GenVertexArray(vao_postprocess);

		GLM_ConfigureVertexAttribPointer(vao_postprocess, post_process_vbo, 0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 5, (void*)0, 0);
		GLM_ConfigureVertexAttribPointer(vao_postprocess, post_process_vbo, 1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 5, (void*)(sizeof(float) * 3), 0);

		R_BindVertexArray(vao_none);
	}

	return R_VertexArrayCreated(vao_postprocess);
}

// If this returns false then the framebuffer will be blitted instead
static qbool GLM_CompilePostProcessProgram(void)
{
	extern cvar_t vid_framebuffer_palette, vid_framebuffer, vid_framebuffer_hdr, vid_framebuffer_hdr_tonemap;
	int post_process_flags =
		(vid_framebuffer_palette.integer ? POST_PROCESS_PALETTE : 0) |
		(vid_framebuffer.integer == USE_FRAMEBUFFER_3DONLY ? POST_PROCESS_3DONLY : 0) |
		(vid_framebuffer_hdr.integer && vid_framebuffer_hdr_tonemap.integer ? POST_PROCESS_TONEMAP : 0);

	if (R_ProgramRecompileNeeded(r_program_post_process, post_process_flags)) {
		static char included_definitions[512];

		memset(included_definitions, 0, sizeof(included_definitions));
		if (post_process_flags & POST_PROCESS_PALETTE) {
			strlcat(included_definitions, "#define EZ_POSTPROCESS_PALETTE\n", sizeof(included_definitions));
		}
		if (post_process_flags & POST_PROCESS_3DONLY) {
			strlcat(included_definitions, "#define EZ_POSTPROCESS_OVERLAY\n", sizeof(included_definitions));
		}
		if (post_process_flags & POST_PROCESS_TONEMAP) {
			strlcat(included_definitions, "#define EZ_POSTPROCESS_TONEMAP\n", sizeof(included_definitions));
		}

		// Initialise program for drawing image
		R_ProgramCompileWithInclude(r_program_post_process, included_definitions);

		R_ProgramSetCustomOptions(r_program_post_process, post_process_flags);
	}

	return R_ProgramReady(r_program_post_process) && GLM_CompilePostProcessVAO();
}

void GLM_RenderFramebuffers(void)
{
	qbool flip2d = GL_FramebufferEnabled2D();
	qbool flip3d = GL_FramebufferEnabled3D();

	if (GLM_CompilePostProcessProgram()) {
		R_ProgramUse(r_program_post_process);
		R_BindVertexArray(vao_postprocess);

		if (flip2d && flip3d) {
			renderer.TextureUnitBind(0, GL_FramebufferTextureReference(framebuffer_std, fbtex_standard));
			renderer.TextureUnitBind(1, GL_FramebufferTextureReference(framebuffer_hud, fbtex_standard));
		}
		else if (flip3d) {
			renderer.TextureUnitBind(0, GL_FramebufferTextureReference(framebuffer_std, fbtex_standard));
		}
		else if (flip2d) {
			renderer.TextureUnitBind(0, GL_FramebufferTextureReference(framebuffer_hud, fbtex_standard));
		}
		GL_DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}
}
