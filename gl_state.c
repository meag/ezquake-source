
// gl_state.c
// State caching for OpenGL

#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "tr_types.h"

typedef void (APIENTRY *glBindTextures_t)(GLuint first, GLsizei count, const GLuint* format);
typedef void (APIENTRY *glBindImageTexture_t)(GLuint unit, GLuint texture, GLint level, GLboolean layered, GLint layer, GLenum access, GLenum format);
static glBindImageTexture_t     qglBindImageTexture;
static glBindTextures_t         qglBindTextures;
glActiveTexture_t               qglActiveTexture;

void GL_BindBuffer(buffer_ref ref);
void GL_SetElementArrayBuffer(buffer_ref buffer);
const buffer_ref null_buffer_reference = { 0 };

#define MAX_LOGGED_TEXTURE_UNITS 32
#define MAX_LOGGED_IMAGE_UNITS   32

typedef struct image_unit_binding_s {
	GLuint texture;
	GLint level;
	GLboolean layered;
	GLint layer;
	GLenum access;
	GLenum format;
} image_unit_binding_t;

static void GL_BindTexture(GLenum targetType, GLuint texnum, qbool warning);

static GLenum currentDepthFunc = GL_LESS;
static double currentNearRange = 0;
static double currentFarRange = 1;
static GLenum currentCullFace = GL_BACK;
static GLenum currentBlendSFactor = GL_ONE;
static GLenum currentBlendDFactor = GL_ZERO;
// FIXME: currentWidth & currentHeight should be initialised to dimensions of window
static GLint currentViewportX = 0, currentViewportY = 0;
static GLsizei currentViewportWidth, currentViewportHeight;
static qbool gl_depthTestEnabled = false;
static qbool gl_framebuffer_srgb = false;
static qbool gl_blend = false;
static qbool gl_cull_face = false;
static qbool gl_line_smooth = false;
static qbool gl_fog = false;
static GLboolean gl_depth_mask = GL_FALSE;
static GLfloat polygonOffsetFactor = 0;
static GLfloat polygonOffsetUnits = 0;
static qbool gl_polygon_offset_line;
static qbool gl_polygon_offset_fill;
static GLenum perspectiveCorrectionHint;
static GLenum polygonMode;
static float clearColor[4];

static GLenum currentTextureUnit = GL_TEXTURE0;
static GLuint bound_textures[MAX_LOGGED_TEXTURE_UNITS];
static GLuint bound_arrays[MAX_LOGGED_TEXTURE_UNITS];
static qbool texunitenabled[MAX_LOGGED_TEXTURE_UNITS];
static GLenum unit_texture_mode[MAX_LOGGED_TEXTURE_UNITS];
static image_unit_binding_t bound_images[MAX_LOGGED_IMAGE_UNITS];

static int old_alphablend_flags = 0;
static void GLC_DisableTextureUnitOnwards(int first);

// vid_common_gl.c
#ifdef WITH_OPENGL_TRACE
static const char* TexEnvName(GLenum mode)
{
	switch (mode) {
	case GL_MODULATE:
		return "GL_MODULATE";
	case GL_REPLACE:
		return "GL_REPLACE";
	case GL_BLEND:
		return "GL_BLEND";
	case GL_DECAL:
		return "GL_DECAL";
	case GL_ADD:
		return "GL_ADD";
	default:
		return "???";
	}
}
#endif

// gl_texture.c
GLuint GL_TextureNameFromReference(texture_ref ref);
GLenum GL_TextureTargetFromReference(texture_ref ref);

static void GL_BindTextureUnitImpl(GLuint unit, texture_ref reference, qbool always_select_unit)
{
	int unit_num = unit - GL_TEXTURE0;
	GLuint texture = GL_TextureNameFromReference(reference);
	GLenum targetType = GL_TextureTargetFromReference(reference);

	if (unit_num >= 0 && unit_num < sizeof(bound_arrays) / sizeof(bound_arrays[0])) {
		if (targetType == GL_TEXTURE_2D_ARRAY) {
			if (bound_arrays[unit_num] == texture) {
				if (always_select_unit) {
					GL_SelectTexture(unit);
				}
				return;
			}
		}
		else if (targetType == GL_TEXTURE_2D) {
			if (bound_textures[unit_num] == texture) {
				if (always_select_unit) {
					GL_SelectTexture(unit);
				}
				return;
			}
		}
	}

	GL_SelectTexture(unit);
	GL_BindTexture(targetType, texture, true);
	return;
}

void GL_EnsureTextureUnitBound(GLuint unit, texture_ref reference)
{
	GL_BindTextureUnitImpl(unit, reference, false);
}

void GL_BindTextureUnit(GLuint unit, texture_ref reference)
{
	GL_BindTextureUnitImpl(unit, reference, true);
}

void GL_DepthFunc(GLenum func)
{
	if (func != currentDepthFunc) {
		glDepthFunc(func);
		currentDepthFunc = func;
	}
}

void GL_DepthRange(double nearVal, double farVal)
{
	if (nearVal != currentNearRange || farVal != currentFarRange) {
		glDepthRange(nearVal, farVal);

		currentNearRange = nearVal;
		currentFarRange = farVal;
	}
}

void GL_CullFace(GLenum mode)
{
	if (mode != currentCullFace) {
		glCullFace(mode);
		currentCullFace = mode;
	}
}

void GL_BlendFunc(GLenum sfactor, GLenum dfactor)
{
	if (sfactor != currentBlendSFactor || dfactor != currentBlendDFactor) {
		glBlendFunc(sfactor, dfactor);
		currentBlendSFactor = sfactor;
		currentBlendDFactor = dfactor;
	}
}

void GL_Viewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
	if (x != currentViewportX || y != currentViewportY || width != currentViewportWidth || height != currentViewportHeight) {
		glViewport(x, y, width, height);

		currentViewportX = x;
		currentViewportY = y;
		currentViewportWidth = width;
		currentViewportHeight = height;
	}
}

void GL_GetViewport(GLint* view)
{
	view[0] = currentViewportX;
	view[1] = currentViewportY;
	view[2] = currentViewportWidth;
	view[3] = currentViewportHeight;
}

void GL_InitialiseState(void)
{
	int i;

	currentDepthFunc = GL_LESS;
	currentNearRange = 0;
	currentFarRange = 1;
	currentCullFace = GL_BACK;
	currentBlendSFactor = GL_ONE;
	currentBlendDFactor = GL_ZERO;
	polygonMode = GL_FILL;
	currentViewportX = 0;
	currentViewportY = 0;
	// FIXME: currentWidth & currentHeight should be initialised to dimensions of window
	currentViewportWidth = 0;
	currentViewportHeight = 0;

	gl_depthTestEnabled = false;
	gl_framebuffer_srgb = false;
	gl_blend = false;
	gl_cull_face = false;
	gl_line_smooth = false;
	gl_fog = false;
	gl_depth_mask = GL_FALSE;
	for (i = 0; i < sizeof(unit_texture_mode) / sizeof(unit_texture_mode[0]); ++i) {
		unit_texture_mode[i] = GL_MODULATE;
	}
	old_alphablend_flags = 0;
	polygonOffsetFactor = polygonOffsetUnits = 0;
	gl_polygon_offset_line = gl_polygon_offset_fill = false;
	perspectiveCorrectionHint = GL_DONT_CARE;
	clearColor[0] = clearColor[1] = clearColor[2] = clearColor[3] = 0;

	GLM_SetIdentityMatrix(GLM_ProjectionMatrix());
	GLM_SetIdentityMatrix(GLM_ModelviewMatrix());

	memset(bound_textures, 0, sizeof(bound_textures));
	memset(bound_arrays, 0, sizeof(bound_arrays));
	memset(bound_images, 0, sizeof(bound_images));
	memset(texunitenabled, 0, sizeof(texunitenabled));

	GL_InitialiseBufferState();
	GL_InitialiseProgramState();
}

// These functions taken from gl_texture.c
static void GL_BindTexture(GLenum targetType, GLuint texnum, qbool warning)
{
	assert(targetType);
	assert(texnum);

#ifdef GL_PARANOIA
	if (warning && !glIsTexture(texnum)) {
		Con_Printf("ERROR: Non-texture %d passed to GL_BindTexture\n", texnum);
		return;
	}

	GL_ProcessErrors("glBindTexture/Prior");
#endif

	if (targetType == GL_TEXTURE_2D) {
		if (bound_textures[currentTextureUnit - GL_TEXTURE0] == texnum) {
			return;
		}

		bound_textures[currentTextureUnit - GL_TEXTURE0] = texnum;
		glBindTexture(GL_TEXTURE_2D, texnum);
		GL_LogAPICall("glBindTexture(unit=GL_TEXTURE%d, target=GL_TEXTURE_2D, texnum=%u[%s])", currentTextureUnit - GL_TEXTURE0, texnum, GL_UseGLSL() ? "" : GL_TextureIdentifierByGLReference(texnum));
	}
	else if (targetType == GL_TEXTURE_2D_ARRAY) {
		if (bound_arrays[currentTextureUnit - GL_TEXTURE0] == texnum) {
			return;
		}

		bound_arrays[currentTextureUnit - GL_TEXTURE0] = texnum;
		glBindTexture(GL_TEXTURE_2D_ARRAY, texnum);
		GL_LogAPICall("glBindTexture(unit=GL_TEXTURE%d, target=GL_TEXTURE_2D_ARRAY, texnum=%u[%s])", currentTextureUnit - GL_TEXTURE0, texnum, GL_UseGLSL() ? "" : GL_TextureIdentifierByGLReference(texnum));
	}
	else {
		// No caching...
		glBindTexture(targetType, texnum);
		GL_LogAPICall("glBindTexture(unit=GL_TEXTURE%d, target=<other>, texnum=%u[%s])", currentTextureUnit - GL_TEXTURE0, texnum, GL_UseGLSL() ? "" : GL_TextureIdentifierByGLReference(texnum));
	}

	++frameStats.texture_binds;

#ifdef GL_PARANOIA
	GL_ProcessErrors("glBindTexture/After");
#endif
}

void GL_SelectTexture(GLenum textureUnit)
{
	if (textureUnit == currentTextureUnit) {
		return;
	}

#ifdef GL_PARANOIA
	GL_ProcessErrors("glActiveTexture/Prior");
#endif
	if (qglActiveTexture) {
		qglActiveTexture(textureUnit);
	}
#ifdef GL_PARANOIA
	GL_ProcessErrors("glActiveTexture/After");
#endif

	currentTextureUnit = textureUnit;
	GL_LogAPICall("glActiveTexture(GL_TEXTURE%d)", textureUnit - GL_TEXTURE0);
}

void GLC_DisableAllTexturing(void)
{
	GLC_DisableTextureUnitOnwards(0);
}

void GLC_EnableTMU(GLenum target)
{
	if (GL_UseImmediateMode()) {
		GL_SelectTexture(target);
		glEnable(GL_TEXTURE_2D);
	}
}

void GLC_DisableTMU(GLenum target)
{
	if (GL_UseImmediateMode()) {
		GL_SelectTexture(target);
		glDisable(GL_TEXTURE_2D);
	}
}

void GLC_EnsureTMUEnabled(GLenum textureUnit)
{
	if (GL_UseImmediateMode()) {
		if (texunitenabled[textureUnit - GL_TEXTURE0]) {
			return;
		}

		GLC_EnableTMU(textureUnit);
	}
}

void GLC_EnsureTMUDisabled(GLenum textureUnit)
{
	if (GL_UseImmediateMode()) {
		if (!texunitenabled[textureUnit - GL_TEXTURE0]) {
			return;
		}

		GLC_DisableTMU(textureUnit);
	}
}

void GL_InitTextureState(void)
{
	// Multi texture.
	currentTextureUnit = GL_TEXTURE0;

	memset(bound_textures, 0, sizeof(bound_textures));
	memset(bound_arrays, 0, sizeof(bound_arrays));
	memset(texunitenabled, 0, sizeof(texunitenabled));
}

void GL_DepthMask(GLboolean mask)
{
	if (mask != gl_depth_mask) {
		glDepthMask(mask);
		GL_LogAPICall("glDepthMask(%s)", mask ? "enabled" : "disabled");

		gl_depth_mask = mask;
	}
}

void GL_TextureEnvModeForUnit(GLenum unit, GLenum mode)
{
	if (GL_UseImmediateMode() && mode != unit_texture_mode[unit - GL_TEXTURE0]) {
		GL_SelectTexture(unit);
		GL_TextureEnvMode(mode);
	}
}

void GL_TextureEnvMode(GLenum mode)
{
	if (GL_UseImmediateMode() && mode != unit_texture_mode[currentTextureUnit - GL_TEXTURE0]) {
		GL_LogAPICall("GL_TextureEnvMode(GL_TEXTURE%d, mode=%s)", currentTextureUnit - GL_TEXTURE0, TexEnvName(mode));
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, mode);
		unit_texture_mode[currentTextureUnit - GL_TEXTURE0] = mode;
	}
}

static void GLC_DisableTextureUnitOnwards(int first)
{
	int i;

	for (i = first; i < sizeof(texunitenabled) / sizeof(texunitenabled[0]); ++i) {
		if (texunitenabled[i]) {
			GLC_EnsureTMUDisabled(GL_TEXTURE0 + i);
		}
	}
	GL_SelectTexture(GL_TEXTURE0);
}

void GLC_InitTextureUnitsNoBind1(GLenum envMode0)
{
	GLC_DisableTextureUnitOnwards(1);
	GL_TextureEnvModeForUnit(GL_TEXTURE0, envMode0);
}

void GLC_InitTextureUnits1(texture_ref texture0, GLenum envMode0)
{
	GLC_DisableTextureUnitOnwards(1);

	GLC_EnsureTMUEnabled(GL_TEXTURE0);
	GL_EnsureTextureUnitBound(GL_TEXTURE0, texture0);
	GL_TextureEnvModeForUnit(GL_TEXTURE0, envMode0);
}

void GLC_InitTextureUnits2(texture_ref texture0, GLenum envMode0, texture_ref texture1, GLenum envMode1)
{
	GLC_DisableTextureUnitOnwards(2);

	GLC_EnsureTMUEnabled(GL_TEXTURE0);
	GL_EnsureTextureUnitBound(GL_TEXTURE0, texture0);
	GL_TextureEnvModeForUnit(GL_TEXTURE0, envMode0);

	GLC_EnsureTMUEnabled(GL_TEXTURE1);
	GL_EnsureTextureUnitBound(GL_TEXTURE1, texture1);
	GL_TextureEnvModeForUnit(GL_TEXTURE1, envMode1);
}

int GL_AlphaBlendFlags(int flags)
{
	if (GL_UseImmediateMode()) {
		if ((flags & GL_ALPHATEST_ENABLED) && !(old_alphablend_flags & GL_ALPHATEST_ENABLED)) {
			glEnable(GL_ALPHA_TEST);
		}
		else if ((flags & GL_ALPHATEST_DISABLED) && (old_alphablend_flags & GL_ALPHATEST_ENABLED)) {
			glDisable(GL_ALPHA_TEST);
		}
		else if (!(flags & (GL_ALPHATEST_ENABLED | GL_ALPHATEST_DISABLED))) {
			flags |= (old_alphablend_flags & GL_ALPHATEST_ENABLED);
		}
	}

	if ((flags & GL_BLEND_ENABLED) && !(old_alphablend_flags & GL_BLEND_ENABLED)) {
		glEnable(GL_BLEND);
	}
	else if (flags & GL_BLEND_DISABLED && (old_alphablend_flags & GL_BLEND_ENABLED)) {
		glDisable(GL_BLEND);
	}
	else if (!(flags & (GL_BLEND_ENABLED | GL_BLEND_DISABLED))) {
		flags |= (old_alphablend_flags & GL_BLEND_ENABLED);
	}

	old_alphablend_flags = flags;
	return old_alphablend_flags;
}

void GL_EnableFog(void)
{
	if (GL_UseImmediateMode() && gl_fogenable.integer) {
		glEnable(GL_FOG);
	}
}

void GL_DisableFog(void)
{
	if (GL_UseImmediateMode() && gl_fogenable.integer) {
		glDisable(GL_FOG);
	}
}

void GL_ConfigureFog(void)
{
	vec3_t colors;

	if (GL_UseGLSL()) {
		// TODO
		return;
	}

	// START shaman BUG fog was out of control when fogstart>fogend {
	if (gl_fogenable.integer && gl_fogstart.value >= 0 && gl_fogstart.value < gl_fogend.value) {
		// } END shaman BUG fog was out of control when fogstart>fogend
		glFogi(GL_FOG_MODE, GL_LINEAR);
		colors[0] = gl_fogred.value;
		colors[1] = gl_foggreen.value;
		colors[2] = gl_fogblue.value;
		glFogfv(GL_FOG_COLOR, colors);
		glFogf(GL_FOG_START, gl_fogstart.value);
		glFogf(GL_FOG_END, gl_fogend.value);
		glEnable(GL_FOG);
	}
	else {
		glDisable(GL_FOG);
	}
}

void GL_EnableWaterFog(int contents)
{
	extern cvar_t gl_waterfog_color_water;
	extern cvar_t gl_waterfog_color_lava;
	extern cvar_t gl_waterfog_color_slime;

	float colors[4];

	// TODO
	if (GL_UseGLSL()) {
		return;
	}

	switch (contents) {
	case CONTENTS_LAVA:
		colors[0] = (float) gl_waterfog_color_lava.color[0] / 255.0;
		colors[1] = (float) gl_waterfog_color_lava.color[1] / 255.0;
		colors[2] = (float) gl_waterfog_color_lava.color[2] / 255.0;
		colors[3] = (float) gl_waterfog_color_lava.color[3] / 255.0;
		break;
	case CONTENTS_SLIME:
		colors[0] = (float) gl_waterfog_color_slime.color[0] / 255.0;
		colors[1] = (float) gl_waterfog_color_slime.color[1] / 255.0;
		colors[2] = (float) gl_waterfog_color_slime.color[2] / 255.0;
		colors[3] = (float) gl_waterfog_color_slime.color[3] / 255.0;
		break;
	default:
		colors[0] = (float) gl_waterfog_color_water.color[0] / 255.0;
		colors[1] = (float) gl_waterfog_color_water.color[1] / 255.0;
		colors[2] = (float) gl_waterfog_color_water.color[2] / 255.0;
		colors[3] = (float) gl_waterfog_color_water.color[3] / 255.0;
		break;
	}

	glFogfv(GL_FOG_COLOR, colors);
	if (((int)gl_waterfog.value) == 2) {
		glFogf(GL_FOG_DENSITY, 0.0002 + (0.0009 - 0.0002) * bound(0, gl_waterfog_density.value, 1));
		glFogi(GL_FOG_MODE, GL_EXP);
	}
	else {
		glFogi(GL_FOG_MODE, GL_LINEAR);
		glFogf(GL_FOG_START, 150.0f);
		glFogf(GL_FOG_END, 4250.0f - (4250.0f - 1536.0f) * bound(0, gl_waterfog_density.value, 1));
	}
	glEnable(GL_FOG);
}

void GL_InvalidateTextureReferences(GLuint texture)
{
	int i;

	// glDeleteTextures(texture) has been called - same reference might be re-used in future
	// If a texture that is currently bound is deleted, the binding reverts to 0 (the default texture)
	for (i = 0; i < sizeof(bound_textures) / sizeof(bound_textures[0]); ++i) {
		if (bound_textures[i] == texture) {
			bound_textures[i] = 0;
		}
		if (bound_arrays[i] == texture) {
			bound_arrays[i] = 0;
		}
	}

	for (i = 0; i < sizeof(bound_images) / sizeof(bound_images[0]); ++i) {
		if (bound_images[i].texture == texture) {
			bound_images[i].texture = 0;
		}
	}
}

void GL_PolygonOffset(int option)
{
	extern cvar_t gl_brush_polygonoffset;
	float factor = (option == POLYGONOFFSET_STANDARD ? 0.05 : 1);
	float units = (option == POLYGONOFFSET_STANDARD ? bound(0, gl_brush_polygonoffset.value, 25.0) : 1);
	qbool enabled = (option == POLYGONOFFSET_STANDARD || option == POLYGONOFFSET_OUTLINES) && units > 0;

	if (enabled) {
		GL_Enable(GL_POLYGON_OFFSET_FILL);
		GL_Enable(GL_POLYGON_OFFSET_LINE);

		if (polygonOffsetFactor != factor || polygonOffsetUnits != units) {
			glPolygonOffset(factor, units);

			polygonOffsetFactor = factor;
			polygonOffsetUnits = units;
		}
	}
	else {
		GL_Disable(GL_POLYGON_OFFSET_FILL);
		GL_Disable(GL_POLYGON_OFFSET_LINE);
	}
}

void GL_Hint(GLenum target, GLenum mode)
{
	if (GL_UseImmediateMode()) {
		if (target == GL_PERSPECTIVE_CORRECTION_HINT) {
			if (mode == perspectiveCorrectionHint) {
				return;
			}

			perspectiveCorrectionHint = mode;
		}

		glHint(target, mode);
	}
}

void GL_BindTextures(GLuint first, GLsizei count, const texture_ref* textures)
{
	int i;

	if (qglBindTextures) {
		GLuint glTextures[MAX_LOGGED_TEXTURE_UNITS];
		qbool already_bound = true;

		count = min(count, MAX_LOGGED_TEXTURE_UNITS);
		for (i = 0; i < count; ++i) {
			glTextures[i] = GL_TextureNameFromReference(textures[i]);
			if (i + first < MAX_LOGGED_TEXTURE_UNITS) {
				GLenum target = GL_TextureTargetFromReference(textures[i]);

				if (target == GL_TEXTURE_2D_ARRAY || glTextures[i] == 0) {
					already_bound &= (bound_arrays[i + first] == glTextures[i]);
					bound_arrays[i + first] = glTextures[i];
				}
				if (target == GL_TEXTURE_2D || glTextures[i] == 0) {
					already_bound &= (bound_textures[i + first] == glTextures[i]);
					bound_textures[i + first] = glTextures[i];
				}
			}
		}

		if (!already_bound) {
			qglBindTextures(first, count, glTextures);
		}
	}
	else {
		for (i = 0; i < count; ++i) {
			if (GL_TextureReferenceIsValid(textures[i])) {
				GL_EnsureTextureUnitBound(GL_TEXTURE0 + first + i, textures[i]);
			}
		}
	}

#ifdef WITH_OPENGL_TRACE
	if (GL_LoggingEnabled())
	{
		static char temp[1024];

		temp[0] = '\0';
		for (i = 0; i < count; ++i) {
			if (i) {
				strlcat(temp, ",", sizeof(temp));
			}
			strlcat(temp, GL_TextureIdentifier(textures[i]), sizeof(temp));
		}
		GL_LogAPICall("glBindTextures(GL_TEXTURE%d, %d[%s])", first, count, temp);
	}
#endif
}

void GL_PolygonMode(GLenum mode)
{
	if (mode != polygonMode) {
		glPolygonMode(GL_FRONT_AND_BACK, mode);
		polygonMode = mode;

		GL_LogAPICall("glPolygonMode(%s)", mode == GL_FILL ? "fill" : (mode == GL_LINE ? "lines" : "???"));
	}
}

// ---

#undef glEnable
#undef glDisable

void GL_Enable(GLenum option)
{
	if (GL_UseGLSL() && option == GL_TEXTURE_2D) {
		Con_Printf("WARNING: glEnable(GL_TEXTURE_2D) called in modern\n");
		return;
	}

#ifdef GL_PARANOIA
	GL_ProcessErrors("glEnable/Prior");
#endif

	if (option == GL_DEPTH_TEST) {
		if (gl_depthTestEnabled) {
			return;
		}

		gl_depthTestEnabled = true;
		GL_LogAPICall("glEnable(GL_DEPTH_TEST)");
	}
	else if (option == GL_FRAMEBUFFER_SRGB) {
		if (gl_framebuffer_srgb) {
			return;
		}

		gl_framebuffer_srgb = true;
		GL_LogAPICall("glEnable(GL_FRAMEBUFFER_SRGB)");
	}
	else if (option == GL_TEXTURE_2D) {
		if (texunitenabled[currentTextureUnit - GL_TEXTURE0]) {
			return;
		}

		texunitenabled[currentTextureUnit - GL_TEXTURE0] = true;
		GL_LogAPICall("glEnable(GL_TEXTURE%u, GL_TEXTURE_2D)", currentTextureUnit - GL_TEXTURE0);
	}
	else if (option == GL_BLEND) {
		if (gl_blend) {
			return;
		}

		gl_blend = true;
		GL_LogAPICall("glEnable(GL_BLEND)");
	}
	else if (option == GL_CULL_FACE) {
		if (gl_cull_face) {
			return;
		}

		gl_cull_face = true;
		GL_LogAPICall("glEnable(GL_CULL_FACE)");
	}
	else if (option == GL_POLYGON_OFFSET_FILL) {
		if (gl_polygon_offset_fill) {
			return;
		}

		gl_polygon_offset_fill = true;
		GL_LogAPICall("glEnable(GL_POLYGON_OFFSET_FILL)");
	}
	else if (option == GL_POLYGON_OFFSET_LINE) {
		if (gl_polygon_offset_line) {
			return;
		}

		gl_polygon_offset_line = true;
		GL_LogAPICall("glEnable(GL_POLYGON_OFFSET_LINE)");
	}
	else if (option == GL_LINE_SMOOTH) {
		if (gl_line_smooth) {
			return;
		}

		gl_line_smooth = true;
		GL_LogAPICall("glEnable(GL_LINE_SMOOTH)");
	}
	else if (option == GL_FOG) {
		if (gl_fog) {
			return;
		}

		gl_fog = true;
		GL_LogAPICall("glEnable(GL_FOG)");
	}

	glEnable(option);
#ifdef GL_PARANOIA
	GL_ProcessErrors("glEnable");
#endif
}

void GL_Disable(GLenum option)
{
	if (GL_UseGLSL() && option == GL_TEXTURE_2D) {
		Con_Printf("WARNING: glDisable(GL_TEXTURE_2D) called in modern\n");
		return;
	}

#ifdef GL_PARANOIA
	GL_ProcessErrors("glDisable/Prior");
#endif

	if (option == GL_DEPTH_TEST) {
		if (!gl_depthTestEnabled) {
			return;
		}

		GL_LogAPICall("glDisable(GL_DEPTH_TEST)");
		gl_depthTestEnabled = false;
	}
	else if (option == GL_FRAMEBUFFER_SRGB) {
		if (!gl_framebuffer_srgb) {
			return;
		}

		GL_LogAPICall("glDisable(GL_FRAMEBUFFER_SRGB)");
		gl_framebuffer_srgb = false;
	}
	else if (option == GL_TEXTURE_2D) {
		if (!texunitenabled[currentTextureUnit - GL_TEXTURE0]) {
			return;
		}

		texunitenabled[currentTextureUnit - GL_TEXTURE0] = false;
		GL_LogAPICall("glDisable(GL_TEXTURE%u, GL_TEXTURE_2D)", currentTextureUnit - GL_TEXTURE0);
	}
	else if (option == GL_BLEND) {
		if (!gl_blend) {
			return;
		}

		GL_LogAPICall("glDisable(GL_BLEND)");
		gl_blend = false;
	}
	else if (option == GL_CULL_FACE) {
		if (!gl_cull_face) {
			return;
		}

		GL_LogAPICall("glDisable(GL_CULL_FACE)");
		gl_cull_face = false;
	}
	else if (option == GL_POLYGON_OFFSET_FILL) {
		if (!gl_polygon_offset_fill) {
			return;
		}

		GL_LogAPICall("glDisable(GL_POLYGON_OFFSET_FILL)");
		gl_polygon_offset_fill = false;
	}
	else if (option == GL_POLYGON_OFFSET_LINE) {
		if (!gl_polygon_offset_line) {
			return;
		}

		GL_LogAPICall("glDisable(GL_POLYGON_OFFSET_LINE)");
		gl_polygon_offset_line = false;
	}
	else if (option == GL_LINE_SMOOTH) {
		if (!gl_line_smooth) {
			return;
		}

		gl_line_smooth = false;
		GL_LogAPICall("glDisable(GL_LINE_SMOOTH)");
	}
	else if (option == GL_FOG) {
		if (!gl_fog) {
			return;
		}

		gl_fog = false;
		GL_LogAPICall("glDisable(GL_FOG)");
	}

	glDisable(option);
}

#undef glBegin

static int glcVertsPerPrimitive = 0;
static int glcBaseVertsPerPrimitive = 0;
static int glcVertsSent = 0;
static const char* glcPrimitiveName = "?";

void GL_Begin(GLenum primitive)
{
	if (GL_UseGLSL()) {
		return;
	}

	glcVertsSent = 0;
	glcVertsPerPrimitive = 0;
	glcBaseVertsPerPrimitive = 0;
	glcPrimitiveName = "?";

	switch (primitive) {
	case GL_QUADS:
		glcVertsPerPrimitive = 4;
		glcBaseVertsPerPrimitive = 0;
		glcPrimitiveName = "GL_QUADS";
		break;
	case GL_POLYGON:
		glcVertsPerPrimitive = 0;
		glcBaseVertsPerPrimitive = 0;
		glcPrimitiveName = "GL_POLYGON";
		break;
	case GL_TRIANGLE_FAN:
		glcVertsPerPrimitive = 1;
		glcBaseVertsPerPrimitive = 2;
		glcPrimitiveName = "GL_TRIANGLE_FAN";
		break;
	case GL_TRIANGLE_STRIP:
		glcVertsPerPrimitive = 1;
		glcBaseVertsPerPrimitive = 2;
		glcPrimitiveName = "GL_TRIANGLE_STRIP";
		break;
	case GL_LINE_LOOP:
		glcVertsPerPrimitive = 1;
		glcBaseVertsPerPrimitive = 1;
		glcPrimitiveName = "GL_LINE_LOOP";
		break;
	case GL_LINES:
		glcVertsPerPrimitive = 2;
		glcBaseVertsPerPrimitive = 0;
		glcPrimitiveName = "GL_LINES";
		break;
	}

	++frameStats.draw_calls;
	glBegin(primitive);
	//GL_LogAPICall("GL_Begin()...");
}

#undef glEnd

void GL_End(void)
{
#ifdef WITH_OPENGL_TRACE
	int primitives;
	const char* count_name = "vertices";
#endif

	if (GL_UseGLSL()) {
		return;
	}

	glEnd();

#ifdef WITH_OPENGL_TRACE
	primitives = max(0, glcVertsSent - glcBaseVertsPerPrimitive);
	if (glcVertsPerPrimitive) {
		primitives = glcVertsSent / glcVertsPerPrimitive;
		count_name = "primitives";
	}
	GL_LogAPICall("glBegin/End(%s: %d %s)", glcPrimitiveName, primitives, count_name);
#endif
}

#undef glVertex2f
#undef glVertex3f
#undef glVertex3fv

void GL_Vertex2f(GLfloat x, GLfloat y)
{
	glVertex2f(x, y);
	++glcVertsSent;
}

void GL_Vertex3f(GLfloat x, GLfloat y, GLfloat z)
{
	glVertex3f(x, y, z);
	++glcVertsSent;
}

void GL_Vertex3fv(const GLfloat* v)
{
	glVertex3fv(v);
	++glcVertsSent;
}

void GL_BindImageTexture(GLuint unit, texture_ref texture, GLint level, GLboolean layered, GLint layer, GLenum access, GLenum format)
{
	GLuint glRef = 0;

	if (GL_TextureReferenceIsValid(texture)) {
		glRef = GL_TextureNameFromReference(texture);

		if (unit < MAX_LOGGED_IMAGE_UNITS) {
			if (bound_images[unit].texture == glRef && bound_images[unit].level == level && bound_images[unit].layered == layered && bound_images[unit].layer == layer && bound_images[unit].access == access && bound_images[unit].format == format) {
				return;
			}

			bound_images[unit].texture = glRef;
			bound_images[unit].level = level;
			bound_images[unit].layered = layered;
			bound_images[unit].layer = layer;
			bound_images[unit].access = access;
			bound_images[unit].format = format;
		}
	}
	else {
		if (unit < MAX_LOGGED_IMAGE_UNITS) {
			memset(&bound_images[unit], 0, sizeof(bound_images[unit]));
		}
	}

	qglBindImageTexture(unit, glRef, level, layered, layer, access, format);
}

#ifdef WITH_OPENGL_TRACE
void GL_PrintState(FILE* debug_frame_out)
{
	int i;

	if (debug_frame_out) {
		fprintf(debug_frame_out, "... <state-dump>\n");
		fprintf(debug_frame_out, "..... Z-Buffer: %s, func %u range %f=>%f [mask %s]\n", gl_depthTestEnabled ? "enabled" : "disabled", currentDepthFunc, currentNearRange, currentFarRange, gl_depth_mask ? "on" : "off");
		fprintf(debug_frame_out, "..... Cull-face: %s, mode %u\n", gl_cull_face ? "enabled" : "disabled", currentCullFace);
		fprintf(debug_frame_out, "..... Blending: %s, sfactor %u, dfactor %u\n", gl_blend ? "enabled" : "disabled", currentBlendSFactor, currentBlendDFactor);
		fprintf(debug_frame_out, "..... Texturing: %s, tmu %d [", texunitenabled[currentTextureUnit - GL_TEXTURE0] ? "enabled" : "disabled", currentTextureUnit - GL_TEXTURE0);
		for (i = 0; i < sizeof(texunitenabled) / sizeof(texunitenabled[0]); ++i) {
			fprintf(debug_frame_out, "%s%s", i ? "," : "", texunitenabled[i] ? TexEnvName(unit_texture_mode[i]) : "n");
		}
		fprintf(debug_frame_out, "]\n");
		fprintf(debug_frame_out, "..... glPolygonMode: %s\n", polygonMode == GL_FILL ? "fill" : polygonMode == GL_LINE ? "line" : "???");
		fprintf(debug_frame_out, "... </state-dump>\n");
	}
}
#endif

void GL_ClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
	if (r != clearColor[0] || g != clearColor[1] || b != clearColor[2] || a != clearColor[3]) {
		glClearColor(r, g, b, a);
		clearColor[0] = r;
		clearColor[1] = g;
		clearColor[2] = b;
		clearColor[3] = a;
	}
}

qbool GLM_LoadStateFunctions(void)
{
	qbool all_available = true;

	GL_LoadMandatoryFunctionExtension(glActiveTexture, all_available);
	GL_LoadMandatoryFunctionExtension(glBindImageTexture, all_available);

	// 4.4 - binds textures to consecutive texture units
	GL_LoadOptionalFunction(glBindTextures);

	return all_available;
}

void GL_CheckMultiTextureExtensions(void)
{
	extern cvar_t gl_maxtmu2;

	if (!COM_CheckParm(cmdline_param_client_nomultitexturing) && SDL_GL_ExtensionSupported("GL_ARB_multitexture")) {
		if (strstr(gl_renderer, "Savage")) {
			return;
		}
		qglMultiTexCoord2f = SDL_GL_GetProcAddress("glMultiTexCoord2fARB");
		if (!qglActiveTexture) {
			qglActiveTexture = SDL_GL_GetProcAddress("glActiveTextureARB");
		}
		qglClientActiveTexture = SDL_GL_GetProcAddress("glClientActiveTexture");
		if (!qglMultiTexCoord2f || !qglActiveTexture || !qglClientActiveTexture) {
			return;
		}
		Com_Printf_State(PRINT_OK, "Multitexture extensions found\n");
		gl_mtexable = true;

		qglBindTextures = SDL_GL_GetProcAddress("glBindTextures");
	}

	gl_textureunits = min(glConfig.texture_units, 4);

	if (COM_CheckParm(cmdline_param_client_maximum2textureunits) /*|| !strcmp(gl_vendor, "ATI Technologies Inc.")*/ || gl_maxtmu2.value) {
		gl_textureunits = min(gl_textureunits, 2);
	}

	if (gl_textureunits < 2) {
		gl_mtexable = false;
	}

	if (!gl_mtexable) {
		gl_textureunits = 1;
	}
	else {
		Com_Printf_State(PRINT_OK, "Enabled %i texture units on hardware\n", gl_textureunits);
	}
}

void GLC_ClientActiveTexture(GLenum texture_unit)
{
	if (qglClientActiveTexture) {
		qglClientActiveTexture(texture_unit);
	}
	else {
		assert(texture_unit == GL_TEXTURE0);
	}
}
