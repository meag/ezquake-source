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

// OpenGL buffer handling

#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "tr_types.h"
#include "r_local.h"
#include "glm_vao.h"
#include "r_trace.h"
#include "r_buffers.h"
#include "r_renderer.h"

static void GL_BufferStartFrame(void);
static void GL_BufferEndFrame(void);
static qbool GL_BuffersReady(void);

static void GL_InitialiseBufferState(void);

static qbool GL_CreateFixedBuffer(r_buffer_id id, buffertype_t type, const char* name, int size, void* data, bufferusage_t usage);
static void GL_GenFixedBuffer(r_buffer_id id, buffertype_t type, const char* name, int size, void* data, bufferusage_t usage);
static uintptr_t GL_BufferOffset(r_buffer_id ref);

static size_t GL_BufferSize(r_buffer_id id);

static void GL_BindBuffer(r_buffer_id id);
static void GL_BindBufferBase(r_buffer_id id, unsigned int index);
static void GL_BindBufferRange(r_buffer_id id, unsigned int index, ptrdiff_t offset, int size);
static void GL_UnBindBuffer(buffertype_t type);
static void GL_UpdateBuffer(r_buffer_id id, int size, void* data);
static void GL_UpdateBufferSection(r_buffer_id id, ptrdiff_t offset, int size, const void* data);
static void GL_ResizeBuffer(r_buffer_id id, int size, void* data);
static void GL_EnsureBufferSize(r_buffer_id id, int size);

static qbool GL_BindBufferImpl(GLenum target, GLuint buffer);

typedef struct buffer_data_s {
	GLuint glref;
	char name[64];

	// These set at creation
	buffertype_t type;
	bufferusage_t usage;
	GLsizei size;
	GLuint storageFlags;         // flags for BufferStorage()
	qbool using_storage;

	void* persistent_mapped_ptr;

	struct buffer_data_s* next_free;
} buffer_data_t;

static buffer_data_t buffer_data[r_buffer_count];
static int buffer_count;
static buffer_data_t* next_free_buffer = NULL;
static qbool tripleBuffer_supported = false;
static GLsync tripleBufferSyncObjects[3];

static GLenum GL_BufferTypeToTarget(buffertype_t type)
{
	switch (type) {
		case buffertype_index:
			return GL_ELEMENT_ARRAY_BUFFER;
		case buffertype_indirect:
			return GL_DRAW_INDIRECT_BUFFER;
		case buffertype_storage:
			return GL_SHADER_STORAGE_BUFFER;
		case buffertype_vertex:
			return GL_ARRAY_BUFFER;
		case buffertype_uniform:
			return GL_UNIFORM_BUFFER;
		default:
			assert(false);
			return 0;
	}
}

static GLenum GL_BufferUsageToGLUsage(bufferusage_t usage)
{
	switch (usage) {
		case bufferusage_once_per_frame:
			return GL_STREAM_DRAW;
		case bufferusage_reuse_per_frame:
			return GL_STREAM_DRAW;
		case bufferusage_reuse_many_frames:
			return GL_STATIC_DRAW;
		case bufferusage_constant_data:
			return GL_STATIC_COPY;
		default:
			assert(false);
			return 0;
	}
}

typedef void (APIENTRY *glBindBuffer_t)(GLenum target, GLuint buffer);
typedef void (APIENTRY *glBufferData_t)(GLenum target, GLsizeiptr size, const GLvoid* data, GLenum usage);
typedef void (APIENTRY *glBufferSubData_t)(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid* data);
typedef void (APIENTRY *glGenBuffers_t)(GLsizei n, GLuint* buffers);
typedef void (APIENTRY *glDeleteBuffers_t)(GLsizei n, const GLuint* buffers);
typedef void (APIENTRY *glBindBufferBase_t)(GLenum target, GLuint index, GLuint buffer);
typedef void (APIENTRY *glBindBufferRange_t)(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size);
typedef void (APIENTRY *glNamedBufferSubData_t)(GLuint buffer, GLintptr offset, GLsizei size, const void* data);
typedef void (APIENTRY *glNamedBufferData_t)(GLuint buffer, GLsizei size, const void* data, GLenum usage);
typedef void (APIENTRY *glUnmapNamedBuffer_t)(GLuint buffer);
typedef void (APIENTRY *glUnmapBuffer_t)(GLenum target);

// AZDO-buffer-streaming (4.4)
typedef void* (APIENTRY *glMapBufferRange_t)(GLenum mtarget, GLintptr offset, GLsizeiptr length, GLbitfield access);
typedef void (APIENTRY *glBufferStorage_t)(GLenum target, GLsizeiptr size, const GLvoid* data, GLbitfield flags);
typedef GLsync (APIENTRY *glFenceSync_t)(GLenum condition, GLbitfield flags);
typedef GLenum (APIENTRY *glClientWaitSync_t)(GLsync sync, GLbitfield flags, GLuint64 timeout);
typedef void (APIENTRY *glDeleteSync_t)(GLsync sync);

// Buffer functions
static glBindBuffer_t        qglBindBuffer = NULL;
static glBufferData_t        qglBufferData = NULL;
static glBufferSubData_t     qglBufferSubData = NULL;
static glGenBuffers_t        qglGenBuffers = NULL;
static glDeleteBuffers_t     qglDeleteBuffers = NULL;
static glBindBufferBase_t    qglBindBufferBase = NULL;
static glBindBufferRange_t   qglBindBufferRange = NULL;
static glUnmapBuffer_t       qglUnmapBuffer = NULL;

// DSA
static glNamedBufferSubData_t qglNamedBufferSubData = NULL;
static glNamedBufferData_t    qglNamedBufferData = NULL;
static glUnmapNamedBuffer_t   qglUnmapNamedBuffer = NULL;

// Persistent mapped buffers
static glMapBufferRange_t qglMapBufferRange = NULL;
static glBufferStorage_t  qglBufferStorage = NULL;
static glFenceSync_t      qglFenceSync = NULL;
static glClientWaitSync_t qglClientWaitSync = NULL;
static glDeleteSync_t     qglDeleteSync = NULL;

// Cache OpenGL state
static struct {
	GLuint currentArrayBuffer;
	GLuint currentUniformBuffer;
	GLuint currentDrawIndirectBuffer;
	GLuint currentElementArrayBuffer;
} glBufferState;

#define GL_BufferInvalidateReference(name, glref) { name = ((name) == (glref)) ? 0 : (name); }

static buffer_data_t* GL_BufferAllocateSlot(r_buffer_id id, buffertype_t type, const char* name, GLsizei size, bufferusage_t usage)
{
	buffer_data_t* buffer = &buffer_data[id];

	assert(usage < bufferusage_count);

	if (buffer->glref) {
		GL_BufferInvalidateReference(glBufferState.currentArrayBuffer, buffer->glref);
		GL_BufferInvalidateReference(glBufferState.currentUniformBuffer, buffer->glref);
		GL_BufferInvalidateReference(glBufferState.currentDrawIndirectBuffer, buffer->glref);
		GL_BufferInvalidateReference(glBufferState.currentElementArrayBuffer, buffer->glref);
		if (buffer->type == buffertype_index) {
			R_BindVertexArrayElementBuffer(r_buffer_none);
		}
		R_BufferInvalidateBoundState(id);
		qglDeleteBuffers(1, &buffer->glref);
#ifdef DEBUG_MEMORY_ALLOCATIONS
		Sys_Printf("\nbuffer,free,%d,%s,%u\n", id, name, buffer->size * (buffer->persistent_mapped_ptr ? 3 : 1));
#endif
	}

	memset(buffer, 0, sizeof(buffer_data[0]));
	strlcpy(buffer->name, name ? name : "?", sizeof(buffer->name));
	buffer->size = size;
	buffer->usage = usage;
	buffer->type = type;
	qglGenBuffers(1, &buffer->glref);
	return buffer;
}

static void GL_GenFixedBuffer(r_buffer_id id, buffertype_t type, const char* name, int size, void* data, bufferusage_t usage)
{
	GLenum target = GL_BufferTypeToTarget(type);
	GLenum glUsage = GL_BufferUsageToGLUsage(usage);
	buffer_data_t* buffer = GL_BufferAllocateSlot(id, type, name, size, usage);

	buffer->persistent_mapped_ptr = NULL;
	buffer->using_storage = false;
	GL_BindBufferImpl(target, buffer->glref);
	GL_TraceObjectLabelSet(GL_BUFFER, buffer->glref, -1, name);
	qglBufferData(target, size, data, glUsage);
	if (target == GL_ELEMENT_ARRAY_BUFFER) {
		R_BindVertexArrayElementBuffer(id);
	}
#ifdef DEBUG_MEMORY_ALLOCATIONS
	Sys_Printf("\nbuffer,alloc,%d,%s,%u\n", id, name, size);
#endif
}

static qbool GL_CreateFixedBuffer(r_buffer_id id, buffertype_t type, const char* name, int size, void* data, bufferusage_t usage)
{
	GLenum target = GL_BufferTypeToTarget(type);
	buffer_data_t* buffer;

	qbool tripleBuffer = false;
	qbool useStorage = false;
	GLbitfield storageFlags = 0;
	GLsizei alignment = 1;

	if (!size) {
		return false;
	}

	buffer = GL_BufferAllocateSlot(id, type, name, size, usage);

	if (usage == bufferusage_once_per_frame) {
		useStorage = tripleBuffer = tripleBuffer_supported;
		storageFlags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
	}
	else if (usage == bufferusage_reuse_per_frame) {
		useStorage = (qglBufferStorage != NULL);
		storageFlags = GL_DYNAMIC_STORAGE_BIT;
		tripleBuffer = false;
	}
	else if (usage == bufferusage_reuse_many_frames) {
		storageFlags = GL_MAP_WRITE_BIT;
		useStorage = (qglBufferStorage != NULL);
		if (!data) {
			Sys_Error("Buffer %s flagged as constant but no initial data", name);
			return false;
		}
	}
	else if (usage == bufferusage_constant_data) {
		storageFlags = (data ? 0 : GL_DYNAMIC_STORAGE_BIT);
		useStorage = (qglBufferStorage != NULL);
		if (!data) {
			Sys_Error("Buffer %s flagged as constant but no initial data", name);
			return false;
		}
	}
	else {
		Sys_Error("Unknown usage flag passed to GL_CreateFixedBuffer");
		return false;
	}

	if (!useStorage) {
		// Fall back to standard mutable buffer
		GL_GenFixedBuffer(id, type, name, size, data, usage);
		return true;
	}

	GL_BindBufferImpl(target, buffer->glref);
	if (target == GL_UNIFORM_BUFFER) {
		alignment = glConfig.uniformBufferOffsetAlignment;
	}
	else if (target == GL_SHADER_STORAGE_BUFFER) {
		alignment = glConfig.shaderStorageBufferOffsetAlignment;
	}

	if (alignment > 1) {
		buffer->size = size = ((size + (alignment - 1)) / alignment) * alignment;
	}

	GL_TraceObjectLabelSet(GL_BUFFER, buffer->glref, -1, name);

	if (tripleBuffer) {
		qglBufferStorage(target, size * 3, NULL, storageFlags);
		buffer->persistent_mapped_ptr = qglMapBufferRange(target, 0, size * 3, storageFlags);

		if (buffer->persistent_mapped_ptr) {
			if (data) {
				void* base = (void*)((uintptr_t)buffer->persistent_mapped_ptr + size * glConfig.tripleBufferIndex);

				memcpy(base, data, size);
			}
#ifdef DEBUG_MEMORY_ALLOCATIONS
			Sys_Printf("\nbuffer,alloc,%d,%s,%u\n", id, name, size * 3);
#endif
		}
		else {
			Con_Printf("\20opengl\21 triple-buffered allocation failed (%dKB)\n", (size * 3) / 1024);
			GL_GenFixedBuffer(id, type, name, size, data, usage);
			return true;
		}
	}
	else {
		qglBufferStorage(target, size, data, storageFlags);
#ifdef DEBUG_MEMORY_ALLOCATIONS
		Sys_Printf("\nbuffer,alloc,%d,%s,%u\n", id, name, size);
#endif
	}

	buffer->using_storage = true;
	buffer->storageFlags = storageFlags;
	return true;
}

static void GL_UpdateBuffer(r_buffer_id id, int size, void* data)
{
	assert(id != r_buffer_none);
	assert(buffer_data[id].glref);
	assert(data != NULL);
	assert(size <= buffer_data[id].size);

	if (buffer_data[id].persistent_mapped_ptr) {
		void* start = (void*)((uintptr_t)buffer_data[id].persistent_mapped_ptr + GL_BufferOffset(id));

		memcpy(start, data, size);

		R_TraceLogAPICall("GL_UpdateBuffer[memcpy](%s, %u)", buffer_data[id].name, size);
	}
	else {
		if (qglNamedBufferSubData) {
			qglNamedBufferSubData(buffer_data[id].glref, 0, (GLsizei)size, data);
		}
		else {
			GL_BindBuffer(id);
			qglBufferSubData(GL_BufferTypeToTarget(buffer_data[id].type), 0, (GLsizeiptr)size, data);
		}

		R_TraceLogAPICall("GL_UpdateBuffer(%s)", buffer_data[id].name);
	}
}

static size_t GL_BufferSize(r_buffer_id id)
{
	if (id == r_buffer_none) {
		return 0;
	}

	return buffer_data[id].size;
}

static void GL_ResizeBuffer(r_buffer_id id, int size, void* data)
{
	assert(id != r_buffer_none);
	assert(buffer_data[id].glref);

	if (buffer_data[id].using_storage) {
		if (buffer_data[id].persistent_mapped_ptr) {
			if (qglUnmapNamedBuffer) {
				qglUnmapNamedBuffer(buffer_data[id].glref);
			}
			else {
				GL_BindBuffer(id);
				qglUnmapBuffer(GL_BufferTypeToTarget(buffer_data[id].type));
			}
		}
		qglDeleteBuffers(1, &buffer_data[id].glref);
		buffer_data[id].glref = 0;
#ifdef DEBUG_MEMORY_ALLOCATIONS
		Sys_Printf("\nbuffer,free-resize,%d,%s,%u\n", id, buffer_data[id].name, buffer_data[id].size * (buffer_data[id].persistent_mapped_ptr ? 3 : 1));
#endif
		buffer_data[id].next_free = next_free_buffer;

		GL_CreateFixedBuffer(id, buffer_data[id].type, buffer_data[id].name, size, data, buffer_data[id].usage);
	}
	else {
		if (qglNamedBufferData) {
			qglNamedBufferData(buffer_data[id].glref, size, data, GL_BufferUsageToGLUsage(buffer_data[id].usage));
		}
		else {
			GL_BindBuffer(id);
			qglBufferData(GL_BufferTypeToTarget(buffer_data[id].type), size, data, GL_BufferUsageToGLUsage(buffer_data[id].usage));
		}
#ifdef DEBUG_MEMORY_ALLOCATIONS
		Sys_Printf("\nbuffer,free-resize,%d,%s,%u\n", id, buffer_data[id].name, buffer_data[id].size);
		Sys_Printf("\nbuffer,alloc-resize,%d,%s,%u\n", id, buffer_data[id].name, size);
#endif

		buffer_data[id].size = size;
		R_TraceLogAPICall("GL_ResizeBuffer(%s)", buffer_data[id].name);
	}
}

static void GL_UpdateBufferSection(r_buffer_id id, ptrdiff_t offset, int size, const void* data)
{
	assert(id != r_buffer_none);
	assert(buffer_data[id].glref);
	assert(data);
	assert(offset >= 0);
	assert(offset < buffer_data[id].size);
	assert(offset + size <= buffer_data[id].size);

	if (buffer_data[id].persistent_mapped_ptr) {
		void* base = (void*)((uintptr_t)buffer_data[id].persistent_mapped_ptr + GL_BufferOffset(id) + offset);

		memcpy(base, data, size);
	}
	else {
		if (qglNamedBufferSubData) {
			qglNamedBufferSubData(buffer_data[id].glref, offset, size, data);
		}
		else {
			GL_BindBuffer(id);
			qglBufferSubData(GL_BufferTypeToTarget(buffer_data[id].type), offset, size, data);
		}
	}
	R_TraceLogAPICall("GL_UpdateBufferSection(%s)", buffer_data[id].name);
}

static void GL_BufferShutdown(void)
{
	int i;

	for (i = 0; i < buffer_count; ++i) {
		if (buffer_data[i].glref) {
			if (qglDeleteBuffers) {
				qglDeleteBuffers(1, &buffer_data[i].glref);
#ifdef DEBUG_MEMORY_ALLOCATIONS
				Sys_Printf("\nbuffer,free,%d,%s,%u\n", i, buffer_data[i].name, buffer_data[i].size * (buffer_data[i].persistent_mapped_ptr ? 3 : 1));
#endif
			}
		}
	}
	memset(buffer_data, 0, sizeof(buffer_data));
	next_free_buffer = NULL;
	buffer_count = 0;

	for (i = 0; i < 3; ++i) {
		if (tripleBufferSyncObjects[i]) {
			qglDeleteSync(tripleBufferSyncObjects[i]);
		}
	}
	memset(tripleBufferSyncObjects, 0, sizeof(tripleBufferSyncObjects));
	memset(&glBufferState, 0, sizeof(glBufferState));
}

static void GL_BindBufferBase(r_buffer_id id, unsigned int index)
{
	assert(id != r_buffer_none);
	assert(buffer_data[id].glref);

	qglBindBufferBase(GL_BufferTypeToTarget(buffer_data[id].type), index, buffer_data[id].glref);
}

static void GL_BindBufferRange(r_buffer_id id, unsigned int index, ptrdiff_t offset, int size)
{
	if (size == 0) {
		return;
	}

	assert(id != r_buffer_none);
	assert(buffer_data[id].glref);
	assert(size >= 0);
	assert(offset >= 0);
	assert(offset + size <= buffer_data[id].size * (buffer_data[id].persistent_mapped_ptr ? 3 : 1));

	qglBindBufferRange(GL_BufferTypeToTarget(buffer_data[id].type), index, buffer_data[id].glref, offset, size);
}

static qbool GL_BindBufferImpl(GLenum target, GLuint buffer)
{
	if (target == GL_ARRAY_BUFFER) {
		if (buffer == glBufferState.currentArrayBuffer) {
			return false;
		}
		glBufferState.currentArrayBuffer = buffer;
	}
	else if (target == GL_UNIFORM_BUFFER) {
		if (buffer == glBufferState.currentUniformBuffer) {
			return false;
		}
		glBufferState.currentUniformBuffer = buffer;
	}
	else if (target == GL_DRAW_INDIRECT_BUFFER) {
		if (buffer == glBufferState.currentDrawIndirectBuffer) {
			return false;
		}
		glBufferState.currentDrawIndirectBuffer = buffer;
	}
	else if (target == GL_ELEMENT_ARRAY_BUFFER) {
		if (buffer == glBufferState.currentElementArrayBuffer) {
			return false;
		}
		glBufferState.currentElementArrayBuffer = buffer;
	}

	qglBindBuffer(target, buffer);
	return true;
}

static void GL_BindBuffer(r_buffer_id id)
{
	qbool switched;
	GLenum glTarget;

	if (!(R_BufferReferenceIsValid(id) && buffer_data[id].glref)) {
		R_TraceLogAPICall("GL_BindBuffer(<invalid-reference:%s>)", buffer_data[id].name);
		return;
	}

	glTarget = GL_BufferTypeToTarget(buffer_data[id].type);
	switched = GL_BindBufferImpl(glTarget, buffer_data[id].glref);
	if (buffer_data[id].type == buffertype_index) {
		R_BindVertexArrayElementBuffer(id);
	}

	if (switched) {
		R_TraceLogAPICall("GL_BindBuffer(%s)", buffer_data[id].name);
	}
}

static void GL_InitialiseBufferState(void)
{
	memset(&glBufferState, 0, sizeof(glBufferState));
	R_BindVertexArrayElementBuffer(r_buffer_none);
	R_InitialiseVAOState();
}

static void GL_UnBindBuffer(buffertype_t type)
{
	GLenum target = GL_BufferTypeToTarget(type);
	GL_BindBufferImpl(target, 0);
	R_TraceLogAPICall("GL_UnBindBuffer(%s)", target == GL_ARRAY_BUFFER ? "array-buffer" : (target == GL_ELEMENT_ARRAY_BUFFER ? "element-array" : "?"));
}

static qbool GL_BufferValid(r_buffer_id id)
{
	return id > r_buffer_none && id < r_buffer_count && buffer_data[id].glref != 0;
}

// Called when the VAO is bound
static void GL_SetElementArrayBuffer(r_buffer_id id)
{
	if (GL_BufferValid(id)) {
		glBufferState.currentElementArrayBuffer = buffer_data[id].glref;
	}
	else {
		glBufferState.currentElementArrayBuffer = 0;
	}
}

static void GL_EnsureBufferSize(r_buffer_id id, int size)
{
	assert(id);
	assert(buffer_data[id].glref);

	if (buffer_data[id].size < size) {
		GL_ResizeBuffer(id, size, NULL);
	}
}

static void GL_BufferStartFrame(void)
{
	if (tripleBuffer_supported && tripleBufferSyncObjects[glConfig.tripleBufferIndex]) {
		GLenum waitRet = qglClientWaitSync(tripleBufferSyncObjects[glConfig.tripleBufferIndex], 0, 0);
		while (waitRet == GL_TIMEOUT_EXPIRED) {
			// Flush commands and wait for longer
			waitRet = qglClientWaitSync(tripleBufferSyncObjects[glConfig.tripleBufferIndex], GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000);
		}
	}
}

static void GL_BufferEndFrame(void)
{
	if (tripleBuffer_supported) {
		if (tripleBufferSyncObjects[glConfig.tripleBufferIndex]) {
			qglDeleteSync(tripleBufferSyncObjects[glConfig.tripleBufferIndex]);
		}
		tripleBufferSyncObjects[glConfig.tripleBufferIndex] = qglFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		glConfig.tripleBufferIndex = (glConfig.tripleBufferIndex + 1) % 3;
	}
}

static uintptr_t GL_BufferOffset(r_buffer_id id)
{
	return buffer_data[id].persistent_mapped_ptr ? buffer_data[id].size * glConfig.tripleBufferIndex : 0;
}

static qbool GL_BuffersReady(void)
{
	if (tripleBuffer_supported && tripleBufferSyncObjects[glConfig.tripleBufferIndex]) {
		if (qglClientWaitSync(tripleBufferSyncObjects[glConfig.tripleBufferIndex], 0, 0) == GL_TIMEOUT_EXPIRED) {
			return false;
		}
	}
	return true;
}

#ifdef WITH_RENDERING_TRACE
static void GL_PrintBufferState(FILE* debug_frame_out, int debug_frame_depth)
{
	char label[64];

	if (glBufferState.currentArrayBuffer) {
		GL_TraceObjectLabelGet(GL_BUFFER, glBufferState.currentArrayBuffer, sizeof(label), NULL, label);
		fprintf(debug_frame_out, "%.*s   ArrayBuffer: %u (%s)\n", debug_frame_depth, "                                                          ", glBufferState.currentArrayBuffer, label);
	}
	if (glBufferState.currentElementArrayBuffer) {
		GL_TraceObjectLabelGet(GL_BUFFER, glBufferState.currentElementArrayBuffer, sizeof(label), NULL, label);
		fprintf(debug_frame_out, "%.*s   ElementArray: %u (%s)\n", debug_frame_depth, "                                                          ", glBufferState.currentElementArrayBuffer, label);
	}
}
#endif

void R_Stub_NoOperation(void)
{
}

qbool R_Stub_True(void)
{
	return true;
}

qbool R_Stub_False(void)
{
	return false;
}

uintptr_t R_Stub_BufferZero(r_buffer_id ref)
{
	return 0;
}

void GL_InitialiseBufferHandling(api_buffers_t* api)
{
	qbool buffers_supported = renderer.vaos_supported;

	memset(api, 0, sizeof(*api));

	GL_LoadMandatoryFunctionExtension(glBindBuffer, buffers_supported);
	GL_LoadMandatoryFunctionExtension(glBufferData, buffers_supported);
	GL_LoadMandatoryFunctionExtension(glBufferSubData, buffers_supported);
	GL_LoadMandatoryFunctionExtension(glGenBuffers, buffers_supported);
	GL_LoadMandatoryFunctionExtension(glDeleteBuffers, buffers_supported);
	GL_LoadMandatoryFunctionExtension(glUnmapBuffer, buffers_supported);

	// OpenGL 3.0 onwards, for 4.3+ support only
	if (GL_VersionAtLeast(3, 0)) {
		GL_LoadOptionalFunction(glBindBufferBase);
		GL_LoadOptionalFunction(glBindBufferRange);
	}

	// OpenGL 4.4, persistent mapping of buffers
	tripleBuffer_supported = !COM_CheckParm(cmdline_param_client_notriplebuffering);
	if (SDL_GL_ExtensionSupported("GL_ARB_sync")) {
		GL_LoadMandatoryFunctionExtension(glFenceSync, tripleBuffer_supported);
		GL_LoadMandatoryFunctionExtension(glClientWaitSync, tripleBuffer_supported);
		GL_LoadMandatoryFunctionExtension(glDeleteSync, tripleBuffer_supported);
	}
	else {
		tripleBuffer_supported = false;
	}
	if (SDL_GL_ExtensionSupported("GL_ARB_buffer_storage")) {
		GL_LoadMandatoryFunctionExtension(glBufferStorage, tripleBuffer_supported);
	}
	else {
		tripleBuffer_supported = false;
	}
	if (GL_VersionAtLeast(3, 0)) {
		GL_LoadMandatoryFunctionExtension(glMapBufferRange, tripleBuffer_supported);
	}
	else {
		tripleBuffer_supported = false;
	}

	// OpenGL 4.5 onwards, update directly
	if (GL_UseDirectStateAccess()) {
		GL_LoadOptionalFunction(glNamedBufferSubData);
		GL_LoadOptionalFunction(glNamedBufferData);
		GL_LoadOptionalFunction(glUnmapNamedBuffer);
	}

	tripleBuffer_supported &= buffers_supported;

	api->supported = buffers_supported;
	if (!api->supported) {
		api->FrameReady = R_Stub_True;
		api->InitialiseState = api->Shutdown = R_Stub_NoOperation;
		api->StartFrame = api->EndFrame = R_Stub_NoOperation;
		api->BufferOffset = R_Stub_BufferZero;

		Con_Printf("Using GL buffers: disabled\n");
	}
	else {
		api->Bind = GL_BindBuffer;
		api->BindBase = GL_BindBufferBase;
		api->BindRange = GL_BindBufferRange;
		api->BufferOffset = GL_BufferOffset;
		api->Create = GL_CreateFixedBuffer;

		api->StartFrame = GL_BufferStartFrame;
		api->EndFrame = GL_BufferEndFrame;
		api->FrameReady = GL_BuffersReady;

		api->EnsureSize = GL_EnsureBufferSize;
		api->Resize = GL_ResizeBuffer;

		api->InitialiseState = GL_InitialiseBufferState;
		api->Size = GL_BufferSize;
		api->UnBind = GL_UnBindBuffer;
		api->Update = GL_UpdateBuffer;
		api->UpdateSection = GL_UpdateBufferSection;

		api->Shutdown = GL_BufferShutdown;
		api->SetElementArray = GL_SetElementArrayBuffer;
		api->IsValid = GL_BufferValid;
#ifdef WITH_RENDERING_TRACE
		api->PrintState = GL_PrintBufferState;
#endif
		api->supported = true;

		Con_Printf("Triple-buffering of GL buffers: %s\n", tripleBuffer_supported ? "enabled" : "disabled");
	}
}
