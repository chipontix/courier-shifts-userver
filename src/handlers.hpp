#pragma once

#include <chrono>
#include <cstdint>
#include <string_view>

#include <userver/dynamic_config/source.hpp>
#include <userver/server/handlers/http_handler_json_base.hpp>
#include <userver/storages/postgres/postgres_fwd.hpp>
#include <userver/storages/redis/client_fwd.hpp>

namespace courier_shifts {

/// Common dependencies of all API handlers: the database, the earnings cache
/// and dynamic config.
class HandlerBase : public userver::server::handlers::HttpHandlerJsonBase {
public:
    HandlerBase(const userver::components::ComponentConfig&, const userver::components::ComponentContext&);

protected:
    /// Drops the cached work of the courier's UTC day that contains `shift_start`
    /// after any write that changes it.
    void InvalidateEarnings(std::int64_t courier_id, std::chrono::system_clock::time_point shift_start) const;

    userver::storages::postgres::ClusterPtr pg_;
    userver::storages::redis::ClientPtr redis_;
    userver::dynamic_config::Source config_;
};

#define COURIER_SHIFTS_HANDLER(Class, name)                                                               \
    class Class final : public HandlerBase {                                                              \
    public:                                                                                               \
        static constexpr std::string_view kName = name;                                                   \
        using HandlerBase::HandlerBase;                                                                   \
        userver::formats::json::Value HandleRequestJsonThrow(                                             \
            const userver::server::http::HttpRequest& request,                                            \
            const userver::formats::json::Value& body,                                                    \
            userver::server::request::RequestContext& context                                             \
        ) const override;                                                                                 \
    }

COURIER_SHIFTS_HANDLER(CreateCourier, "handler-create-courier");
COURIER_SHIFTS_HANDLER(CreateShift, "handler-create-shift");
COURIER_SHIFTS_HANDLER(CancelShift, "handler-cancel-shift");
COURIER_SHIFTS_HANDLER(ListShifts, "handler-list-shifts");
COURIER_SHIFTS_HANDLER(CreateAssignment, "handler-create-assignment");
COURIER_SHIFTS_HANDLER(GetEarnings, "handler-get-earnings");

#undef COURIER_SHIFTS_HANDLER

}  // namespace courier_shifts
