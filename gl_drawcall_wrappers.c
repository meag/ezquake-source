
#include <SDL.h>

#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "tr_types.h"

// <draw-functions (various)>
typedef void (APIENTRY *glMultiDrawArrays_t)(GLenum mode, const GLint * first, const GLsizei* count, GLsizei drawcount);
typedef void (APIENTRY *glMultiDrawElements_t)(GLenum mode, const GLsizei * count, GLenum type, const GLvoid * const * indices, GLsizei drawcount);
typedef void (APIENTRY *glDrawElementsBaseVertex_t)(GLenum mode, GLsizei count, GLenum type, GLvoid* indices, GLint basevertex);
typedef void (APIENTRY *glPrimitiveRestartIndex_t)(GLuint index);

static glMultiDrawArrays_t                               qglMultiDrawArrays;
static glMultiDrawElements_t                             qglMultiDrawElements;
static glDrawElementsBaseVertex_t                        qglDrawElementsBaseVertex;
static glPrimitiveRestartIndex_t                         qglPrimitiveRestartIndex;

// (modern/4.3+)
typedef void (APIENTRY *glDrawArraysInstanced_t)(GLenum mode, GLint first, GLsizei count, GLsizei primcount);
typedef void (APIENTRY *glMultiDrawArraysIndirect_t)(GLenum mode, const void *indirect, GLsizei drawcount, GLsizei stride);
typedef void (APIENTRY *glMultiDrawElementsIndirect_t)(GLenum mode, GLenum type, const void* indirect, GLsizei drawcount, GLsizei stride);
typedef void (APIENTRY *glDrawArraysInstancedBaseInstance_t)(GLenum mode, GLint first, GLsizei count, GLsizei primcount, GLuint baseinstance);
typedef void (APIENTRY *glDrawElementsInstancedBaseInstance_t)(GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei primcount, GLuint baseinstance);
typedef void (APIENTRY *glDrawElementsInstancedBaseVertexBaseInstance_t)(GLenum mode, GLsizei count, GLenum type, GLvoid* indices, GLsizei primcount, GLint basevertex, GLuint baseinstance);

static glMultiDrawArraysIndirect_t                       qglMultiDrawArraysIndirect;
static glMultiDrawElementsIndirect_t                     qglMultiDrawElementsIndirect;
static glDrawArraysInstancedBaseInstance_t               qglDrawArraysInstancedBaseInstance;
static glDrawElementsInstancedBaseInstance_t             qglDrawElementsInstancedBaseInstance;
static glDrawElementsInstancedBaseVertexBaseInstance_t   qglDrawElementsInstancedBaseVertexBaseInstance;
// </draw-functions>

qbool GLM_LoadDrawFunctions(void)
{
	qbool all_available = true;

	GL_LoadMandatoryFunctionExtension(glMultiDrawArraysIndirect, all_available);
	GL_LoadMandatoryFunctionExtension(glMultiDrawElementsIndirect, all_available);
	GL_LoadMandatoryFunctionExtension(glDrawArraysInstancedBaseInstance, all_available);
	GL_LoadMandatoryFunctionExtension(glDrawElementsInstancedBaseInstance, all_available);
	GL_LoadMandatoryFunctionExtension(glDrawElementsInstancedBaseVertexBaseInstance, all_available);

	return all_available;
}

void GL_LoadDrawFunctions(void)
{
	// Draw functions used for modern & classic
	GL_LoadOptionalFunction(glMultiDrawArrays);
	GL_LoadOptionalFunction(glMultiDrawElements);
	GL_LoadOptionalFunction(glDrawElementsBaseVertex);

	GL_LoadOptionalFunction(glPrimitiveRestartIndex);
	if (qglPrimitiveRestartIndex) {
		glEnable(GL_PRIMITIVE_RESTART);
		if (glConfig.majorVersion > 4 || (glConfig.majorVersion == 4 && glConfig.minorVersion >= 3)) {
			glEnable(GL_PRIMITIVE_RESTART_FIXED_INDEX);
		}
		else {
			qglPrimitiveRestartIndex(~(GLuint)0);
		}
	}
}

// Wrappers around drawing functions
void GL_MultiDrawArrays(GLenum mode, GLint* first, GLsizei* count, GLsizei primcount)
{
	if (qglMultiDrawArrays) {
		qglMultiDrawArrays(mode, first, count, primcount);
		++frameStats.draw_calls;
		frameStats.subdraw_calls += primcount;
		GL_LogAPICall("glMultiDrawElements(%d sub-draws)", primcount);
	}
	else {
		int i;
		for (i = 0; i < primcount; ++i) {
			GL_DrawArrays(mode, first[i], count[i]);
		}
	}
}

void GL_DrawArrays(GLenum mode, GLint first, GLsizei count)
{
	glDrawArrays(mode, first, count);
	++frameStats.draw_calls;
	GL_LogAPICall("glDrawArrays(%d verts)", count);
}

void GL_DrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, GLvoid* indices, GLint basevertex)
{
	if (basevertex && !qglDrawElementsBaseVertex) {
		Sys_Error("glDrawElementsBaseVertex called, not supported");
	}
	else if (qglDrawElementsBaseVertex) {
		qglDrawElementsBaseVertex(mode, count, type, indices, basevertex);
	}
	else {
		glDrawElements(mode, count, type, indices);
	}
	++frameStats.draw_calls;
	GL_LogAPICall("glDrawElements(%d verts)", count);
}

qbool GL_DrawElementsBaseVertexAvailable(void)
{
	return qglDrawElementsBaseVertex != NULL;
}

void GL_DrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices)
{
	glDrawElements(mode, count, type, indices);
	++frameStats.draw_calls;
	GL_LogAPICall("glDrawElements(%d verts)", count);
}

void GL_MultiDrawArraysIndirect(GLenum mode, const void* indirect, GLsizei drawcount, GLsizei stride)
{
	qglMultiDrawArraysIndirect(mode, indirect, drawcount, stride);
	++frameStats.draw_calls;
	frameStats.subdraw_calls += drawcount;
	GL_LogAPICall("glMultiDrawArraysIndirect(%d subdraws)", drawcount);
}

void GL_MultiDrawElementsIndirect(GLenum mode, GLenum type, const void* indirect, GLsizei drawcount, GLsizei stride)
{
	qglMultiDrawElementsIndirect(mode, type, indirect, drawcount, stride);
	++frameStats.draw_calls;
	frameStats.subdraw_calls += drawcount;
	GL_LogAPICall("glMultiDrawElementsIndirect(%d subdraws)", drawcount);
}
