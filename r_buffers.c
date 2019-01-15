
#include "quakedef.h"
#include "r_local.h"
#include "r_buffers.h"

api_buffers_t buffers;
const buffer_ref null_buffer_reference = { 0 };

buffer_ref R_CreateInstanceVBO(void)
{
	unsigned int values[MAX_STANDARD_ENTITIES];
	int i;

	for (i = 0; i < MAX_STANDARD_ENTITIES; ++i) {
		values[i] = i;
	}

	return buffers.Create(buffertype_vertex, "instance#", sizeof(values), values, bufferusage_constant_data);
}
