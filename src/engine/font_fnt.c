/* 
 * Loadable FNT font engine for Microwindows
 * Copyright (c) 2003 Greg Haerr <greg@censoft.com>
 *
 * Load a .fnt (Microwindows native) binary font, store in incore format.
 */
#include <stdio.h>
#include <string.h>
#include "swap.h"
#include "device.h"
#include "devfont.h"
#include "../drivers/genfont.h"

/*
 * .fnt loadable font file format definition
 *
 * format                     len	description
 * -------------------------  ----	------------------------------
 * UCHAR version[4]		4	magic number and version bytes
 * UCHAR name[64]	       64	font name, space padded
 * UCHAR copyright[256]	      256	copyright info, space padded
 * USHORT maxwidth		2	font max width in pixels
 * USHORT height		2	font height in pixels
 * USHORT ascent		2	font ascent (baseline) in pixels
 * USHORT pad                   2       unused, pad to 32-bit boundary
 * ULONG firstchar		4	first character code in font
 * ULONG defaultchar		4	default character code in font
 * ULONG size			4	# characters in font
 * ULONG nbits			4	# words imagebits data in file
 * ULONG noffset		4	# longs offset data in file
 * ULONG nwidth			4	# bytes width data in file
 * MWIMAGEBITS bits	  nbits*2	image bits variable data
 * [MWIMAGEBITS padded to 32-bit boundary]
 * ULONG offset         noffset*4	offset variable data
 * UCHAR width		 nwidth*1	width variable data
 */

/* loadable font magic and version #*/
#define VERSION		"RB11"

/* Handling routines for FNT fonts, use MWCOREFONT structure */
static void fnt_unloadfont(PMWFONT font);
static PMWCFONT fnt_load_font(char *path);

static MWFONTPROCS fnt_fontprocs = {
	MWTF_ASCII,		/* routines expect ascii */
	gen_getfontinfo,
	gen_gettextsize,
	gen_gettextbits,
	fnt_unloadfont,
	corefont_drawtext,
	NULL,			/* setfontsize */
	NULL,			/* setfontrotation */
	NULL,			/* setfontattr */
};

PMWCOREFONT
fnt_createfont(char *name, MWCOORD height, int attr)
{
	PMWCOREFONT	pf;
	PMWCFONT	cfont;

	/* try to open file and read in font data*/
	cfont = fnt_load_font(name);
	if (!cfont)
		return NULL;

	if (!(pf = (MWCOREFONT *) malloc(sizeof(MWCOREFONT)))) {
		free(cfont);
		return NULL;
	}

	pf->fontprocs = &fnt_fontprocs;
	pf->fontsize = pf->fontrotation = pf->fontattr = 0;
	pf->name = "FNT";
	pf->cfont = cfont;
	return pf;
}

void
fnt_unloadfont(PMWFONT font)
{
	PMWCOREFONT pf = (PMWCOREFONT)font;
	PMWCFONT    pfc = pf->cfont;

	if (pfc) {
		if (pfc->width)
			free(pf->cfont->width);
		if (pfc->offset)
			free(pf->cfont->offset);
		if (pfc->bits)
			free(pf->cfont->bits);
		if (pfc->name)
			free(pf->cfont->name);

		free(pfc);
	}

	free(font);
}

static int
READBYTE(FILE *fp, unsigned char *cp)
{
	int c;

	if ((c = getc(fp)) == EOF)
		return 0;
	*cp = (unsigned char)c;
	return 1;
}

static int
READSHORT(FILE *fp, unsigned short *sp)
{
	int c;
	unsigned short s;

	if ((c = getc(fp)) == EOF)
		return 0;
	s = c & 0xff;
	if ((c = getc(fp)) == EOF)
		return 0;
	*sp = (c << 8) | s;
	return 1;
}

static int
READLONG(FILE *fp, unsigned long *lp)
{
	int c;
	unsigned long l;

	if ((c = getc(fp)) == EOF)
		return 0;
	l = c & 0xff;
	if ((c = getc(fp)) == EOF)
		return 0;
	l |= (c << 8);
	if ((c = getc(fp)) == EOF)
		return 0;
	l |= (c << 16);
	if ((c = getc(fp)) == EOF)
		return 0;
	*lp = (c << 24) | l;
	return 1;
}

/* read count bytes*/
static int
READSTR(FILE *fp, char *buf, int count)
{
	return fread(buf, 1, count, fp);
}

/* read totlen bytes, return NUL terminated string*/
/* may write 1 past buf[totlen]; removes blank pad*/
static int
READSTRPAD(FILE *fp, char *buf, int totlen)
{
	char *p;

	if (fread(buf, 1, totlen, fp) != totlen)
		return 0;
	p = &buf[totlen];
	*p-- = 0;
	while (*p == ' ' && p >= buf)
		*p-- = '\0';
	return totlen;
}

/* read and load font, return incore font structure*/
static PMWCFONT
fnt_load_font(char *path)
{
	FILE *ifp;
	PMWCFONT pf = NULL;
	int i;
	unsigned short maxwidth, height, ascent, pad;
	unsigned long firstchar, defaultchar, size;
	unsigned long nbits, noffset, nwidth;
	char version[4+1];
	char name[64+1];
	char copyright[256+1];

	ifp = fopen(path, "rb");
	if (!ifp)
		return NULL;

	/* read magic and version #*/
	memset(version, 0, sizeof(version));
	if (READSTR(ifp, version, 4) != 4)
		goto errout;
	if (strcmp(version, VERSION) != 0)
		goto errout;

	pf = (PMWCFONT)calloc(1, sizeof(MWCFONT));
	if (!pf)
		goto errout;

	/* internal font name*/
	if (READSTRPAD(ifp, name, 64) != 64)
		goto errout;
	pf->name = (char *)malloc(strlen(name)+1);
	if (!pf->name)
		goto errout;
	strcpy(pf->name, name);

	/* copyright, not currently stored*/
	if (READSTRPAD(ifp, copyright, 256) != 256)
		goto errout;

	/* font info*/
	if (!READSHORT(ifp, &maxwidth))
		goto errout;
	pf->maxwidth = maxwidth;
	if (!READSHORT(ifp, &height))
		goto errout;
	pf->height = height;
	if (!READSHORT(ifp, &ascent))
		goto errout;
	pf->ascent = ascent;
	if (!READSHORT(ifp, &pad))
		goto errout;
	if (!READLONG(ifp, &firstchar))
		goto errout;
	pf->firstchar = firstchar;
	if (!READLONG(ifp, &defaultchar))
		goto errout;
	pf->defaultchar = defaultchar;
	if (!READLONG(ifp, &size))
		goto errout;
	pf->size = size;

	/* variable font data sizes*/
	/* # words of MWIMAGEBITS*/
	if (!READLONG(ifp, &nbits))
		goto errout;
	pf->bits = (MWIMAGEBITS *)malloc(nbits * sizeof(MWIMAGEBITS));
	if (!pf->bits)
		goto errout;
	pf->bits_size = nbits;

	/* # longs of offset*/
	if (!READLONG(ifp, &noffset))
		goto errout;
	if (noffset) {
		pf->offset = (unsigned long *)malloc(noffset * sizeof(unsigned long));
		if (!pf->offset)
			goto errout;
	}

	/* # bytes of width*/
	if (!READLONG(ifp, &nwidth))
		goto errout;
	if (nwidth) {
		pf->width = (unsigned char *)malloc(nwidth * sizeof(unsigned char));
		if (!pf->width)
			goto errout;
	}

	/* variable font data*/
	for (i=0; i<nbits; ++i)
		if (!READSHORT(ifp, &pf->bits[i]))
			goto errout;
	if (noffset)
		for (i=0; i<pf->size; ++i)
			if (!READLONG(ifp, &pf->offset[i]))
				goto errout;
	if (nwidth)
		for (i=0; i<pf->size; ++i)
			if (!READBYTE(ifp, &pf->width[i]))
				goto errout;
	
	fclose(ifp);
	return pf;	/* success!*/

errout:
	fclose(ifp);
	if (!pf)
		return NULL;
	if (pf->name)
		free(pf->name);
	if (pf->bits)
		free(pf->bits);
	if (pf->offset)
		free(pf->offset);
	if (pf->width)
		free(pf->width);
	free(pf);
	return NULL;
}
