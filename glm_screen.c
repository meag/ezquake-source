
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

// Functions taken from cl_screen.c, specific to OpenGL 'modern'

#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "r_draw.h"

void GLM_DrawAccelBar(int x, int y, int length, int charsize, int pos)
{
	byte color[] = { 0, 0, 255, 255 };

	// draw the coloured indicator strip
	//Draw_Fill(x, y, length, charsize, 184);
	GLM_DrawAlphaRectangleRGB(x, y, length, charsize, 0, true, color_white);

	GLM_DrawAlphaRectangleRGB(x + pos - 1, y, 3, charsize, 0, true, color);
}
