#version 430

#ezquake-definitions

layout(location = 0) in vec2 inPositionTL;
layout(location = 1) in vec2 inPositionBR;
layout(location = 2) in vec2 inTexCoordTL;
layout(location = 3) in vec2 inTexCoordBR;
layout(location = 4) in vec4 inColour;
layout(location = 5) in int inFlags;

out vec4 gPositionTL;
out vec4 gPositionBR;
out vec2 gTexCoordTL;
out vec2 gTexCoordBR;
out vec4 gColour;
out float gAlphaTest;

void main()
{
	gl_Position = gPositionTL = vec4(inPositionTL, 0, 1);
	gPositionBR = vec4(inPositionBR, 0, 1);
	gTexCoordTL = inTexCoordTL;
	gTexCoordBR = inTexCoordBR;
	gColour = inColour;
	if ((inFlags & 4) != 0) {
		gAlphaTest = r_alphafont != 0 ? 0 : 1;
	}
	else {
		gAlphaTest = (inFlags & 2);
	}
}
