#include "route_schedules.h"
#include "thermometer.h"
#include "request_handle.h"
#include "type/pb_converter.h"
#include "ptreferential/ptreferential.h"



namespace pt = boost::posix_time;

namespace navitia { namespace timetables {

std::vector<std::vector<datetime_stop_time> >
get_all_stop_times(const vector_idx &journey_patterns,
                   const type::DateTime &dateTime,
                   const type::DateTime &max_datetime, type::Data &d) {
    std::vector<std::vector<datetime_stop_time> > result;

    //On cherche les premiers journey_pattern_points
    //de tous les journey_patterns
    std::vector<type::idx_t> first_journey_pattern_points;
    for(type::idx_t jp_idx : journey_patterns) {
        auto jpp = d.pt_data.journey_patterns[jp_idx];
        auto first_jpp_idx = jpp->journey_pattern_point_list.front()->idx;
        first_journey_pattern_points.push_back(first_jpp_idx);
    }

    //On fait un best_stop_time sur ces journey_pattern points
    auto first_dt_st = get_stop_times(first_journey_pattern_points,
                                      dateTime, max_datetime,
                                      std::numeric_limits<int>::max(), d);

    //On va chercher tous les prochains horaires
    for(auto ho : first_dt_st) {
        result.push_back(std::vector<datetime_stop_time>());
        type::DateTime dt = ho.first;
        for(auto stop_time : ho.second->vehicle_journey->stop_time_list) {
            dt.update(stop_time->departure_time);
            result.back().push_back(std::make_pair(dt, stop_time));
        }
    }
    return result;
}


std::vector<std::vector<datetime_stop_time> >
make_matrice(const std::vector<std::vector<datetime_stop_time> >& stop_times,
             Thermometer &thermometer, type::Data &) {
    std::vector<std::vector<datetime_stop_time> > result;
    //On initilise le tableau vide
    for(unsigned int i=0; i<thermometer.get_thermometer().size(); ++i) {
        result.push_back(std::vector<datetime_stop_time>());
        result.back().resize(stop_times.size());
    }

    //On remplit le tableau
    int y=0;
    for(std::vector<datetime_stop_time> vec : stop_times) {
        auto jpp = *vec.front().second->vehicle_journey->journey_pattern;
        std::vector<uint32_t> orders = thermometer.match_journey_pattern(jpp);
        int order = 0;
        for(datetime_stop_time dt_stop_time : vec) {
            result[orders[order]][y] = dt_stop_time;
            ++order;
        }
        ++y;
    }

    return result;
}


std::vector<type::VehicleJourney*>
get_vehicle_jorney(const std::vector<std::vector<datetime_stop_time> >& stop_times){
    std::vector<type::VehicleJourney*> result;
    for(const std::vector<datetime_stop_time> vec : stop_times){
        type::VehicleJourney* vj = vec.front().second->vehicle_journey;
        auto it = std::find_if(result.begin(), result.end(),
                               [vj](type::VehicleJourney* vj1){
                                return (vj->idx == vj1->idx);
                        });
        if(it == result.end()){
            result.push_back(vj);
        }
    }
    return result;
}


pbnavitia::Response
route_schedule(const std::string& filter, const std::string &str_dt,
               uint32_t duration, const uint32_t max_depth,
               int count, int start_page, type::Data &d) {
    RequestHandle handler("ROUTE_SCHEDULE", filter, str_dt, duration, d,
                          count, start_page);

    if(handler.pb_response.has_error()) {
        return handler.pb_response;
    }
    auto now = pt::second_clock::local_time();
    auto pt_datetime = to_posix_time(handler.date_time, d);
    auto pt_max_datetime = to_posix_time(handler.max_datetime, d);
    pt::time_period action_period(pt_datetime, pt_max_datetime);
    Thermometer thermometer(d);
    auto routes_idx = navitia::ptref::make_query(type::Type_e::Route, filter, d);
    for(type::idx_t route_idx : routes_idx) {
        auto route = d.pt_data.routes[route_idx];
        auto jps = route->get(type::Type_e::JourneyPattern, d.pt_data);
        //On récupère les stop_times
        auto stop_times = get_all_stop_times(jps, handler.date_time,
                                             handler.max_datetime, d);
        std::vector<vector_idx> stop_points;
        for(auto jp_idx : jps) {
            auto jp = d.pt_data.journey_patterns[jp_idx];
            stop_points.push_back(vector_idx());
            for(auto jpp : jp->journey_pattern_point_list) {
                stop_points.back().push_back(jpp->stop_point->idx);
            }
        }
        thermometer.generate_thermometer(stop_points);
        //On génère la matrice
        auto  matrice = make_matrice(stop_times, thermometer, d);
         //On récupère les vehicleJourny de manière unique
        auto vehicle_journy_list = get_vehicle_jorney(stop_times);
        auto schedule = handler.pb_response.add_route_schedules();
        pbnavitia::Table *table = schedule->mutable_table();
        auto m_route = schedule->mutable_route();
        fill_pb_object(route, d, m_route, 0,
                       now, action_period);
        if (route->line != nullptr){
            auto m_line = m_route->mutable_line();

    auto pagination = handler.pb_response.mutable_pagination();
    pagination->set_totalresult(handler.total_result);
    pagination->set_startpage(start_page);
    pagination->set_itemsperpage(count);
    pagination->set_itemsonpage(std::max(handler.pb_response.departure_boards_size(),
                                         handler.pb_response.stop_schedules_size()));
            fill_pb_object(route->line, d, m_line, 0, now, action_period);
            if(route->line->commercial_mode){
                auto m_commercial_mode = m_line->mutable_commercial_mode();
                fill_pb_object(route->line->commercial_mode,
                               d, m_commercial_mode, 0);
            }
            if(route->line->network){
                auto m_network = m_line->mutable_network();
                fill_pb_object(route->line->network, d, m_network, 0);
            }
        }

        for(type::VehicleJourney* vj : vehicle_journy_list){
            pbnavitia::Header* header = table->add_headers();
            auto m_vj = header->mutable_vehiclejourney();
            fill_pb_object(vj, d, m_vj, 0, now, action_period);
            if (vj->physical_mode != nullptr){
                fill_pb_object(vj->physical_mode, d,
                               m_vj->mutable_physical_mode(),0, now, action_period);
            }
            header->set_direction(vj->get_direction());
        }

        for(unsigned int i=0; i < thermometer.get_thermometer().size(); ++i) {
            type::idx_t spidx=thermometer.get_thermometer()[i];
            const type::StopPoint* sp = d.pt_data.stop_points[spidx];
            pbnavitia::RouteScheduleRow* row = table->add_rows();
            fill_pb_object(sp, d, row->mutable_stop_point(), max_depth,
                           now, action_period);
            for(unsigned int j=0; j<stop_times.size(); ++j) {
                datetime_stop_time dt_stop_time  = matrice[i][j];
                auto rs_st = row->add_stop_times();
                fill_pb_object(dt_stop_time.second, d, rs_st, max_depth,
                               now, action_period, dt_stop_time.first);
            }
        }
    }
    auto pagination = handler.pb_response.mutable_pagination();
    pagination->set_totalresult(handler.total_result);
    pagination->set_startpage(start_page);
    pagination->set_itemsperpage(count);
    pagination->set_itemsonpage(handler.pb_response.route_schedules_size());
    return handler.pb_response;
}
}}
