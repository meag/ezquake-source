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

// GLM_program.c
// - All glsl program-related code

#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "glm_local.h"

#define GLM_VERTEX_SHADER   0
#define GLM_FRAGMENT_SHADER 1
#define GLM_GEOMETRY_SHADER 2
#define GLM_COMPUTE_SHADER  3
#define GLM_SHADER_COUNT    4

typedef struct glm_program_s {
	GLuint vertex_shader;
	GLuint geometry_shader;
	GLuint fragment_shader;
	GLuint compute_shader;
	GLuint program;

	const char* friendly_name;
	const char* shader_text[GLM_SHADER_COUNT];
	char* included_definitions;
	GLuint shader_length[GLM_SHADER_COUNT];
	qbool uniforms_found;

	unsigned int custom_options;
	qbool force_recompile;
} glm_program_t;

static glm_program_t program_data[r_program_count];

// Cached OpenGL state
static GLuint currentProgram = 0;

// Shader functions
typedef GLuint(APIENTRY *glCreateShader_t)(GLenum shaderType);
typedef void (APIENTRY *glShaderSource_t)(GLuint shader, GLsizei count, const GLchar **string, const GLint *length);
typedef void (APIENTRY *glCompileShader_t)(GLuint shader);
typedef void (APIENTRY *glDeleteShader_t)(GLuint shader);
typedef void (APIENTRY *glGetShaderInfoLog_t)(GLuint shader, GLsizei maxLength, GLsizei *length, GLchar *infoLog);
typedef void (APIENTRY *glGetShaderiv_t)(GLuint shader, GLenum pname, GLint* params);

// Program functions
typedef GLuint(APIENTRY *glCreateProgram_t)(void);
typedef void (APIENTRY *glLinkProgram_t)(GLuint program);
typedef void (APIENTRY *glDeleteProgram_t)(GLuint program);
typedef void (APIENTRY *glGetProgramInfoLog_t)(GLuint program, GLsizei maxLength, GLsizei *length, GLchar *infoLog);
typedef void (APIENTRY *glUseProgram_t)(GLuint program);
typedef void (APIENTRY *glAttachShader_t)(GLuint program, GLuint shader);
typedef void (APIENTRY *glDetachShader_t)(GLuint program, GLuint shader);
typedef void (APIENTRY *glGetProgramiv_t)(GLuint program, GLenum pname, GLint* params);

// Uniforms
typedef GLint(APIENTRY *glGetUniformLocation_t)(GLuint program, const GLchar* name);
typedef void (APIENTRY *glUniform1i_t)(GLint location, GLint v0);
typedef void (APIENTRY *glUniform4fv_t)(GLint location, GLsizei count, const GLfloat *value);
typedef void (APIENTRY *glUniformMatrix4fv_t)(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);

// Compute shaders
typedef void (APIENTRY *glDispatchCompute_t)(GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z);
typedef void (APIENTRY *glMemoryBarrier_t)(GLbitfield barriers);

// Shader functions
static glCreateShader_t      qglCreateShader = NULL;
static glShaderSource_t      qglShaderSource = NULL;
static glCompileShader_t     qglCompileShader = NULL;
static glDeleteShader_t      qglDeleteShader = NULL;
static glGetShaderInfoLog_t  qglGetShaderInfoLog = NULL;
static glGetShaderiv_t       qglGetShaderiv = NULL;

// Program functions
static glCreateProgram_t     qglCreateProgram = NULL;
static glLinkProgram_t       qglLinkProgram = NULL;
static glDeleteProgram_t     qglDeleteProgram = NULL;
static glGetProgramiv_t      qglGetProgramiv = NULL;
static glGetProgramInfoLog_t qglGetProgramInfoLog = NULL;
static glUseProgram_t        qglUseProgram = NULL;
static glAttachShader_t      qglAttachShader = NULL;
static glDetachShader_t      qglDetachShader = NULL;

// Uniform functions
static glGetUniformLocation_t      qglGetUniformLocation = NULL;
static glUniform1i_t               qglUniform1i;
static glUniformMatrix4fv_t        qglUniformMatrix4fv;
static glUniform4fv_t              qglUniform4fv;

// Compute shaders
static glDispatchCompute_t         qglDispatchCompute;
static glMemoryBarrier_t           qglMemoryBarrier;

#define MAX_SHADER_COMPONENTS 6
#define EZQUAKE_DEFINITIONS_STRING "#ezquake-definitions"

static char core_definitions[512];

// GLM Utility functions
static void GLM_ConPrintShaderLog(GLuint shader)
{
	GLint log_length;
	char* buffer;

	qglGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
	if (log_length) {
		GLsizei written;

		buffer = Q_malloc(log_length);
		qglGetShaderInfoLog(shader, log_length, &written, buffer);
		Con_Printf(buffer);
		Q_free(buffer);
	}
}

static void GLM_ConPrintProgramLog(GLuint program)
{
	GLint log_length;
	char* buffer;

	qglGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
	if (log_length) {
		GLsizei written;

		buffer = Q_malloc(log_length);
		qglGetProgramInfoLog(program, log_length, &written, buffer);
		Con_Printf(buffer);
		Q_free(buffer);
	}
}

static qbool GLM_CompileShader(GLsizei shaderComponents, const char* shaderText[], GLint shaderTextLength[], GLenum shaderType, GLuint* shaderId)
{
	GLuint shader;
	GLint result;

	*shaderId = 0;
	shader = qglCreateShader(shaderType);
	if (shader) {
		qglShaderSource(shader, shaderComponents, shaderText, shaderTextLength);
		qglCompileShader(shader);
		qglGetShaderiv(shader, GL_COMPILE_STATUS, &result);
		if (result) {
			*shaderId = shader;
			return true;
		}

		Con_Printf("Shader->Compile(%X) failed\n", shaderType);
		GLM_ConPrintShaderLog(shader);
		qglDeleteShader(shader);
	}
	else {
		Con_Printf("glCreateShader failed\n");
	}
	return false;
}

// Couldn't find standard library call to do this (!?)
// Search for <search_string> in non-nul-terminated <source>
const char* safe_strstr(const char* source, size_t max_length, const char* search_string)
{
	size_t search_length = strlen(search_string);
	const char* position;

	position = (const char*)memchr(source, search_string[0], max_length);
	while (position) {
		// Move along
		if (max_length < (position - source)) {
			break;
		}
		max_length -= (position - source);

		if (max_length < search_length) {
			break;
		}
		if (!memcmp(position, search_string, search_length)) {
			return position;
		}

		// Try again
		source = position;
		position = (const char*)memchr(source + 1, search_string[0], max_length);
	}

	return NULL;
}

static int GLM_InsertDefinitions(
	const char* strings[],
	GLint lengths[],
	const char* definitions
)
{
	extern unsigned char constants_glsl[], common_glsl[];
	extern unsigned int constants_glsl_len, common_glsl_len;
	const char* break_point;

	if (!strings[0] || !strings[0][0]) {
		return 0;
	}

	break_point = safe_strstr(strings[0], lengths[0], EZQUAKE_DEFINITIONS_STRING);
	
	if (break_point) {
		int position = break_point - strings[0];

		lengths[5] = lengths[0] - position - strlen(EZQUAKE_DEFINITIONS_STRING);
		lengths[4] = definitions ? strlen(definitions) : 0;
		lengths[3] = strlen(core_definitions);
		lengths[2] = common_glsl_len;
		lengths[1] = constants_glsl_len;
		lengths[0] = position;
		strings[5] = break_point + strlen(EZQUAKE_DEFINITIONS_STRING);
		strings[4] = definitions ? definitions : "";
		strings[3] = core_definitions;
		strings[2] = (const char*)common_glsl;
		strings[1] = (const char*)constants_glsl;

		return 6;
	}

	return 1;
}

static qbool GLM_CompileProgram(
	glm_program_t* program
)
{
	GLuint vertex_shader = 0;
	GLuint fragment_shader = 0;
	GLuint geometry_shader = 0;
	GLuint shader_program = 0;
	GLint result = 0;

	const char* friendlyName = program->friendly_name;
	GLsizei vertex_components = 1;
	const char* vertex_shader_text[MAX_SHADER_COMPONENTS] = { program->shader_text[GLM_VERTEX_SHADER], "", "", "", "", "" };
	GLint vertex_shader_text_length[MAX_SHADER_COMPONENTS] = { program->shader_length[GLM_VERTEX_SHADER], 0, 0, 0, 0, 0 };
	GLsizei geometry_components = 1;
	const char* geometry_shader_text[MAX_SHADER_COMPONENTS] = { program->shader_text[GLM_GEOMETRY_SHADER], "", "", "", "", "" };
	GLint geometry_shader_text_length[MAX_SHADER_COMPONENTS] = { program->shader_length[GLM_GEOMETRY_SHADER], 0, 0, 0, 0, 0 };
	GLsizei fragment_components = 1;
	const char* fragment_shader_text[MAX_SHADER_COMPONENTS] = { program->shader_text[GLM_FRAGMENT_SHADER], "", "", "", "", "" };
	GLint fragment_shader_text_length[MAX_SHADER_COMPONENTS] = { program->shader_length[GLM_FRAGMENT_SHADER], 0, 0, 0, 0, 0 };

	Con_Printf("Compiling: %s\n", friendlyName);

	vertex_components = GLM_InsertDefinitions(vertex_shader_text, vertex_shader_text_length, program->included_definitions);
	geometry_components = GLM_InsertDefinitions(geometry_shader_text, geometry_shader_text_length, program->included_definitions);
	fragment_components = GLM_InsertDefinitions(fragment_shader_text, fragment_shader_text_length, program->included_definitions);

	if (GLM_CompileShader(vertex_components, vertex_shader_text, vertex_shader_text_length, GL_VERTEX_SHADER, &vertex_shader)) {
		if (geometry_shader_text[0] == NULL || GLM_CompileShader(geometry_components, geometry_shader_text, geometry_shader_text_length, GL_GEOMETRY_SHADER, &geometry_shader)) {
			if (GLM_CompileShader(fragment_components, fragment_shader_text, fragment_shader_text_length, GL_FRAGMENT_SHADER, &fragment_shader)) {
				Con_DPrintf("Shader compilation completed successfully\n");

				shader_program = qglCreateProgram();
				if (shader_program) {
					qglAttachShader(shader_program, fragment_shader);
					qglAttachShader(shader_program, vertex_shader);
					if (geometry_shader) {
						qglAttachShader(shader_program, geometry_shader);
					}
					qglLinkProgram(shader_program);
					qglGetProgramiv(shader_program, GL_LINK_STATUS, &result);

					if (result) {
						Con_DPrintf("ShaderProgram.Link() was successful\n");
						program->geometry_shader = geometry_shader;
						program->fragment_shader = fragment_shader;
						program->vertex_shader = vertex_shader;
						program->program = shader_program;
						program->uniforms_found = false;
						program->force_recompile = false;

						R_TraceObjectLabelSet(GL_PROGRAM, program->program, -1, program->friendly_name);
						return true;
					}
					else {
						Con_Printf("ShaderProgram.Link() failed\n");
						GLM_ConPrintProgramLog(shader_program);
					}
				}
			}
			else {
				Con_Printf("FragmentShader.Compile() failed\n");
			}
		}
		else {
			Con_Printf("GeometryShader.Compile() failed\n");
		}
	}
	else {
		Con_Printf("VertexShader.Compile() failed\n");
	}

	if (shader_program) {
		qglDeleteProgram(shader_program);
	}
	if (fragment_shader) {
		qglDeleteShader(fragment_shader);
	}
	if (vertex_shader) {
		qglDeleteShader(vertex_shader);
	}
	if (geometry_shader) {
		qglDeleteShader(geometry_shader);
	}
	return false;
}

qbool GLM_CreateVGFProgram(
	const char* friendlyName,
	const char* vertex_shader_text,
	GLuint vertex_shader_text_length,
	const char* geometry_shader_text,
	GLuint geometry_shader_text_length,
	const char* fragment_shader_text,
	GLuint fragment_shader_text_length,
	r_program_id program_id
)
{
	return GLM_CreateVGFProgramWithInclude(
		friendlyName,
		vertex_shader_text,
		vertex_shader_text_length,
		geometry_shader_text,
		geometry_shader_text_length,
		fragment_shader_text,
		fragment_shader_text_length,
		program_id,
		NULL
	);
}

qbool GLM_CreateVGFProgramWithInclude(
	const char* friendlyName,
	const char* vertex_shader_text,
	GLuint vertex_shader_text_length,
	const char* geometry_shader_text,
	GLuint geometry_shader_text_length,
	const char* fragment_shader_text,
	GLuint fragment_shader_text_length,
	r_program_id program_id,
	const char* included_definitions
)
{
	glm_program_t* program = &program_data[program_id];

	program->program = 0;
	program->fragment_shader = program->vertex_shader = program->geometry_shader = 0;
	program->shader_text[GLM_VERTEX_SHADER] = vertex_shader_text;
	program->shader_length[GLM_VERTEX_SHADER] = vertex_shader_text_length;
	program->shader_text[GLM_GEOMETRY_SHADER] = geometry_shader_text;
	program->shader_length[GLM_GEOMETRY_SHADER] = geometry_shader_text_length;
	program->shader_text[GLM_FRAGMENT_SHADER] = fragment_shader_text;
	program->shader_length[GLM_FRAGMENT_SHADER] = fragment_shader_text_length;
	program->friendly_name = friendlyName;
	Q_free(program->included_definitions);
	program->included_definitions = included_definitions ? Q_strdup(included_definitions) : NULL;

	return GLM_CompileProgram(program);
}

qbool GLM_CreateVFProgram(
	const char* friendlyName,
	const char* vertex_shader_text,
	GLuint vertex_shader_text_length,
	const char* fragment_shader_text,
	GLuint fragment_shader_text_length,
	r_program_id program_id
)
{
	return GLM_CreateVFProgramWithInclude(friendlyName, vertex_shader_text, vertex_shader_text_length, fragment_shader_text, fragment_shader_text_length, program_id, NULL);
}

qbool GLM_CreateVFProgramWithInclude(
	const char* friendlyName,
	const char* vertex_shader_text,
	GLuint vertex_shader_text_length,
	const char* fragment_shader_text,
	GLuint fragment_shader_text_length,
	r_program_id program_id,
	const char* included_definitions
)
{
	glm_program_t* program = &program_data[program_id];

	program->program = 0;
	program->fragment_shader = program->vertex_shader = program->geometry_shader = 0;
	program->shader_text[GLM_VERTEX_SHADER] = vertex_shader_text;
	program->shader_length[GLM_VERTEX_SHADER] = vertex_shader_text_length;
	program->shader_text[GLM_GEOMETRY_SHADER] = NULL;
	program->shader_length[GLM_GEOMETRY_SHADER] = 0;
	program->shader_text[GLM_FRAGMENT_SHADER] = fragment_shader_text;
	program->shader_length[GLM_FRAGMENT_SHADER] = fragment_shader_text_length;
	program->friendly_name = friendlyName;
	program->included_definitions = included_definitions ? Q_strdup(included_definitions) : NULL;

	return GLM_CompileProgram(program);
}

// Called during vid_shutdown
void GLM_DeletePrograms(qbool restarting)
{
	r_program_id p;

	GLM_UseProgram(r_program_none);
	for (p = r_program_none; p < r_program_count; ++p) {
		if (program_data[p].program) {
			qglDeleteProgram(program_data[p].program);
			program_data[p].program = 0;
		}
		if (program_data[p].fragment_shader) {
			qglDeleteShader(program_data[p].fragment_shader);
			program_data[p].fragment_shader = 0;
		}
		if (program_data[p].vertex_shader) {
			qglDeleteShader(program_data[p].vertex_shader);
			program_data[p].vertex_shader = 0;
		}
		if (program_data[p].geometry_shader) {
			qglDeleteShader(program_data[p].geometry_shader);
			program_data[p].geometry_shader = 0;
		}
		if (!restarting && program_data[p].included_definitions) {
			Q_free(program_data[p].included_definitions);
		}
	}
}

static void GLM_BuildCoreDefinitions(void)
{
	// Set common definitions here (none yet)
	memset(core_definitions, 0, sizeof(core_definitions));
}

// Called during vid_restart, as starting up again
void GLM_InitPrograms(void)
{
	r_program_id p;

	GLM_BuildCoreDefinitions();

	for (p = r_program_none; p < r_program_count; ++p) {
		// FIXME: At the moment we need the name to be set to know if we should compile...
		if (!program_data[p].program && program_data[p].friendly_name) {
			if (program_data[p].shader_text[GLM_COMPUTE_SHADER]) {
				GLM_CompileComputeShaderProgram(p, program_data[p].shader_text[GLM_COMPUTE_SHADER], program_data[p].shader_length[GLM_COMPUTE_SHADER]);
			}
			else {
				GLM_CompileProgram(&program_data[p]);
			}
		}
	}
}

qbool GLM_ProgramRecompileNeeded(r_program_id program_id, unsigned int options)
{
	//
	const glm_program_t* program = &program_data[program_id];

	return (!program->program) || program->force_recompile || program->custom_options != options;
}

void GLM_CvarForceRecompile(void)
{
	r_program_id p;

	for (p = r_program_none; p < r_program_count; ++p) {
		program_data[p].force_recompile = true;
	}

	GLM_BuildCoreDefinitions();
}

qbool GLM_CompileComputeShaderProgram(r_program_id program_id, const char* shadertext, GLint length)
{
	glm_program_t* program = &program_data[program_id];
	const char* shader_text[MAX_SHADER_COMPONENTS] = { shadertext, "", "", "", "", "" };
	GLint shader_text_length[MAX_SHADER_COMPONENTS] = { length, 0, 0, 0, 0, 0 };
	int components;
	GLuint shader;

	program->program = 0;
	program->fragment_shader = program->vertex_shader = program->geometry_shader = 0;
	memset(program->shader_length, 0, sizeof(program->shader_length));
	program->shader_text[GLM_COMPUTE_SHADER] = shadertext;
	program->shader_length[GLM_COMPUTE_SHADER] = length;

	components = GLM_InsertDefinitions(shader_text, shader_text_length, "");
	if (GLM_CompileShader(components, shader_text, shader_text_length, GL_COMPUTE_SHADER, &shader)) {
		GLuint shader_program = qglCreateProgram();
		if (shader_program) {
			GLint result;

			qglAttachShader(shader_program, shader);
			qglLinkProgram(shader_program);
			qglGetProgramiv(shader_program, GL_LINK_STATUS, &result);

			if (result) {
				Con_DPrintf("ShaderProgram.Link() was successful\n");
				program->compute_shader = shader;
				program->program = shader_program;
				program->uniforms_found = false;
				program->force_recompile = false;

				R_TraceObjectLabelSet(GL_PROGRAM, program->program, -1, program->friendly_name);
				return true;
			}
			else {
				Con_Printf("ShaderProgram.Link() failed\n");
				GLM_ConPrintProgramLog(shader_program);
			}
		}
	}
	return false;
}

qbool GLM_LoadProgramFunctions(void)
{
	qbool all_available = true;

	GL_LoadMandatoryFunctionExtension(glCreateShader, all_available);
	GL_LoadMandatoryFunctionExtension(glCreateShader, all_available);
	GL_LoadMandatoryFunctionExtension(glShaderSource, all_available);
	GL_LoadMandatoryFunctionExtension(glCompileShader, all_available);
	GL_LoadMandatoryFunctionExtension(glDeleteShader, all_available);
	GL_LoadMandatoryFunctionExtension(glGetShaderInfoLog, all_available);
	GL_LoadMandatoryFunctionExtension(glGetShaderiv, all_available);

	GL_LoadMandatoryFunctionExtension(glCreateProgram, all_available);
	GL_LoadMandatoryFunctionExtension(glLinkProgram, all_available);
	GL_LoadMandatoryFunctionExtension(glDeleteProgram, all_available);
	GL_LoadMandatoryFunctionExtension(glUseProgram, all_available);
	GL_LoadMandatoryFunctionExtension(glAttachShader, all_available);
	GL_LoadMandatoryFunctionExtension(glDetachShader, all_available);
	GL_LoadMandatoryFunctionExtension(glGetProgramInfoLog, all_available);
	GL_LoadMandatoryFunctionExtension(glGetProgramiv, all_available);

	GL_LoadMandatoryFunctionExtension(glGetUniformLocation, all_available);
	GL_LoadMandatoryFunctionExtension(glUniform1i, all_available);
	GL_LoadMandatoryFunctionExtension(glUniform4fv, all_available);
	GL_LoadMandatoryFunctionExtension(glUniformMatrix4fv, all_available);

	GL_LoadMandatoryFunctionExtension(glDispatchCompute, all_available);
	GL_LoadMandatoryFunctionExtension(glMemoryBarrier, all_available);

	return all_available;
}

void GLM_UseProgram(r_program_id program_id)
{
	GLuint program = program_data[program_id].program;

	if (program != currentProgram) {
		qglUseProgram(program);

		currentProgram = program;
	}

	if (program != 0) {
		GLM_UploadFrameConstants();
	}
}

void GLM_InitialiseProgramState(void)
{
	currentProgram = 0;
}

void GLM_Uniform1i(GLint location, GLint value)
{
	qglUniform1i(location, value);
}

void GLM_Uniform4fv(GLint location, GLsizei count, GLfloat* values)
{
	qglUniform4fv(location, count, values);
}

void GLM_UniformMatrix4fv(GLint location, GLsizei count, qbool transpose, GLfloat* values)
{
	qglUniformMatrix4fv(location, count, transpose, values);
}

GLint GLM_UniformGetLocation(GLuint program, const char* name)
{
	return qglGetUniformLocation(program, name);
}

// Compute shaders
void GL_DispatchCompute(GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z)
{
	qglDispatchCompute(num_groups_x, num_groups_y, num_groups_z);
}

void GL_MemoryBarrier(GLbitfield barriers)
{
	qglMemoryBarrier(barriers);
}

// Wrappers
int R_ProgramCustomOptions(r_program_id program_id)
{
	return program_data[program_id].custom_options;
}

void R_ProgramSetCustomOptions(r_program_id program_id, int options)
{
	program_data[program_id].custom_options = options;
}

qbool R_ProgramReady(r_program_id program_id)
{
	return program_data[program_id].program != 0;
}

qbool R_ProgramUniformsFound(r_program_id program_id)
{
	return program_data[program_id].uniforms_found;
}

// FIXME: Get rid of this
void R_ProgramSetUniformsFound(r_program_id program_id)
{
	program_data[program_id].uniforms_found = true;
}

