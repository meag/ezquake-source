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

$Id: r_part.c,v 1.19 2007-10-29 00:13:26 d3urk Exp $

*/

#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "particles_classic.h"

extern particle_t particles[ABSOLUTE_MAX_PARTICLES];
extern glm_particle_t glparticles[ABSOLUTE_MAX_PARTICLES];

static glm_vbo_t particleVBO;
static glm_vao_t particleVAO;

static glm_program_t billboardProgram;
static int billboard_modelViewMatrix;
static int billboard_projectionMatrix;
static int billboard_materialTex;
static int billboard_apply_texture;
static int billboard_alpha_texture;
static int billboard_gamma3d;

void GLM_CreateParticleProgram(void)
{
	if (!billboardProgram.program) {
		GL_VGFDeclare(particles_classic);

		// Initialise program for drawing image
		GLM_CreateVGFProgram("Classic particles", GL_VGFParams(particles_classic), &billboardProgram);
	}

	if (billboardProgram.program && !billboardProgram.uniforms_found) {
		billboard_modelViewMatrix = glGetUniformLocation(billboardProgram.program, "modelViewMatrix");
		billboard_projectionMatrix = glGetUniformLocation(billboardProgram.program, "projectionMatrix");
		billboard_materialTex = glGetUniformLocation(billboardProgram.program, "materialTex");
		billboard_apply_texture = glGetUniformLocation(billboardProgram.program, "apply_texture");
		billboard_alpha_texture = glGetUniformLocation(billboardProgram.program, "alpha_texture");
		billboard_gamma3d = glGetUniformLocation(billboardProgram.program, "gamma3d");
		billboardProgram.uniforms_found = true;

		glProgramUniform1i(billboardProgram.program, billboard_materialTex, 0);
		glProgramUniform1i(billboardProgram.program, billboard_alpha_texture, 0);
	}
}

static GLuint GLM_CreateParticleVAO(void)
{
	if (!particleVBO.vbo) {
		GL_GenBuffer(&particleVBO, "particle");
		glBindBufferExt(GL_ARRAY_BUFFER, particleVBO.vbo);
		glBufferDataExt(GL_ARRAY_BUFFER, sizeof(particles), glparticles, GL_STATIC_DRAW);
	}

	if (!particleVAO.vao) {
		GL_GenVertexArray(&particleVAO);
		GL_BindVertexArray(particleVAO.vao);
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glEnableVertexAttribArray(2);
		glBindBufferExt(GL_ARRAY_BUFFER, particleVBO.vbo);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm_particle_t), (void*) 0);
		glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(glm_particle_t), (void*) (sizeof(float) * 3));
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(glm_particle_t), (void*) (sizeof(float) * 4));
	}

	return particleVAO.vao;
}

void GLM_DrawParticles(int number, qbool square)
{
	GLuint vao;
	float modelViewMatrix[16];
	float projectionMatrix[16];

	if (!billboardProgram.program) {
		GLM_CreateParticleProgram();
	}

	vao = GLM_CreateParticleVAO();

	if (billboardProgram.program && vao) {
		GLM_GetMatrix(GL_MODELVIEW, modelViewMatrix);
		GLM_GetMatrix(GL_PROJECTION, projectionMatrix);

		GL_UseProgram(billboardProgram.program);
		glUniformMatrix4fv(billboard_modelViewMatrix, 1, GL_FALSE, modelViewMatrix);
		glUniformMatrix4fv(billboard_projectionMatrix, 1, GL_FALSE, projectionMatrix);
		glUniform1i(billboard_apply_texture, !square);
		glUniform1f(billboard_gamma3d, v_gamma.value);

		GL_BindVertexArray(vao);
		glDrawArrays(GL_POINTS, 0, number);
	}
}

void GLM_UpdateParticles(int particles_to_draw)
{
	// Update VBO
	/*
	{
	void* buffer = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	memcpy(buffer, particles, sizeof(particles[0]) * particles_to_draw);
	glUnmapBuffer(GL_ARRAY_BUFFER);
	}*/
	glBindBufferExt(GL_ARRAY_BUFFER, particleVBO.vbo);
	glBufferDataExt(GL_ARRAY_BUFFER, sizeof(glparticles[0]) * particles_to_draw, glparticles, GL_STATIC_DRAW);
}
