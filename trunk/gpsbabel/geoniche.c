/*
    Read and write GeoNiche files.

    Copyright (C) 2003 Rick Richardson <rickr@mn.rr.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111 USA

 */

#include "defs.h"
#include "coldsync/palm.h"
#include "coldsync/pdb.h"
#include "jeeps/gpsmath.h"
#include "garmin_tables.h"

#include <math.h>
#include <stdlib.h>

#define MYNAME		"Geoniche"
#define MYTYPE_ASC	0x50454e44  	/* PEND */
#define MYTYPE_BIN	0x44415441  	/* DATA */
#define MYCREATOR	0x47656f4e 	/* GeoN */

#undef GEONICHE_DBG

static FILE		*FileIn;
static FILE		*FileOut;
static const char	*FilenameOut;
static struct pdb	*PdbOut;

static char		Rec0Magic[] = "68000NV4Q2";

static char *Arg_dbname = NULL;
static char *Arg_category = NULL;

static
arglist_t Args[] = {
	{"dbname", &Arg_dbname,
	    "Database name (filename)", NULL, ARGTYPE_STRING, ARG_NOMINMAX },
	{"category", &Arg_category,
	    "Category name (Cache)", NULL, ARGTYPE_STRING, ARG_NOMINMAX },
	ARG_TERMINATOR
};

#define	ARG_FREE(X) do { if (X) { xfree(X); X = NULL; } } while (0)

/*
 * Conversions between gc.com ID's and GID's
 */
static char	GcSet[] = "0123456789ABCDEFGHJKMNPQRTVWXYZ";
static int	GcOffset = 16 * 31 * 31 * 31 - 65536;

static int
gid2id(char *gid)
{
    char	*p;
    int		i, val;

    if (strncmp(gid, "GC", 2) != 0)
	return -1;
    if (strlen(gid) != 6)
	return -1;
    gid += 2;

    if (strcmp(gid, "G000") < 0)
	return strtol(gid, NULL, 16);

    for (val = i = 0; i < 4; ++i)
    {
	val *= 31;
	p = strchr(GcSet, gid[i]);
	if (!p) return -1;
	val += p - GcSet;
    }
    return val - GcOffset;
}

static void
id2gid(char gid[6+1], int id)
{
    gid[0] = 0;
    if (id < 0)
	return;
    else if (id < 65536)
	snprintf(gid, 6+1, "GC%04X", id);
    else
    {
	int	i;

	id += GcOffset;
	gid[0] = 'G';
	gid[1] = 'C';
	for (i = 5; i >= 2; --i) {
	    gid[i] = GcSet[id%31];
	    id /= 31;
	}
	gid[6] = 0;
	if (id)
	    gid[0] = 0;
    }
    return;
}

static void
rd_init(const char *fname)
{
    FileIn = xfopen(fname, "rb", MYNAME);
}

static void
rd_deinit(void)
{
    fclose(FileIn);
    ARG_FREE(Arg_dbname);
    ARG_FREE(Arg_category);
}

static void
wr_init(const char *fname)
{
    FileOut = xfopen(fname, "wb", MYNAME);
    FilenameOut = fname;
}

static void
wr_deinit(void)
{
    fclose(FileOut);
    ARG_FREE(Arg_dbname);
    ARG_FREE(Arg_category);
}

static char *
field(char **pp, int *lenp)
{
    int		len = *lenp;
    char	*p = *pp;
    char	*dp, *dbuf;
    int		state = 0;

    if (len == 0 || *p == 0)
	return NULL;

    dbuf = dp = xmalloc(len);
    while (len)
    {
	char	ch;

	ch = *p++;
	--len;
	if (ch == 0 || len == 0)
	    break;
	switch (state)
	{
	case 0:
	    if (ch == '\\')
		state = 1;
	    else if (ch == ',')
		goto eof;
	    else
		*dp++ = ch;
	    break;
	default:
	    *dp++ = ch;
	    state = 0;
	    break;
	}
    }
eof:
    *dp++ = 0;
    dbuf = xrealloc(dbuf, dp - dbuf);
    /* fprintf(stderr, "<%.8s> dbuf=%x, len=%d\n", *pp, dbuf, len); */
    *pp = p;
    *lenp = len;
    return dbuf;
}

static void
geoniche_read_asc(const struct pdb *pdb)
{
    struct pdb_record *pdb_rec;

    /* Process record 0 */
    pdb_rec = pdb->rec_index.rec;
    if (strcmp((char *) pdb_rec->data, Rec0Magic))
	fatal(MYNAME ": Bad record 0, not a GeoNiche file.\n");
    pdb_rec = pdb_rec->next;

    /* Process the rest of the records */
    for (; pdb_rec; pdb_rec = pdb_rec->next)
    {
	waypoint	*wpt;
	char		*vdata;
	int		vlen;
	char		*p;

	int		id;
	int		route_id;
	char		*title;
	char		*category;
	double		lat, lon, alt;
	char		*datestr, *timestr;
	int		icon;
	char		*notes;
	char		gid[6+1];
	struct tm	tm;
	
	memset(&tm, 0, sizeof(tm));

	wpt = waypt_new();
	if (!wpt)
	    fatal(MYNAME ": Couldn't allocate waypoint.\n");
	vdata = (char *) pdb_rec->data;
	vlen = pdb_rec->data_len;

	/* Field 1: Target */
	p = field(&vdata, &vlen);
	if (!p) fatal(MYNAME ": Premature EOD processing field 1.\n");
	if (strcmp(p, "Route") == 0)
	    fatal(MYNAME ": Route record type is not implemented.\n");
	if (strcmp(p, "Target"))
	    fatal(MYNAME ": Unknown record type '%s'.\n", p);
	xfree(p);

	/* Field 2: Import ID number */
	p = field(&vdata, &vlen);
	if (!p) fatal(MYNAME ": Premature EOD processing field 2.\n");
	id = atoi(p);
	xfree(p);

	/* Field 3: Title */
	p = field(&vdata, &vlen);
	if (!p) fatal(MYNAME ": Premature EOD processing field 3.\n");
	title = p;

	/* Field 4: Route ID number */
	p = field(&vdata, &vlen);
	if (!p) fatal(MYNAME ": Premature EOD processing field 4.\n");
	route_id = atoi(p);
	xfree(p);

	/* Field 5: Category */
	p = field(&vdata, &vlen);
	if (!p) fatal(MYNAME ": Premature EOD processing field 5.\n");
	category = p;

	/* Field 6: Latitude */
	p = field(&vdata, &vlen);
	if (!p) fatal(MYNAME ": Premature EOD processing field 6.\n");
	lat = atof(p);
	xfree(p);

	/* Field 7: Longitude */
	p = field(&vdata, &vlen);
	if (!p) fatal(MYNAME ": Premature EOD processing field 7.\n");
	lon = atof(p);
	xfree(p);

	/* Field 8: Altitude */
	p = field(&vdata, &vlen);
	if (!p) fatal(MYNAME ": Premature EOD processing field 8.\n");
	alt = atof(p);
	xfree(p);

	/* Field 9: Creation date */
	p = field(&vdata, &vlen);
	if (!p) fatal(MYNAME ": Premature EOD processing field 9.\n");
	datestr = p;

	/* Field 10: Creation time */
	p = field(&vdata, &vlen);
	if (!p) fatal(MYNAME ": Premature EOD processing field 10.\n");
	timestr = p;

	/* Field 11: Visited date */
	p = field(&vdata, &vlen);
	if (!p) fatal(MYNAME ": Premature EOD processing field 11.\n");
	xfree(p);

	/* Field 12: Visited time */
	p = field(&vdata, &vlen);
	if (!p) fatal(MYNAME ": Premature EOD processing field 12.\n");
	xfree(p);

	/* Field 13: Icon color (R G B) */
	p = field(&vdata, &vlen);
	if (!p) fatal(MYNAME ": Premature EOD processing field 13.\n");
	xfree(p);

	/* Field 14: icon number */
	p = field(&vdata, &vlen);
	if (!p) fatal(MYNAME ": Premature EOD processing field 14.\n");
	icon = atoi(p);
	xfree(p);

	/* Field 15: unused */
	p = field(&vdata, &vlen);
	if (!p) fatal(MYNAME ": Premature EOD processing field 15.\n");
	xfree(p);

	/* Field 16: unused */
	p = field(&vdata, &vlen);
	if (!p) fatal(MYNAME ": Premature EOD processing field 16.\n");
	xfree(p);

	/* Field 17: unused */
	p = field(&vdata, &vlen);
	if (!p) fatal(MYNAME ": Premature EOD processing field 17.\n");
	xfree(p);

	/* Field 18: Notes */
	p = field(&vdata, &vlen);
	if (!p) fatal(MYNAME ": Premature EOD processing field 18.\n");
	notes = p;

	sscanf(datestr, "%d/%d/%d", &tm.tm_mon, &tm.tm_mday, &tm.tm_year);
	tm.tm_mon -= 1;
	tm.tm_year -= 1900;
	sscanf(timestr, "%d:%d:%d", &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
	if (tm.tm_year >= 1970) {
	    wpt->creation_time = mktime(&tm);
	}
	xfree(datestr);
	xfree(timestr);

	id2gid(gid, id);
	wpt->latitude = lat;
	wpt->longitude = lon;
	wpt->altitude = alt;
	wpt->icon_descr = category;
	wpt->wpt_flags.icon_descr_is_dynamic = 1;

	if (gid[0])
	{
	    wpt->shortname = xstrdup(gid);
	    wpt->description = title;
	    wpt->notes = notes;
	}
	else
	{
	    wpt->shortname = xstrdup(title);
	    wpt->description = title;
	    wpt->notes = notes;
	}

	waypt_add(wpt);
    } 
}

static char *geoniche_icon_map[] = 			/* MPS */
{
	 /* 21 */ "Cross",
	 /* 22 */ "Cross (light)",			
	 /* 23 */ "Cross (little)",
	 /* 24 */ "Cross (straight)",
	 /* 25 */ "Cross (light straight)",
	 /* 26 */ "Cross (little straight)",
	 /* 27 */ NULL,
	 /* 28 */ NULL,
	 /* 29 */ NULL,
	 /* 2A */ "Flag",
	 /* 2B */ "Car",				/* 56 */
	 /* 2C */ "Gas Station",			/* 8 */
	 /* 2D */ "Observation Point",
	 /* 2E */ "Scenic Area",			/* 48 */
	 /* 2F */ "City",
	 /* 30 */ "Mountains",
	 /* 31 */ "Park",				/* 46 */
	 /* 32 */ "Forest",				/* 105 */
	 /* 33 */ "Campground",				/* 38 */
	 /* 34 */ NULL,
	 /* 35 */ "Men",
	 /* 36 */ "Woman",
	 /* 37 */ "Hotel",				/* 59 */
	 /* 38 */ "Residence",				/* 10 */
	 /* 39 */ "Restaurant",				/* 11 */
	 /* 3A */ "Cafe",
	 /* 3B */ NULL,
	 /* 3C */ "Airport",				/* 107 */
	 /* 3D */ "Medical Facility",			/* 43 */
	 /* 3E */ "Ropeway",
	 /* 3F */ "Sailing Area",
	 /* 40 */ "Anchor",
	 /* 41 */ NULL, /* Half Anchor ??? */
	 /* 42 */ "Fishing Area",			/* 7 */
	 /* 43 */ "Stop Sign",
	 /* 44 */ "Question Sign",
	 /* 45 */ NULL,
	 /* 46 */ NULL,
	 /* 47 */ "Euro Sign",
	 /* 48 */ "Bank",				/* 6 */
	 /* 49 */ NULL,
	 /* 4A */ "Left Arrow",
	 /* 4B */ "Right Arrow",
	 /* 4C */ "Traditional Cache",
	 /* 4D */ "Multi-Cache",			/* 86 */
	 /* 4E */ "Virtual Cache",			/* 48 */
	 /* 4F */ "Letterbox Cache",	 		
	 /* 50 */ "Event Cache",			/* 47 */
	 /* 51 */ "Webcam Cache",			/* 90 */
	 /* 52 */ "Mystery or puzzle Cache",
};

static char *
geoniche_icon_to_descr(const int no)
{
    char *result = NULL;

    if (no >= 0x21) 
    {
	int i = no - 0x21;
	if (i <= 49) 
	{
	    result = geoniche_icon_map[i];
	}
    }
    if (result != NULL) 
    {
	result = xstrdup(result);
    }
    return result;
}

static void
geoniche_read_bin(const struct pdb *pdb)
{
    struct pdb_record *pdb_rec;

    /* Process records */
    
    for (pdb_rec = pdb->rec_index.rec; pdb_rec != NULL; pdb_rec = pdb_rec->next)
    {
	char *vdata = (char *) pdb_rec->data;
	struct tm created, visited;
	int icon_nr, selected;
	int latdeg, londeg;
	double lat, lon, altitude;
	waypoint *waypt;
	
	memset(&visited, 0, sizeof(visited));
	memset(&created, 0, sizeof(created));
	
	latdeg = be_read16(vdata + 0);
	lat = be_read32(vdata + 2);
	londeg = be_read16(vdata + 6);
	lon = be_read32(vdata + 8);
	altitude = (float) be_read32(vdata + 12);
	selected = vdata[16];
	created.tm_min = be_read16(vdata + 20);
	created.tm_hour = be_read16(vdata + 22);
	created.tm_mday = be_read16(vdata + 24);
	created.tm_mon = be_read16(vdata + 26);
	created.tm_year = be_read16(vdata + 28);
	visited.tm_min = be_read16(vdata + 34);
	visited.tm_hour = be_read16(vdata + 36);
	visited.tm_mday = be_read16(vdata + 38);
	visited.tm_mon = be_read16(vdata + 40);
	visited.tm_year = be_read16(vdata + 42);

#ifdef GEONICHE_DBG
	printf(MYNAME "-date: %04d/%02d/%02d, %02d:%02d (%04d/%02d/%02d, %02d:%02d)\n", 
	    created.tm_year, created.tm_mon, created.tm_mday, created.tm_hour, created.tm_min,
	    visited.tm_year, visited.tm_mon, visited.tm_mday, visited.tm_hour, visited.tm_min);
#endif
	icon_nr = vdata[62];
	
	latdeg = 89 - latdeg;
	lat = lat * (double) 0.0000006;
	if (latdeg >= 0) 
	    lat = (double) 60.0 - lat;
	else
	    latdeg++;

	lon = lon * (double) 0.0000006;
	while (londeg >= 360) londeg-=360;
	if (londeg > 180)
	{
	    lon = (double) 60.0 - lon;
	    londeg = londeg - 359;
	}

	created.tm_year-=1900;
	created.tm_mon--;

	waypt = waypt_new();
	
	waypt->shortname = xstrdup(vdata + 63);
	waypt->altitude = altitude;
	waypt->creation_time = mkgmtime(&created);
	
	GPS_Math_DegMin_To_Deg(latdeg, lat, &waypt->latitude);
	GPS_Math_DegMin_To_Deg(londeg, lon, &waypt->longitude);
	
	waypt->icon_descr = geoniche_icon_to_descr(icon_nr);
	if (waypt->icon_descr != NULL)
		waypt->wpt_flags.icon_descr_is_dynamic = 1;
	
	waypt_add(waypt);
    }
}

static void
data_read(void)
{
    struct pdb *pdb;

    if (NULL == (pdb = pdb_Read(fileno(FileIn))))
	fatal(MYNAME ": pdb_Read failed\n");

    if (pdb->creator != MYCREATOR)
	fatal(MYNAME ": Not a GeoNiche file.\n");
	
    switch(pdb->type)
    {
	case MYTYPE_ASC:
	    geoniche_read_asc(pdb);
	    break;
	case MYTYPE_BIN:
	    geoniche_read_bin(pdb);
	    break;
	default:
	    fatal(MYNAME ": Unsupported GeoNiche file.\n");
    }

    free_pdb(pdb);
}

static char *
enscape(char *s)
{
    char	*buf, *d;

    if (!s)
    {
	d =  xmalloc(1);
	*d = 0;
	return d;
    }
    buf = d = xmalloc(strlen(s) * 2 + 1);
    for (; *s; ++s)
    {
	if (*s == '\\' || *s == ',')
	    *d++ = '\\';
	*d++ = *s;
    }

    *d = 0;
    return buf;
}

/*
 * Attempt to map an icon description into a GeoNiche icon number
 */
static int
wpt2icon(const waypoint *wpt)
{
    const char	*desc = wpt->icon_descr;

    if (!desc) return 0;
    else if (strstr(desc, "reg")) return 43;
    else if (strstr(desc, "trad")) return 43;
    else if (strstr(desc, "multi")) return 44;
    else if (strstr(desc, "offset")) return 44;
    else if (strstr(desc, "virt")) return 45;
    else if (strstr(desc, "loca")) return 45;
    else if (strstr(desc, "event")) return 46;
    else if (strstr(desc, "lett")) return 47;
    else if (strstr(desc, "hyb")) return 47;
    else if (strstr(desc, "unk")) return 48;
    else if (strstr(desc, "cam")) return 49;
    else return 0;
}

static void
copilot_writewpt(const waypoint *wpt)
{
    static int		ct = 0;
    struct pdb_record	*opdb_rec;
    int			vlen;
    static int		vsize = 4096;
    ubyte		*vdata;
    char		*title;
    struct tm		tm;
    char		datestr[10+1];
    char		timestr[8+1];
    char		*notes;
    int			id;
    time_t		tx;

    if (ct == 0)
    {
	opdb_rec = new_Record (0, 0, ct++, sizeof(Rec0Magic), (ubyte *) Rec0Magic);	       
	if (opdb_rec == NULL)
	    fatal(MYNAME ": libpdb couldn't create record\n");
	if (pdb_AppendRecord(PdbOut, opdb_rec))
	    fatal(MYNAME ": libpdb couldn't append record\n");
    }

    if (wpt->description[0])
	title = enscape(wpt->description);
    else
	title = enscape(wpt->shortname);

    id = gid2id(wpt->shortname);
    if (id < 0)
	id = ct;

    tx = (wpt->creation_time != 0) ? wpt->creation_time : gpsbabel_time;
    if (tx == 0) {	/* maybe zero during testo (freezed time) */
	strcpy(datestr, "01/01/1904");	/* this seems to be the uninitialized date value for geoniche */
	strcpy(timestr, "00:00:00");
    }
    else  {
	tm = *localtime(&tx);
	strftime(datestr, sizeof(datestr), "%m/%d/%Y", &tm);
	strftime(timestr, sizeof(timestr), "%H:%M:%S", &tm);
    }

    /* Notes field MUST have soemthing in it */
    if (!wpt->notes || wpt->notes[0] == 0)
	notes = xstrdup(title);
    else
	notes = enscape(wpt->notes);

    vdata = (ubyte *) xmalloc(vsize);
    if (vdata == NULL)
	fatal(MYNAME ": libpdb couldn't get record memory\n");

    for (;;)
    {
	vlen = snprintf((char *) vdata, vsize,
	    "Target,%d,%s,,%s,%f,%f,%f,%s,%s,,,,%d,,,,%s"
	    , id
	    , title
	    /* route ID */
	    , Arg_category ? Arg_category : "Cache"
	    , wpt->latitude
	    , wpt->longitude
	    , wpt->altitude
	    , datestr
	    , timestr
	    /* visited date */
	    /* visited time */
	    /* icon color R G B */
	    , wpt2icon(wpt)
	    /* unused1 */
	    /* unused2 */
	    /* unused3 */
	    , notes
	    );

	if (vlen > -1 && vlen < vsize)
	    break;

	/* try again with more space. */
	if (vlen > -1)
	    vsize = vlen + 1;
	else
	    vsize *= 2;
	vdata = (ubyte *) xrealloc(vdata, vsize);
	if (vdata == NULL)
	    fatal(MYNAME ": libpdb couldn't get record memory\n");
    }

    opdb_rec = new_Record (0, 0, ct++, (uword) (vlen+1), vdata);	       

    if (opdb_rec == NULL)
	fatal(MYNAME ": libpdb couldn't create record\n");
    if (pdb_AppendRecord(PdbOut, opdb_rec))
	fatal(MYNAME ": libpdb couldn't append record\n");

    xfree(notes);
    xfree(title);
    xfree(vdata);
}

static void
data_write(void)
{
    if (NULL == (PdbOut = new_pdb()))
	fatal (MYNAME ": new_pdb failed\n");

    if (Arg_dbname) {
	if (case_ignore_strcmp(Arg_dbname, "GeoNiche Targets") == 0)
	    fatal(MYNAME ": Reserved database name!\n");
	strncpy(PdbOut->name, Arg_dbname, PDB_DBNAMELEN);
    }
    else
	strncpy(PdbOut->name, FilenameOut, PDB_DBNAMELEN);
    PdbOut->name[PDB_DBNAMELEN-1] = 0;

    PdbOut->attributes = PDB_ATTR_BACKUP;
    PdbOut->ctime = PdbOut->mtime = current_time() + (49*365 + 17*366) * (60*60*24);
    PdbOut->type = MYTYPE_ASC;
    PdbOut->creator = MYCREATOR; 
    PdbOut->version = 0;
    PdbOut->modnum = 1;

    waypt_disp_all(copilot_writewpt);
    
    pdb_Write(PdbOut, fileno(FileOut));

    free_pdb(PdbOut);
}


ff_vecs_t geoniche_vecs =
{
	ff_type_file,
	FF_CAP_RW_WPT,
	rd_init,
	wr_init,
	rd_deinit,
	wr_deinit,
	data_read,
	data_write,
	NULL, 
	Args,
	CET_CHARSET_MS_ANSI, 0	/* CET-REVIEW */
};
