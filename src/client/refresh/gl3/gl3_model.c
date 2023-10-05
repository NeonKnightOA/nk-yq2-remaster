/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 * Copyright (C) 2016-2017 Daniel Gibson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * Model loading and caching for OpenGL3. Includes the .bsp file format
 *
 * =======================================================================
 */

#include "header/local.h"

static YQ2_ALIGNAS_TYPE(int) byte mod_novis[MAX_MAP_LEAFS / 8];

enum { MAX_MOD_KNOWN = 512 };

gl3model_t	mod_known[MAX_MOD_KNOWN];
static int	mod_numknown;
static int	mod_max = 0;

int	registration_sequence;

//===============================================================================

static qboolean
Mod_HasFreeSpace(void)
{
	int		i, used;
	gl3model_t	*mod;

	used = 0;

	for (i=0, mod=mod_known ; i < mod_numknown ; i++, mod++)
	{
		if (!mod->name[0])
			continue;
		if (mod->registration_sequence == registration_sequence)
		{
			used ++;
		}
	}

	if (mod_max < used)
	{
		mod_max = used;
	}

	// should same size of free slots as currently used
	return (mod_numknown + mod_max) < MAX_MOD_KNOWN;
}

const byte*
GL3_Mod_ClusterPVS(int cluster, const gl3model_t *model)
{
	if ((cluster == -1) || !model->vis)
	{
		return mod_novis;
	}

	return Mod_DecompressVis((byte *)model->vis +
			model->vis->bitofs[cluster][DVIS_PVS],
			(model->vis->numclusters + 7) >> 3);
}

void
GL3_Mod_Modellist_f(void)
{
	int i, total, used;
	gl3model_t *mod;
	qboolean freeup;

	total = 0;
	used = 0;
	R_Printf(PRINT_ALL, "Loaded models:\n");

	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		char *in_use = "";

		if (mod->registration_sequence == registration_sequence)
		{
			in_use = "*";
			used ++;
		}

		if (!mod->name[0])
		{
			continue;
		}

		R_Printf(PRINT_ALL, "%8i : %s %s r: %f #%d\n",
			 mod->extradatasize, mod->name, in_use, mod->radius, mod->numsubmodels);
		total += mod->extradatasize;
	}

	R_Printf(PRINT_ALL, "Total resident: %i\n", total);
	// update statistics
	freeup = Mod_HasFreeSpace();
	R_Printf(PRINT_ALL, "Used %d of %d models%s.\n", used, mod_max, freeup ? ", has free space" : "");
}

void
GL3_Mod_Init(void)
{
	mod_max = 0;
	memset(mod_novis, 0xff, sizeof(mod_novis));
}

static void
Mod_LoadSubmodels(gl3model_t *loadmodel, byte *mod_base, lump_t *l)
{
	dmodel_t *in;
	gl3model_t *out;
	int i, j, count;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		ri.Sys_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, loadmodel->name);
	}

	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->submodels = out;
	loadmodel->numsubmodels = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		if (i == 0)
		{
			// copy parent as template for first model
			memcpy(out, loadmodel, sizeof(*out));
		}
		else
		{
			// copy first as template for model
			memcpy(out, loadmodel->submodels, sizeof(*out));
		}

		Com_sprintf (out->name, sizeof(out->name), "*%d", i);

		for (j = 0; j < 3; j++)
		{
			/* spread the mins / maxs by a pixel */
			out->mins[j] = LittleFloat(in->mins[j]) - 1;
			out->maxs[j] = LittleFloat(in->maxs[j]) + 1;
			out->origin[j] = LittleFloat(in->origin[j]);
		}

		out->radius = Mod_RadiusFromBounds(out->mins, out->maxs);
		out->firstnode = LittleLong(in->headnode);
		out->firstmodelsurface = LittleLong(in->firstface);
		out->nummodelsurfaces = LittleLong(in->numfaces);
		// visleafs
		out->numleafs = 0;
		//  check limits
		if (out->firstnode >= loadmodel->numnodes)
		{
			ri.Sys_Error(ERR_DROP, "%s: Inline model %i has bad firstnode",
					__func__, i);
		}
	}
}

/*
 * Fills in s->texturemins[] and s->extents[]
 */
static void
Mod_CalcSurfaceExtents(gl3model_t *loadmodel, msurface_t *s)
{
	float mins[2], maxs[2], val;
	int i;
	mtexinfo_t *tex;
	int bmins[2], bmaxs[2];

	mins[0] = mins[1] = 999999;
	maxs[0] = maxs[1] = -99999;

	tex = s->texinfo;

	for (i = 0; i < s->numedges; i++)
	{
		int e, j;
		mvertex_t *v;

		e = loadmodel->surfedges[s->firstedge + i];

		if (e >= 0)
		{
			v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
		}
		else
		{
			v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];
		}

		for (j = 0; j < 2; j++)
		{
			val = v->position[0] * tex->vecs[j][0] +
				  v->position[1] * tex->vecs[j][1] +
				  v->position[2] * tex->vecs[j][2] +
				  tex->vecs[j][3];

			if (val < mins[j])
			{
				mins[j] = val;
			}

			if (val > maxs[j])
			{
				maxs[j] = val;
			}
		}
	}

	for (i = 0; i < 2; i++)
	{
		bmins[i] = floor(mins[i] / 16);
		bmaxs[i] = ceil(maxs[i] / 16);

		s->texturemins[i] = bmins[i] * 16;
		s->extents[i] = (bmaxs[i] - bmins[i]) * 16;
	}
}

extern void
GL3_SubdivideSurface(msurface_t *fa, gl3model_t* loadmodel);

static int
calcTexinfoAndFacesSize(const byte *mod_base, const lump_t *fl, const lump_t *tl)
{
	dface_t* face_in = (void *)(mod_base + fl->fileofs);
	texinfo_t* texinfo_in = (void *)(mod_base + tl->fileofs);

	if (fl->filelen % sizeof(*face_in) || tl->filelen % sizeof(*texinfo_in))
	{
		// will error out when actually loading it
		return 0;
	}

	int ret = 0;

	int face_count = fl->filelen / sizeof(*face_in);
	int texinfo_count = tl->filelen / sizeof(*texinfo_in);

	{
		// out = Hunk_Alloc(count * sizeof(*out));
		int baseSize = face_count * sizeof(msurface_t);
		baseSize = (baseSize + 31) & ~31;
		ret += baseSize;

		int ti_size = texinfo_count * sizeof(mtexinfo_t);
		ti_size = (ti_size + 31) & ~31;
		ret += ti_size;
	}

	int numWarpFaces = 0;

	for (int surfnum = 0; surfnum < face_count; surfnum++, face_in++)
	{
		int numverts = LittleShort(face_in->numedges);
		int ti = LittleShort(face_in->texinfo);
		if ((ti < 0) || (ti >= texinfo_count))
		{
			return 0; // will error out
		}
		int texFlags = LittleLong(texinfo_in[ti].flags);

		/* set the drawing flags */
		if (texFlags & SURF_WARP)
		{
			if (numverts > 60)
				return 0; // will error out in R_SubdividePolygon()

			// GL3_SubdivideSurface(out, loadmodel); /* cut up polygon for warps */
			// for each (pot. recursive) call to R_SubdividePolygon():
			//   sizeof(mpoly_t) + ((numverts - 4) + 2) * sizeof(mvtx_t)

			// this is tricky, how much is allocated depends on the size of the surface
			// which we don't know (we'd need the vertices etc to know, but we can't load
			// those without allocating...)
			// so we just count warped faces and use a generous estimate below

			++numWarpFaces;
		}
		else
		{
			// GL3_LM_BuildPolygonFromSurface(out);
			// => poly = Hunk_Alloc(sizeof(mpoly_t) + (numverts - 4) * sizeof(mvtx_t));
			int polySize = sizeof(mpoly_t) + (numverts - 4) * sizeof(mvtx_t);
			polySize = (polySize + 31) & ~31;
			ret += polySize;
		}
	}

	// yeah, this is a bit hacky, but it looks like for each warped face
	// 256-55000 bytes are allocated (usually on the lower end),
	// so just assume 48k per face to be safe
	ret += numWarpFaces * 49152;
	ret += 5000000; // and 5MB extra just in case

	return ret;
}

static int
calcTexinfoAndQFacesSize(const byte *mod_base, const lump_t *fl, const lump_t *tl)
{
	dqface_t* face_in = (void *)(mod_base + fl->fileofs);
	texinfo_t* texinfo_in = (void *)(mod_base + tl->fileofs);

	if (fl->filelen % sizeof(*face_in) || tl->filelen % sizeof(*texinfo_in))
	{
		// will error out when actually loading it
		return 0;
	}

	int ret = 0;

	int face_count = fl->filelen / sizeof(*face_in);
	int texinfo_count = tl->filelen / sizeof(*texinfo_in);

	{
		// out = Hunk_Alloc(count * sizeof(*out));
		int baseSize = face_count * sizeof(msurface_t);
		baseSize = (baseSize + 31) & ~31;
		ret += baseSize;

		int ti_size = texinfo_count * sizeof(mtexinfo_t);
		ti_size = (ti_size + 31) & ~31;
		ret += ti_size;
	}

	int numWarpFaces = 0;

	for (int surfnum = 0; surfnum < face_count; surfnum++, face_in++)
	{
		int numverts = LittleLong(face_in->numedges);
		int ti = LittleLong(face_in->texinfo);
		if ((ti < 0) || (ti >= texinfo_count))
		{
			return 0; // will error out
		}
		int texFlags = LittleLong(texinfo_in[ti].flags);

		/* set the drawing flags */
		if (texFlags & SURF_WARP)
		{
			if (numverts > 60)
				return 0; // will error out in R_SubdividePolygon()

			// GL3_SubdivideSurface(out, loadmodel); /* cut up polygon for warps */
			// for each (pot. recursive) call to R_SubdividePolygon():
			//   sizeof(mpoly_t) + ((numverts - 4) + 2) * sizeof(mvtx_t)

			// this is tricky, how much is allocated depends on the size of the surface
			// which we don't know (we'd need the vertices etc to know, but we can't load
			// those without allocating...)
			// so we just count warped faces and use a generous estimate below

			++numWarpFaces;
		}
		else
		{
			// GL3_LM_BuildPolygonFromSurface(out);
			// => poly = Hunk_Alloc(sizeof(mpoly_t) + (numverts - 4) * sizeof(mvtx_t));
			int polySize = sizeof(mpoly_t) + (numverts - 4) * sizeof(mvtx_t);
			polySize = (polySize + 31) & ~31;
			ret += polySize;
		}
	}

	// yeah, this is a bit hacky, but it looks like for each warped face
	// 256-55000 bytes are allocated (usually on the lower end),
	// so just assume 48k per face to be safe
	ret += numWarpFaces * 49152;
	ret += 5000000; // and 5MB extra just in case

	return ret;
}

static void
SetSurfaceLighting(gl3model_t *loadmodel, msurface_t *out, byte *styles, int lightofs)
{
	int i;

	/* lighting info */
	for (i = 0; i < MAX_LIGHTMAPS_PER_SURFACE; i++)
	{
		out->styles[i] = styles[i];
	}

	i = LittleLong(lightofs);
	if (i == -1 || loadmodel->lightdata == NULL)
	{
		out->samples = NULL;
	}
	else
	{
		out->samples = loadmodel->lightdata + i;
	}
}

static void
Mod_LoadFaces(gl3model_t *loadmodel, const byte *mod_base, const lump_t *l,
	const bspx_header_t *bspx_header)
{
	int i, count, surfnum;
	msurface_t *out;
	dface_t *in;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		ri.Sys_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, loadmodel->name);
	}

	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->surfaces = out;
	loadmodel->numsurfaces = count;

	GL3_LM_BeginBuildingLightmaps(loadmodel);

	for (surfnum = 0; surfnum < count; surfnum++, in++, out++)
	{
		int	side, ti, planenum;

		out->firstedge = LittleLong(in->firstedge);
		out->numedges = LittleShort(in->numedges);

		if (out->numedges < 3)
		{
			ri.Sys_Error(ERR_DROP, "%s: Surface with %d edges",
					__func__, out->numedges);
		}
		out->flags = 0;
		out->polys = NULL;

		planenum = LittleShort(in->planenum);
		side = LittleShort(in->side);

		if (side)
		{
			out->flags |= SURF_PLANEBACK;
		}

		if (planenum < 0 || planenum >= loadmodel->numplanes)
		{
			ri.Sys_Error(ERR_DROP, "%s: Incorrect %d planenum.",
					__func__, planenum);
		}
		out->plane = loadmodel->planes + planenum;

		ti = LittleShort(in->texinfo);

		if ((ti < 0) || (ti >= loadmodel->numtexinfo))
		{
			ri.Sys_Error(ERR_DROP, "%s: bad texinfo number",
					__func__);
		}

		out->texinfo = loadmodel->texinfo + ti;
		out->lmshift = DEFAULT_LMSHIFT;

		Mod_CalcSurfaceExtents(loadmodel, out);

		SetSurfaceLighting(loadmodel, out, in->styles, in->lightofs);

		/* set the drawing flags */
		if (out->texinfo->flags & SURF_WARP)
		{
			out->flags |= SURF_DRAWTURB;

			for (i = 0; i < 2; i++)
			{
				out->extents[i] = 16384;
				out->texturemins[i] = -8192;
			}

			GL3_SubdivideSurface(out, loadmodel); /* cut up polygon for warps */
		}

		if (r_fixsurfsky->value)
		{
			if (out->texinfo->flags & SURF_SKY)
			{
				out->flags |= SURF_DRAWSKY;
			}
		}

		/* create lightmaps and polygons */
		if (!(out->texinfo->flags & (SURF_SKY | SURF_TRANS33 | SURF_TRANS66 | SURF_WARP)))
		{
			GL3_LM_CreateSurfaceLightmap(out);
		}

		if (!(out->texinfo->flags & SURF_WARP))
		{
			GL3_LM_BuildPolygonFromSurface(loadmodel, out);
		}
	}

	GL3_LM_EndBuildingLightmaps();
}

static void
Mod_LoadQFaces(gl3model_t *loadmodel, const byte *mod_base, const lump_t *l,
	const bspx_header_t *bspx_header)
{
	int i, count, surfnum;
	msurface_t *out;
	dqface_t *in;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		ri.Sys_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, loadmodel->name);
	}

	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->surfaces = out;
	loadmodel->numsurfaces = count;

	GL3_LM_BeginBuildingLightmaps(loadmodel);

	for (surfnum = 0; surfnum < count; surfnum++, in++, out++)
	{
		int	side, ti, planenum;

		out->firstedge = LittleLong(in->firstedge);
		out->numedges = LittleLong(in->numedges);

		if (out->numedges < 3)
		{
			ri.Sys_Error(ERR_DROP, "%s: Surface with %d edges",
					__func__, out->numedges);
		}
		out->flags = 0;
		out->polys = NULL;

		planenum = LittleLong(in->planenum);
		side = LittleLong(in->side);

		if (side)
		{
			out->flags |= SURF_PLANEBACK;
		}

		if (planenum < 0 || planenum >= loadmodel->numplanes)
		{
			ri.Sys_Error(ERR_DROP, "%s: Incorrect %d planenum.",
					__func__, planenum);
		}
		out->plane = loadmodel->planes + planenum;

		ti = LittleLong(in->texinfo);

		if ((ti < 0) || (ti >= loadmodel->numtexinfo))
		{
			ri.Sys_Error(ERR_DROP, "%s: bad texinfo number",
					__func__);
		}

		out->texinfo = loadmodel->texinfo + ti;
		out->lmshift = DEFAULT_LMSHIFT;

		Mod_CalcSurfaceExtents(loadmodel, out);

		SetSurfaceLighting(loadmodel, out, in->styles, in->lightofs);

		/* set the drawing flags */
		if (out->texinfo->flags & SURF_WARP)
		{
			out->flags |= SURF_DRAWTURB;

			for (i = 0; i < 2; i++)
			{
				out->extents[i] = 16384;
				out->texturemins[i] = -8192;
			}

			GL3_SubdivideSurface(out, loadmodel); /* cut up polygon for warps */
		}

		if (r_fixsurfsky->value)
		{
			if (out->texinfo->flags & SURF_SKY)
			{
				out->flags |= SURF_DRAWSKY;
			}
		}

		/* create lightmaps and polygons */
		if (!(out->texinfo->flags & (SURF_SKY | SURF_TRANS33 | SURF_TRANS66 | SURF_WARP)))
		{
			GL3_LM_CreateSurfaceLightmap(out);
		}

		if (!(out->texinfo->flags & SURF_WARP))
		{
			GL3_LM_BuildPolygonFromSurface(loadmodel, out);
		}
	}

	GL3_LM_EndBuildingLightmaps();
}

static void
Mod_LoadLeafs(gl3model_t *loadmodel, const byte *mod_base, const lump_t *l)
{
	dleaf_t *in;
	mleaf_t *out;
	int i, j, count;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		ri.Sys_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, loadmodel->name);
	}

	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		unsigned firstleafface;

		for (j = 0; j < 3; j++)
		{
			out->minmaxs[j] = LittleShort(in->mins[j]);
			out->minmaxs[3 + j] = LittleShort(in->maxs[j]);
		}

		out->contents = LittleLong(in->contents);
		out->cluster = LittleShort(in->cluster);
		out->area = LittleShort(in->area);

		// make unsigned long from signed short
		firstleafface = LittleShort(in->firstleafface) & 0xFFFF;
		out->nummarksurfaces = LittleShort(in->numleaffaces) & 0xFFFF;

		out->firstmarksurface = loadmodel->marksurfaces + firstleafface;
		if ((firstleafface + out->nummarksurfaces) > loadmodel->nummarksurfaces)
		{
			ri.Sys_Error(ERR_DROP, "%s: wrong marksurfaces position in %s",
				__func__, loadmodel->name);
		}
	}
}

static void
Mod_LoadQLeafs(gl3model_t *loadmodel, const byte *mod_base, const lump_t *l)
{
	dqleaf_t *in;
	mleaf_t *out;
	int i, j, count;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		ri.Sys_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, loadmodel->name);
	}

	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		unsigned firstleafface;

		for (j = 0; j < 3; j++)
		{
			out->minmaxs[j] = LittleFloat(in->mins[j]);
			out->minmaxs[3 + j] = LittleFloat(in->maxs[j]);
		}

		out->contents = LittleLong(in->contents);
		out->cluster = LittleLong(in->cluster);
		out->area = LittleLong(in->area);

		// make unsigned long from signed short
		firstleafface = LittleLong(in->firstleafface) & 0xFFFFFFFF;
		out->nummarksurfaces = LittleLong(in->numleaffaces) & 0xFFFFFFFF;

		out->firstmarksurface = loadmodel->marksurfaces + firstleafface;
		if ((firstleafface + out->nummarksurfaces) > loadmodel->nummarksurfaces)
		{
			ri.Sys_Error(ERR_DROP, "%s: wrong marksurfaces position in %s",
				__func__, loadmodel->name);
		}
	}
}

static void
Mod_LoadMarksurfaces(gl3model_t *loadmodel, const byte *mod_base, const lump_t *l)
{
	int i, count;
	short *in;
	msurface_t **out;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		ri.Sys_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, loadmodel->name);
	}

	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->marksurfaces = out;
	loadmodel->nummarksurfaces = count;

	for (i = 0; i < count; i++)
	{
		int j;

		j = LittleShort(in[i]);

		if ((j < 0) || (j >= loadmodel->numsurfaces))
		{
			ri.Sys_Error(ERR_DROP, "%s: bad surface number",
					__func__);
		}

		out[i] = loadmodel->surfaces + j;
	}
}

static void
Mod_LoadQMarksurfaces(gl3model_t *loadmodel, const byte *mod_base, const lump_t *l)
{
	int i, count;
	int *in;
	msurface_t **out;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		ri.Sys_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, loadmodel->name);
	}

	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->marksurfaces = out;
	loadmodel->nummarksurfaces = count;

	for (i = 0; i < count; i++)
	{
		int j;
		j = LittleLong(in[i]);

		if ((j < 0) || (j >= loadmodel->numsurfaces))
		{
			ri.Sys_Error(ERR_DROP, "%s: bad surface number",
					__func__);
		}

		out[i] = loadmodel->surfaces + j;
	}
}

static void
Mod_LoadBrushModel(gl3model_t *mod, const void *buffer, int modfilelen)
{
	const bspx_header_t	*bspx_header;
	byte		*mod_base;
	dheader_t	*header;
	int			i;

	if (mod != mod_known)
	{
		ri.Sys_Error(ERR_DROP, "%s: Loaded a brush model after the world", __func__);
	}

	header = (dheader_t *)buffer;

	i = LittleLong(header->ident);

	if (i != IDBSPHEADER && i != QBSPHEADER)
	{
		ri.Sys_Error(ERR_DROP, "%s: %s has wrong ident (%i should be %i)",
				__func__, mod->name, i, IDBSPHEADER);
	}

	i = LittleLong(header->version);

	if (i != BSPVERSION && i != BSPDKMVERSION)
	{
		ri.Sys_Error(ERR_DROP, "%s: %s has wrong version number (%i should be %i)",
				__func__, mod->name, i, BSPVERSION);
	}

	/* swap all the lumps */
	mod_base = (byte *)header;

	for (i = 0; i < sizeof(dheader_t) / 4; i++)
	{
		((int *)header)[i] = LittleLong(((int *)header)[i]);
	}

	// calculate the needed hunksize from the lumps
	int hunkSize = 0;
	hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_VERTEXES], sizeof(dvertex_t), sizeof(mvertex_t), 0);
	if (header->ident == IDBSPHEADER)
	{
		hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_EDGES], sizeof(dedge_t), sizeof(medge_t), 0);
	}
	else
	{
		hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_EDGES], sizeof(dqedge_t), sizeof(medge_t), 0);
	}
	hunkSize += sizeof(medge_t) + 31; // for count+1 in Mod_LoadEdges()
	int surfEdgeCount = (header->lumps[LUMP_SURFEDGES].filelen+sizeof(int)-1)/sizeof(int);
	if(surfEdgeCount < MAX_MAP_SURFEDGES) // else it errors out later anyway
		hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_SURFEDGES], sizeof(int), sizeof(int), 0);
	hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_LIGHTING], 1, 1, 0);
	hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_PLANES], sizeof(dplane_t), sizeof(cplane_t)*2, 0);
	if (header->ident == IDBSPHEADER)
	{
		hunkSize += calcTexinfoAndFacesSize(mod_base, &header->lumps[LUMP_FACES], &header->lumps[LUMP_TEXINFO]);
		hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_LEAFFACES], sizeof(short), sizeof(msurface_t *), 0); // yes, out is indeed a pointer!
	}
	else
	{
		hunkSize += calcTexinfoAndQFacesSize(mod_base, &header->lumps[LUMP_FACES], &header->lumps[LUMP_TEXINFO]);
		hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_LEAFFACES], sizeof(int), sizeof(msurface_t *), 0); // yes, out is indeed a pointer!
	}
	hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_VISIBILITY], 1, 1, 0);
	if (header->ident == IDBSPHEADER)
	{
		hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_LEAFS], sizeof(dleaf_t), sizeof(mleaf_t), 0);
		hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_NODES], sizeof(dnode_t), sizeof(mnode_t), 0);
	}
	else
	{
		hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_LEAFS], sizeof(dqleaf_t), sizeof(mleaf_t), 0);
		hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_NODES], sizeof(dqnode_t), sizeof(mnode_t), 0);
	}
	hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_MODELS], sizeof(dmodel_t), sizeof(gl3model_t), 0);

	mod->extradata = Hunk_Begin(hunkSize);
	mod->type = mod_brush;


	/* check for BSPX extensions */
	bspx_header = Mod_LoadBSPX(modfilelen, (byte*)header);

	/* load into heap */
	Mod_LoadVertexes(mod->name, &mod->vertexes, &mod->numvertexes, mod_base,
		&header->lumps[LUMP_VERTEXES], 0);
	if (header->ident == IDBSPHEADER)
	{
		Mod_LoadEdges(mod->name, &mod->edges, &mod->numedges,
			mod_base, &header->lumps[LUMP_EDGES], 1);
	}
	else
	{
		Mod_LoadQEdges(mod->name, &mod->edges, &mod->numedges,
			mod_base, &header->lumps[LUMP_EDGES], 1);
	}
	Mod_LoadSurfedges(mod->name, &mod->surfedges, &mod->numsurfedges,
		mod_base, &header->lumps[LUMP_SURFEDGES], 0);
	Mod_LoadLighting(&mod->lightdata, mod_base, &header->lumps[LUMP_LIGHTING]);
	Mod_LoadPlanes(mod->name, &mod->planes, &mod->numplanes,
		mod_base, &header->lumps[LUMP_PLANES], 0);
	Mod_LoadTexinfo(mod->name, &mod->texinfo, &mod->numtexinfo,
		mod_base, &header->lumps[LUMP_TEXINFO], (findimage_t)GL3_FindImage,
		gl3_notexture, 0);
	if (header->ident == IDBSPHEADER)
	{
		Mod_LoadFaces(mod, mod_base, &header->lumps[LUMP_FACES], bspx_header);
		Mod_LoadMarksurfaces(mod, mod_base, &header->lumps[LUMP_LEAFFACES]);
	}
	else
	{
		Mod_LoadQFaces(mod, mod_base, &header->lumps[LUMP_FACES], bspx_header);
		Mod_LoadQMarksurfaces(mod, mod_base, &header->lumps[LUMP_LEAFFACES]);
	}
	Mod_LoadVisibility(&mod->vis, mod_base, &header->lumps[LUMP_VISIBILITY]);
	if (header->ident == IDBSPHEADER)
	{
		Mod_LoadLeafs(mod, mod_base, &header->lumps[LUMP_LEAFS]);
		Mod_LoadNodes(mod->name, mod->planes, mod->numplanes, mod->leafs,
			mod->numleafs, &mod->nodes, &mod->numnodes, mod_base,
			&header->lumps[LUMP_NODES]);
	}
	else
	{
		Mod_LoadQLeafs(mod, mod_base, &header->lumps[LUMP_LEAFS]);
		Mod_LoadQNodes(mod->name, mod->planes, mod->numplanes, mod->leafs,
			mod->numleafs, &mod->nodes, &mod->numnodes, mod_base,
			&header->lumps[LUMP_NODES]);
	}
	Mod_LoadSubmodels(mod, mod_base, &header->lumps[LUMP_MODELS]);
	mod->numframes = 2; /* regular and alternate animation */
}

/*
 * Loads in a model for the given name
 */
static gl3model_t *
Mod_ForName (const char *name, gl3model_t *parent_model, qboolean crash)
{
	gl3model_t *mod;
	void *buf;
	int i, modfilelen;

	if (!name[0])
	{
		ri.Sys_Error(ERR_DROP, "%s: NULL name", __func__);
	}

	/* inline models are grabbed only from worldmodel */
	if (name[0] == '*' && parent_model)
	{
		i = (int)strtol(name + 1, (char **)NULL, 10);

		if (i < 1 || i >= parent_model->numsubmodels)
		{
			ri.Sys_Error(ERR_DROP, "%s: bad inline model number",
					__func__);
		}

		return &parent_model->submodels[i];
	}

	/* search the currently loaded models */
	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		if (!mod->name[0])
		{
			continue;
		}

		if (!strcmp(mod->name, name))
		{
			return mod;
		}
	}

	/* find a free model slot spot */
	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		if (!mod->name[0])
		{
			break; /* free spot */
		}
	}

	if (i == mod_numknown)
	{
		if (mod_numknown == MAX_MOD_KNOWN)
		{
			ri.Sys_Error(ERR_DROP, "%s: mod_numknown == MAX_MOD_KNOWN", __func__);
		}

		mod_numknown++;
	}

	strcpy(mod->name, name);

	/* load the file */
	modfilelen = Mod_LoadFile (mod->name, &buf);

	if (!buf)
	{
		if (crash)
		{
			ri.Sys_Error(ERR_DROP, "%s: %s not found",
					__func__, mod->name);
		}

		memset(mod->name, 0, sizeof(mod->name));
		return NULL;
	}

	/* call the apropriate loader */
	switch (LittleLong(*(unsigned *)buf))
	{
		case DKMHEADER:
			/* fall through */
		case RAVENFMHEADER:
			/* fall through */
		case IDALIASHEADER:
			/* fall through */
		case IDMDLHEADER:
			{
				mod->extradata = Mod_LoadAliasModel(mod->name, buf, modfilelen,
					mod->mins, mod->maxs,
					(struct image_s **)mod->skins, (findimage_t)GL3_FindImage,
					&(mod->type));
				if (!mod->extradata)
				{
					ri.Sys_Error(ERR_DROP, "%s: Failed to load %s",
						__func__, mod->name);
				}
			};
			break;

		case IDSPRITEHEADER:
			{
				mod->extradata = Mod_LoadSP2(mod->name, buf, modfilelen,
					(struct image_s **)mod->skins, (findimage_t)GL3_FindImage,
					&(mod->type));
				if (!mod->extradata)
				{
					ri.Sys_Error(ERR_DROP, "%s: Failed to load %s",
						__func__, mod->name);
				}
			}
			break;

		case IDBSPHEADER:
			/* fall through */
		case QBSPHEADER:
			Mod_LoadBrushModel(mod, buf, modfilelen);
			break;

		default:
			ri.Sys_Error(ERR_DROP, "%s: unknown fileid for %s",
					__func__, mod->name);
			break;
	}

	mod->radius = Mod_RadiusFromBounds(mod->mins, mod->maxs);
	mod->extradatasize = Hunk_End();

	ri.FS_FreeFile(buf);

	return mod;
}

static void
Mod_Free(gl3model_t *mod)
{
	Hunk_Free(mod->extradata);
	memset(mod, 0, sizeof(*mod));
}

void
GL3_Mod_FreeAll(void)
{
	int i;

	for (i = 0; i < mod_numknown; i++)
	{
		if (mod_known[i].extradatasize)
		{
			Mod_Free(&mod_known[i]);
		}
	}
}

/*
 * Specifies the model that will be used as the world
 */
void
GL3_BeginRegistration(const char *model)
{
	char fullname[MAX_QPATH];
	cvar_t *flushmap;

	registration_sequence++;
	gl3_oldviewcluster = -1; /* force markleafs */

	gl3state.currentlightmap = -1;

	Com_sprintf(fullname, sizeof(fullname), "maps/%s.bsp", model);

	/* explicitly free the old map if different
	   this guarantees that mod_known[0] is the
	   world map */
	flushmap = ri.Cvar_Get("flushmap", "0", 0);

	if (strcmp(mod_known[0].name, fullname) || flushmap->value)
	{
		Mod_Free(&mod_known[0]);
	}

	gl3_worldmodel = Mod_ForName(fullname, NULL, true);

	gl3_viewcluster = -1;
}

struct model_s *
GL3_RegisterModel(const char *name)
{
	gl3model_t *mod;

	mod = Mod_ForName(name, gl3_worldmodel, false);

	if (mod)
	{
		mod->registration_sequence = registration_sequence;

		/* register any images used by the models */
		if (mod->type == mod_brush)
		{
			int i;

			for (i = 0; i < mod->numtexinfo; i++)
			{
				mod->texinfo[i].image->registration_sequence =
					registration_sequence;
			}
		}
		else
		{
			/* numframes is unused for SP2 but lets set it also  */
			mod->numframes = Mod_ReLoadSkins((struct image_s **)mod->skins,
				(findimage_t)GL3_FindImage, mod->extradata, mod->type);
		}
	}

	return mod;
}

void
GL3_EndRegistration(void)
{
	int i;
	gl3model_t *mod;

	if (Mod_HasFreeSpace() && GL3_ImageHasFreeSpace())
	{
		// should be enough space for load next maps
		return;
	}

	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		if (!mod->name[0])
		{
			continue;
		}

		if (mod->registration_sequence != registration_sequence)
		{
			/* don't need this model */
			Mod_Free(mod);
		}
	}

	GL3_FreeUnusedImages();
}
