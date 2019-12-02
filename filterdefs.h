/*
    Filter definitions.

    Copyright (C) 2005-2014 Robert Lipe, robertlipe+source@gpsbabel.org

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
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

 */



/*
 * Filters can do some things that modules really shouldn't do.
 * This is our (weak) attempt to make that distinction.
 */

#ifndef FILTERDEFS_H_INCLUDED_
#define FILTERDEFS_H_INCLUDED_

#include "defs.h"
#include "filter.h"

extern WaypointList* global_waypoint_list;

using filter_init = void (*)();
using filter_process = void (*)();
using filter_deinit = void (*)();
using filter_exit = void (*)();

struct filter_vecs_t {
  filter_init f_init;
  filter_process f_process;
  filter_deinit f_deinit;
  filter_exit f_exit;
  arglist_t* args;
};

Filter* find_filter_vec(const QString&);
void free_filter_vec(Filter*);
void disp_filters(int version);
void disp_filter_vec(const QString& vecname);
void disp_filter_vecs();
void init_filter_vecs();
void exit_filter_vecs();
bool validate_filters();

#endif // FILTERDEFS_H_INCLUDED_
