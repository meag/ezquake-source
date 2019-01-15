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
// glc_turb_surface.c: surface-related refresh code

#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "glc_local.h"
#include "rulesets.h"
#include "utils.h"
#include "r_brushmodel.h"
#include "r_renderer.h"
#include "tr_types.h"
#include "r_program.h"

extern msurface_t* waterchain;
void GLC_EmitWaterPoly(msurface_t* fa);

static qbool GLC_TurbSurfaceProgramCompile(void)
{
	if (R_ProgramRecompileNeeded(r_program_turb_glc, 0)) {
		R_ProgramCompile(r_program_turb_glc);
		R_ProgramUniform1i(r_program_uniform_turb_glc_texSampler, 0);
		R_ProgramSetCustomOptions(r_program_turb_glc, 0);
	}

	return R_ProgramReady(r_program_turb_glc);
}

static void GLC_DrawWaterSurfaces_Program(void)
{
	qbool use_vbo = buffers.supported && modelIndexes;
	msurface_t* fa;
	texture_ref prev_tex = null_texture_reference;
	int index_count = 0;

	R_ProgramUse(r_program_turb_glc);
	R_ProgramUniform1f(r_program_uniform_turb_glc_time, cl.time);

	for (fa = waterchain; fa; fa = fa->texturechain) {
		glpoly_t *p;

		if (!R_TextureReferenceEqual(fa->texinfo->texture->gl_texturenum, prev_tex)) {
			prev_tex = fa->texinfo->texture->gl_texturenum;

			if (index_count) {
				GL_DrawElements(GL_TRIANGLE_STRIP, index_count, GL_UNSIGNED_INT, modelIndexes);
				index_count = 0;
			}
			renderer.TextureUnitBind(0, prev_tex);
		}

		if (use_vbo) {
			for (p = fa->polys; p; p = p->next) {
				index_count = GLC_DrawIndexedPoly(p, modelIndexes, modelIndexMaximum, index_count);
			}
		}
		else {
			GLC_EmitWaterPoly(fa);
		}
	}

	if (index_count) {
		GL_DrawElements(GL_TRIANGLE_STRIP, index_count, GL_UNSIGNED_INT, modelIndexes);
	}
	R_ProgramUse(r_program_none);
}

static void GLC_DrawWaterSurfaces_Immediate(void)
{
	msurface_t *s;

	for (s = waterchain; s; s = s->texturechain) {
		renderer.TextureUnitBind(0, s->texinfo->texture->gl_texturenum);

		GLC_EmitWaterPoly(s);
	}
}

void GLC_DrawWaterSurfaces(void)
{
	extern cvar_t gl_program_turbsurfaces;

	if (!waterchain) {
		return;
	}

	R_TraceEnterRegion(__FUNCTION__, true);
	GLC_StateBeginWaterSurfaces();

	if (gl_program_turbsurfaces.integer && GL_Supported(R_SUPPORT_RENDERING_SHADERS) && GLC_TurbSurfaceProgramCompile()) {
		GLC_DrawWaterSurfaces_Program();
	}
	else {
		GLC_DrawWaterSurfaces_Immediate();
	}

	R_TraceLeaveRegion(true);

	waterchain = NULL;
}

