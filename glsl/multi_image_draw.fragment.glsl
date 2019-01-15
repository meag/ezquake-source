#version 430

uniform sampler2D tex;

in vec2 TextureCoord;
in vec4 Colour;
in float AlphaTest;
in float UseTexture;

out vec4 frag_colour;

void main()
{
	vec4 texColor;

	if (UseTexture != 0) {
		texColor = texture(tex, TextureCoord);
		if (AlphaTest != 0 && texColor.a < 1) {
			discard;
		}
		frag_colour = texColor * Colour;
	}
	else {
		frag_colour = Colour;
	}
}
