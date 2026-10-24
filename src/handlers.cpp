#include <handlers.hpp>

#include <optional>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <userver/components/component_context.hpp>
#include <userver/dynamic_config/storage/component.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/logging/log.hpp>
#include <userver/server/handlers/exceptions.hpp>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/storages/postgres/exceptions.hpp>
#include <userver/storages/postgres/io/chrono.hpp>
#include <userver/storages/redis/client.hpp>
#include <userver/storages/redis/component.hpp>
#include <userver/utils/datetime_light.hpp>
#include <userver/utils/from_string.hpp>

#include <earnings.hpp>

namespace courier_shifts {

namespace {

namespace pg = userver::storages::postgres;
namespace json = userver::formats::json;
using userver::server::handlers::ClientError;
using userver::server::handlers::ConflictError;
using userver::server::handlers::ExternalBody;
using userver::server::handlers::ResourceNotFound;
using userver::server::http::HttpStatus;
using TimePoint = std::chrono::system_clock::time_point;

constexpr auto kMaster = pg::ClusterHostType::kMaster;
const userver::storages::redis::CommandControl kRedisCc{
    std::chrono::milliseconds{50},
    std::chrono::milliseconds{200},
    1,
};

[[noreturn]] void ThrowBadRequest(const std::string& message) { throw ClientError(ExternalBody{message}); }

std::int64_t ParseId(const std::string& raw, std::string_view what) {
    try {
        return userver::utils::FromString<std::int64_t>(raw);
    } catch (const std::exception&) {
        ThrowBadRequest(fmt::format("invalid {}: '{}'", what, raw));
    }
}

TimePoint ParseTime(const std::string& raw, std::string_view what) {
    try {
        return userver::utils::datetime::UtcStringtime(raw, userver::utils::datetime::kRfc3339Format);
    } catch (const std::exception&) {
        ThrowBadRequest(fmt::format("invalid {}: '{}', expected RFC 3339", what, raw));
    }
}

std::string FormatTime(TimePoint tp) {
    return userver::utils::datetime::UtcTimestring(tp, userver::utils::datetime::kRfc3339Format);
}

std::string RequireString(const json::Value& body, std::string_view field) {
    const auto value = body[std::string{field}];
    if (!value.IsString() || value.As<std::string>().empty()) {
        ThrowBadRequest(fmt::format("'{}' must be a non-empty string", field));
    }
    return value.As<std::string>();
}

std::int64_t RequireInt(const json::Value& body, std::string_view field) {
    const auto value = body[std::string{field}];
    if (!value.IsInt64()) {
        ThrowBadRequest(fmt::format("'{}' must be an integer", field));
    }
    return value.As<std::int64_t>();
}

using Days = std::chrono::duration<std::int64_t, std::ratio<86400>>;

// Earnings are cached as the courier's work per UTC day of shift start, so a write invalidates one day instead of
// every cached period of the courier. `{courier}` is a hash tag: all keys of a courier share a Redis Cluster slot,
// which keeps MGET valid if the cache is ever sharded.
std::string EarningsCacheKey(std::int64_t courier_id, std::int64_t day) {
    return fmt::format("earnings:{{{}}}:{}", courier_id, day);
}

std::int64_t DayOf(TimePoint tp) { return std::chrono::floor<Days>(tp.time_since_epoch()).count(); }

constexpr std::int64_t kMaxEarningsWindowDays = 31;

struct DayWork {
    std::int64_t start_us{0};  // PostgreSQL timestamps have microsecond precision
    ShiftWork work;
};

std::string SerializeDay(const std::vector<DayWork>& shifts) {
    json::ValueBuilder day{json::Type::kArray};
    for (const auto& shift : shifts) {
        json::ValueBuilder item;
        item["id"] = shift.work.shift_id;
        item["zone"] = shift.work.zone;
        item["start_us"] = shift.start_us;
        item["duration_s"] = shift.work.duration.count();
        item["deliveries"] = shift.work.deliveries;
        item["distance_m"] = shift.work.distance_m;
        day.PushBack(std::move(item));
    }
    return json::ToString(day.ExtractValue());
}

std::vector<DayWork> ParseDay(const std::string& raw) {
    std::vector<DayWork> shifts;
    for (const auto& item : json::FromString(raw)) {
        shifts.push_back(
            {item["start_us"].As<std::int64_t>(),
             {item["id"].As<std::int64_t>(),
              item["zone"].As<std::string>(),
              std::chrono::seconds{item["duration_s"].As<std::int64_t>()},
              item["deliveries"].As<std::int64_t>(),
              item["distance_m"].As<std::int64_t>()}}
        );
    }
    return shifts;
}

struct ShiftRow {
    std::int64_t id;
    std::string zone;
    pg::TimePointTz starts_at;
    pg::TimePointTz ends_at;
    std::optional<pg::TimePointTz> cancelled_at;
};

json::Value ShiftToJson(const ShiftRow& row) {
    json::ValueBuilder shift;
    shift["id"] = row.id;
    shift["zone"] = row.zone;
    shift["starts_at"] = FormatTime(row.starts_at.GetUnderlying());
    shift["ends_at"] = FormatTime(row.ends_at.GetUnderlying());
    shift["cancelled"] = row.cancelled_at.has_value();
    return shift.ExtractValue();
}

}  // namespace

HandlerBase::HandlerBase(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context
)
    : HttpHandlerJsonBase(config, context),
      pg_(context.FindComponent<userver::components::Postgres>("courier-db").GetCluster()),
      redis_(context.FindComponent<userver::components::Redis>("courier-cache").GetClient("courier-cache")),
      config_(context.FindComponent<userver::components::DynamicConfig>().GetSource())
{}

void HandlerBase::InvalidateEarnings(std::int64_t courier_id, TimePoint shift_start) const {
    try {
        redis_->Del(EarningsCacheKey(courier_id, DayOf(shift_start)), kRedisCc).Get();
    } catch (const std::exception& e) {
        // The cache entry expires by TTL anyway; failing the write would be worse.
        LOG_ERROR() << "failed to invalidate earnings cache for courier " << courier_id << ": " << e;
    }
}

json::Value CreateCourier::HandleRequestJsonThrow(
    const userver::server::http::HttpRequest& request,
    const json::Value& body,
    userver::server::request::RequestContext&
) const {
    const auto name = RequireString(body, "name");
    const auto id =
        pg_->Execute(kMaster, "INSERT INTO courier_shifts.couriers (name) VALUES ($1) RETURNING id", name)
            .AsSingleRow<std::int64_t>();

    request.SetResponseStatus(HttpStatus::kCreated);
    json::ValueBuilder response;
    response["id"] = id;
    return response.ExtractValue();
}

json::Value CreateShift::HandleRequestJsonThrow(
    const userver::server::http::HttpRequest& request,
    const json::Value& body,
    userver::server::request::RequestContext&
) const {
    const auto courier_id = RequireInt(body, "courier_id");
    const auto zone = RequireString(body, "zone");
    const auto starts_at = ParseTime(RequireString(body, "starts_at"), "starts_at");
    const auto ends_at = ParseTime(RequireString(body, "ends_at"), "ends_at");

    if (ends_at <= starts_at) {
        ThrowBadRequest("ends_at must be after starts_at");
    }
    const auto snapshot = config_.GetSnapshot();
    const auto max_duration = snapshot[kMaxShiftDuration];
    if (ends_at - starts_at > max_duration) {
        ThrowBadRequest(fmt::format("shift is longer than {}h", max_duration.count()));
    }

    // The exclusion constraint is the invariant. Concurrent inserts for one courier can still deadlock while
    // checking it (each waits for the other's uncommitted row, PostgreSQL aborts one with 40P01), so writers of
    // the same courier are queued on a transaction-scoped advisory lock taken by the same statement.
    std::int64_t id = 0;
    try {
        id = pg_->Execute(
                    kMaster,
                    "WITH lock AS (SELECT pg_advisory_xact_lock($1)) "
                    "INSERT INTO courier_shifts.shifts (courier_id, zone, period) "
                    "SELECT $1, $2, tstzrange($3, $4, '[)') FROM lock RETURNING id",
                    courier_id,
                    zone,
                    pg::TimePointTz{starts_at},
                    pg::TimePointTz{ends_at}
        )
                 .AsSingleRow<std::int64_t>();
    } catch (const pg::ExclusionViolation&) {
        throw ConflictError(ExternalBody{"shift overlaps another shift of this courier"});
    } catch (const pg::ForeignKeyViolation&) {
        throw ResourceNotFound(ExternalBody{fmt::format("courier {} not found", courier_id)});
    }
    InvalidateEarnings(courier_id, starts_at);

    request.SetResponseStatus(HttpStatus::kCreated);
    json::ValueBuilder response;
    response["id"] = id;
    return response.ExtractValue();
}

json::Value CancelShift::HandleRequestJsonThrow(
    const userver::server::http::HttpRequest& request,
    const json::Value&,
    userver::server::request::RequestContext&
) const {
    const auto shift_id = ParseId(request.GetPathArg("shift_id"), "shift_id");
    // Idempotent: cancelling twice keeps the first cancellation time.
    struct Cancelled {
        std::int64_t courier_id;
        pg::TimePointTz starts_at;
    };
    const auto cancelled = pg_->Execute(
                                  kMaster,
                                  "UPDATE courier_shifts.shifts SET cancelled_at = coalesce(cancelled_at, now()) "
                                  "WHERE id = $1 RETURNING courier_id, lower(period)",
                                  shift_id
    )
                               .AsOptionalSingleRow<Cancelled>(pg::kRowTag);
    if (!cancelled) {
        throw ResourceNotFound(ExternalBody{fmt::format("shift {} not found", shift_id)});
    }
    InvalidateEarnings(cancelled->courier_id, cancelled->starts_at.GetUnderlying());

    json::ValueBuilder response;
    response["id"] = shift_id;
    response["cancelled"] = true;
    return response.ExtractValue();
}

json::Value ListShifts::HandleRequestJsonThrow(
    const userver::server::http::HttpRequest& request,
    const json::Value&,
    userver::server::request::RequestContext&
) const {
    const auto courier_id = ParseId(request.GetPathArg("courier_id"), "courier_id");
    const auto from = ParseTime(request.GetArg("from"), "from");
    const auto to = ParseTime(request.GetArg("to"), "to");

    const auto rows = pg_->Execute(
                             kMaster,
                             "SELECT id, zone, lower(period), upper(period), cancelled_at "
                             "FROM courier_shifts.shifts "
                             "WHERE courier_id = $1 AND lower(period) >= $2 AND lower(period) < $3 "
                             "ORDER BY lower(period)",
                             courier_id,
                             pg::TimePointTz{from},
                             pg::TimePointTz{to}
    )
                          .AsContainer<std::vector<ShiftRow>>(pg::kRowTag);

    json::ValueBuilder shifts{json::Type::kArray};
    for (const auto& row : rows) {
        shifts.PushBack(ShiftToJson(row));
    }
    json::ValueBuilder response;
    response["shifts"] = std::move(shifts);
    return response.ExtractValue();
}

json::Value CreateAssignment::HandleRequestJsonThrow(
    const userver::server::http::HttpRequest& request,
    const json::Value& body,
    userver::server::request::RequestContext&
) const {
    const auto shift_id = ParseId(request.GetPathArg("shift_id"), "shift_id");
    const auto order_id = RequireString(body, "order_id");
    const auto distance_m = RequireInt(body, "distance_m");
    const auto delivered_at = ParseTime(RequireString(body, "delivered_at"), "delivered_at");
    if (distance_m < 0) {
        ThrowBadRequest("'distance_m' must be non-negative");
    }

    struct Inserted {
        std::int64_t id;
        std::int64_t courier_id;
        pg::TimePointTz shift_start;
    };
    std::optional<Inserted> inserted;
    try {
        inserted = pg_->Execute(
                          kMaster,
                          // FOR SHARE: a concurrent cancel waits for this insert (or wins and the
                          // re-checked row no longer matches), so no delivery lands on a cancelled shift.
                          "WITH s AS ("
                          "  SELECT id, courier_id, lower(period) AS shift_start FROM courier_shifts.shifts "
                          "  WHERE id = $1 AND cancelled_at IS NULL AND period @> $4::timestamptz FOR SHARE"
                          "), ins AS ("
                          "  INSERT INTO courier_shifts.assignments (shift_id, order_id, distance_m, delivered_at) "
                          "  SELECT id, $2, $3, $4 FROM s RETURNING id"
                          ") SELECT ins.id, s.courier_id, s.shift_start FROM ins, s",
                          shift_id,
                          order_id,
                          static_cast<std::int32_t>(distance_m),
                          pg::TimePointTz{delivered_at}
        )
                       .AsOptionalSingleRow<Inserted>(pg::kRowTag);
    } catch (const pg::UniqueViolation&) {
        throw ConflictError(ExternalBody{fmt::format("order '{}' is already assigned", order_id)});
    }

    if (!inserted) {
        const bool shift_exists =
            pg_->Execute(kMaster, "SELECT EXISTS (SELECT 1 FROM courier_shifts.shifts WHERE id = $1)", shift_id)
                .AsSingleRow<bool>();
        if (!shift_exists) {
            throw ResourceNotFound(ExternalBody{fmt::format("shift {} not found", shift_id)});
        }
        throw ConflictError(ExternalBody{"shift is cancelled or delivered_at is outside of it"});
    }
    InvalidateEarnings(inserted->courier_id, inserted->shift_start.GetUnderlying());

    request.SetResponseStatus(HttpStatus::kCreated);
    json::ValueBuilder response;
    response["id"] = inserted->id;
    return response.ExtractValue();
}

json::Value GetEarnings::HandleRequestJsonThrow(
    const userver::server::http::HttpRequest& request,
    const json::Value&,
    userver::server::request::RequestContext&
) const {
    const auto courier_id = ParseId(request.GetPathArg("courier_id"), "courier_id");
    const auto from = ParseTime(request.GetArg("from"), "from");
    const auto to = ParseTime(request.GetArg("to"), "to");
    if (to <= from) {
        ThrowBadRequest("'to' must be after 'from'");
    }
    const auto first_day = DayOf(from);
    const auto last_day = DayOf(to - std::chrono::microseconds{1});
    if (last_day - first_day + 1 > kMaxEarningsWindowDays) {
        ThrowBadRequest(fmt::format("the window must not exceed {} days", kMaxEarningsWindowDays));
    }

    const auto snapshot = config_.GetSnapshot();
    std::vector<std::string> keys;
    for (auto day = first_day; day <= last_day; ++day) {
        keys.push_back(EarningsCacheKey(courier_id, day));
    }
    std::vector<std::optional<std::string>> cached(keys.size());
    try {
        cached = redis_->Mget(keys, kRedisCc).Get();
    } catch (const std::exception& e) {
        LOG_WARNING() << "earnings cache read failed, falling back to the database: " << e;
    }

    // Days are contiguous, so one range query covers all the missing ones (rows of cached days are dropped).
    std::vector<std::vector<DayWork>> days(keys.size());
    std::optional<std::int64_t> first_missing;
    std::int64_t last_missing = 0;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (cached[i]) {
            days[i] = ParseDay(*cached[i]);
        } else {
            const auto day = first_day + static_cast<std::int64_t>(i);
            first_missing = first_missing.value_or(day);
            last_missing = day;
        }
    }

    if (first_missing) {
        struct WorkRow {
            std::int64_t shift_id;
            std::string zone;
            std::int64_t start_us;
            std::int64_t duration_s;
            std::int64_t deliveries;
            std::int64_t distance_m;
        };
        const auto range_start = TimePoint{Days{*first_missing}};
        const auto range_end = TimePoint{Days{last_missing + 1}};
        const auto rows = pg_->Execute(
                                 kMaster,
                                 "SELECT s.id, s.zone, "
                                 "  (EXTRACT(EPOCH FROM lower(s.period)) * 1000000)::bigint, "
                                 "  EXTRACT(EPOCH FROM upper(s.period) - lower(s.period))::bigint, "
                                 "  count(a.id), coalesce(sum(a.distance_m), 0)::bigint "
                                 "FROM courier_shifts.shifts s "
                                 "LEFT JOIN courier_shifts.assignments a ON a.shift_id = s.id "
                                 "WHERE s.courier_id = $1 AND s.cancelled_at IS NULL "
                                 "  AND lower(s.period) >= $2 AND lower(s.period) < $3 "
                                 "GROUP BY s.id ORDER BY lower(s.period)",
                                 courier_id,
                                 pg::TimePointTz{range_start},
                                 pg::TimePointTz{range_end}
        )
                              .AsContainer<std::vector<WorkRow>>(pg::kRowTag);

        std::vector<bool> missing(keys.size());
        for (std::size_t i = 0; i < keys.size(); ++i) {
            missing[i] = !cached[i];
        }
        for (const auto& row : rows) {
            const auto i = static_cast<std::size_t>(DayOf(TimePoint{std::chrono::microseconds{row.start_us}}) - first_day);
            if (missing[i]) {
                days[i].push_back(
                    {row.start_us,
                     {row.shift_id, row.zone, std::chrono::seconds{row.duration_s}, row.deliveries, row.distance_m}}
                );
            }
        }

        // ponytail: a reader that queried the DB before a concurrent write can store a stale day after that write's
        // invalidation; it lives until TTL. Version the day key (INCR on write) if that window matters.
        const auto ttl = snapshot[kEarningsCacheTtl];
        std::vector<userver::storages::redis::RequestSet> writes;
        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (missing[i]) {
                writes.push_back(redis_->Set(keys[i], SerializeDay(days[i]), ttl, kRedisCc));
            }
        }
        try {
            for (auto& write : writes) {
                write.Get();
            }
        } catch (const std::exception& e) {
            LOG_WARNING() << "earnings cache write failed: " << e;
        }
    }

    std::vector<ShiftWork> work;
    for (const auto& day : days) {
        for (const auto& shift : day) {
            const TimePoint start{std::chrono::microseconds{shift.start_us}};
            if (start >= from && start < to) {
                work.push_back(shift.work);
            }
        }
    }

    const auto& tariffs = snapshot[kTariffs];
    const auto earnings = ComputeEarnings(tariffs, work);

    json::ValueBuilder response;
    response["courier_id"] = courier_id;
    response["tariffs_version"] = tariffs.version;
    response["total_cents"] = earnings.total_cents;
    response["shifts"] = json::ValueBuilder{json::Type::kArray};
    for (const auto& shift : earnings.shifts) {
        json::ValueBuilder item;
        item["shift_id"] = shift.shift_id;
        item["time_cents"] = shift.time_cents;
        item["deliveries_cents"] = shift.deliveries_cents;
        item["total_cents"] = shift.total_cents;
        response["shifts"].PushBack(std::move(item));
    }
    return response.ExtractValue();
}

}  // namespace courier_shifts
