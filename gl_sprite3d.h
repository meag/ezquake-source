
#ifndef EZQUAKE_GL_SPRITE3D_HEADER
#define EZQUAKE_GL_SPRITE3D_HEADER

// Billboards
typedef enum {
	SPRITE3D_ENTITIES,
	SPRITE3D_PARTICLES_CLASSIC,
	SPRITE3D_PARTICLES_NEW_p_spark,
	SPRITE3D_PARTICLES_NEW_p_smoke,
	SPRITE3D_PARTICLES_NEW_p_fire,
	SPRITE3D_PARTICLES_NEW_p_bubble,
	SPRITE3D_PARTICLES_NEW_p_lavasplash,
	SPRITE3D_PARTICLES_NEW_p_gunblast,
	SPRITE3D_PARTICLES_NEW_p_chunk,
	SPRITE3D_PARTICLES_NEW_p_shockwave,
	SPRITE3D_PARTICLES_NEW_p_inferno_flame,
	SPRITE3D_PARTICLES_NEW_p_inferno_trail,
	SPRITE3D_PARTICLES_NEW_p_sparkray,
	SPRITE3D_PARTICLES_NEW_p_staticbubble,
	SPRITE3D_PARTICLES_NEW_p_trailpart,
	SPRITE3D_PARTICLES_NEW_p_dpsmoke,
	SPRITE3D_PARTICLES_NEW_p_dpfire,
	SPRITE3D_PARTICLES_NEW_p_teleflare,
	SPRITE3D_PARTICLES_NEW_p_blood1,
	SPRITE3D_PARTICLES_NEW_p_blood2,
	SPRITE3D_PARTICLES_NEW_p_blood3,
	//VULT PARTICLES
	SPRITE3D_PARTICLES_NEW_p_rain,
	SPRITE3D_PARTICLES_NEW_p_alphatrail,
	SPRITE3D_PARTICLES_NEW_p_railtrail,
	SPRITE3D_PARTICLES_NEW_p_streak,
	SPRITE3D_PARTICLES_NEW_p_streaktrail,
	SPRITE3D_PARTICLES_NEW_p_streakwave,
	SPRITE3D_PARTICLES_NEW_p_lightningbeam,
	SPRITE3D_PARTICLES_NEW_p_vxblood,
	SPRITE3D_PARTICLES_NEW_p_lavatrail,
	SPRITE3D_PARTICLES_NEW_p_vxsmoke,
	SPRITE3D_PARTICLES_NEW_p_vxsmoke_red,
	SPRITE3D_PARTICLES_NEW_p_muzzleflash,
	SPRITE3D_PARTICLES_NEW_p_inferno,
	SPRITE3D_PARTICLES_NEW_p_2dshockwave,
	SPRITE3D_PARTICLES_NEW_p_vxrocketsmoke,
	SPRITE3D_PARTICLES_NEW_p_trailbleed,
	SPRITE3D_PARTICLES_NEW_p_bleedspike,
	SPRITE3D_PARTICLES_NEW_p_flame,
	SPRITE3D_PARTICLES_NEW_p_bubble2,
	SPRITE3D_PARTICLES_NEW_p_bloodcloud,
	SPRITE3D_PARTICLES_NEW_p_chunkdir,
	SPRITE3D_PARTICLES_NEW_p_smallspark,
	SPRITE3D_PARTICLES_NEW_p_slimeglow,
	SPRITE3D_PARTICLES_NEW_p_slimebubble,
	SPRITE3D_PARTICLES_NEW_p_blacklavasmoke,
	SPRITE3D_FLASHBLEND_LIGHTS,
	SPRITE3D_CORONATEX_STANDARD,
	SPRITE3D_CORONATEX_GUNFLASH,
	SPRITE3D_CORONATEX_EXPLOSIONFLASH1,
	SPRITE3D_CORONATEX_EXPLOSIONFLASH2,
	SPRITE3D_CORONATEX_EXPLOSIONFLASH3,
	SPRITE3D_CORONATEX_EXPLOSIONFLASH4,
	SPRITE3D_CORONATEX_EXPLOSIONFLASH5,
	SPRITE3D_CORONATEX_EXPLOSIONFLASH6,
	SPRITE3D_CORONATEX_EXPLOSIONFLASH7,
	SPRITE3D_CHATICON_AFK_CHAT,
	SPRITE3D_CHATICON_CHAT,
	SPRITE3D_CHATICON_AFK,

	MAX_SPRITE3D_BATCHES
} sprite3d_batch_id;

typedef struct gl_sprite3d_vert_s {
	float position[3];
	float tex[3];
	GLubyte color[4];
} gl_sprite3d_vert_t;

void GL_Sprite3DInitialiseBatch(sprite3d_batch_id type, GLenum blendSource, GLenum blendDestination, texture_ref texture, int index, GLenum primitive_type, qbool depthTest, qbool depthMask);
gl_sprite3d_vert_t* GL_Sprite3DAddEntry(sprite3d_batch_id type, int verts_required);
gl_sprite3d_vert_t* GL_Sprite3DAddEntrySpecific(sprite3d_batch_id type, int verts_required, texture_ref texture, int index);
void GL_Sprite3DSetVert(gl_sprite3d_vert_t* vert, float x, float y, float z, float s, float t, GLubyte color[4], int texture_index);
void GL_Draw3DSprites(void);
void GLM_RenderSprite(gl_sprite3d_vert_t* vert, vec3_t origin, vec3_t up, vec3_t right, float scale, float s, float t, int index);

#endif // #ifndef EZQUAKE_GL_SPRITE3D_HEADER
