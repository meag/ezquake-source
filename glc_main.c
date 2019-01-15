
#include "quakedef.h"
#include "r_renderer.h"
#include "r_buffers.h"
#include "glc_vao.h"
#include "r_brushmodel.h"
#include "r_aliasmodel.h"
#include "tr_types.h"

cvar_t gl_program_sky = { "gl_program_sky", "1" };
cvar_t gl_program_turbsurfaces = { "gl_program_turbsurfaces", "1" };
cvar_t gl_program_aliasmodels = { "gl_program_aliasmodels", "1", CVAR_LATCH };

void GL_Init(void);
qbool GLC_InitialiseVAOHandling(void);
void GL_InitialiseBufferHandling(api_buffers_t* buffers);
void GL_InitialiseState(void);

static void GLC_NoOperation(void)
{
}

static void GLC_BindVertexArrayElementBuffer(r_vao_id vao, buffer_ref ref)
{
}

static void GLC_TextureLabelSetNull(texture_ref texture, const char* name)
{
}

qbool GLC_False(void)
{
	return false;
}

static void GLC_ScreenDrawStart(void)
{
	GL_FramebufferScreenDrawStart();
}

static void GLC_PostProcessScreen(void)
{
	GL_FramebufferPostProcessScreen();
}

static void GLC_Begin2DRendering(void)
{
	GL_Framebuffer2DSwitch();
}

#define GLC_PrintGfxInfo                   GL_PrintGfxInfo
#define GLC_Viewport                       GL_Viewport
#define GLC_RenderDynamicLightmaps         R_RenderDynamicLightmaps
#define GLC_InvalidateLightmapTextures     GLC_NoOperation
#define GLC_LightmapFrameInit              GLC_NoOperation
#define GLC_LightmapShutdown               GLC_NoOperation
#define GLC_ClearRenderingSurface          GL_Clear
#define GLC_EnsureFinished                 GL_EnsureFinished
#define GLC_RenderSceneBlur                GLC_RenderSceneBlurDo
#define GLC_RenderView                     GLC_NoOperation
#define GLC_Screenshot                     GL_Screenshot
#define GLC_InitialiseVAOState             GL_InitialiseVAOState
#define GLC_DescriptiveString              GL_DescriptiveString
#define GLC_Draw3DSprites                  GLC_NoOperation
#define GLC_Prepare3DSprites               GLC_NoOperation
#define GLC_IsFramebufferEnabled3D         GL_FramebufferEnabled3D
#define GLC_TextureDelete                  GL_TextureDelete
#define GLC_TextureMipmapGenerate          GL_TextureMipmapGenerate
#define GLC_TextureWrapModeClamp           GL_TextureWrapModeClamp
#ifdef WITH_RENDERING_TRACE
#define GLC_TextureLabelSet                GL_TextureLabelSet
#else
#define GLC_TextureLabelSet                GLC_TextureLabelSetNull
#endif
#define GLC_TextureUnitBind                GL_EnsureTextureUnitBound
#define GLC_TextureGet                     GL_TextureGet
#define GLC_TextureCompressionSet          GL_TextureCompressionSet
#define GLC_TextureCreate2D                GL_TextureCreate2D
#define GLC_TextureUnitMultiBind           GL_TextureUnitMultiBind
#define GLC_TexturesCreate                 GL_TexturesCreate
#define GLC_TextureReplaceSubImageRGBA     GL_TextureReplaceSubImageRGBA
#define GLC_TextureSetAnisotropy           GL_TextureSetAnisotropy
#define GLC_TextureSetFiltering            GL_TextureSetFiltering
#define GLC_TextureLoadCubemapFace         GL_TextureLoadCubemapFace
#define GLC_CvarForceRecompile             GL_CvarForceRecompile
#define GLC_ProgramsInitialise             GL_ProgramsInitialise
#define GLC_ProgramsShutdown               GL_ProgramsShutdown
#define GLC_FramebufferCreate              GL_FramebufferCreate

#define RENDERER_METHOD(returntype, name, ...) \
{ \
	extern returntype GLC_ ## name(__VA_ARGS__); \
	renderer.name = GLC_ ## name; \
}

void GLC_Initialise(void)
{
	#include "r_renderer_structure.h"

	if (!host_initialized) {
		Cvar_SetCurrentGroup(CVAR_GROUP_OPENGL);
		Cvar_Register(&gl_program_sky);
		Cvar_Register(&gl_program_turbsurfaces);
		Cvar_ResetCurrentGroup();
	}

	Cvar_SetCurrentGroup(CVAR_GROUP_OPENGL);
	Cvar_Register(&gl_program_aliasmodels);
	Cvar_ResetCurrentGroup();

	GL_Init();
	renderer.vaos_supported = GLC_InitialiseVAOHandling();
	GL_ProcessErrors("post-GLC_InitialiseVAOHandling");
	GL_InitialiseBufferHandling(&buffers);
	GL_ProcessErrors("post-GL_InitialiseBufferHandling");
	GL_InitialiseState();
	GL_ProcessErrors("post-GL_InitialiseState");

	if (!GL_Supported(R_SUPPORT_RENDERING_SHADERS)) {
		Cvar_LatchedSetValue(&gl_program_aliasmodels, 0);
		Cvar_SetValue(&gl_program_sky, 0);
		Cvar_SetValue(&gl_program_turbsurfaces, 0);
		Cvar_SetFlags(&gl_program_aliasmodels, Cvar_GetFlags(&gl_program_aliasmodels) | CVAR_ROM);
		Cvar_SetFlags(&gl_program_sky, Cvar_GetFlags(&gl_program_sky) | CVAR_ROM);
		Cvar_SetFlags(&gl_program_turbsurfaces, Cvar_GetFlags(&gl_program_turbsurfaces) | CVAR_ROM);
		Con_Printf("&cf00ERROR&r: GLSL programs not supported.\n");
	}
	else {
		Cvar_SetFlags(&gl_program_aliasmodels, Cvar_GetFlags(&gl_program_aliasmodels) & ~CVAR_ROM);
		Cvar_SetFlags(&gl_program_sky, Cvar_GetFlags(&gl_program_sky) & ~CVAR_ROM);
		Cvar_SetFlags(&gl_program_turbsurfaces, Cvar_GetFlags(&gl_program_turbsurfaces) & ~CVAR_ROM);
	}
}

void GLC_PrepareModelRendering(qbool vid_restart)
{
	if (buffers.supported) {
		buffer_ref instance_vbo = R_CreateInstanceVBO();

		R_CreateAliasModelVBO(instance_vbo);
		R_BrushModelCreateVBO(instance_vbo);
	}

	renderer.ProgramsInitialise();
}
