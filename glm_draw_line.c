/*
Copyright (C) 2017 ezQuake team

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
#include "common_draw.h"
#ifndef __APPLE__
#include "tr_types.h"
#endif
#include "glm_draw.h"

static glm_program_t line_program;
static glm_vao_t line_vao;
static buffer_ref line_vbo;
static GLint line_matrix;

glm_line_framedata_t lineData;

void GLM_PrepareLineProgram(void)
{
	if (!GL_BufferReferenceIsValid(line_vbo)) {
		line_vbo = GL_GenFixedBuffer(GL_ARRAY_BUFFER, "line", sizeof(lineData.line_points), lineData.line_points, GL_STREAM_DRAW);
	}
	else if (lineData.lineCount) {
		GL_UpdateBuffer(line_vbo, sizeof(lineData.line_points[0]) * lineData.lineCount * 2, lineData.line_points);
	}

	if (!line_vao.vao) {
		GL_GenVertexArray(&line_vao, "line-vao");

		GL_ConfigureVertexAttribPointer(&line_vao, line_vbo, 0, 2, GL_FLOAT, GL_FALSE, sizeof(glm_line_point_t), VBO_FIELDOFFSET(glm_line_point_t, position), 0);
		GL_ConfigureVertexAttribPointer(&line_vao, line_vbo, 1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(glm_line_point_t), VBO_FIELDOFFSET(glm_line_point_t, color), 0);
	}

	if (GLM_ProgramRecompileNeeded(&line_program, 0)) {
		GL_VFDeclare(line_draw);

		// Very simple line-drawing
		GLM_CreateVFProgram(
			"LineDrawing",
			GL_VFParams(line_draw),
			&line_program
		);
	}

	if (line_program.program && !line_program.uniforms_found) {
		line_matrix = glGetUniformLocation(line_program.program, "matrix");
		line_program.uniforms_found = true;
	}
}

void GLM_Draw_Line3D(byte* color, vec3_t start, vec3_t end)
{
	if (lineData.lineCount >= MAX_LINES_PER_FRAME) {
		return;
	}
}

void GLM_Draw_LineRGB(byte* color, int x_start, int y_start, int x_end, int y_end)
{
	if (lineData.lineCount >= MAX_LINES_PER_FRAME) {
		return;
	}
	if (!GLM_LogCustomImageType(imagetype_line, lineData.lineCount)) {
		return;
	}

	VectorSet(lineData.line_points[lineData.lineCount * 2 + 0].position, x_start, y_start, 0);
	memcpy(lineData.line_points[lineData.lineCount * 2 + 0].color, color, sizeof(lineData.line_points[lineData.lineCount * 2 + 0].color));
	VectorSet(lineData.line_points[lineData.lineCount * 2 + 1].position, x_end, y_end, 0);
	memcpy(lineData.line_points[lineData.lineCount * 2 + 1].color, color, sizeof(lineData.line_points[lineData.lineCount * 2 + 0].color));
	++lineData.lineCount;
}

void GLM_DrawLines(int start, int end)
{
	if (line_program.program && line_vao.vao) {
		float matrix[16];
		int i;

		GL_UseProgram(line_program.program);
		GLM_GetMatrix(GL_PROJECTION, matrix);
		glUniformMatrix4fv(line_matrix, 1, GL_FALSE, matrix);
		GL_BindVertexArray(&line_vao);

		for (i = start; i <= end; ++i) {
			GL_DrawArrays(GL_LINES, i * 2, 2);
		}
	}
}
