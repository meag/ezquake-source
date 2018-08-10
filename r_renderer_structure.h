
// Meta
RENDERER_METHOD(void, Shutdown, qbool restart)
RENDERER_METHOD(void, CvarForceRecompile, cvar_t* cvar)
RENDERER_METHOD(void, PrintGfxInfo, void)
RENDERER_METHOD(const char*, DescriptiveString, void)

// Config/State
RENDERER_METHOD(void, Viewport, int x, int y, int width, int height)
RENDERER_METHOD(void, ApplyRenderingState, r_state_id state)

// Called after map has been loaded
RENDERER_METHOD(void, PrepareModelRendering, qbool vid_restart)
RENDERER_METHOD(void, PrepareAliasModel, model_t* model, aliashdr_t* hdr)

// Sky surfaces
RENDERER_METHOD(qbool, LoadSkyboxTextures, const char* name)
RENDERER_METHOD(void, DrawSky, void)
RENDERER_METHOD(void, DrawWorld, void)

// Entities
RENDERER_METHOD(void, DrawAliasFrame, entity_t* ent, model_t* model, int pose1, int pose2, texture_ref texture, texture_ref fb_texture, qbool outline, int effects, int render_effects)
RENDERER_METHOD(void, DrawAlias3Model, entity_t *ent)
RENDERER_METHOD(void, DrawAliasModelShadow, entity_t* ent)
RENDERER_METHOD(void, DrawAliasModelPowerupShell, entity_t *ent)
RENDERER_METHOD(void, DrawAlias3ModelPowerupShell, entity_t *ent)
RENDERER_METHOD(void, DrawSpriteModel, entity_t *ent)
RENDERER_METHOD(void, DrawSimpleItem, model_t* model, int skin, vec3_t origin, float scale, vec3_t up, vec3_t right)

// Particles
RENDERER_METHOD(void, DrawClassicParticles, int)

// HUD
RENDERER_METHOD(void, DrawImage, float x, float y, float width, float height, float tex_s, float tex_t, float tex_width, float tex_height, byte* color, int flags)
RENDERER_METHOD(void, DrawRectangle, float x, float y, float width, float height, byte* color)
RENDERER_METHOD(void, AdjustImages, int first, int last, float x_offset)
RENDERER_METHOD(void, DrawDisc, void)
RENDERER_METHOD(void, DrawVelocity3D, void)

// Lightmaps
RENDERER_METHOD(void, UploadLightmap, int textureUnit, int lightmapnum)
RENDERER_METHOD(void, LightmapFrameInit, void)
RENDERER_METHOD(void, RenderDynamicLightmaps, msurface_t* surf, qbool world)
RENDERER_METHOD(void, CreateLightmapTextures, void)
RENDERER_METHOD(void, BuildLightmap, int lightmapnum)
RENDERER_METHOD(void, InvalidateLightmapTextures, void)
RENDERER_METHOD(void, LightmapShutdown, void)

// Rendering loop
RENDERER_METHOD(void, SetupGL, void)
RENDERER_METHOD(void, ChainBrushModelSurfaces, model_t* model)
RENDERER_METHOD(void, DrawBrushModel, entity_t* ent, qbool polygonOffset, qbool caustics)
RENDERER_METHOD(int, BrushModelCopyVertToBuffer, model_t* mod, void* vbo_buffer_, int position, float* source, int lightmap, int material, float scaleS, float scaleT, msurface_t* surf, qbool has_luma_texture)
RENDERER_METHOD(void, ClearRenderingSurface, qbool clear_color)
RENDERER_METHOD(void, DrawWaterSurfaces, void)
RENDERER_METHOD(void, ScreenDrawStart, void)
RENDERER_METHOD(void, EnsureFinished, void)
RENDERER_METHOD(void, Begin2DRendering, void)
RENDERER_METHOD(qbool, IsFramebufferEnabled3D, void)

// Pre-processing
RENDERER_METHOD(void, ConfigureFog, int contents)

// Post-processing (scene)
RENDERER_METHOD(void, RenderSceneBlur, float alpha)
RENDERER_METHOD(void, RenderView, void)
RENDERER_METHOD(void, PreRenderView, void)

// Post-processing (screen)
RENDERER_METHOD(void, PostProcessScreen, void)
RENDERER_METHOD(void, PolyBlend, float v_blend[4])

// Performance
RENDERER_METHOD(void, TimeRefresh, void)

// Misc
RENDERER_METHOD(void, Screenshot, byte* buffer, size_t size)

// Textures
RENDERER_METHOD(void, TextureInitialiseState, void)
RENDERER_METHOD(void, TextureDelete, texture_ref texture)
RENDERER_METHOD(void, TextureMipmapGenerate, texture_ref texture)
RENDERER_METHOD(void, TextureWrapModeClamp, texture_ref tex)
RENDERER_METHOD(void, TextureLabelSet, texture_ref texnum, const char* identifier)
RENDERER_METHOD(void, TextureUnitBind, int unit, texture_ref texture)

// VAOs
RENDERER_METHOD(void, DeleteVAOs, void)
RENDERER_METHOD(void, GenVertexArray, r_vao_id vao, const char* name)
RENDERER_METHOD(void, BindVertexArray, r_vao_id vao)
RENDERER_METHOD(void, BindVertexArrayElementBuffer, r_vao_id vao, buffer_ref ref)
RENDERER_METHOD(qbool, VertexArrayCreated, r_vao_id vao)

// Sprites
RENDERER_METHOD(void, Prepare3DSprites, void)
RENDERER_METHOD(void, Draw3DSprites, void)
RENDERER_METHOD(void, Draw3DSpritesInline, void)   // FIXME get rid of this and all other inline rendering
