/* Copyright © 2001-2014, Canal TP and/or its affiliates. All rights reserved.
  
This file is part of Navitia,
    the software to build cool stuff with public transport.
 
Hope you'll enjoy and contribute to this project,
    powered by Canal TP (www.canaltp.fr).
Help us simplify mobility and open public transport:
    a non ending quest to the responsive locomotion way of traveling!
  
LICENCE: This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
   
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU Affero General Public License for more details.
   
You should have received a copy of the GNU Affero General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.
  
Stay tuned using
twitter @navitia 
IRC #navitia on freenode
https://groups.google.com/d/forum/navitia
www.navitia.io
*/

#include "departure_boards.h"
#include "request_handle.h"
#include "routing/get_stop_times.h"
#include "type/pb_converter.h"
#include "routing/dataraptor.h"
#include "boost/lexical_cast.hpp"
#include "boost/date_time/posix_time/posix_time.hpp"
#include "utils/paginate.h"
#include "routing/dataraptor.h"

namespace pt = boost::posix_time;

namespace navitia { namespace timetables {

static void
render(PbCreator& pb_creator,
          const std::map<uint32_t, pbnavitia::ResponseStatus>& response_status,
          const std::map<stop_point_route, vector_dt_st>& map_route_stop_point,
          DateTime datetime,
          DateTime max_datetime,
          boost::optional<const std::string> calendar_id,
          uint32_t depth) {
    pb_creator.action_period = pt::time_period(to_posix_time(datetime, pb_creator.data),
                                               to_posix_time(max_datetime, pb_creator.data));

    for(auto id_vec : map_route_stop_point) {
        auto schedule = pb_creator.add_stop_schedules();
        //Each schedule has a stop_point and a route
        pb_creator.fill(pb_creator.data.pt_data->stop_points[id_vec.first.first.val],
                schedule->mutable_stop_point(), depth);

        auto m_route = schedule->mutable_route();
        pb_creator.fill(pb_creator.data.pt_data->routes[id_vec.first.second.val], m_route, depth);
        if (pb_creator.data.pt_data->routes[id_vec.first.second.val]->line != nullptr){
            auto m_line = m_route->mutable_line();
            pb_creator.fill(pb_creator.data.pt_data->routes[id_vec.first.second.val]->line, m_line, 0);
        }
        auto pt_display_information = schedule->mutable_pt_display_informations();

        pb_creator.fill(pb_creator.data.pt_data->routes[id_vec.first.second.val], pt_display_information, 0);

        //Now we fill the date_times
        for(auto dt_st : id_vec.second) {
            auto date_time = schedule->add_date_times();
            const auto& st_calendar = navitia::StopTimeCalandar(dt_st.second, dt_st.first, calendar_id);
            pb_creator.fill(&st_calendar, date_time, 0);
            if (dt_st.second != nullptr) {
                auto vj = dt_st.second->vehicle_journey;
                if(vj != nullptr) {
                    for (const auto& comment: pb_creator.data.pt_data->comments.get(*vj)) {
                        pb_creator.fill(&comment, date_time->mutable_properties()->add_notes(), 0);
                    }
                }
            }
        }
        const auto& it = response_status.find(id_vec.first.second.val);
        if(it != response_status.end()){
            schedule->set_response_status(it->second);
        }
    }    
}


void departure_board(PbCreator& pb_creator, const std::string& request,
                boost::optional<const std::string> calendar_id,
                const std::vector<std::string>& forbidden_uris,
                const pt::ptime date,
                uint32_t duration, uint32_t depth,
                int count, int start_page, const type::RTLevel rt_level, const size_t items_per_route_point) {

    RequestHandle handler(pb_creator, request, forbidden_uris, date, duration, calendar_id);

    if (pb_creator.has_error() || (handler.journey_pattern_points.size() == 0)) {
        return;
    }

    if (calendar_id) {
        //check whether that calendar exists, to raise an early error
        if (pb_creator.data.pt_data->calendars_map.find(*calendar_id) == pb_creator.data.pt_data->calendars_map.end()) {
            pb_creator.fill_pb_error(pbnavitia::Error::bad_filter, "stop_schedules : calendar does not exist");
            return;
        }
    }
    //  <idx_route, status>
    std::map<uint32_t, pbnavitia::ResponseStatus> response_status;

    std::map<stop_point_route, vector_dt_st> map_route_stop_point;

    //Mapping route/stop_point
    std::vector<stop_point_route> sps_routes;
    for(auto jpp_idx : handler.journey_pattern_points) {
        const auto& jpp = pb_creator.data.dataRaptor->jp_container.get(jpp_idx);
        const auto& jp = pb_creator.data.dataRaptor->jp_container.get(jpp.jp_idx);
        stop_point_route key = {jpp.sp_idx, jp.route_idx};
        auto find_predicate = [&](stop_point_route spl) {
            return spl.first == key.first && spl.second == key.second;
        };
        auto it = std::find_if(sps_routes.begin(), sps_routes.end(), find_predicate);
        if(it == sps_routes.end()){
            sps_routes.push_back(key);
        }
    }
    size_t total_result = sps_routes.size();
    sps_routes = paginate(sps_routes, count, start_page);
    //Trie des vecteurs de date_times stop_times
    auto sort_predicate = [](routing::datetime_stop_time dt1, routing::datetime_stop_time dt2) {
                    return dt1.first < dt2.first;
                };
    // On regroupe entre eux les stop_times appartenant
    // au meme couple (stop_point, route)
    // On veut en effet afficher les départs regroupés par route
    // (une route étant une vague direction commerciale
    for(auto sp_route : sps_routes) {
        std::vector<routing::datetime_stop_time> stop_times;
        const type::StopPoint* stop_point = pb_creator.data.pt_data->stop_points[sp_route.first.val];
        const type::Route* route = pb_creator.data.pt_data->routes[sp_route.second.val];
        const auto& jpps = pb_creator.data.dataRaptor->jpps_from_sp[sp_route.first];
        std::vector<routing::JppIdx> routepoint_jpps;
        for (const auto& jpp_from_sp: jpps) {
            const routing::JppIdx& jpp_idx = jpp_from_sp.idx;
            const auto& jpp = pb_creator.data.dataRaptor->jp_container.get(jpp_idx);
            const auto& jp = pb_creator.data.dataRaptor->jp_container.get(jpp.jp_idx);
            if (jp.route_idx != sp_route.second) { continue; }

            routepoint_jpps.push_back(jpp_idx);
        }

        std::vector<routing::datetime_stop_time> tmp;
        if (! calendar_id) {
            stop_times = routing::get_stop_times(routing::StopEvent::pick_up, routepoint_jpps, handler.date_time,
                    handler.max_datetime, items_per_route_point, pb_creator.data, rt_level);
        } else {
            stop_times = routing::get_stop_times(routepoint_jpps, DateTimeUtils::hour(handler.date_time),
                    DateTimeUtils::hour(handler.max_datetime), pb_creator.data, *calendar_id);
        }
        if ( ! calendar_id) {
            std::sort(stop_times.begin(), stop_times.end(), sort_predicate);
        } else {
            // for calendar we want the first stop time to start from handler.date_time
            std::sort(stop_times.begin(), stop_times.end(), routing::CalendarScheduleSort(handler.date_time));
            if (stop_times.size() > items_per_route_point) {
                stop_times.resize(items_per_route_point);
            }
        }

        //we compute the route status
        for (const auto& jpp_from_sp: jpps) {
            const routing::JppIdx& jpp_idx = jpp_from_sp.idx;
            const auto& jpp = pb_creator.data.dataRaptor->jp_container.get(jpp_idx);
            const auto& jp = pb_creator.data.dataRaptor->jp_container.get(jpp.jp_idx);
            const auto& last_jpp = pb_creator.data.dataRaptor->jp_container.get(jp.jpps.back());
            if (sp_route.first == last_jpp.sp_idx) {
                if (stop_point->stop_area == route->destination) {
                    response_status[route->idx] = pbnavitia::ResponseStatus::terminus;
                } else {
                    response_status[route->idx] = pbnavitia::ResponseStatus::partial_terminus;
                }
            }
        }

        //If there is no departure for a request with "RealTime", Test existance of any departure with "base_schedule"
        //If departure with base_schedule is not empty, additional_information = active_disruption
        //Else additional_information = no_departure_this_day
        if (stop_times.empty() && (response_status.find(route->idx) == response_status.end())) {
            auto resp_status = pbnavitia::ResponseStatus::no_departure_this_day;
            if (rt_level != navitia::type::RTLevel::Base) {
                auto tmp_stop_times = routing::get_stop_times(routing::StopEvent::pick_up, routepoint_jpps, handler.date_time,
                                                              handler.max_datetime, 1, pb_creator.data,
                                                              navitia::type::RTLevel::Base);
                if (!tmp_stop_times.empty()) { resp_status = pbnavitia::ResponseStatus::active_disruption; }
            }
            response_status[route->idx] = resp_status;
        }

        map_route_stop_point[sp_route] = stop_times;
    }

    render(pb_creator, response_status, map_route_stop_point, handler.date_time, handler.max_datetime,
              calendar_id, depth);

    pb_creator.make_paginate(total_result, start_page, count, std::max(pb_creator.departure_boards_size(),
                                                                       pb_creator.stop_schedules_size()));
}
}
}
