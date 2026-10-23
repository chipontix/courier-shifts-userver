#include <earnings.hpp>

#include <userver/dynamic_config/value.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/formats/parse/common_containers.hpp>

namespace courier_shifts {

namespace {

constexpr std::int64_t kSecondsPerHour = 3600;
constexpr std::int64_t kMetersPerKm = 1000;
constexpr std::int64_t kFullPct = 100;

}  // namespace

Tariffs Parse(const userver::formats::json::Value& value, userver::formats::parse::To<Tariffs>) {
    Tariffs tariffs;
    tariffs.version = value["version"].As<std::int64_t>();
    tariffs.per_hour_cents = value["per_hour_cents"].As<std::int64_t>();
    tariffs.per_delivery_cents = value["per_delivery_cents"].As<std::int64_t>();
    tariffs.per_km_cents = value["per_km_cents"].As<std::int64_t>();
    tariffs.zone_multiplier_pct =
        value["zone_multiplier_pct"].As<std::unordered_map<std::string, std::int64_t>>(
            std::unordered_map<std::string, std::int64_t>{}
        );
    return tariffs;
}

const userver::dynamic_config::Key<Tariffs> kTariffs{
    "COURIER_TARIFFS",
    userver::dynamic_config::DefaultAsJsonString{R"({
        "version": 1,
        "per_hour_cents": 25000,
        "per_delivery_cents": 9000,
        "per_km_cents": 1500,
        "zone_multiplier_pct": {"center": 120}
    })"},
};

const userver::dynamic_config::Key<std::chrono::seconds> kEarningsCacheTtl{
    "COURIER_EARNINGS_CACHE_TTL_SECONDS",
    userver::dynamic_config::DefaultAsJsonString{"60"},
};

const userver::dynamic_config::Key<std::chrono::hours> kMaxShiftDuration{
    "COURIER_MAX_SHIFT_HOURS",
    userver::dynamic_config::DefaultAsJsonString{"12"},
};

Earnings ComputeEarnings(const Tariffs& tariffs, const std::vector<ShiftWork>& work) {
    Earnings result;
    result.shifts.reserve(work.size());
    for (const auto& shift : work) {
        const auto time_cents = tariffs.per_hour_cents * shift.duration.count() / kSecondsPerHour;
        const auto deliveries_cents =
            tariffs.per_delivery_cents * shift.deliveries + tariffs.per_km_cents * shift.distance_m / kMetersPerKm;

        const auto it = tariffs.zone_multiplier_pct.find(shift.zone);
        const auto pct = it == tariffs.zone_multiplier_pct.end() ? kFullPct : it->second;

        const auto total = (time_cents + deliveries_cents) * pct / kFullPct;
        result.shifts.push_back({shift.shift_id, time_cents, deliveries_cents, total});
        result.total_cents += total;
    }
    return result;
}

}  // namespace courier_shifts
