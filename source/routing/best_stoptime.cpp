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

#include "best_stoptime.h"

namespace navitia { namespace routing {

std::pair<const type::StopTime*, uint32_t>
best_stop_time(const type::JourneyPatternPoint* jpp,
               const DateTime dt,
               const type::VehicleProperties& vehicle_properties,
               const bool clockwise, bool disruption_active, const type::Data &data, bool reconstructing_path) {
    if(clockwise)
        return earliest_stop_time(jpp, dt, data, disruption_active, reconstructing_path, vehicle_properties);
    else
        return tardiest_stop_time(jpp, dt, data, disruption_active, reconstructing_path, vehicle_properties);
}

/** Which is the first valid stop_time in this range ?
 *  Returns invalid_idx is none is
 */
static std::pair<const type::StopTime*, DateTime> 
next_valid_discrete_pick_up(type::idx_t idx, 
			const type::idx_t end, 
			const DateTime dt,
        const type::Data &data, 
	bool reconstructing_path,
        const type::VehicleProperties &required_vehicle_properties,
        bool disruption_active){
    const auto date = DateTimeUtils::date(dt);
    for(; idx < end; ++idx) {
        const type::StopTime* st = data.dataRaptor->st_forward[idx];
        if (st->valid_end(reconstructing_path) &&
            st->is_valid_day(date, false, disruption_active)
            && st->vehicle_journey->accessible(required_vehicle_properties) ){
                return {st, DateTimeUtils::set(date, st->departure_time)};
        }
    }
    return {nullptr, std::numeric_limits<DateTime>::max()};
}

std::pair<const type::StopTime*, DateTime>
next_valid_frequency_pick_up(const type::JourneyPatternPoint* jpp, const DateTime dt,
                             bool reconstructing_path,
                             const type::VehicleProperties &vehicle_properties,
                             bool disruption_active) {
    // to find the next frequency VJ, for the moment we loop through all frequency VJ of the JP
    // and for each jp, get compute the datetime on the jpp
    std::pair<const type::StopTime*, DateTime> best = {nullptr, std::numeric_limits<DateTime>::max()};
    for (const auto& freq_vj: jpp->journey_pattern->frequency_vehicle_journey_list) {
        //we get stop time corresponding to the jpp

        const auto& st = freq_vj.stop_time_list[jpp->order];

        if (! st.valid_end(reconstructing_path) ||
            freq_vj.accessible(vehicle_properties)) {
            continue;
        }

        const auto next_dt = get_next_departure(dt, freq_vj, st, disruption_active);

        if (next_dt < best.second) {
            best = {&st, next_dt};
        }
    }
    return best;
}


/**
 * valid_pick_up function:
 * we look for the new valid stop time valid the the day of the date time and after the hour of the date time.
 *
 * 2 lookup are done,
 *  one on the ordered vector of departures (for the non frequency VJ),
 *  and one on the frenquency VJ (for the moment we loop through all frequency VJ, no clever data structure, we'll see if it is worth adding it)
 *
 *      Note: if nothing found, we also look for a stop time the day after
 */
static std::pair<const type::StopTime*, DateTime>
valid_pick_up(const type::JourneyPatternPoint* jpp, 
	const DateTime dt,
        const type::Data& data, 
	bool reconstructing_path,
        const type::VehicleProperties &vehicle_properties,
        bool disruption_active) {

    auto begin = data.dataRaptor->departure_times.begin() +
            data.dataRaptor->first_stop_time[jpp->journey_pattern->idx] +
            jpp->order * data.dataRaptor->nb_trips[jpp->journey_pattern->idx];
    auto end = begin + data.dataRaptor->nb_trips[jpp->journey_pattern->idx];
    auto it = std::lower_bound(begin, end, DateTimeUtils::hour(dt),
                               bound_predicate_earliest);

    type::idx_t idx = it - data.dataRaptor->departure_times.begin();
    type::idx_t end_idx = (begin - data.dataRaptor->departure_times.begin()) +
                           data.dataRaptor->nb_trips[jpp->journey_pattern->idx];

    const auto first_discrete_st_pair =
            next_valid_discrete_pick_up(idx, end_idx, dt, data, reconstructing_path, vehicle_properties, disruption_active);

    //TODO use first_discrete as a LB ?
    const auto first_frequency_st_pair =
            next_valid_frequency_pick_up(jpp, dt, reconstructing_path, vehicle_properties, disruption_active);

    auto date = DateTimeUtils::date(dt);

    // If no trip was found, we look for one the day after
    if (first_discrete_st_pair.first || first_frequency_st_pair.first) {
        //we need to find the best between the frequency one and the 'normal' one
        if (first_discrete_st_pair.second <= first_frequency_st_pair.second) {
            //discrete's better
            return first_discrete_st_pair;
        }
        return first_frequency_st_pair;
    }

    idx = begin - data.dataRaptor->departure_times.begin();
    auto working_dt = DateTimeUtils::set(date + 1, 0);
    const auto tomorow_first_discrete =
            next_valid_discrete_pick_up(idx, end_idx, working_dt, data, reconstructing_path, vehicle_properties,disruption_active);

    //TODO use first_discrete as a LB ?
    const auto tomorow_first_frequency =
            next_valid_frequency_pick_up(jpp, dt, reconstructing_path, vehicle_properties, disruption_active);

    if (tomorow_first_discrete.second <= tomorow_first_frequency.second) {
        //discrete's better
        return tomorow_first_discrete;
    }
    return tomorow_first_frequency;
}

static const type::StopTime*
valid_drop_off(type::idx_t idx,
               const type::idx_t end,
               const DateTime dt,
               const type::Data& data,
               bool reconstructing_path,
               const type::VehicleProperties& required_vehicle_properties,
               bool disruption_active){
    const auto date = DateTimeUtils::date(dt);
    const auto hour = DateTimeUtils::hour(dt);
    for(; idx < end; ++idx) {
        const type::StopTime* st = data.dataRaptor->st_backward[idx];
        if (st->valid_end(!reconstructing_path) && st->valid_hour(hour, false) &&
            st->is_valid_day(date, true, disruption_active)
            && st->vehicle_journey->accessible(required_vehicle_properties) ){
                return st;
        }
    }
    return nullptr;
}


std::pair<const type::StopTime*, uint32_t>
earliest_stop_time(const type::JourneyPatternPoint* jpp,
                   const DateTime dt, const type::Data &data,
                   bool disruption_active,
                   bool reconstructing_path,
                   const type::VehicleProperties& vehicle_properties) {
    //We look for the earliest stop time of the journey_pattern >= dt.hour()

    //Return the first valid trip
    return valid_pick_up(jpp, dt, data, reconstructing_path, vehicle_properties, disruption_active);
}

// get all stop times for a given jpp and a given calendar
//
// earliest stop time for calendar is different than for a datetime
// we have to consider only the first theoric vj of all meta vj for the given jpp
// for all those vj, we select the one associated to the calendar,
// and we loop through all stop times for the jpp 
std::vector<std::pair<uint32_t, const type::StopTime*>>
get_all_stop_times(const type::JourneyPatternPoint* jpp,
                   const std::string calendar_id,
                   const type::VehicleProperties& vehicle_properties) {

    std::set<const type::MetaVehicleJourney*> meta_vjs;
    throw "todo pas trop chiant";
//    for (auto vj: jpp->journey_pattern->vehicle_journey_list()) {
//        if (! vj->meta_vj) {
//            throw navitia::exception("vj " + vj->uri + " has been ill constructed, it has no meta vj");
//        }
//        meta_vjs.insert(vj->meta_vj);
//    }
//    std::vector<const type::VehicleJourney*> vjs;
//    for (const auto meta_vj: meta_vjs) {
//        if (meta_vj->associated_calendars.find(calendar_id) == meta_vj->associated_calendars.end()) {
//            //meta vj not associated with the calender, we skip
//            continue;
//        }
//        //we can get only the first theoric one, because BY CONSTRUCTION all theoric vj have the same local times
//        vjs.push_back(meta_vj->theoric_vj.front());
//    }
//    if (vjs.empty()) {
//        return {};
//    }

//    std::vector<std::pair<DateTime, const type::StopTime*>> res;
//    for (const auto vj: vjs) {
//        //loop through stop times for stop jpp->stop_point
//        const auto& st = *(vj->stop_time_list.begin() + jpp->order);
//        if (! st.vehicle_journey->accessible(vehicle_properties)) {
//            continue; //the stop time must be accessible
//        }
//        if (st.is_frequency()) {
//            //if it is a frequency, we got to expand the timetable

//            //Note: end can be lower than start, so we have to cycle through the day
//            bool is_looping = (vj->start_time > vj->end_time);
//            auto stop_loop = [vj, is_looping](u_int32_t t) {
//                if (! is_looping)
//                    return t <= vj->end_time;
//                return t > vj->end_time;
//            };
//            for (auto time = vj->start_time; stop_loop(time); time += vj->headway_secs) {
//                if (is_looping && time > DateTimeUtils::SECONDS_PER_DAY) {
//                    time -= DateTimeUtils::SECONDS_PER_DAY;
//                }

//                //we need to convert this to local there since we do not have a precise date (just a period)
//                res.push_back({time + vj->utc_to_local_offset, &st});
//            }
//        } else {
//            //same utc tranformation
//            res.push_back({st.departure_time + vj->utc_to_local_offset, &st});
//        }
//    }

//    return res;
}

/**
* Here we want the next dt in the period of the frequency.
* If none is found, we return std::numeric_limits<DateTime>::max()
*
*  In normal case we have something like:
* 0-------------------------------------86400(midnight)
*     start-------end
*
* in this case, it's easy, hour should be in [start; end]
* if before, next departure is start, if after, no next departure
*
*
* If end_time, is after midnight, so end_time%86400 will be < start_time
*
* So we will have something like:
* 0-------------------------------------86400----------------------------86400*2
*                             start----------------end
*
* by check that on only one day (modulating by 86400), we have:
* 0-------------------------------------86400(midnight)
*  -------end                 start--------
*
* Note: If hour in [0, end] we have to check the previous day validity pattern
**/
DateTime get_next_departure(DateTime dt, const type::FrequencyVehicleJourney& freq_vj, const type::StopTime& st, const bool adapted) {
    const u_int32_t lower_bound = (freq_vj.start_time + st.departure_time) % DateTimeUtils::SECONDS_PER_DAY;
    const u_int32_t higher_bound = (freq_vj.end_time + st.departure_time) % DateTimeUtils::SECONDS_PER_DAY;

    auto hour = DateTimeUtils::hour(dt);
    auto date = DateTimeUtils::date(dt);

    const bool classic_case = lower_bound <= higher_bound;

    //in the 'classic' case, hour should be in [lower, higher]
    // but in case of midnight overrun, hour should be in [higher, lower]
    if (classic_case) {
        if (hour <= lower_bound) {
            if (freq_vj.is_valid(date, adapted)) {
                return DateTimeUtils::set(date, lower_bound);
            }
            return std::numeric_limits<DateTime>::max();
        }
        if (hour > higher_bound) {
            //no solution on the day
            return std::numeric_limits<DateTime>::max();
        }
    }


    double diff;
    if (classic_case || //in classic case, we must be in [start, end]
            (! classic_case && within(hour, {lower_bound, DateTimeUtils::SECONDS_PER_DAY}))) {
        //we need to check if the vj is valid for our day
        if (! freq_vj.is_valid(date, adapted)) {
            return std::numeric_limits<DateTime>::max();
        }

         diff = hour - lower_bound;
    } else {
        // overnight case and hour > midnight
        if (hour > higher_bound) {
            if (freq_vj.is_valid(date, adapted)) {
                return DateTimeUtils::set(date, lower_bound);
            }
            return std::numeric_limits<DateTime>::max();
        }
        //we need to see if the vj was valid the day before
        if (! freq_vj.is_valid(date - 1, adapted)) {
            //the vj was not valid, next departure is lower
            return DateTimeUtils::set(date, lower_bound);
        }

        diff = higher_bound - hour;
    }
    const uint32_t x = std::ceil(diff / double(freq_vj.headway_secs));

    return DateTimeUtils::set(date, lower_bound + x * freq_vj.headway_secs);
}

std::pair<const type::StopTime*, uint32_t>
tardiest_stop_time(const type::JourneyPatternPoint* jpp,
                   const DateTime dt, const type::Data &data, bool disruption_active,
                   bool reconstructing_path,
                   const type::VehicleProperties& vehicle_properties) {
    throw "TODO";
    //On cherche le plus grand stop time de la journey_pattern <= dt.hour()
//    const auto begin = data.dataRaptor->arrival_times.begin() +
//                       data.dataRaptor->first_stop_time[jpp->journey_pattern->idx] +
//                       jpp->order * data.dataRaptor->nb_trips[jpp->journey_pattern->idx];
//    const auto end = begin + data.dataRaptor->nb_trips[jpp->journey_pattern->idx];
//    auto it = std::lower_bound(begin, end, DateTimeUtils::hour(dt), bound_predicate_tardiest);

//    type::idx_t idx = it - data.dataRaptor->arrival_times.begin();
//    type::idx_t end_idx = (begin - data.dataRaptor->arrival_times.begin()) +
//                           data.dataRaptor->nb_trips[jpp->journey_pattern->idx];

//    const type::StopTime* first_st = valid_drop_off(idx, end_idx,
//            dt, data,
//            reconstructing_path, vehicle_properties, disruption_active);

//    auto working_dt = dt;
//    // If no trip was found, we look for one the day before
//    if(first_st == nullptr && DateTimeUtils::date(dt) > 0){
//        idx = begin - data.dataRaptor->arrival_times.begin();
//        working_dt = DateTimeUtils::set(DateTimeUtils::date(working_dt) - 1,
//                                        DateTimeUtils::SECONDS_PER_DAY - 1);
//        first_st = valid_drop_off(idx, end_idx, working_dt, data, reconstructing_path,
//                vehicle_properties, disruption_active);
//    }

//    if(first_st != nullptr){
//        if(!first_st->is_frequency()) {
//            DateTimeUtils::update(working_dt, DateTimeUtils::hour(first_st->arrival_time), false);
//        } else {
//            working_dt = dt;
//            const DateTime tmp_dt = f_arrival_time(DateTimeUtils::hour(working_dt), *first_st);
//            DateTimeUtils::update(working_dt, DateTimeUtils::hour(tmp_dt), false);
//        }
//        assert(first_st->journey_pattern_point == jpp);
//        return std::make_pair(first_st, working_dt);
//    }

    //Cette journey_pattern ne comporte aucun trip compatible
    return std::make_pair(nullptr, 0);
}
}}

