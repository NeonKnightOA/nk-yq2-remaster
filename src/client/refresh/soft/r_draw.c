/*
Copyright (C) 1997-2001 Id Software, Inc.

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

// draw.c

#include "r_local.h"


image_t		*draw_chars;	// 8*8 graphic characters

//=============================================================================

/*
================
Draw_FindPic
================
*/
image_t *Draw_FindPic (char *name)
{
	image_t	*image;

	if (name[0] != '/' && name[0] != '\\')
	{
		char fullname[MAX_QPATH];

		Com_sprintf (fullname, sizeof(fullname), "pics/%s.pcx", name);
		image = R_FindImage (fullname, it_pic);
	}
	else
		image = R_FindImage (name+1, it_pic);

	return image;
}



/*
===============
Draw_InitLocal
===============
*/
void Draw_InitLocal (void)
{
	draw_chars = Draw_FindPic ("conchars");
}



/*
================
Draw_Char

Draws one 8*8 graphics character
It can be clipped to the top of the screen to allow the console to be
smoothly scrolled off.
================
*/
void Draw_CharScaled(int x, int y, int num, float scale)
{
	pixel_t	*dest;
	byte	*source;
	int		drawline;
	int		row, col, u, xpos, ypos, iscale;

	iscale = (int) scale;

	num &= 255;

	if (num == 32 || num == 32+128)
		return;

	if (y <= -8)
		return;	// totally off screen

	//	if ( ( y + 8 ) >= vid.height )
	if ( ( y + 8 ) > vid.height )	// PGM - status text was missing in sw...
		return;

	row = num>>4;
	col = num&15;
	source = draw_chars->pixels[0] + (row<<10) + (col<<3);

	if (y < 0)
	{	// clipped
		drawline = 8 + y;
		source -= 128*y;
		y = 0;
	}
	else
		drawline = 8;

	dest = vid_buffer + y * vid.width + x;

	while (drawline--)
	{
		for (ypos=0; ypos < iscale; ypos ++)
		{
			for(u=0; u < 8; u++)
			{
				if (source[u] != TRANSPARENT_COLOR)
					for (xpos=0; xpos < iscale; xpos ++)
					{
						dest[u * iscale + xpos] = source[u];
					}
			}
			dest += vid.width;
		}
		source += 128;
	}
}

/*
=============
Draw_GetPicSize
=============
*/
void Draw_GetPicSize (int *w, int *h, char *pic)
{
	image_t *gl;

	gl = Draw_FindPic (pic);
	if (!gl)
	{
		*w = *h = -1;
		return;
	}
	*w = gl->width;
	*h = gl->height;
}

/*
=============
Draw_StretchPicImplementation
=============
*/
void Draw_StretchPicImplementation (int x, int y, int w, int h, image_t	*pic)
{
	pixel_t	*dest;
	byte	*source;
	int		v, u;
	int		height;
	int		f, fstep;
	int		skip;

	if ((x < 0) ||
		(x + w > vid.width) ||
		(y + h > vid.height))
	{
		ri.Sys_Error (ERR_FATAL,"Draw_Pic: bad coordinates");
	}

	height = h;
	if (y < 0)
	{
		skip = -y;
		height += y;
		y = 0;
	}
	else
		skip = 0;

	dest = vid_buffer + y * vid.width + x;

	for (v=0 ; v<height ; v++, dest += vid.width)
	{
		int sv = (skip + v)*pic->height/h;
		source = pic->pixels[0] + sv*pic->width;
		if (w == pic->width)
			memcpy (dest, source, w);
		else
		{
			f = 0;
			fstep = pic->width*0x10000/w;
			for (u=0 ; u<w ; u++)
			{
				dest[u] = source[f>>16];
				f += fstep;
			}
		}
	}
}

/*
=============
Draw_StretchPic
=============
*/
void Draw_StretchPic (int x, int y, int w, int h, char *name)
{
	image_t	*pic;

	pic = Draw_FindPic (name);
	if (!pic)
	{
		R_Printf(PRINT_ALL, "Can't find pic: %s\n", name);
		return;
	}
	Draw_StretchPicImplementation (x, y, w, h, pic);
}

/*
=============
Draw_StretchRaw
=============
*/
void Draw_StretchRaw (int x, int y, int w, int h, int cols, int rows, byte *data)
{
	image_t	pic;

	pic.pixels[0] = data;
	pic.width = cols;
	pic.height = rows;
	Draw_StretchPicImplementation (x, y, w, h, &pic);
}

/*
=============
Draw_Pic
=============
*/
void Draw_PicScaled(int x, int y, char *name, float scale)
{
	image_t		*pic;
	pixel_t		*dest;
	byte 		*source;
	int			v, u, xpos, ypos, iscale;
	int			height;

	iscale = (int)scale;
	pic = Draw_FindPic (name);
	if (!pic)
	{
		R_Printf(PRINT_ALL, "Can't find pic: %s\n", name);
		return;
	}

	if ((x < 0) ||
		(x + pic->width > vid.width) ||
		(y + pic->height > vid.height))
	{
		R_Printf(PRINT_ALL, "Draw_Pic: bad coordinates\n");
		return;
	}

	height = pic->height;
	source = pic->pixels[0];
	if (y < 0)
	{
		height += y;
		source += pic->width*-y;
		y = 0;
	}

	dest = vid_buffer + y * vid.width + x;

	if (!pic->transparent)
	{
		for (v=0; v<height; v++)
		{
			for(ypos=0; ypos < iscale; ypos++)
			{
				for (u=0; u<pic->width; u++)
				{
					for(xpos=0; xpos < iscale; xpos++)
					{
						dest[u * iscale + xpos] = source[u];
					}
				}
				dest += vid.width;
			}
			source += pic->width;
		}
	}
	else
	{
		for (v=0; v<height; v++)
		{
			for(ypos=0; ypos < iscale; ypos++)
			{
				for (u=0; u<pic->width; u++)
				{
					if (source[u] != TRANSPARENT_COLOR)
						for(xpos=0; xpos < iscale; xpos++)
						{
							dest[u * iscale + xpos] = source[u];
						}
				}
				dest += vid.width;
			}
			source += pic->width;
		}
	}
}

/*
=============
Draw_TileClear

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
void Draw_TileClear (int x, int y, int w, int h, char *name)
{
	int			i, j;
	byte		*psrc;
	pixel_t		*pdest;
	image_t		*pic;
	int			x2;

	if (x < 0)
	{
		w += x;
		x = 0;
	}
	if (y < 0)
	{
		h += y;
		y = 0;
	}
	if (x + w > vid.width)
		w = vid.width - x;
	if (y + h > vid.height)
		h = vid.height - y;
	if (w <= 0 || h <= 0)
		return;

	pic = Draw_FindPic (name);
	if (!pic)
	{
		R_Printf(PRINT_ALL, "Can't find pic: %s\n", name);
		return;
	}
	x2 = x + w;
	pdest = vid_buffer + y * vid.width;
	for (i=0 ; i<h ; i++, pdest += vid.width)
	{
		psrc = pic->pixels[0] + pic->width * ((i+y)&63);
		for (j=x ; j<x2 ; j++)
			pdest[j] = psrc[j&63];
	}
}


/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/
void Draw_Fill (int x, int y, int w, int h, int c)
{
	pixel_t	*dest;
	int		u, v;

	if (x+w > vid.width)
		w = vid.width - x;
	if (y+h > vid.height)
		h = vid.height - y;

	if (x < 0)
	{
		w += x;
		x = 0;
	}
	if (y < 0)
	{
		h += y;
		y = 0;
	}

	if (w < 0 || h < 0)
		return;

	dest = vid_buffer + y * vid.width + x;
	for (v=0 ; v<h ; v++, dest += vid.width)
		for (u=0 ; u<w ; u++)
			dest[u] = c;
}
//=============================================================================

/*
================
Draw_FadeScreen

================
*/
void Draw_FadeScreen (void)
{
	int x,y;

	for (y=0 ; y<vid.height ; y++)
	{
		int t;
		pixel_t *pbuf;

		pbuf = vid_buffer + vid.width * y;
		t = (y & 1) << 1;

		for (x=0 ; x<vid.width ; x++)
		{
			if ((x & 3) != t)
				pbuf[x] = 0;
		}
	}
}
