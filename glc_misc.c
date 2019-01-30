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
#include "glc_local.h"
#include "r_matrix.h"
#include "r_state.h"
#include "r_texture.h"
#include "r_renderer.h"
#include "gl_texture.h"
#include "r_program.h"

// motion blur.
void GLC_PolyBlend(float v_blend[4])
{
	R_ProgramUse(r_program_none);
	GLC_StateBeginPolyBlend(v_blend);

	GLC_Begin(GL_QUADS);
	GLC_Vertex2f(r_refdef.vrect.x, r_refdef.vrect.y);
	GLC_Vertex2f(r_refdef.vrect.x + r_refdef.vrect.width, r_refdef.vrect.y);
	GLC_Vertex2f(r_refdef.vrect.x + r_refdef.vrect.width, r_refdef.vrect.y + r_refdef.vrect.height);
	GLC_Vertex2f(r_refdef.vrect.x, r_refdef.vrect.y + r_refdef.vrect.height);
	GLC_End();
}

void GLC_BrightenScreen(void)
{
	extern float vid_gamma;
	float f;

	if (vid_hwgamma_enabled) {
		return;
	}
	if (v_contrast.value <= 1.0) {
		return;
	}

	f = min(v_contrast.value, 3);
	f = pow(f, vid_gamma);

	R_ProgramUse(r_program_none);
	GLC_StateBeginBrightenScreen();

	GLC_Begin(GL_QUADS);
	while (f > 1) {
		if (f >= 2) {
			R_CustomColor(1, 1, 1, 1);
		}
		else {
			R_CustomColor(f - 1, f - 1, f - 1, 1);
		}

		GLC_Vertex2f(0, 0);
		GLC_Vertex2f(vid.width, 0);
		GLC_Vertex2f(vid.width, vid.height);
		GLC_Vertex2f(0, vid.height);

		f *= 0.5;
	}
	GLC_End();
}

void GLC_PreRenderView(void)
{
	// TODO
}

void GLC_SetupGL(void)
{
}

void GLC_Shutdown(qbool restarting)
{
	GLC_FreeAliasPoseBuffer();
	renderer.ProgramsShutdown(restarting);
	GL_DeleteSamplers();
}

void GLC_TextureInitialiseState(void)
{
	GL_TextureInitialiseState();
}
