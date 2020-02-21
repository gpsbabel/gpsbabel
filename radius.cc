/*
    Radius Filter

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
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

 */

#include <algorithm>        // for sort
#include <cstdlib>          // for atoi, strtod

#include <QtCore/QString>   // for QString
#include <QtCore/QVector>   // for QVector<>::iterator, QVector
#include <QtCore/QtGlobal>  // for qAsConst, QAddConst<>::Type, foreach

#include "defs.h"           // for Waypoint, waypt_del, route_add_head, route_add_wpt, route_head, waypt_add, waypt_count, xcalloc, xfree, kMilesPerKilometer
#include "radius.h"
#include "grtcirc.h"        // for RAD, gcdist, radtomiles


#if FILTERS_ENABLED

double RadiusFilter::gc_distance(double lat1, double lon1, double lat2, double lon2)
{
  return radtomiles(gcdist(RAD(lat1), RAD(lon1), RAD(lat2), RAD(lon2)));
}

void RadiusFilter::process()
{
  for (Waypoint* waypointp : qAsConst(*global_waypoint_list)) {
    double dist = gc_distance(waypointp->latitude, waypointp->longitude,
                              home_pos->latitude, home_pos->longitude);

    if ((dist >= pos_dist) == (exclopt == nullptr)) {
      waypt_del(waypointp);
      delete waypointp;
      continue;
    }

    auto* ed = new extra_data;
    ed->distance = dist;
    waypointp->extra_data = ed;
  }

  /*
   * Create an list of remaining waypoints, popping them off the
   * master queue as we go.  This gives us something reasonable
   * for sort.
   */

  WaypointList comp;
  waypt_swap(comp);

  if (nosort == nullptr) {
    auto dist_comp_lambda = [](const Waypoint* a, const Waypoint* b)->bool {
      auto* aed = reinterpret_cast<const extra_data*>(a->extra_data);
      auto* bed = reinterpret_cast<const extra_data*>(b->extra_data);
      return aed->distance < bed->distance;
    };
    std::sort(comp.begin(), comp.end(), dist_comp_lambda);
  }

  route_head* rte_head = nullptr;
  if (routename != nullptr) {
    rte_head = new route_head;
    rte_head->rte_name = routename;
    route_add_head(rte_head);
  }

  /*
   * The comp list is now sorted by distance.   As we run through it,
   * we push them back onto the master wp list, letting us pass them
   * on through in the modified order.
   */
  int i = 0;
  for (Waypoint* wp : qAsConst(comp)) {

    delete reinterpret_cast<extra_data*>(wp->extra_data);
    wp->extra_data = nullptr;

    if ((maxctarg != nullptr) && (i >= maxct)) {
      delete wp;
    } else {
      if (routename != nullptr) {
        route_add_wpt(rte_head, wp);
      } else {
        waypt_add(wp);
      }
    }
    ++i;
  }
}

void RadiusFilter::init()
{
  pos_dist = 0;

  if (distopt != nullptr) {
    char* fm;
    pos_dist = strtod(distopt, &fm);

    if ((*fm == 'k') || (*fm == 'K')) {
      /* distance is kilometers, convert to miles */
      pos_dist *= kMilesPerKilometer;
    }
  }

  if (maxctarg != nullptr) {
    maxct = atoi(maxctarg);
  } else {
    maxct = 0;
  }

  home_pos = new Waypoint;

  if (latopt != nullptr) {
    home_pos->latitude = strtod(latopt, nullptr);
  }
  if (lonopt != nullptr) {
    home_pos->longitude = strtod(lonopt, nullptr);
  }
}

void RadiusFilter::deinit()
{
  delete home_pos;
}

#endif // FILTERS_ENABLED
