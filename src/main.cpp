#include <userver/clients/dns/component.hpp>
#include <userver/clients/http/component.hpp>
#include <userver/clients/http/component_list.hpp>
#include <userver/components/minimal_server_component_list.hpp>
#include <userver/congestion_control/component.hpp>
#include <userver/dynamic_config/client/component.hpp>
#include <userver/dynamic_config/updater/component.hpp>
#include <userver/server/handlers/ping.hpp>
#include <userver/server/handlers/server_monitor.hpp>
#include <userver/server/handlers/tests_control.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/storages/redis/component.hpp>
#include <userver/storages/secdist/component.hpp>
#include <userver/storages/secdist/provider_component.hpp>
#include <userver/testsuite/testsuite_support.hpp>
#include <userver/utils/daemon_run.hpp>

#include <handlers.hpp>

int main(int argc, char* argv[]) {
    const auto component_list = userver::components::MinimalServerComponentList()
                                    .Append<userver::server::handlers::Ping>()
                                    .Append<userver::server::handlers::ServerMonitor>()
                                    .Append<userver::components::TestsuiteSupport>()
                                    .AppendComponentList(userver::clients::http::ComponentList())
                                    .Append<userver::clients::dns::Component>()
                                    .Append<userver::server::handlers::TestsControl>()
                                    .Append<userver::congestion_control::Component>()
                                    .Append<userver::components::HttpClient>("dynamic-config-http-client")
                                    .Append<userver::components::DynamicConfigClient>()
                                    .Append<userver::components::DynamicConfigClientUpdater>()
                                    .Append<userver::components::Secdist>()
                                    .Append<userver::components::DefaultSecdistProvider>()
                                    .Append<userver::components::Postgres>("courier-db")
                                    .Append<userver::components::Redis>("courier-cache")
                                    .Append<courier_shifts::CreateCourier>()
                                    .Append<courier_shifts::CreateShift>()
                                    .Append<courier_shifts::CancelShift>()
                                    .Append<courier_shifts::ListShifts>()
                                    .Append<courier_shifts::CreateAssignment>()
                                    .Append<courier_shifts::GetEarnings>();
    return userver::utils::DaemonMain(argc, argv, component_list);
}
