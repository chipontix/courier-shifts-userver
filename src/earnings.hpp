#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <userver/dynamic_config/snapshot.hpp>
#include <userver/formats/json_fwd.hpp>
#include <userver/formats/parse/to.hpp>

namespace courier_shifts {

/// Pay rules, all money in integer cents. Lives in dynamic config so it can be
/// changed without a restart; `version` is part of the earnings cache key.
struct Tariffs {
    std::int64_t version{0};
    std::int64_t per_hour_cents{0};
    std::int64_t per_delivery_cents{0};
    std::int64_t per_km_cents{0};
    /// Percent multiplier per zone, zones not listed pay 100%.
    std::unordered_map<std::string, std::int64_t> zone_multiplier_pct;
};

Tariffs Parse(const userver::formats::json::Value& value, userver::formats::parse::To<Tariffs>);

extern const userver::dynamic_config::Key<Tariffs> kTariffs;
extern const userver::dynamic_config::Key<std::chrono::seconds> kEarningsCacheTtl;
extern const userver::dynamic_config::Key<std::chrono::hours> kMaxShiftDuration;

/// What a courier did during one shift, already aggregated by the database.
struct ShiftWork {
    std::int64_t shift_id{0};
    std::string zone;
    std::chrono::seconds duration{0};
    std::int64_t deliveries{0};
    std::int64_t distance_m{0};
};

struct ShiftEarnings {
    std::int64_t shift_id{0};
    std::int64_t time_cents{0};
    std::int64_t deliveries_cents{0};
    std::int64_t total_cents{0};
};

struct Earnings {
    std::vector<ShiftEarnings> shifts;
    std::int64_t total_cents{0};
};

/// Every partial amount is rounded down to a whole cent, then the zone
/// multiplier is applied to the shift subtotal (rounded down again).
Earnings ComputeEarnings(const Tariffs& tariffs, const std::vector<ShiftWork>& work);

}  // namespace courier_shifts
