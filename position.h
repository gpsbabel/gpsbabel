/*
    Distance Between Points Filter(s)

    Copyright (C) 2002 Robert Lipe, robertlipe+source@gpsbabel.org

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

#ifndef POSITION_H_INCLUDED_
#define POSITION_H_INCLUDED_

#include "defs.h"
#include "filter.h"
#include "filterdefs.h"
#include "grtcirc.h"
#include <cmath>
#include <cstdlib>

#if FILTERS_ENABLED

class PositionFilter:public Filter
{
public:
  arglist_t* get_args() override
  {
    return args;
  }
  void process() override;
  void init(const char*) override;

private:
  route_head* cur_rte = nullptr;

  double pos_dist;
  double max_diff_time;
  char* distopt = nullptr;
  char* timeopt = nullptr;
  char* purge_duplicates = nullptr;
  int check_time;

  typedef struct {
    double distance;
  } extra_data;

  arglist_t args[4] = {
    {
      "distance", &distopt, "Maximum positional distance",
      nullptr, ARGTYPE_FLOAT | ARGTYPE_REQUIRED, ARG_NOMINMAX, nullptr
    },
    {
      "all", &purge_duplicates,
      "Suppress all points close to other points",
      nullptr, ARGTYPE_BOOL, ARG_NOMINMAX, nullptr
    },
    {
      "time", &timeopt, "Maximum time in seconds beetween two points",
      nullptr, ARGTYPE_FLOAT | ARGTYPE_REQUIRED, ARG_NOMINMAX, nullptr
    },
    ARG_TERMINATOR
  };

  double gc_distance(double lat1, double lon1, double lat2, double lon2);
  void position_runqueue(queue* q, int nelems, int qtype);
  void position_process_any_route(const route_head* rh, int type);
  void position_process_rte(const route_head* rh);
  void position_process_trk(const route_head* rh);

  static void position_process_rte_glue(const route_head* rh)
  {
    return fObj->position_process_rte(rh);
  }
  static void position_process_trk_glue(const route_head* rh)
  {
    return fObj->position_process_trk(rh);
  }
  static void setObj(PositionFilter& obj)
  {
    fObj = &obj;
  }
  static PositionFilter* fObj;
};
#endif // FILTERS_ENABLED
#endif // POSITION_H_INCLUDED_
