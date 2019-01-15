/*
Copyright (C) 2001-2002       A Nourai

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the included (GNU.txt) GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#ifndef EZQUAKE_GL_TEXTURE_H
#define EZQUAKE_GL_TEXTURE_H

#include "r_texture.h"
#include "r_texture_internal.h"

// External API
void GL_EnsureTextureUnitBound(int unit, texture_ref texture);
void GL_TextureMipmapGenerate(texture_ref texture);
void GL_TextureAnistropyChanged(texture_ref texture);
void GL_TextureGet(texture_ref tex, int buffer_size, byte* buffer);
void GL_TextureWrapModeClamp(texture_ref tex);
void GL_UploadTexture(texture_ref texture, int mode, int width, int height, byte* newdata);
void GL_ReplaceSubImageRGBA(texture_ref ref, int offsetx, int offsety, int width, int height, byte* buffer);

// Internal
qbool GLM_TextureAllocateArrayStorage(gltexture_t* slot, int minimum_depth, int* depth);
void GL_AllocateStorage(gltexture_t* texture);
void GL_AllocateTextureNames(gltexture_t* glt);

#endif // EZQUAKE_GL_TEXTURE_H
