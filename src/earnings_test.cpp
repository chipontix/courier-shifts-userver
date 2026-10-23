#include <earnings.hpp>

#include <userver/dynamic_config/test_helpers.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/utest/utest.hpp>

namespace courier_shifts {

namespace {

using std::chrono::hours;
using std::chrono::minutes;
using std::chrono::seconds;

Tariffs MakeTariffs() {
    Tariffs tariffs;
    tariffs.per_hour_cents = 25000;
    tariffs.per_delivery_cents = 9000;
    tariffs.per_km_cents = 1500;
    tariffs.zone_multiplier_pct = {{"center", 120}, {"suburb", 85}};
    return tariffs;
}

}  // namespace

TEST(Earnings, Empty) {
    const auto earnings = ComputeEarnings(MakeTariffs(), {});
    EXPECT_TRUE(earnings.shifts.empty());
    EXPECT_EQ(earnings.total_cents, 0);
}

TEST(Earnings, TimeAndDeliveries) {
    const auto earnings = ComputeEarnings(MakeTariffs(), {{1, "north", hours{4} + minutes{30}, 2, 3833}});
    ASSERT_EQ(earnings.shifts.size(), 1);
    EXPECT_EQ(earnings.shifts[0].time_cents, 112500);
    // 2 * 9000 + floor(1500 * 3833 / 1000) = 18000 + 5749
    EXPECT_EQ(earnings.shifts[0].deliveries_cents, 23749);
    EXPECT_EQ(earnings.shifts[0].total_cents, 136249);
    EXPECT_EQ(earnings.total_cents, 136249);
}

TEST(Earnings, PartialHourRoundsDown) {
    // 25000 cents/h * 1s / 3600s = 6.94 -> 6
    const auto earnings = ComputeEarnings(MakeTariffs(), {{1, "north", seconds{1}, 0, 0}});
    EXPECT_EQ(earnings.total_cents, 6);
}

TEST(Earnings, ZoneMultiplierAppliesToShiftSubtotal) {
    const auto earnings = ComputeEarnings(
        MakeTariffs(),
        {
            {1, "center", hours{1}, 1, 1000},  // (25000 + 9000 + 1500) * 1.2 = 42600
            {2, "suburb", hours{1}, 0, 0},     // 25000 * 0.85 = 21250
            {3, "unknown", hours{1}, 0, 0},    // 100%
        }
    );
    ASSERT_EQ(earnings.shifts.size(), 3);
    EXPECT_EQ(earnings.shifts[0].total_cents, 42600);
    EXPECT_EQ(earnings.shifts[1].total_cents, 21250);
    EXPECT_EQ(earnings.shifts[2].total_cents, 25000);
    EXPECT_EQ(earnings.total_cents, 42600 + 21250 + 25000);
}

TEST(Earnings, NoOverflowOnLongestShift) {
    // Max shift is hours-long, so int64 intermediate values stay far from the limit even with large rates.
    Tariffs tariffs = MakeTariffs();
    tariffs.per_hour_cents = 10'000'000;
    tariffs.per_km_cents = 10'000'000;
    const auto earnings = ComputeEarnings(tariffs, {{1, "north", hours{24}, 1000, 1'000'000}});
    EXPECT_EQ(earnings.shifts[0].time_cents, 240'000'000);
    EXPECT_EQ(earnings.shifts[0].deliveries_cents, 9000 * 1000 + 10'000'000'000);
}

TEST(Tariffs, Parse) {
    const auto tariffs = userver::formats::json::FromString(R"({
        "version": 3, "per_hour_cents": 1, "per_delivery_cents": 2, "per_km_cents": 3,
        "zone_multiplier_pct": {"center": 150}
    })")
                             .As<Tariffs>();
    EXPECT_EQ(tariffs.version, 3);
    EXPECT_EQ(tariffs.per_hour_cents, 1);
    EXPECT_EQ(tariffs.per_delivery_cents, 2);
    EXPECT_EQ(tariffs.per_km_cents, 3);
    EXPECT_EQ(tariffs.zone_multiplier_pct.at("center"), 150);
}

TEST(Tariffs, ZoneMultipliersAreOptional) {
    const auto tariffs = userver::formats::json::FromString(
                             R"({"version": 1, "per_hour_cents": 1, "per_delivery_cents": 2, "per_km_cents": 3})"
    )
                             .As<Tariffs>();
    EXPECT_TRUE(tariffs.zone_multiplier_pct.empty());
}

TEST(Tariffs, DynamicConfigDefaultsParse) {
    const auto& snapshot = userver::dynamic_config::GetDefaultSnapshot();
    EXPECT_EQ(snapshot[kTariffs].per_hour_cents, 25000);
    EXPECT_EQ(snapshot[kEarningsCacheTtl], seconds{60});
    EXPECT_EQ(snapshot[kMaxShiftDuration], hours{12});
}

}  // namespace courier_shifts
