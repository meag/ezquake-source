#version 430

#ezquake-definitions

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 tex;
layout(location = 2) in vec2 lightmapCoord;
layout(location = 3) in vec2 detailCoord;
layout(location = 4) in int lightmapNumber;
layout(location = 5) in int materialNumber;
layout(location = 6) in int _instanceId;
layout(location = 7) in int vboFlags;
layout(location = 8) in vec3 flatColor;
layout(location = 9) in int surfaceNumber;

out vec3 TexCoordLightmap;
out vec3 TextureCoord;
#if defined(DRAW_LUMA_TEXTURES) || defined(DRAW_LUMA_TEXTURES_FB)
out vec3 LumaCoord;
#endif
#ifdef DRAW_DETAIL_TEXTURES
out vec2 DetailCoord;
#endif
out vec3 FlatColor;
out flat int Flags;
out vec3 Direction;

layout(std140, binding=EZQ_GL_BINDINGPOINT_BRUSHMODEL_DRAWDATA) buffer WorldCvars {
	WorldDrawInfo drawInfo[];
};
layout(std140, binding=EZQ_GL_BINDINGPOINT_BRUSHMODEL_SAMPLERS) buffer SamplerMappingsBuffer {
	SamplerMapping samplerMapping[];
};

void main()
{
	float materialArrayIndex = samplerMapping[drawInfo[_instanceId].samplerBase + materialNumber].layer;
	int drawCallFlags = drawInfo[_instanceId].drawFlags;
	int textureFlags = samplerMapping[drawInfo[_instanceId].samplerBase + materialNumber].flags;

	gl_Position = projectionMatrix * drawInfo[_instanceId].mvMatrix * vec4(position, 1.0);

	FlatColor = flatColor;
	Flags = vboFlags | drawCallFlags | textureFlags;

	if (lightmapNumber < 0) {
		TextureCoord = vec3(tex, materialArrayIndex);
		TexCoordLightmap = vec3(0, 0, 0);
		Direction = (position - cameraPosition);
#if defined(DRAW_SKYBOX)
		Direction = Direction.xzy;
#endif
#ifdef DRAW_DETAIL_TEXTURES
		DetailCoord = vec2(0, 0);
#endif
#if defined(DRAW_LUMA_TEXTURES) || defined(DRAW_LUMA_TEXTURES_FB)
		LumaCoord = TextureCoord;
#endif
	}
	else {
#ifdef DRAW_TEXTURELESS
		TextureCoord = vec3(0, 0, materialArrayIndex);
#else
		TextureCoord = vec3(tex, materialArrayIndex);
#endif

#if defined(DRAW_LUMA_TEXTURES) || defined(DRAW_LUMA_TEXTURES_FB)
		LumaCoord = (Flags & EZQ_SURFACE_HAS_LUMA) == EZQ_SURFACE_HAS_LUMA ? vec3(TextureCoord.st, TextureCoord.z + 1) : TextureCoord;
#endif
		TexCoordLightmap = vec3(lightmapCoord, lightmapNumber);
#ifdef DRAW_DETAIL_TEXTURES
		DetailCoord = detailCoord;
#endif
	}
}
