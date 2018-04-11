/*
Copyright (C) 2011 ezQuake team

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __GL_FRAMEBUFFER_H__
#define __GL_FRAMEBUFFER_H__

typedef struct framebuffer_ref_s {
	int index;
} framebuffer_ref;

void GL_InitialiseFramebufferHandling(void);
framebuffer_ref GL_FramebufferCreate(GLsizei width, GLsizei height, qbool depthBuffer);
void GL_FramebufferDelete(framebuffer_ref* pref);
void GL_FramebufferStartUsing(framebuffer_ref ref);
void GL_FramebufferStopUsing(framebuffer_ref ref);
texture_ref GL_FramebufferTextureReference(framebuffer_ref ref, int index);

extern const framebuffer_ref null_framebuffer_ref;

#define GL_FramebufferReferenceIsValid(x) ((x).index)
#define GL_FramebufferReferenceInvalidate(ref) { (ref).index = 0; }
#define GL_FramebufferReferenceEqual(ref1, ref2) ((ref1).index == (ref2).index)
#define GL_FramebufferReferenceCompare(ref1, ref2) ((ref1).index < (ref2).index ? -1 : (ref1).index > (ref2).index ? 1 : 0)

#endif // __GL_FRAMEBUFFER_H__
