/*
    National Geographic Topo! Waypoint
    Contributed to gpsbabel by Steve Chamberlin
    
    Copyright (C) 2005 Steve Chamberlin, slc at alum.mit.edu

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


/*
 ***** This format has problems and the author hasn't returned emails
 * on it, so we've set the format type to 'ff_type_internal' to make it
 * disappear from the various lists...
 */


#include "defs.h"
#include <string.h>
#include <ctype.h>
#include "jeeps/gpsmath.h" /* for datum conversions */

#define MYNAME	"TPO"

static char *dumpheader = NULL;
static char *output_state = NULL;
  
static
arglist_t tpo_args[] = {
	{ "dumpheader", &dumpheader, "Display the file header bytes", 
		"0", ARGTYPE_BOOL, ARG_NOMINMAX} , 
	{ "state", &output_state, "State map format to write, default=CA", 
	  "CA", ARGTYPE_STRING, ARG_NOMINMAX} , 
	ARG_TERMINATOR
};

static FILE *tpo_file_in;
static FILE *tpo_file_out;
static short_handle mkshort_handle;

static double output_track_lon_scale;
static double output_track_lat_scale;

static unsigned int track_out_count;
static double first_track_waypoint_lat;
static double first_track_waypoint_lon;
static double track_length;
static double last_waypoint_x;
static double last_waypoint_y;
static double last_waypoint_z;

/*******************************************************************************/
/*                                      READ                                   */
/*******************************************************************************/
 
static int
tpo_fread(void *buff, size_t size, size_t members, FILE * fp) 
{
    size_t br;

    br = fread(buff, size, members, fp);

    if (br != members) {
        fatal(MYNAME ": The input file does not look like a valid .TPO file.\n");
    }

    return (br);
}

static double
tpo_fread_double(FILE *fp)
{
	unsigned char buf[8];
	unsigned char sbuf[8];

	tpo_fread(buf, 1, 8, fp);
	le_read64(sbuf, buf);
	return *(double *)sbuf;
}

static void
tpo_fwrite_double(double x, FILE *fp)
{
	unsigned char *cptr = (unsigned char *)&x;
	unsigned char cbuf[8];

	le_read64(cbuf, cptr);
	fwrite(cbuf, 8, 1, fp);
}

/* tpo_check_version_string()
   Check the first bytes of the file for a version 3.0 header. */
static void
tpo_check_version_string()
{

	char string_size;
	char* string_buffer;
	char* v3_id_string = "TOPO! Ver";

	/* read the id string */
	tpo_fread(&string_size, 1, 1, tpo_file_in);
	string_buffer = xmalloc(string_size+1);
	tpo_fread(string_buffer, 1, string_size, tpo_file_in);
	
	/* terminate the strig */
	string_buffer[string_size] = 0;

	/* check for the presence of a 3.0-style id string */
	if (strncmp(v3_id_string, string_buffer, strlen(v3_id_string)) == 0)
	{
		fatal(MYNAME ": gpsbabel can only read TPO version 2.7.7 or below; this file is %s\n", string_buffer);
	}
	else {
		/* seek back to the beginning of the file */
		fseek(tpo_file_in, -(string_size+1), SEEK_CUR);
	}
	xfree(string_buffer);

}

static void
/* tpo_dump_header_bytes(int header_size)
   Write the first header_size bytes of the file to standard output
   as a C array definition. */
tpo_dump_header_bytes(int header_size)
{
	int i;
	unsigned char* buffer = (unsigned char*)xmalloc(header_size);

	tpo_fread(buffer, 1, header_size, tpo_file_in);

	printf("unsigned char header_bytes[] = {\n");
	
	for (i=0; i<header_size; i++) {
		if (i%8 == 0)
			printf("    ");
		printf("0x%02X", buffer[i]);
		if (i != header_size-1)
			printf(", ");
		if (i%8 == 7)
			printf("\n");
	}

	printf("};\n");

	xfree(buffer);
}

/* tpo_read_until_section()
   Keep reading bytes from the file until the section name is encountered,
   then seek backwards to the start of the section data. */
static void
tpo_read_until_section(const char* section_name)
{
	char byte;
	unsigned int match_index = 0;
	int header_size = 0;

	while (1) {
		tpo_fread(&byte, 1, 1, tpo_file_in);
		header_size++;

		if (byte == section_name[match_index]) {
			match_index++;
			if (match_index == strlen(section_name)) {
				fseek(tpo_file_in, -18, SEEK_CUR);
				header_size -= 18;
				if (dumpheader && dumpheader[0] == '1') {
					fseek(tpo_file_in, -header_size, SEEK_CUR);
					tpo_dump_header_bytes(header_size);
				}
				return;
			}
		}
		else {
			match_index = 0;
		}
	}
}

static void
tpo_rd_init(const char *fname)
{
	if (doing_wpts || doing_rtes)
	{
		fatal(MYNAME ": this file format only supports tracks, not waypoints or routes.\n");
	}
	
	tpo_file_in = xfopen(fname, "rb", MYNAME);
	tpo_check_version_string();
	tpo_read_until_section("CTopoRoute");
}

static void
tpo_rd_deinit(void)
{
	fclose(tpo_file_in);
}

static void
tpo_read(void)
{
	char buff[16];
	short track_count, waypoint_count;
	double first_lat, first_lon, lat_scale, lon_scale, amt;
	short *lon_delta, *lat_delta;
	int i, j;
	route_head* track_temp;
	waypoint* waypoint_temp;	

	/* track count */
	tpo_fread(&buff[0], 1, 2, tpo_file_in);
	track_count = le_read16(&buff[0]);
	
	/* 4 unknown bytes */
	tpo_fread(&buff[0], 1, 4, tpo_file_in);

	/* chunk name: "CTopoRoute" */
	tpo_fread(&buff[0], 1, 12, tpo_file_in);

	for (i=0; i<track_count; i++) {

		track_temp = route_head_alloc();
		track_add_head(track_temp);

		/* generate a generic track name */
		sprintf(buff, "Track %d", i+1);
		track_temp->rte_name = xstrdup(buff);

		/* zoom level 1-5 visibility flags */
		tpo_fread(&buff[0], 1, 10, tpo_file_in);

		/* 8 bytes of zeros, meaning unknown */
		tpo_fread(&buff[0], 1, 8, tpo_file_in);

		/* 4 more unknown bytes, possibly sign flags for the longitude and latitude? */
		tpo_fread(&buff[0], 1, 4, tpo_file_in);

        /* read the position of the initial track point */
		/* for some very odd reason, signs on longitude are swapped */
        /* coordinates are in NAD27/CONUS datum                     */
            
        /* 8 bytes - longitude, sign swapped  */
	    first_lon = tpo_fread_double(tpo_file_in);

        /* 8 bytes - latitude */
	    first_lat = tpo_fread_double(tpo_file_in);
	    
        /* swap sign before we do datum conversions */
	    first_lon *= -1.0;

		/* 8 unknown bytes: seems to be some kind of bounding box info */
		tpo_fread(&buff[0], 1, 8, tpo_file_in);

		/* number of route points */
		tpo_fread(&buff[0], 1, 2, tpo_file_in);
		waypoint_count = le_read16(&buff[0]);

		/* allocate temporary memory for the waypoint deltas */
		lon_delta = (short*)xmalloc(waypoint_count * sizeof(short));
		lat_delta = (short*)xmalloc(waypoint_count * sizeof(short));

		for (j=0; j<waypoint_count; j++) {
			
			/* get this point's longitude delta from the first waypoint */
			tpo_fread(&buff[0], 1, 2, tpo_file_in);
			lon_delta[j] = le_read16(&buff[0]);

			/* get this point's latitude delta from the first waypoint */
			tpo_fread(&buff[0], 1, 2, tpo_file_in);
			lat_delta[j] = le_read16(&buff[0]);
		}

		/* 8 bytes - longitude delta to degrees scale  */
	    lon_scale = tpo_fread_double(tpo_file_in);

        /* 8 bytes - latitude delta to degrees scale */
	    lat_scale = tpo_fread_double(tpo_file_in);

		/* 4 bytes: the total length of the route in feet*/
		tpo_fread(&buff[0], 1, 4, tpo_file_in);

		/* 2 unknown bytes */
		tpo_fread(&buff[0], 1, 2, tpo_file_in);

		/* 2 bytes: continuation marker */
		tpo_fread(&buff[0], 1, 2, tpo_file_in);

		/* multiply all the deltas by the scaling factors to determine the waypoint positions */
		for (j=0; j<waypoint_count; j++) {
			
			waypoint_temp = waypt_new();

			/* convert incoming NAD27/CONUS coordinates to WGS84 */
			GPS_Math_Known_Datum_To_WGS84_M(
				first_lat-lat_delta[j]*lat_scale, 
				first_lon+lon_delta[j]*lon_scale,
				0.0,
				&waypoint_temp->latitude,
				&waypoint_temp->longitude,
				&amt,
				78);
			
			/* there is no elevation data for the waypoints */
			waypoint_temp->altitude = 0;

			route_add_wpt(track_temp, waypoint_temp);
		}	

		/* free temporary memory */
		xfree(lon_delta);
		xfree(lat_delta);
	}
}

/*******************************************************************************/
/*                                     WRITE                                   */
/*******************************************************************************/

/* tpo_write_file_header()
   Write the appropriate header for the desired TOPO! state. 
   
   National Geographic sells about 75 different state and regional software 
   programs called TOPO! that use the TPO format. Each one uses a different 
   header data sequence. The header contains the name of the state maps, as well 
   as some map scaling information and other data. In most cases, you can't open
   a TPO file created by a different state/regional version of TOPO! than the one
   you're using yourself. When writing a TPO file, it is therefore necessary to 
   specify what TOPO! state product to create the file for. I believe that the 
   TOPO! regional products can open TPO files created by the TOPO! state products
   as long as the track data is within the area covered by the regional product.
   As a result, it's only necessary to decide what state product output format to
   use.

   TO ADD SUPPORT FOR ANOTHER STATE:
   1. Obtain an example .tpo file generated by the state product for which you wish
   to add support. National Geographic MapXchange (http://maps.nationalgeographic.com/topo/search.cfm) 
   is a good source of .tpo files.
   2. Run gpsbabel using the "dumpheader" option of the TPO format converter, and
   specifying a dummy ouput file. For example:
     gpsbabel -t -i tpo,dumpheader=1 -f sample_file.tpo -o csv -F dummy.txt
   This will write a snippet of C code containing the header bytes to the shell window.  
   3. Add a new if() clause to tpo_write_file_header(). Copy the header bytes definition 
   from the previous step.
   4. Recompile gpsbabel.
   5. You should now be able write TPO ouput in the new state's format. For example, if
      you added support for Texas:
     gpsbabel -t -i gpx -f input.gpx -o tpo,state="TX" -F output.tpo */
static void
tpo_write_file_header()
{
	int i;
	
	/* force upper-case state name */
	for (i=0; i<(int)strlen(output_state); i++) {
		output_state[i] = toupper(output_state[i]);
	}
	
	if (strncmp("CA", output_state, 2) == 0) {

		unsigned char header_bytes[] = {
			0x18, 0x43, 0x61, 0x6C, 0x69, 0x66, 0x6F, 0x72,
			0x6E, 0x69, 0x61, 0x20, 0x53, 0x68, 0x61, 0x64,
			0x65, 0x64, 0x20, 0x52, 0x65, 0x6C, 0x69, 0x65,
			0x66, 0x03, 0x43, 0x41, 0x31, 0x05, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x40, 0x5F, 0x40, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x45, 0x40, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x80, 0x5C, 0x40, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x40, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x27, 0x43, 0x3A, 0x5C,
			0x50, 0x72, 0x6F, 0x67, 0x72, 0x61, 0x6D, 0x20,
			0x46, 0x69, 0x6C, 0x65, 0x73, 0x5C, 0x54, 0x4F,
			0x50, 0x4F, 0x21, 0x5C, 0x54, 0x50, 0x4F, 0x5F,
			0x44, 0x41, 0x54, 0x41, 0x5C, 0x43, 0x41, 0x5F,
			0x44, 0x30, 0x31, 0x5C, 0x20, 0x43, 0x3A, 0x5C,
			0x50, 0x72, 0x6F, 0x67, 0x72, 0x61, 0x6D, 0x20,
			0x46, 0x69, 0x6C, 0x65, 0x73, 0x5C, 0x54, 0x4F,
			0x50, 0x4F, 0x21, 0x5C, 0x54, 0x50, 0x4F, 0x5F,
			0x44, 0x41, 0x54, 0x41, 0x5C, 0x12, 0x43, 0x3A,
			0x5C, 0x54, 0x4F, 0x50, 0x4F, 0x21, 0x5C, 0x54,
			0x50, 0x4F, 0x5F, 0x44, 0x41, 0x54, 0x41, 0x5C,
			0x00, 0x00, 0x00, 0xDC, 0x30, 0x32, 0x30, 0x32,
			0x30, 0x32, 0x30, 0x33, 0x30, 0x33, 0x30, 0x30,
			0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			0x30, 0x30, 0x30, 0x32, 0x30, 0x32, 0x30, 0x32,
			0x30, 0x33, 0x30, 0x33, 0x30, 0x30, 0x30, 0x30,
			0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			0x30, 0x34, 0x30, 0x34, 0x30, 0x34, 0x30, 0x34,
			0x30, 0x36, 0x30, 0x36, 0x30, 0x30, 0x30, 0x30,
			0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			0x30, 0x35, 0x30, 0x35, 0x30, 0x34, 0x30, 0x36,
			0x30, 0x36, 0x30, 0x36, 0x30, 0x30, 0x30, 0x30,
			0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x35,
			0x30, 0x35, 0x30, 0x35, 0x30, 0x36, 0x30, 0x37,
			0x30, 0x37, 0x30, 0x38, 0x30, 0x30, 0x30, 0x30,
			0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x35,
			0x30, 0x35, 0x30, 0x37, 0x30, 0x37, 0x30, 0x37,
			0x30, 0x38, 0x30, 0x38, 0x30, 0x39, 0x30, 0x30,
			0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x35,
			0x31, 0x30, 0x30, 0x38, 0x30, 0x38, 0x30, 0x38,
			0x30, 0x39, 0x30, 0x39, 0x30, 0x39, 0x30, 0x30,
			0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x31, 0x30,
			0x31, 0x30, 0x31, 0x30, 0x31, 0x30, 0x31, 0x31,
			0x30, 0x39, 0x30, 0x39, 0x30, 0x30, 0x30, 0x30,
			0x30, 0x30, 0x30, 0x30, 0x31, 0x30, 0x31, 0x30,
			0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31,
			0x30, 0x39, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x31, 0x31,
			0x31, 0x31, 0x31, 0x31, 0x30, 0x39, 0x30, 0x39,
			0x0D, 0x55, 0x6E, 0x69, 0x74, 0x65, 0x64, 0x20,
			0x53, 0x74, 0x61, 0x74, 0x65, 0x73, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x40, 0x5F, 0x40, 0xBC, 0x23,
			0x63, 0xB5, 0xF9, 0x3A, 0x50, 0x40, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x80, 0x50, 0x40, 0x22, 0xE2,
			0xE6, 0x54, 0x32, 0x28, 0x22, 0x40, 0x0A, 0x43,
			0x61, 0x6C, 0x69, 0x66, 0x6F, 0x72, 0x6E, 0x69,
			0x61, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x5F,
			0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x45,
			0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x5C,
			0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40,
			0x40, 0x16, 0x2A, 0x47, 0x65, 0x6E, 0x65, 0x72,
			0x61, 0x6C, 0x20, 0x52, 0x65, 0x66, 0x65, 0x72,
			0x65, 0x6E, 0x63, 0x65, 0x20, 0x4D, 0x61, 0x70,
			0x00, 0x09, 0x3D, 0x00, 0x0C, 0x43, 0x41, 0x31,
			0x5F, 0x4D, 0x41, 0x50, 0x31, 0x5C, 0x53, 0x31,
			0x4C, 0xAF, 0x02, 0x15, 0x03, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x26, 0x40, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x24, 0x40, 0x84, 0x00, 0x78,
			0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x16,
			0x2A, 0x4E, 0x61, 0x74, 0x69, 0x6F, 0x6E, 0x61,
			0x6C, 0x20, 0x41, 0x74, 0x6C, 0x61, 0x73, 0x20,
			0x53, 0x65, 0x72, 0x69, 0x65, 0x73, 0xE8, 0x32,
			0x0D, 0x00, 0x02, 0x44, 0x41, 0x23, 0x01, 0x6C,
			0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0,
			0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0,
			0x3F, 0x3C, 0x00, 0x3C, 0x00, 0x01, 0x00, 0x00,
			0x00, 0x01, 0x00, 0x10, 0x2A, 0x35, 0x30, 0x30,
			0x4B, 0x20, 0x4D, 0x61, 0x70, 0x20, 0x53, 0x65,
			0x72, 0x69, 0x65, 0x73, 0xC0, 0xFE, 0x04, 0x00,
			0x02, 0x44, 0x46, 0x00, 0x01, 0x40, 0x01, 0xB5,
			0x2B, 0x4C, 0x55, 0x55, 0x55, 0xD5, 0x3F, 0xB5,
			0x2B, 0x4C, 0x55, 0x55, 0x55, 0xD5, 0x3F, 0x28,
			0x00, 0x28, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02,
			0x00, 0x10, 0x2A, 0x31, 0x30, 0x30, 0x4B, 0x20,
			0x4D, 0x61, 0x70, 0x20, 0x53, 0x65, 0x72, 0x69,
			0x65, 0x73, 0x50, 0xC3, 0x00, 0x00, 0x02, 0x44,
			0x4B, 0x00, 0x00, 0x89, 0x01, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0xB0, 0x3F, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0xB0, 0x3F, 0x2D, 0x00, 0x2D,
			0x00, 0x0C, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x10,
			0x2A, 0x37, 0x2E, 0x35, 0x27, 0x20, 0x4D, 0x61,
			0x70, 0x20, 0x53, 0x65, 0x72, 0x69, 0x65, 0x73,
			0x0F, 0x3C, 0x00, 0x00, 0x02, 0x44, 0x51, 0x00,
			0x00, 0x00, 0x01, 0x9A, 0x99, 0x99, 0x99, 0x99,
			0x99, 0x99, 0x3F, 0x9A, 0x99, 0x99, 0x99, 0x99,
			0x99, 0x89, 0x3F, 0x5A, 0x00, 0x2D, 0x00, 0x0D,
			0x00, 0x01, 0x00, 0x28, 0x00 
		};

		fwrite(header_bytes, sizeof(header_bytes), 1, tpo_file_out);
	}
	else if (strncmp("CT", output_state, 2) == 0 ||
			 strncmp("MA", output_state, 2) == 0 ||
			 strncmp("ME", output_state, 2) == 0 ||
			 strncmp("NJ", output_state, 2) == 0 ||
			 strncmp("NH", output_state, 2) == 0 ||
			 strncmp("NY", output_state, 2) == 0 ||
			 strncmp("RI", output_state, 2) == 0 ||
			 strncmp("VT", output_state, 2) == 0) {
		/* These 8 states are all covered in a single "Northeast" title */

		unsigned char header_bytes[] = {
			0x1E, 0x4E, 0x6F, 0x72, 0x74, 0x68, 0x65, 0x61,
			0x73, 0x74, 0x65, 0x72, 0x6E, 0x20, 0x55, 0x53,
			0x41, 0x20, 0x53, 0x68, 0x61, 0x64, 0x65, 0x64,
			0x20, 0x52, 0x65, 0x6C, 0x69, 0x65, 0x66, 0x03,
			0x4E, 0x45, 0x31, 0x05, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x54, 0x40, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x48, 0x40, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x80, 0x50, 0x40, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x43, 0x40, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x0B, 0x44, 0x3A, 0x5C, 0x4E, 0x45,
			0x31, 0x5F, 0x44, 0x30, 0x31, 0x5C, 0x12, 0x43,
			0x3A, 0x5C, 0x54, 0x4F, 0x50, 0x4F, 0x21, 0x5C,
			0x54, 0x50, 0x4F, 0x5F, 0x44, 0x41, 0x54, 0x41,
			0x5C, 0x12, 0x45, 0x3A, 0x5C, 0x54, 0x4F, 0x50,
			0x4F, 0x21, 0x5C, 0x54, 0x50, 0x4F, 0x5F, 0x44,
			0x41, 0x54, 0x41, 0x5C, 0x00, 0x00, 0x00, 0xFF,
			0x18, 0x01, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x37,
			0x30, 0x37, 0x30, 0x37, 0x30, 0x30, 0x30, 0x30,
			0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			0x30, 0x37, 0x30, 0x37, 0x30, 0x37, 0x30, 0x37,
			0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			0x30, 0x30, 0x30, 0x30, 0x30, 0x34, 0x30, 0x34,
			0x30, 0x35, 0x30, 0x35, 0x30, 0x36, 0x30, 0x37,
			0x30, 0x37, 0x30, 0x37, 0x30, 0x30, 0x30, 0x30,
			0x30, 0x30, 0x30, 0x30, 0x30, 0x33, 0x30, 0x33,
			0x30, 0x34, 0x30, 0x34, 0x30, 0x35, 0x30, 0x35,
			0x30, 0x36, 0x30, 0x36, 0x30, 0x36, 0x30, 0x36,
			0x30, 0x36, 0x30, 0x32, 0x30, 0x32, 0x30, 0x32,
			0x30, 0x33, 0x30, 0x33, 0x30, 0x34, 0x30, 0x34,
			0x30, 0x35, 0x30, 0x35, 0x30, 0x36, 0x30, 0x36,
			0x30, 0x36, 0x30, 0x30, 0x30, 0x30, 0x30, 0x32,
			0x30, 0x32, 0x30, 0x32, 0x30, 0x33, 0x30, 0x33,
			0x30, 0x38, 0x30, 0x38, 0x30, 0x38, 0x30, 0x39,
			0x30, 0x39, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			0x30, 0x30, 0x30, 0x32, 0x30, 0x32, 0x30, 0x32,
			0x30, 0x33, 0x31, 0x30, 0x31, 0x30, 0x30, 0x38,
			0x30, 0x39, 0x30, 0x39, 0x30, 0x39, 0x30, 0x39,
			0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x31, 0x30,
			0x31, 0x30, 0x31, 0x30, 0x30, 0x39, 0x30, 0x30,
			0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			0x30, 0x30, 0x31, 0x30, 0x31, 0x30, 0x30, 0x30,
			0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			0x31, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			0x30, 0x30, 0x0D, 0x55, 0x6E, 0x69, 0x74, 0x65,
			0x64, 0x20, 0x53, 0x74, 0x61, 0x74, 0x65, 0x73,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x5F, 0x40,
			0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x4E, 0x40,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x50, 0x40,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0x40,
			0x10, 0x4E, 0x6F, 0x72, 0x74, 0x68, 0x65, 0x61,
			0x73, 0x74, 0x65, 0x72, 0x6E, 0x20, 0x55, 0x53,
			0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x54,
			0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0xBE, 0x48,
			0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x50,
			0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x42,
			0x40, 0x16, 0x2A, 0x47, 0x65, 0x6E, 0x65, 0x72,
			0x61, 0x6C, 0x20, 0x52, 0x65, 0x66, 0x65, 0x72,
			0x65, 0x6E, 0x63, 0x65, 0x20, 0x4D, 0x61, 0x70,
			0x00, 0x09, 0x3D, 0x00, 0x0C, 0x4E, 0x45, 0x31,
			0x5F, 0x4D, 0x41, 0x50, 0x31, 0x5C, 0x53, 0x31,
			0x4C, 0x68, 0x03, 0x16, 0x03, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x2C, 0x40, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x24, 0x40, 0x8C, 0x00, 0x64,
			0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x16,
			0x2A, 0x4E, 0x61, 0x74, 0x69, 0x6F, 0x6E, 0x61,
			0x6C, 0x20, 0x41, 0x74, 0x6C, 0x61, 0x73, 0x20,
			0x53, 0x65, 0x72, 0x69, 0x65, 0x73, 0xE8, 0x32,
			0x0D, 0x00, 0x02, 0x44, 0x41, 0x0B, 0x01, 0x6C,
			0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0,
			0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0,
			0x3F, 0x3C, 0x00, 0x3C, 0x00, 0x01, 0x00, 0x00,
			0x00, 0x01, 0x00, 0x10, 0x2A, 0x35, 0x30, 0x30,
			0x4B, 0x20, 0x4D, 0x61, 0x70, 0x20, 0x53, 0x65,
			0x72, 0x69, 0x65, 0x73, 0xC0, 0xFE, 0x04, 0x00,
			0x02, 0x44, 0x46, 0xEA, 0x00, 0x40, 0x01, 0xB5,
			0x2B, 0x4C, 0x55, 0x55, 0x55, 0xD5, 0x3F, 0xB5,
			0x2B, 0x4C, 0x55, 0x55, 0x55, 0xD5, 0x3F, 0x28,
			0x00, 0x28, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02,
			0x00, 0x10, 0x2A, 0x31, 0x30, 0x30, 0x4B, 0x20,
			0x4D, 0x61, 0x70, 0x20, 0x53, 0x65, 0x72, 0x69,
			0x65, 0x73, 0x50, 0xC3, 0x00, 0x00, 0x02, 0x44,
			0x4B, 0x00, 0x00, 0x89, 0x01, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0xB0, 0x3F, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0xB0, 0x3F, 0x2D, 0x00, 0x2D,
			0x00, 0x0C, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x10,
			0x2A, 0x37, 0x2E, 0x35, 0x27, 0x20, 0x4D, 0x61,
			0x70, 0x20, 0x53, 0x65, 0x72, 0x69, 0x65, 0x73,
			0x0F, 0x3C, 0x00, 0x00, 0x02, 0x44, 0x51, 0x00,
			0x00, 0x00, 0x01, 0x9A, 0x99, 0x99, 0x99, 0x99,
			0x99, 0x99, 0x3F, 0x9A, 0x99, 0x99, 0x99, 0x99,
			0x99, 0x89, 0x3F, 0x5A, 0x00, 0x2D, 0x00, 0x0D,
			0x00, 0x01, 0x00, 0x28, 0x00
		};

		fwrite(header_bytes, sizeof(header_bytes), 1, tpo_file_out);
	}

	else {
		fatal(MYNAME ": writing ouput for state \"%s\" is not currently supported.\n", output_state);
	}
}

static void
tpo_track_hdr(const route_head *rte)
{
	double amt;
	unsigned char temp_buffer[8];
	unsigned char visibility_flags[] = { 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00 };
	unsigned char unknown1[] = { 0xFF, 0x00, 0x00, 0x00 };
	unsigned char bounding_box[8] = { 0x00, 0x80, 0x00, 0x80, 0xFF, 0x7F, 0xFF, 0x7F };

	waypoint* first_track_waypoint = (waypoint*) QUEUE_FIRST(&rte->waypoint_list);

	/* zoom level 1-5 visibility flags */
	fwrite(visibility_flags, 1, sizeof(visibility_flags), tpo_file_out);

	/* 8 bytes of zeros, meaning unknown */
	memset(temp_buffer, 0, sizeof(temp_buffer));
	fwrite(temp_buffer, 1, sizeof(temp_buffer), tpo_file_out);

	/* 4 more unknown bytes, possibly sign flags for the longitude and latitude? */
	fwrite(unknown1, 1, sizeof(unknown1), tpo_file_out);

	/* the starting point of the route */
    /* convert lat/long to NAD27/CONUS datum */
    GPS_Math_WGS84_To_Known_Datum_M(
        first_track_waypoint->latitude,
        first_track_waypoint->longitude,
        first_track_waypoint->altitude,
        &first_track_waypoint_lat,
        &first_track_waypoint_lon,
        &amt,
        78);
        
    /* swap the sign back *after* the datum conversion */
    first_track_waypoint_lon *= -1.0;	

	/* Compute this track's scaling factors: Used for scaling each track point and then
	   later written out to the track footer. These are approximately the ratios between 
	   pixels and degrees when viewing the 1:24000 map in TOPO!. In practice, it doesn't 
	   appear to be necessary that they be correct, as long as the same values are used 
	   for doing the scaling and for writing into the track footer data. */
	output_track_lat_scale = 4.8828125e-005; /* TOPO! appears to use a constant lat scale */
	output_track_lon_scale = output_track_lat_scale / cos(GPS_Math_Deg_To_Rad(first_track_waypoint_lat));

	/* 8 bytes - longitude */
    tpo_fwrite_double(first_track_waypoint_lon, tpo_file_out);

    /* 8 bytes - latitude */
    tpo_fwrite_double(first_track_waypoint_lat, tpo_file_out);

    /* 8 bytes: seems to be bounding box info */
	fwrite(bounding_box, 1, sizeof(bounding_box), tpo_file_out);

	/* number of route points */
	le_write16(temp_buffer, rte->rte_waypt_ct);
	fwrite(temp_buffer, 1, 2, tpo_file_out);

	/* initialize the track length computation */
	track_length = 0;
	GPS_Math_WGS84LatLonH_To_XYZ(
		first_track_waypoint->latitude, 
		first_track_waypoint->longitude,
		0.0,
		&last_waypoint_x,
		&last_waypoint_y,
		&last_waypoint_z);
}

static void
tpo_track_disp(const waypoint *waypointp)
{
	double lat, lon, amt, x, y, z;
	short lat_delta, lon_delta;
    unsigned char temp_buffer[2];
// fprintf(stderr, "%f/%f\n", waypointp->latitude, waypointp->longitude);
	/* convert lat/lon position to XYZ meters */
	GPS_Math_WGS84LatLonH_To_XYZ(
		waypointp->latitude, 
		waypointp->longitude,
		0.0,
		&x,
		&y,
		&z);

	/* increase the track length by the 3D length of last track segment in feet */
	track_length += 3.2808 * sqrt(
		(x - last_waypoint_x) * (x - last_waypoint_x) + 
		(y - last_waypoint_y) * (y - last_waypoint_y) +
		(z - last_waypoint_z) * (z - last_waypoint_z));
	last_waypoint_x = x;
	last_waypoint_y = y;
	last_waypoint_z = z;

	/* convert lat/long to NAD27/CONUS datum */
    GPS_Math_WGS84_To_Known_Datum_M(
        waypointp->latitude,
        waypointp->longitude,
        waypointp->altitude, 
        &lat,
        &lon,
        &amt,
        78);

    /* swap the sign back *after* the datum conversion */
    lon *= -1.0;

	/* longitude delta from first route point */
	lon_delta = (short)((first_track_waypoint_lon - lon) / output_track_lon_scale);
	le_write16(temp_buffer, lon_delta);
	fwrite(temp_buffer, 1, 2, tpo_file_out);

	/* latitude delta from first route point */
	lat_delta = (short)((first_track_waypoint_lat - lat) / output_track_lat_scale);
	le_write16(temp_buffer, lat_delta);
// fprintf(stderr, "%f %f: %x %x - %f %f %f / %f\n", lon, lat, lon_delta, lat_delta, first_track_waypoint_lat, lat, output_track_lat_scale, (first_track_waypoint_lat - lat) );
	fwrite(temp_buffer, 1, 2, tpo_file_out);
}

static void
tpo_track_tlr(const route_head *rte)
{
	unsigned char temp_buffer[4];

	unsigned char unknown1[] = { 0x06, 0x00 };

	unsigned char continue_marker[] = { 0x01, 0x80 };
	unsigned char end_marker[] = { 0x00, 0x00 };

	/* pixel to degree scaling factors */
	tpo_fwrite_double(output_track_lon_scale, tpo_file_out);
	tpo_fwrite_double(output_track_lat_scale, tpo_file_out);

    /* 4 bytes: the total length of the route */
	le_write32(temp_buffer, (unsigned int)track_length);
    fwrite(temp_buffer, 1, 4, tpo_file_out);

	/* 2 unknown bytes */
	fwrite(unknown1, 1, sizeof(unknown1), tpo_file_out);

	/* the last track ends with 0x0000 instead of 0x0180 */
	track_out_count++;
	if (track_out_count == track_count()) {		
	    fwrite(end_marker, 1, sizeof(end_marker), tpo_file_out);
	} else {
	    fwrite(continue_marker, 1, sizeof(continue_marker), tpo_file_out);
	}
}

static void
tpo_wr_init(const char *fname)
{	
	if (doing_wpts || doing_rtes)
	{
		fatal(MYNAME ": this file format only supports tracks, not waypoints or routes.\n");
	}

	tpo_file_out = xfopen(fname, "wb", MYNAME);
	tpo_write_file_header();
}

static void
tpo_wr_deinit(void)
{
	/* the file footer is six bytes of zeroes */
	unsigned char file_footer_bytes[6];
	memset(file_footer_bytes, 0, sizeof(file_footer_bytes));
	fwrite(file_footer_bytes, 1, sizeof(file_footer_bytes), tpo_file_out);

	fclose(tpo_file_out);
}

static void
tpo_write(void)
{
	unsigned char buffer[8];	
	unsigned char unknown1[] = { 0xFF, 0xFF, 0x01, 0x00 };

	char* chunk_name = "CTopoRoute";
	int chunk_name_length = strlen(chunk_name);

	/* write the total number of tracks */
    le_write16(buffer, track_count());
	fwrite(buffer, 1, 2, tpo_file_out);
	
	/* 4 unknown bytes */
	fwrite(unknown1, 1, 4, tpo_file_out);

	/* chunk name: "CTopoRoute" */
	le_write16(buffer, chunk_name_length);
	fwrite(buffer, 1, 2, tpo_file_out);
	fwrite(chunk_name, 1, chunk_name_length, tpo_file_out);

	track_out_count = 0;
	track_disp_all(tpo_track_hdr, tpo_track_tlr, tpo_track_disp);
}

/* TPO format can read and write tracks only */
ff_vecs_t tpo_vecs = {
ff_type_internal, //	ff_type_file,
	{ ff_cap_none | ff_cap_none, ff_cap_read | ff_cap_write, ff_cap_none | ff_cap_none },
	tpo_rd_init,
	tpo_wr_init,
	tpo_rd_deinit,
	tpo_wr_deinit,
	tpo_read,
	tpo_write,
	NULL,
	tpo_args,
	CET_CHARSET_ASCII, 0	/* CET-REVIEW */
};
