#version 430

uniform vec4 color;
uniform sampler2DArray materialTex;
uniform bool apply_texture;

in vec3 TextureCoord;
out vec4 frag_colour;

void main()
{
	vec4 texColor;

	if (apply_texture) {
		texColor = texture(materialTex, TextureCoord);
		frag_colour = texColor * color;
	}
	else {
		frag_colour = color;
	}
}
