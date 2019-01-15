#version 430

uniform sampler2D materialTex;
uniform float alpha;

in vec2 TextureCoord;
out vec4 frag_colour;

void main()
{
	vec4 texColor;

	texColor = texture(materialTex, TextureCoord);
	texColor.a = alpha;
	frag_colour = texColor;
}
