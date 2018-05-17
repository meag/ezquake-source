
// fonts.c
#ifdef EZ_FREETYPE_SUPPORT

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "fonts.h"

GLuint GL_TextureNameFromReference(texture_ref ref);

typedef struct glyphinfo_s {
	float offsets[2];
	int sizes[2];
	float advance[2];
	qbool loaded;
} glyphinfo_t;

static glyphinfo_t glyphs[4096];
static float max_glyph_width;
static float max_num_glyph_width;
static qbool outline_fonts = false;
static int outline_width = 2;

#define FONT_TEXTURE_SIZE 1024
charset_t proportional_fonts[MAX_CHARSETS];

typedef struct gradient_def_s {
	byte top_color[3];
	byte bottom_color[3];
	float gradient_start;
} gradient_def_t;

static gradient_def_t standard_gradient = { { 255, 255, 255 }, { 107, 98, 86 }, 0.2f };
static gradient_def_t brown_gradient = { { 120, 82, 35 },{ 75, 52, 22 }, 0.0f };
static gradient_def_t numbers_gradient = { { 255, 255, 150 }, { 218, 132, 7 }, 0.2f };

static void FontSetColor(byte* color, byte alpha, gradient_def_t* gradient, float relative_y)
{
	float base_color[3];

	if (relative_y <= gradient->gradient_start) {
		VectorScale(gradient->top_color, 1.0f / 255, base_color);
	}
	else {
		float mix = (relative_y - gradient->gradient_start) / (1.0f - gradient->gradient_start);

		VectorScale(gradient->top_color, (1.0f - mix) / 255.0f, base_color);
		VectorMA(base_color, mix / 255.0f, gradient->bottom_color, base_color);
	}

	color[0] = min(1, base_color[0]) * alpha;
	color[1] = min(1, base_color[1]) * alpha;
	color[2] = min(1, base_color[2]) * alpha;
	color[3] = alpha;
}

// Very simple: boost the alpha of every pixel to strongest alpha nearby
// - small adjustment made for distance
// - not a great solution but works for the moment
// - can supply matrix to freetype, should use that instead
static void SimpleOutline(byte* image_buffer, int base_font_width, int base_font_height)
{
	int x, y;
	int xdiff, ydiff;
	byte* font_buffer = Q_malloc(base_font_width * base_font_height * 4);
	const int search_distance = outline_width;

	memcpy(font_buffer, image_buffer, base_font_width * base_font_height * 4);
	for (x = 0; x < base_font_width; ++x) {
		for (y = 0; y < base_font_height; ++y) {
			int base = (x + y * base_font_width) * 4;
			float best_distance = -1;
			int best_alpha = 0;

			if (font_buffer[base + 3] == 255) {
				continue;
			}

			for (xdiff = max(0, x - search_distance); xdiff <= min(x + search_distance, base_font_width - 1); ++xdiff) {
				for (ydiff = max(0, y - search_distance); ydiff <= min(y + search_distance, base_font_height - 1); ++ydiff) {
					float dist = abs(x - xdiff) + abs(y - ydiff);
					int this_alpha = font_buffer[(xdiff + ydiff * base_font_width) * 4 + 3] / (0.3 * (dist + 1));

					if (this_alpha >= best_alpha) {
						best_alpha = min(255, this_alpha);
					}
				}
			}

			image_buffer[base + 3] = best_alpha;
		}
	}

	Q_free(font_buffer);
}

static void FontLoadBitmap(int ch, FT_Face face, int base_font_width, int base_font_height, byte* image_buffer, gradient_def_t* gradient)
{
	byte* font_buffer;
	int x, y;

	glyphs[ch].loaded = true;
	glyphs[ch].advance[0] = ((face->glyph->metrics.horiAdvance / 64.0f) + (outline_fonts ? 2 * outline_width : 0)) / (base_font_width / 2);
	glyphs[ch].advance[1] = ((face->glyph->metrics.vertAdvance / 64.0f) + (outline_fonts ? 2 * outline_width : 0)) / (base_font_height / 2);
	glyphs[ch].offsets[0] = face->glyph->metrics.horiBearingX / 64.0f - (outline_fonts ? outline_width : 0);
	glyphs[ch].offsets[1] = face->glyph->metrics.horiBearingY / 64.0f - (outline_fonts ? outline_width : 0);
	glyphs[ch].sizes[0] = face->glyph->bitmap.width;
	glyphs[ch].sizes[1] = face->glyph->bitmap.rows;

	max_glyph_width = max(max_glyph_width, glyphs[ch].advance[0] * 8);
	if ((ch >= '0' && ch <= '9') || ch == '+' || ch == '-') {
		max_num_glyph_width = max(max_num_glyph_width, glyphs[ch].advance[0] * 8);
	}

	font_buffer = face->glyph->bitmap.buffer;
	for (y = 0; y < face->glyph->bitmap.rows && y + outline_width < base_font_height / 2; ++y, font_buffer += face->glyph->bitmap.pitch) {
		for (x = 0; x < face->glyph->bitmap.width && x + outline_width < base_font_width / 2; ++x) {
			int base = (x + outline_width + (y + outline_width) * base_font_width) * 4;
			byte alpha = font_buffer[x];

			FontSetColor(&image_buffer[base], alpha, gradient, y * 1.0f / face->glyph->bitmap.rows);
		}
	}

	if (outline_fonts) {
		SimpleOutline(image_buffer, base_font_width, base_font_height);
	}
}

void FontCreate(int grouping, const char* path)
{
	FT_Library library;
	FT_Error error;
	FT_Face face;
	int ch;
	byte* temp_buffer;
	byte* full_buffer;
	byte color_brown[4] = { 100, 64, 24, 255 };
	byte color_numbers[4] = { 227, 224, 130, 255 };
	int original_width, original_height, original_left, original_top;
	int texture_width, texture_height;
	int base_font_width, base_font_height;
	int baseline_offset;
	qbool draw_outline = outline_fonts;
	charset_t* charset;

	error = FT_Init_FreeType(&library);
	if (error) {
		Con_Printf("Error during freetype initialisation\n");
		return;
	}

	error = FT_New_Face(library, path, 0, &face);
	if (error == FT_Err_Unknown_File_Format) {
		Con_Printf("Font file found, but format is unsupported\n");
		return;
	}
	else if (error) {
		Con_Printf("Font file could not be opened\n");
		return;
	}

	charset = &proportional_fonts[grouping];
	if (GL_TextureReferenceIsValid(charset->master)) {
		original_width = GL_TextureWidth(charset->master);
		original_height = GL_TextureHeight(charset->master);
		original_left = 0;
		original_top = 0;
		texture_width = original_width;
		texture_height = original_height;
	}
	else {
		original_width = texture_width = FONT_TEXTURE_SIZE * 2;
		original_height = texture_height = FONT_TEXTURE_SIZE * 2;
		original_left = original_top = 0;
		GL_CreateTexturesWithIdentifier(GL_TEXTURE0, GL_TEXTURE_2D, 1, &charset->master, "font");
		GL_TexStorage2D(GL_TEXTURE0, charset->master, 1, GL_RGBA8, texture_width, texture_height);
		GL_TexParameterf(GL_TEXTURE0, charset->master, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		GL_TexParameterf(GL_TEXTURE0, charset->master, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		GL_TexParameteri(GL_TEXTURE0, charset->master, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		GL_TexParameteri(GL_TEXTURE0, charset->master, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	base_font_width = texture_width / 16;
	base_font_height = texture_height / 16;
	baseline_offset = 0;

	memset(glyphs, 0, sizeof(glyphs));
	max_glyph_width = max_num_glyph_width = 0;

	FT_Set_Pixel_Sizes(
		face,
		base_font_width / 2 - 2 * outline_width,
		base_font_height / 2 - 2 * outline_width
	);

	temp_buffer = full_buffer = Q_malloc(4 * base_font_width * base_font_height * 256);
	for (ch = 18; ch < 128; ++ch, temp_buffer += 4 * base_font_width * base_font_height) {
		FT_UInt glyph_index;
		int offset128 = 4 * base_font_width * base_font_height * 128;

		if (ch >= 28 && ch < 32) {
			continue;
		}

		if (ch < 32) {
			glyph_index = FT_Load_Char(face, '0' + (ch - 18), FT_LOAD_RENDER);
		}
		else {
			glyph_index = FT_Load_Char(face, ch, FT_LOAD_RENDER);
		}

		if (glyph_index) {
			continue;
		}

		error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
		if (error) {
			continue;
		}

		if (ch < 32) {
			FontLoadBitmap(ch, face, base_font_width, base_font_height, temp_buffer, &numbers_gradient);
			FontLoadBitmap(ch + 128, face, base_font_width, base_font_height, temp_buffer + offset128, &numbers_gradient);
		}
		else {
			FontLoadBitmap(ch, face, base_font_width, base_font_height, temp_buffer, &standard_gradient);
			FontLoadBitmap(ch + 128, face, base_font_width, base_font_height, temp_buffer + offset128, &brown_gradient);
		}
	}
	FT_Done_FreeType(library);

	// Work out where the baseline is...
	{
		int max_beneath = 0;
		int beneath_baseline;

		for (ch = 18; ch < 128; ++ch) {
			if (!glyphs[ch].loaded) {
				continue;
			}

			beneath_baseline = glyphs[ch].sizes[1] - glyphs[ch].offsets[1];
			max_beneath = max(max_beneath, beneath_baseline);
		}

		baseline_offset = base_font_height / 2 - 1 - max_beneath - outline_width;
	}

	// Update charset image
	temp_buffer = full_buffer;
	memset(charset->glyphs, 0, sizeof(charset->glyphs));
	for (ch = 18; ch < 256; ++ch, temp_buffer += 4 * base_font_width * base_font_height) {
		int xbase = (ch % 16) * base_font_width;
		int ybase = (ch / 16) * base_font_height;
		int yoffset = max(0, baseline_offset - glyphs[ch].offsets[1]);

		if (!glyphs[ch].loaded) {
			continue;
		}

		if (yoffset) {
			memmove(temp_buffer + yoffset * base_font_width * 4, temp_buffer, (base_font_height - yoffset) * base_font_width * 4);
			memset(temp_buffer, 0, yoffset * base_font_width * 4);
		}

		glyphs[ch].offsets[0] /= base_font_width;

		charset->glyphs[ch].width = base_font_width / 2;
		charset->glyphs[ch].height = base_font_height / 2;
		charset->glyphs[ch].sl = (original_left + xbase) * 1.0f / texture_width;
		charset->glyphs[ch].tl = (original_top + ybase) * 1.0f / texture_height;
		charset->glyphs[ch].sh = charset->glyphs[ch].sl + 0.5f * base_font_width / texture_width;
		charset->glyphs[ch].th = charset->glyphs[ch].tl + 0.5f * base_font_height / texture_height;
		charset->glyphs[ch].texnum = charset->master;

		GL_TexSubImage2D(
			GL_TEXTURE0, charset->master, 0,
			original_left + xbase,
			original_top + ybase,
			base_font_width,
			base_font_height,
			GL_RGBA, GL_UNSIGNED_BYTE, temp_buffer
		);
	}
	Q_free(full_buffer);

	CachePics_MarkAtlasDirty();
}

qbool FontAlterCharCoordsWide(int* x, int* y, wchar ch, qbool bigchar, float scale, qbool proportional)
{
	int char_size = (bigchar ? 64 : 8);

	// Totally off screen.
	if (*y <= (-char_size * scale)) {
		return false;
	}

	// Space.
	if (ch == 32) {
		*x += (proportional ? FontCharacterWidthWide(ch) : 8) * scale;
		return false;
	}

	if (ch <= sizeof(glyphs) / sizeof(glyphs[0]) && glyphs[ch].loaded) {
		*x += glyphs[ch].offsets[0] * char_size * scale;
	}

	return true;
}

void FontAdvanceCharCoordsWide(int* x, int* y, wchar ch, qbool bigchar, float scale, int char_gap)
{
	if (bigchar) {
		*x += 64 * scale + char_gap;
	}
	else if (ch < sizeof(glyphs) / sizeof(glyphs[0]) && glyphs[ch].loaded) {
		*x += ceil(8 * (glyphs[ch].advance[0] - glyphs[ch].offsets[0])) * scale + char_gap;
	}
	else {
		*x += 8 * scale + char_gap;
	}
}

float FontCharacterWidthWide(wchar ch)
{
	if (ch < sizeof(glyphs) / sizeof(glyphs[0]) && glyphs[ch].loaded) {
		return ceil(8 * glyphs[ch].advance[0]);
	}
	else {
		return 8;
	}
}

qbool FontAlterCharCoords(int* x, int* y, char ch_, qbool bigchar, float scale, qbool proportional)
{
	int char_size = (bigchar ? 64 : 8);
	unsigned char ch = (unsigned char)ch_;

	// Totally off screen.
	if (*y <= (-char_size * scale)) {
		return false;
	}

	// Space.
	if (ch == 32) {
		*x += (proportional ? FontCharacterWidthWide(ch) : 8) * scale;
		return false;
	}

	if (proportional && !bigchar && ch <= sizeof(glyphs) / sizeof(glyphs[0]) && glyphs[ch].loaded) {
		*x += glyphs[ch].offsets[0] * char_size * scale;
	}

	return true;
}

void FontAdvanceCharCoords(int* x, int* y, char ch_, qbool bigchar, float scale, int char_gap, qbool proportional)
{
	unsigned char ch = (unsigned char)ch_;

	if (bigchar) {
		*x += 64 * scale + char_gap;
	}
	else if (proportional && ch < sizeof(glyphs) / sizeof(glyphs[0]) && glyphs[ch].loaded) {
		*x += ceil(8 * (glyphs[ch].advance[0] - glyphs[ch].offsets[0])) * scale + char_gap;
	}
	else {
		*x += 8 * scale + char_gap;
	}
}

float FontCharacterWidth(char ch_, qbool proportional)
{
	unsigned char ch = (unsigned char)ch_;

	if (proportional && ch < sizeof(glyphs) / sizeof(glyphs[0]) && glyphs[ch].loaded) {
		return 8 * glyphs[ch].advance[0];
	}
	else {
		return 8;
	}
}

// Used for allocating space - if we just measure the string then other hud elements
//   might move around as content changes, which is probably not what is wanted
int FontFixedWidth(int max_length, float scale, qbool digits_only, qbool proportional)
{
	if (!proportional || !GL_TextureReferenceIsValid(proportional_fonts[0].master)) {
		return max_length * 8 * scale;
	}

	return (int)((digits_only ? max_num_glyph_width : max_glyph_width) * max_length * scale + 0.5f);
}

void FontInitialise(void)
{
	
}

#endif // EZ_FREETYPE_SUPPORT
