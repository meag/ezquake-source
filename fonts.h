
#ifndef EZQUAKE_FONTS_HEADER
#define EZQUAKE_FONTS_HEADER

float FontCharacterWidthWide(wchar ch, float scale, qbool proportional);
qbool FontAlterCharCoordsWide(float* x, float* y, wchar ch, qbool bigchar, float scale, qbool proportional);
//void FontAdvanceCharCoordsWide(int* x, float* y, wchar ch, qbool bigchar, float scale, int char_gap);

float FontCharacterWidth(char ch, qbool proportional);
int FontFixedWidth(int max_length, float scale, qbool digits_only, qbool proportional);
qbool FontAlterCharCoords(float* x, float* y, char ch, qbool bigchar, float scale, qbool proportional);
//void FontAdvanceCharCoords(int* x, int* y, char ch, qbool bigchar, float scale, int char_gap, qbool proportional);

void FontCreate(int grouping, const char* path);

#endif
