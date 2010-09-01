/*
    Copyright (C) 2002 Robert Lipe, robertlipe@usa.net

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

#include <time.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "queue.h"

/*
 * Common definitions.   There should be no protocol or file-specific
 * data in this file.
 */


/*
 * A coordinate in space.
 */
typedef struct {
	double degrees;
} coord;


/*
 * An altitude is essentially a coordinate along only the Z axis.
 */

typedef struct {
	double altitude_meters;
} altitude;


/*
 * A triplet of the coordinates along the three axes describes
 * a position.
 */
typedef struct {
	coord latitude;
	coord longitude;
	altitude altitude;
} position;


/*
 * Define globally on which kind of data gpsbabel is working.
 * Important for "file types" that are essentially a communication
 * protocol for a receiver, like the Magellan serial data.
 */
typedef enum {
	trkdata = 1 ,
	wptdata,
	rtedata
} gpsdata_type;

typedef struct {
	int synthesize_shortnames;
	int debug_level;
	gpsdata_type objective;
} global_options;

extern global_options global_opts;

/*
 * Extended data if waypoint happens to represent a geocache.  This is 
 * totally voluntary data...
 */

typedef enum {
	gt_unknown = 0 ,
	gt_traditional,
	gt_multi,
	gt_virtual,
	gt_letterbox,
	gt_event,
	gt_suprise
} geocache_type;

typedef struct {
	geocache_type type;
	int diff; /* (multiplied by ten internally) */
	int terr; /* (likewise) */
} geocache_data ;



typedef struct xml_tag {
	char *tagname;
	char *cdata;
	int cdatalen;
	char *parentcdata;
	int parentcdatalen;
	char **attributes;
	struct xml_tag *parent;
	struct xml_tag *sibling;
	struct xml_tag *child;
} xml_tag ;

/*
 * This is a waypoint, as stored in the GPSR.   It tries to not 
 * cater to any specific model or protocol.  Anything that needs to
 * be truncated, edited, or otherwise trimmed should be done on the
 * way to the target.
 */
typedef struct {
	queue Q;
	position position;
	/* shortname is a waypoint name as stored in receiver.  It should
	 * strive to be, well, short, and unique.   Enforcing length and
	 * character restrictions is the job of the output.   A typical
	 * minimum length for shortname is 6 characters for NMEA units,
	 * 8 for Magellan and 10 for Vista.   These are only guidelines.
	 */
	char *shortname;	 
	/*
	 * description is typically a human readable description of the 
	 * waypoint.   It may be used as a comment field in some receivers.
	 * These are probably under 40 bytes, but that's only a guideline.
	 */
	char *description;
	/*
	 * notes are relatively long - over 100 characters - prose associated
	 * with the above.   Unlike shortname and description, these are never
	 * used to compute anything else and are strictly "passed through".
	 * Few formats support this.
	 */
	char *notes;
	char *url;
	char *url_link_text;
	const char *icon_descr;
	time_t creation_time;
	geocache_data gc_data;
	xml_tag *gpx_extras;
} waypoint;

typedef struct {
	queue Q;		/* Link onto parent list. */
	queue waypoint_list;	/* List of child waypoints */
	char *rte_name;
	char *rte_desc;
	int rte_num;
} route_head;

typedef void (*ff_init) (char const *, char const *);
typedef void (*ff_deinit) (void);
typedef void (*ff_read) (void);
typedef void (*ff_write) (void);
char * get_option(const char *iarglist, const char *argname);

void fprintdms(FILE *, const coord *, int);

typedef void (*waypt_cb) (const waypoint *);
typedef void (*route_hdr)(const route_head *);
typedef void (*route_trl)(const route_head *);
void waypt_add (waypoint *);
void waypt_del (waypoint *);
void waypt_disp_all(waypt_cb);
void route_disp_all(route_hdr, route_trl, waypt_cb);
unsigned int waypt_count(void);

route_head *route_head_alloc(void);
void route_add (waypoint *);
void route_add_wpt(route_head *rte, waypoint *wpt);
void route_add_head(route_head *rte);

char *mkshort (const char *);
void setshort_length(int n);
void setshort_badchars(const char *);
void setshort_mustupper(int n);
void setshort_whitespace_ok(int n);

typedef struct ff_vecs {
	ff_init rd_init;
	ff_init wr_init;
	ff_deinit rd_deinit;
	ff_deinit wr_deinit;
	ff_read read;
	ff_write write;
} ff_vecs_t;

void waypt_init(void);
void route_init(void);
void waypt_disp(const waypoint *);
void fatal(const char *, ...)
#if __GNUC__
	__attribute__ ((__format__ (__printf__, 1, 2)))
#endif
	;
ff_vecs_t *find_vec(char *, char **);
void disp_vecs(void);
void disp_formats(void);
void printposn(const coord *c, int is_lat);

void *xcalloc(size_t nmemb, size_t size);
void *xmalloc(size_t size);
char *xstrdup(const char *s);
void rtrim(char *s);
signed int get_tz_offset(void);

/*
 * PalmOS records like fixed-point numbers, which should be rounded
 * to deal with possible floating-point representation errors.
 */

signed int si_round( double d );

/* 
 * Data types for Palm/OS files.
 */
typedef struct {
	unsigned char data[4];
} pdb_32;

typedef struct {
	unsigned char data[2];
} pdb_16;

/*
 * Protypes for Endianness helpers.
 */

signed int be_read16(void *p);
signed int be_read32(void *p);
signed int le_read16(void *p);
signed int le_read32(void *p);
void be_write16(void *pp, unsigned i);
void be_write32(void *pp, unsigned i);
void le_write16(void *pp, unsigned i);
void le_write32(void *pp, unsigned i);

/*
 * A constant for unknown altitude.   It's tempting to just use zero
 * but that's not very nice for the folks near sea level.
 */
#define unknown_alt -99999999.0
