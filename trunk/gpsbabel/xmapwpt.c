/*
    Delorme XMap HandHeld .WPT Format 
    (as created by XMapHH Street Atlas/PPC)
    1296126539:1481466224:1895825408:1392508928:3137157:text:text:text\n

    Contributed to gpsbabel by Alex Mottram (geo_alexm at cox-internet.com)

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

#include "defs.h"
#include "csv_util.h"

#define MYNAME	"XMAPWPT"

static void
xmapwpt_set_style()
{
    /* set up the xmapwpt xcsv_file struct */
    xcsv_file_init();

    /* this is an internal style, don't mess with it */
    xcsv_file.is_internal = 1;

    /* how the file gets split up */
    xcsv_file.field_delimiter = ":";
    xcsv_file.record_delimiter = "\n";
    xcsv_file.badchars = ":";

    xcsv_ifield_add("CONSTANT", "1296126539", "%s");
    xcsv_ifield_add("CONSTANT", "1481466224", "%s");
    xcsv_ifield_add("LAT_INT32DEG", "", "%d");
    xcsv_ifield_add("LON_INT32DEG", "", "%d");
    xcsv_ifield_add("CONSTANT", "3137157", "%s");
    xcsv_ifield_add("SHORTNAME", "", "%-.31s");   
    xcsv_ifield_add("IGNORE", "", "%-.31s");      

    /* 
     * actual description len accepted is 79. however under win32, we
     * run the risk of the compiled app ending a line in \r\n when we 
     * say \n.  This, in turn, overruns a fixed len buffer and causes
     * XmapHH to die both occasionally and horribly.
     */
    xcsv_ifield_add("DESCRIPTION", "", "%-.78s"); 

    /* outfields are infields */
    if (xcsv_file.ofield)
        free(xcsv_file.ofield);
    xcsv_file.ofield = &xcsv_file.ifield;
    xcsv_file.ofield_ct = xcsv_file.ifield_ct;

    /* set up mkshort */
    if (global_opts.synthesize_shortnames) {
        setshort_length(32);
        setshort_whitespace_ok(0);
        setshort_badchars(xcsv_file.badchars);
    }
}

static void
xmapwpt_rd_init(const char *fname, const char *args)
{
    xmapwpt_set_style();

    xcsv_file.xcsvfp = fopen(fname, "r");
    
    if (xcsv_file.xcsvfp == NULL) {
        fatal(MYNAME ": Cannot open %s for reading\n", fname );
    }
}

static void
xmapwpt_wr_init(const char *fname, const char *args)
{
    xmapwpt_set_style();

    xcsv_file.xcsvfp = fopen(fname, "w");
    
    if (xcsv_file.xcsvfp == NULL) {
        fatal(MYNAME ": Cannot open %s for reading\n", fname );
    }
}

static void
xmapwpt_deinit(void)
{
    if (xcsv_file.xcsvfp)
        fclose(xcsv_file.xcsvfp);
        
    xcsv_destroy_style();
}

ff_vecs_t xmapwpt_vecs = {
    xmapwpt_rd_init,
    xmapwpt_wr_init,
    xmapwpt_deinit,
    xmapwpt_deinit,
    xcsv_data_read,
    xcsv_data_write,
};
