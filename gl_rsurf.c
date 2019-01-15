/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// r_surf.c: surface-related refresh code

#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "rulesets.h"
#include "utils.h"

static void GL_EmitSurfaceParticleEffects(msurface_t* s);

msurface_t	*skychain = NULL;
msurface_t	**skychain_tail = &skychain;

msurface_t	*waterchain = NULL;

msurface_t	*alphachain = NULL;
msurface_t	**alphachain_tail = &alphachain;

typedef void(*chain_surf_func)(msurface_t** chain_head, msurface_t* surf);

void chain_surfaces_by_lightmap(msurface_t** chain_head, msurface_t* surf)
{
	msurface_t* current = *chain_head;

	while (current) {
		if (surf->lightmaptexturenum > current->lightmaptexturenum) {
			chain_head = &(current->texturechain);
			current = *chain_head;
			continue;
		}

		break;
	}

	surf->texturechain = current;
	*chain_head = surf;
}

// Order by lightmap# then by floor/ceiling... seems faster to switch colour than GL_Bind(lightmap tex)
void chain_surfaces_drawflat(msurface_t** chain_head, msurface_t* surf)
{
	msurface_t* current = *chain_head;
	int surf_order = (surf->flags & SURF_DRAWFLAT_FLOOR ? 1 : 0) + max(surf->lightmaptexturenum, 0) * 2;

	while (current) {
		int current_order = (current->flags & SURF_DRAWFLAT_FLOOR ? 1 : 0) + max(current->lightmaptexturenum, 0) * 2;

		if (surf_order > current_order) {
			chain_head = &(current->drawflatchain);
			current = *chain_head;
			continue;
		}

		break;
	}

	surf->drawflatchain = current;
	*chain_head = surf;
}

#define CHAIN_SURF_F2B(surf, chain_tail)		\
	{											\
		*(chain_tail) = (surf);					\
		(chain_tail) = &(surf)->texturechain;	\
		(surf)->texturechain = NULL;			\
	}

#define CHAIN_SURF_B2F(surf, chain) 			\
	{											\
		(surf)->texturechain = (chain);			\
		(chain) = (surf);						\
	}

#define CHAIN_RESET(chain)			\
{								\
	chain = NULL;				\
	chain##_tail = &chain;		\
}

glpoly_t *caustics_polys = NULL;
glpoly_t *detail_polys = NULL;

extern cvar_t gl_textureless; //Qrack

// mark all surfaces so ALL light maps will reload in R_RenderDynamicLightmaps()
static void R_ForceReloadLightMaps(void)
{
	model_t	*m;
	int i, j;

	Com_DPrintf("forcing of reloading all light maps!\n");

	for (j = 1; j < MAX_MODELS; j++)
	{
		if (!(m = cl.model_precache[j]))
			break;

		if (m->name[0] == '*')
			continue;

		for (i = 0; i < m->numsurfaces; i++)
		{
			m->surfaces[i].cached_dlight = true; // kinda hack, so we force reload light map
		}
	}
}

qbool R_FullBrightAllowed(void)
{
	return r_fullbright.value && r_refdef2.allow_cheats;
}

void R_Check_R_FullBright(void)
{
	static qbool allowed;

	// not changed, nothing to do
	if( allowed == R_FullBrightAllowed() )
		return;

	// ok, it changed, lets update all our light maps...
	allowed = R_FullBrightAllowed();
	R_ForceReloadLightMaps();
}

//Returns the proper texture for a given time and base texture
texture_t *R_TextureAnimation (texture_t *base) {
	int relative, count;

	if (currententity->frame) {
		if (base->alternate_anims)
			base = base->alternate_anims;
	}

	if (!base->anim_total)
		return base;

	relative = (int) (r_refdef2.time * 10) % base->anim_total;

	count = 0;	
	while (base->anim_min > relative || base->anim_max <= relative) {
		base = base->anim_next;
		if (!base)
			Host_Error ("R_TextureAnimation: broken cycle");
		if (++count > 100)
			Host_Error ("R_TextureAnimation: infinite cycle");
	}

	return base;
}

void R_DrawWaterSurfaces(void)
{
	if (!waterchain) {
		return;
	}

	GL_EnterRegion("R_DrawWaterSurfaces");
	if (GL_ShadersSupported()) {
		GLM_DrawWaterSurfaces();
	}
	else {
		GLC_DrawWaterSurfaces();
	}
	GL_LeaveRegion();

	waterchain = NULL;
}

void R_ClearTextureChains(model_t *clmodel) {
	int i, waterline;
	texture_t *texture;

	GLC_ClearTextureChains();

	for (i = 0; i < clmodel->numtextures; i++) {
		if ((texture = clmodel->textures[i])) {
			for (waterline = 0; waterline < 2; waterline++) {
				texture->texturechain[waterline] = NULL;
				texture->texturechain_tail[waterline] = &texture->texturechain[waterline];
			}
		}
	}
	clmodel->drawflat_chain[0] = clmodel->drawflat_chain[1] = NULL;

	r_notexture_mip->texturechain[0] = NULL;
	r_notexture_mip->texturechain_tail[0] = &r_notexture_mip->texturechain[0];
	r_notexture_mip->texturechain[1] = NULL;
	r_notexture_mip->texturechain_tail[1] = &r_notexture_mip->texturechain[1];

	CHAIN_RESET(skychain);
	if (clmodel == cl.worldmodel) {
		waterchain = NULL;
	}
	CHAIN_RESET(alphachain);
}

void OnChange_r_drawflat (cvar_t *var, char *value, qbool *cancel) {
	char *p;
	qbool progress = false;


	p = Info_ValueForKey (cl.serverinfo, "status");
	progress = (strstr (p, "left")) ? true : false;

	if (cls.state >= ca_connected && progress && !r_refdef2.allow_cheats && !cl.spectator) {
		Com_Printf ("%s changes are not allowed during the match.\n", var->name);
		*cancel = true;
		return;
	}
}

void R_RecursiveWorldNode(mnode_t *node, int clipflags)
{
	float wateralpha = bound((1 - r_refdef2.max_watervis), r_wateralpha.value, 1);
	extern cvar_t r_fastturb;

	int c, side, clipped, underwater;
	mplane_t *plane, *clipplane;
	msurface_t *surf, **mark;
	mleaf_t *pleaf;
	float dot;
	qbool drawFlatFloors = (r_drawflat.integer == 2 || r_drawflat.integer == 1);
	qbool drawFlatWalls = (r_drawflat.integer == 3 || r_drawflat.integer == 1);
	qbool solidTexTurb = (!r_fastturb.integer && wateralpha == 1);

	if (node->contents == CONTENTS_SOLID || node->visframe != r_visframecount) {
		return;
	}
	for (c = 0, clipplane = frustum; c < 4; c++, clipplane++) {
		if (!(clipflags & (1 << c))) {
			continue;	// don't need to clip against it
		}

		clipped = BOX_ON_PLANE_SIDE(node->minmaxs, node->minmaxs + 3, clipplane);
		if (clipped == 2) {
			return;
		}
		else if (clipped == 1) {
			clipflags &= ~(1 << c);	// node is entirely on screen
		}
	}

	// if a leaf node, draw stuff
	if (node->contents < 0) {
		pleaf = (mleaf_t *)node;

		mark = pleaf->firstmarksurface;
		c = pleaf->nummarksurfaces;

		if (c) {
			do {
				(*mark)->visframe = r_framecount;
				mark++;
			} while (--c);
		}

		// deal with model fragments in this leaf
		if (pleaf->efrags) {
			R_StoreEfrags(&pleaf->efrags);
		}

		return;
	}

	// node is just a decision point, so go down the apropriate sides

	// find which side of the node we are on
	plane = node->plane;

	dot = PlaneDiff(modelorg, plane);
	side = (dot >= 0) ? 0 : 1;

	// recurse down the children, front side first
	R_RecursiveWorldNode(node->children[side], clipflags);

	// draw stuff
	c = node->numsurfaces;

	if (c) {
		qbool turbSurface;

		surf = cl.worldmodel->surfaces + node->firstsurface;

		if (dot < -BACKFACE_EPSILON) {
			side = SURF_PLANEBACK;
		}
		else if (dot > BACKFACE_EPSILON) {
			side = 0;
		}

		for (; c; c--, surf++) {
			if (surf->visframe != r_framecount) {
				continue;
			}

			if ((dot < 0) ^ !!(surf->flags & SURF_PLANEBACK)) {
				continue;		// wrong side
			}

			// add surf to the right chain
			turbSurface = (surf->flags & SURF_DRAWTURB);
			if (surf->flags & SURF_DRAWSKY) {
				if (r_fastsky.integer || GL_ShadersSupported()) {
					chain_surfaces_drawflat(&cl.worldmodel->drawflat_chain[0], surf);
				}
				if (!r_fastsky.integer) {
					chain_surfaces_by_lightmap(&skychain, surf);
				}
			}
			else if (turbSurface) {
				if (r_fastturb.integer) {
					chain_surfaces_drawflat(&cl.worldmodel->drawflat_chain[0], surf);
				}
				else if (solidTexTurb && GL_ShadersSupported()) {
					chain_surfaces_by_lightmap(&surf->texinfo->texture->texturechain[0], surf);
				}
				else {
					chain_surfaces_by_lightmap(&waterchain, surf);
				}
				GL_EmitSurfaceParticleEffects(surf);
			}
			else if (!turbSurface && (surf->flags & SURF_DRAWALPHA)) {
				CHAIN_SURF_B2F(surf, alphachain);
			}
			else {
				underwater = 0;
				if (GL_TextureReferenceIsValid(underwatertexture) && gl_caustics.value && (surf->flags & SURF_UNDERWATER)) {
					underwater = 1;
				}

				if (drawFlatFloors && (surf->flags & SURF_DRAWFLAT_FLOOR)) {
					chain_surfaces_drawflat(&cl.worldmodel->drawflat_chain[underwater], surf);
				}
				else if (drawFlatWalls && !(surf->flags & SURF_DRAWFLAT_FLOOR)) {
					chain_surfaces_drawflat(&cl.worldmodel->drawflat_chain[underwater], surf);
				}
				else {
					chain_surfaces_by_lightmap(&surf->texinfo->texture->texturechain[underwater], surf);
				}
			}
		}
	}
	// recurse down the back side
	R_RecursiveWorldNode(node->children[!side], clipflags);
}

void R_CreateWorldTextureChains(void)
{
	if (r_speeds.integer) {
		glFinish ();

		memset(&frameStats, 0, sizeof(frameStats));

		frameStats.start_time = Sys_DoubleTime ();
	}

	if (cl.worldmodel) {
		R_ClearTextureChains(cl.worldmodel);

		VectorCopy(r_refdef.vieworg, modelorg);

		//set up texture chains for the world
		R_RecursiveWorldNode(cl.worldmodel->nodes, 15);

		R_RenderAllDynamicLightmaps(cl.worldmodel);
	}
}

void R_DrawWorld(void)
{
	entity_t ent;
	extern cvar_t gl_outline;

	memset(&ent, 0, sizeof(ent));
	ent.model = cl.worldmodel;

	VectorCopy(r_refdef.vieworg, modelorg);

	currententity = &ent;

	//draw the world sky
	R_DrawSky();

	if (cl_firstpassents.count) {
		GL_EnterRegion("Entities-1st");
		R_DrawEntitiesOnList(&cl_firstpassents);
		GL_LeaveRegion();
	}

	if (GL_ShadersSupported()) {
		GL_EnterRegion("DrawWorld");
		GLM_DrawTexturedWorld(cl.worldmodel);
		GL_LeaveRegion();
	}
	else {
		GLC_DrawWorld();
	}
}

void R_MarkLeaves (void) {
	byte *vis;
	mnode_t *node;
	int i;
	byte solid[MAX_MAP_LEAFS/8];

	if (!r_novis.value && r_oldviewleaf == r_viewleaf
		&& r_oldviewleaf2 == r_viewleaf2)	// watervis hack
		return;

	r_visframecount++;
	r_oldviewleaf = r_viewleaf;

	if (r_novis.value) {
		vis = solid;
		memset (solid, 0xff, (cl.worldmodel->numleafs + 7) >> 3);
	} else {
		vis = Mod_LeafPVS (r_viewleaf, cl.worldmodel);

		if (r_viewleaf2) {
			int			i, count;
			unsigned	*src, *dest;

			// merge visibility data for two leafs
			count = (cl.worldmodel->numleafs + 7) >> 3;
			memcpy (solid, vis, count);
			src = (unsigned *) Mod_LeafPVS (r_viewleaf2, cl.worldmodel);
			dest = (unsigned *) solid;
			count = (count + 3) >> 2;
			for (i = 0; i < count; i++)
				*dest++ |= *src++;
			vis = solid;
		}
	}
		
	for (i = 0; i < cl.worldmodel->numleafs; i++)	{
		if (vis[i >> 3] & (1 << (i & 7))) {
			node = (mnode_t *)&cl.worldmodel->leafs[i + 1];
			do {
				if (node->visframe == r_visframecount)
					break;
				node->visframe = r_visframecount;
				node = node->parent;
			} while (node);
		}
	}
}

static void GL_EmitSurfaceParticleEffects(msurface_t* s)
{
#define ESHADER(eshadername)  extern void eshadername (vec3_t org)
	extern void EmitParticleEffect(msurface_t *fa, void (*fun)(vec3_t nv));
	extern cvar_t tei_lavafire, tei_slime;

	ESHADER(FuelRodExplosion);//green mushroom explosion
	ESHADER(ParticleFire);//torch fire
	ESHADER(ParticleFirePool);//lavapool alike fire
	ESHADER(VX_DeathEffect);//big white spark explosion
	ESHADER(VX_GibEffect);//huge red blood cloud
	ESHADER(VX_DetpackExplosion);//cool huge explosion
	ESHADER(VX_Implosion);//TODO
	ESHADER(VX_TeslaCharge);
	ESHADER(ParticleSlime);
	ESHADER(ParticleSlimeHarcore);
	ESHADER(ParticleBloodPool);
	ESHADER(ParticleSlimeBubbles); //HyperNewbie particles init
	ESHADER(ParticleSlimeGlow);
	ESHADER(ParticleSmallerFirePool);
	ESHADER(ParticleLavaSmokePool);

	if (!tei_lavafire.integer && !tei_slime.integer) {
		return;
	}

	//Tei "eshaders".
	if (s->texinfo && s->texinfo->texture && s->texinfo->texture->name[0]) {
		switch (s->texinfo->texture->name[1]) {
			//Lava
		case 'l':
		case 'L':
		{
			switch (tei_lavafire.integer) {
			case 1:
				//Tei lavafire, normal
				EmitParticleEffect(s, ParticleFire);
				break;
			case 2:
				//Tei lavafire HARDCORE
				EmitParticleEffect(s, ParticleFirePool);
				//Tei redblood smoke
				EmitParticleEffect(s, ParticleBloodPool);
				break;
			case 3:
				//HyperNewbie's smokefire
				EmitParticleEffect(s, ParticleSmallerFirePool);
				EmitParticleEffect(s, ParticleLavaSmokePool);
				break;
			case 4:
				EmitParticleEffect(s, ParticleSmallerFirePool);
				EmitParticleEffect(s, ParticleLavaSmokePool);
				EmitParticleEffect(s, ParticleLavaSmokePool);
				EmitParticleEffect(s, ParticleLavaSmokePool);
				break;
			}
			break;
		}
		case 't':
			//Teleport
			//TODO: a cool implosion subtel fx
			//		EmitParticleEffect(s,VX_Implosion);
			break;
		case 's':
		{
			switch (tei_slime.integer) {
			case 1:
				EmitParticleEffect(s, ParticleSlime);
				break;
			case 2:
				EmitParticleEffect(s, ParticleSlimeHarcore);
				break;
			case 3:
				if (!(rand() % 40)) {
					EmitParticleEffect(s, ParticleSlimeGlow);
				}
				if (!(rand() % 40)) {
					EmitParticleEffect(s, ParticleSlimeBubbles);
				}
				break;
			case 4:
				if (!(rand() % 10)) {
					EmitParticleEffect(s, ParticleSlimeGlow);
				}
				if (!(rand() % 10)) {
					EmitParticleEffect(s, ParticleSlimeBubbles);
				}
				break;
			}
			break;
		}
		case 'w':
			//	EmitParticleEffect(s,VX_TeslaCharge);
			break;
		default:
			break;
		}
	}
}

// FIXME: Doesn't work in modern
//draws transparent textures for HL world and nonworld models
void R_DrawAlphaChain(msurface_t* alphachain)
{
	int k;
	msurface_t *s;
	texture_t *t;
	float *v;

	if (!alphachain)
		return;

	GL_AlphaBlendFlags(GL_ALPHATEST_ENABLED);
	for (s = alphachain; s; s = s->texturechain) {
		t = s->texinfo->texture;
		R_RenderDynamicLightmaps(s);

		//bind the world texture
		GL_DisableMultitexture();
		GL_BindTextureUnit(GL_TEXTURE0, GL_TEXTURE_2D, t->gl_texturenum);

		if (gl_mtexable) {
			GLC_MultitextureLightmap(s->lightmaptexturenum);
		}

		glBegin(GL_POLYGON);
		v = s->polys->verts[0];
		for (k = 0; k < s->polys->numverts; k++, v += VERTEXSIZE) {
			if (gl_mtexable) {
				qglMultiTexCoord2f(GL_TEXTURE0, v[3], v[4]);
				qglMultiTexCoord2f(GL_TEXTURE1, v[5], v[6]);
			}
			else {
				glTexCoord2f(v[3], v[4]);
			}
			glVertex3fv(v);
		}
		glEnd();
	}

	alphachain = NULL;

	// FIXME: GL_ResetState()
	GL_AlphaBlendFlags(GL_ALPHATEST_DISABLED);
	GL_DisableMultitexture();
	GL_TextureEnvMode(GL_REPLACE);
}
