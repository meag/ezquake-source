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

#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "r_matrix.h"
#include "r_draw.h"
#include "gl_texture_internal.h"

static qbool glc_pause_updates;

void GLC_RotateModelview(float angle, float x, float y, float z)
{
	if (!glc_pause_updates) {
		glMatrixMode(GL_MODELVIEW);
		glRotatef(angle, x, y, z);
		R_TraceLogAPICall("GL_RotateModelview()");
	}
}

void GLC_IdentityModelview(void)
{
	if (!glc_pause_updates) {
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		R_TraceLogAPICall("GL_IdentityModelView(modelview)");
	}
}

void GLC_OrthographicProjection(float left, float right, float top, float bottom, float zNear, float zFar)
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(left, right, top, bottom, zNear, zFar);
}

void GLC_IdentityProjectionView(void)
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	R_TraceLogAPICall("GL_Identity(projection)");
}

void GLC_PopModelviewMatrix(const float* matrix)
{
	glMatrixMode(GL_MODELVIEW);
	glLoadMatrixf(matrix);
	R_TraceLogAPICall("R_PopModelviewMatrix()");
}

void GLC_PopProjectionMatrix(const float* matrix)
{
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf(matrix);
	R_TraceLogAPICall("R_PopProjectionMatrix()");
}

void GLC_ScaleModelview(float xScale, float yScale, float zScale)
{
	if (!glc_pause_updates) {
		glMatrixMode(GL_MODELVIEW);
		glScalef(xScale, yScale, zScale);
		R_TraceLogAPICall("GL_ScaleModelviewMatrix()");
	}
}

void GLC_Frustum(double left, double right, double bottom, double top, double zNear, double zFar)
{
	glFrustum(left, right, bottom, top, zNear, zFar);
	R_TraceLogAPICall("glFrustum()");
}

void GLC_PauseMatrixUpdate(void)
{
	glc_pause_updates = true;
}

void GLC_ResumeMatrixUpdate(void)
{
	glc_pause_updates = false;
}

void GLC_BeginCausticsTextureMatrix(void)
{
	GL_SelectTexture(GL_TEXTURE1);
	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();
	glScalef(0.5, 0.5, 1);
	glRotatef(r_refdef2.time * 10, 1, 0, 0);
	glRotatef(r_refdef2.time * 10, 0, 1, 0);
	glMatrixMode(GL_MODELVIEW);
}

void GLC_EndCausticsTextureMatrix(void)
{
	GL_SelectTexture(GL_TEXTURE1);
	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
}

void GLC_TranslateModelview(float x, float y, float z)
{
	if (!glc_pause_updates) {
		glMatrixMode(GL_MODELVIEW);
		glTranslatef(x, y, z);
		R_TraceLogAPICall("GL_TranslateModelview()");
	}
}

void GLC_LoadModelviewMatrix(void)
{
	glMatrixMode(GL_MODELVIEW);
	glLoadMatrixf(R_ModelviewMatrix());
	R_TraceLogAPICall("glLoadMatrixf(modelview)");
}
