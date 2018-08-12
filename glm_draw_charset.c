
#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "fonts.h"
#include "tr_types.h"

extern charset_t char_textures[MAX_CHARSETS];
extern int char_mapping[MAX_CHARSETS];

static GLubyte cache_currentColor[4];
static qbool nextCharacterIsCrosshair = false;

void Draw_SetCrosshairTextMode(qbool enabled)
{
	nextCharacterIsCrosshair = enabled;
}

static void Draw_TextCacheInit(float x, float y, float scale)
{
	memcpy(cache_currentColor, color_white, sizeof(cache_currentColor));
}

static void Draw_TextCacheSetColor(GLubyte* color)
{
	memcpy(cache_currentColor, color, sizeof(cache_currentColor));
}

static float Draw_TextCacheAddCharacter(float x, float y, wchar ch, float scale, qbool proportional)
{
	int new_charset = (ch & 0xFF00) >> 8;
	charset_t* texture = &char_textures[0];
	mpic_t* pic;

	if (proportional) {
		extern charset_t proportional_fonts[MAX_CHARSETS];

		if (GL_TextureReferenceIsValid(proportional_fonts[new_charset].master) && GL_TextureReferenceIsValid(proportional_fonts[new_charset].glyphs[ch & 0xFF].texnum)) {
			texture = &proportional_fonts[new_charset];
		}
		else {
			proportional = false;
		}
	}

	if (!proportional) {
		int slot = 0;

		if (new_charset) {
			slot = ((ch >> 8) & 0xFF);
			if (!char_mapping[slot]) {
				slot = 0;
				ch = '?';
			}
		}

		texture = &char_textures[slot];
	}

	ch &= 0xFF;	// Only use the first byte.

	pic = &texture->glyphs[ch];
	GLM_DrawImage(x, y, scale * 8, scale * 8, pic->sl, pic->tl, pic->sh - pic->sl, pic->th - pic->tl, cache_currentColor, false, pic->texnum, true, nextCharacterIsCrosshair);

	return FontCharacterWidth(ch, proportional) * scale;
}

// x, y					= Pixel position of char.
// num					= The character to draw.
// scale				= The scale of the character.
// apply_overall_alpha	= Should the overall alpha for all drawing apply to this char?
// color				= Color!
// bigchar				= Draw this char using the big character charset.
// gl_statechange		= Change the gl state before drawing?
float GLM_Draw_CharacterBase(float x, float y, wchar num, float scale, qbool apply_overall_alpha, byte color[4], qbool bigchar, qbool gl_statechange, qbool proportional)
{
	int char_size = (bigchar ? 64 : 8);

	if (bigchar) {
		mpic_t *p = Draw_CachePicSafe(MCHARSET_PATH, false, true);

		if (p) {
			int sx = 0;
			int sy = 0;
			int char_width = (p->width / 8);
			int char_height = (p->height / 8);
			char c = (char)(num & 0xFF);

			Draw_GetBigfontSourceCoords(c, char_width, char_height, &sx, &sy);

			if (sx >= 0) {
				// Don't apply alpha here, since we already applied it above.
				Draw_SAlphaSubPic(x, y, p, sx, sy, char_width, char_height, (((float)char_size / char_width) * scale), 1);
			}

			return 64 * scale;
		}

		// TODO : Force players to have mcharset.png or fallback to overscaling normal font? :s
	}

	return Draw_TextCacheAddCharacter(x, y, num, scale, proportional);
}

void GLM_Draw_ResetCharGLState(void)
{
	//GL_AlphaBlendFlags(GL_ALPHATEST_ENABLED | GL_BLEND_DISABLED);
	//GL_TextureEnvMode(GL_REPLACE);
	Draw_TextCacheSetColor(color_white);
}

void GLM_Draw_SetColor(byte* rgba)
{
	extern cvar_t scr_coloredText;

	if (scr_coloredText.integer) {
		Draw_TextCacheSetColor(rgba);
	}
}

void GLM_Draw_StringBase_StartString(int x, int y, float scale)
{
	Draw_TextCacheInit(x, y, scale);
}
