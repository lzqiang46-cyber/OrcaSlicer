// Snapmaker: filament-to-nozzle (physical extruder) mapping -- regression tests for the
// retract-override merge in print_config_diffs() (PrintApply.cpp) and the map resolution
// helpers (FilamentNozzleMap.cpp).
//
// The merge loop itself is file-static in PrintApply.cpp, so these cases replay its exact
// steps (fallback-index construction + ConfigOption::apply_override) with the real library
// functions: FilamentNozzleMap::physical_extruder_for_{filament,variant_position} and
// is_filament_flow_variant_option(). Keep this mirror in sync with print_config_diffs().
#include <catch2/catch_test_macros.hpp>

#include "libslic3r/Config.hpp"
#include "libslic3r/FilamentNozzleMap.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace Slic3r;

namespace {

DynamicPrintConfig map_test_config(const std::vector<int> &nozzle_map, const std::vector<int> &flow_steps)
{
    DynamicPrintConfig config;
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats{0.4, 0.8});
    config.set_key_value("filament_nozzle_map", new ConfigOptionInts(nozzle_map));
    config.set_key_value("filament_flow_step_size", new ConfigOptionInts(flow_steps));
    return config;
}

// Mirror of the merge loop in print_config_diffs(): nil positions of a filament override
// array inherit the machine value of the extruder mapped to the position's owner -- the
// position is a flow-variant position for variant keys and a plain filament slot otherwise.
std::vector<double> merged_override(const DynamicPrintConfig &config, const std::string &filament_key,
                                    const ConfigOptionFloats &machine, const ConfigOptionFloatsNullable &filament)
{
    const bool variant_axis = is_filament_flow_variant_option(filament_key);
    std::vector<size_t> fallback_indices;
    fallback_indices.reserve(filament.size());
    for (size_t pos = 0; pos < filament.size(); ++ pos) {
        const size_t extruder = variant_axis ?
            FilamentNozzleMap::physical_extruder_for_variant_position(config, pos) :
            FilamentNozzleMap::physical_extruder_for_filament(config, unsigned(pos));
        fallback_indices.emplace_back(machine.size() == 0 ? 0 : std::min(extruder, machine.size() - 1));
    }
    ConfigOption *merged = machine.clone();
    merged->apply_override(&filament, &fallback_indices);
    const auto *merged_floats = static_cast<const ConfigOptionFloats*>(merged);
    std::vector<double> out(merged_floats->size());
    for (size_t i = 0; i < merged_floats->size(); ++ i)
        out[i] = merged_floats->get_at(i);
    delete merged;
    return out;
}

} // namespace

TEST_CASE("Flow-variant axis classification of retract keys", "[FilamentNozzleMap]")
{
    // Keys declared per flow mode: the override array is variant-expanded.
    REQUIRE(is_filament_flow_variant_option("filament_retraction_length"));
    REQUIRE(is_filament_flow_variant_option("filament_retraction_speed"));
    REQUIRE(is_filament_flow_variant_option("filament_z_hop_types"));
    // Plain keys: one entry per filament slot, never expanded.
    REQUIRE_FALSE(is_filament_flow_variant_option("filament_z_hop"));
    REQUIRE_FALSE(is_filament_flow_variant_option("filament_wipe"));
    REQUIRE_FALSE(is_filament_flow_variant_option("filament_retract_before_wipe"));
}

TEST_CASE("Variant positions resolve through the filament-nozzle map", "[FilamentNozzleMap]")
{
    // Filament A owns variant positions 0..1 (two flow modes), B owns position 2.
    const DynamicPrintConfig config = map_test_config({0, 1}, {2, 1});
    REQUIRE(FilamentNozzleMap::physical_extruder_for_variant_position(config, 0) == 0);
    REQUIRE(FilamentNozzleMap::physical_extruder_for_variant_position(config, 1) == 0);
    REQUIRE(FilamentNozzleMap::physical_extruder_for_variant_position(config, 2) == 1);
    // Plain slots resolve directly.
    REQUIRE(FilamentNozzleMap::physical_extruder_for_filament(config, 0) == 0);
    REQUIRE(FilamentNozzleMap::physical_extruder_for_filament(config, 1) == 1);
}

// F3: with every override nil the merged array must still carry each slot's mapped
// nozzle value; before the fix the merge was skipped and consumers read the raw machine
// array by slot index.
TEST_CASE("All-nil retract overrides inherit the mapped nozzle values", "[FilamentNozzleMap][F3]")
{
    const DynamicPrintConfig config = map_test_config({1, 0}, {1, 1});

    ConfigOptionFloats machine;
    machine.values = {0.2, 0.8};
    ConfigOptionFloatsNullable filament;
    filament.values = {ConfigOptionFloatsNullable::nil_value(), ConfigOptionFloatsNullable::nil_value()};

    const std::vector<double> merged = merged_override(config, "filament_retraction_length", machine, filament);
    REQUIRE(merged.size() == 2);
    CHECK(merged[0] == 0.8); // slot A -> extruder 1
    CHECK(merged[1] == 0.2); // slot B -> extruder 0
}

// F4: plain (non flow-variant) keys must resolve the fallback by filament slot, not by
// variant position; before the fix position 1 of filament_z_hop was treated as filament
// A's second flow mode and inherited extruder 0's machine value.
TEST_CASE("Plain retract keys inherit per slot, not per variant position", "[FilamentNozzleMap][F4]")
{
    const DynamicPrintConfig config = map_test_config({0, 1}, {2, 1});

    ConfigOptionFloats machine;
    machine.values = {0.1, 0.6};
    ConfigOptionFloatsNullable filament;
    filament.values = {0.7, ConfigOptionFloatsNullable::nil_value()};

    const std::vector<double> merged = merged_override(config, "filament_z_hop", machine, filament);
    REQUIRE(merged.size() == 2);
    CHECK(merged[0] == 0.7); // slot A override kept
    CHECK(merged[1] == 0.6); // slot B inherits its own nozzle (extruder 1)
}

// Combined: variant key, asymmetric steps, partial override. A/std inherits, A/high is
// overridden, B/std inherits from B's mapped nozzle.
TEST_CASE("Variant retract keys with asymmetric flow steps merge per variant", "[FilamentNozzleMap][F3][F4]")
{
    const DynamicPrintConfig config = map_test_config({0, 1}, {2, 1});

    ConfigOptionFloats machine;
    machine.values = {0.2, 0.8};
    ConfigOptionFloatsNullable filament; // positions: A/std, A/high_flow, B/std
    filament.values = {ConfigOptionFloatsNullable::nil_value(), 0.5, ConfigOptionFloatsNullable::nil_value()};

    const std::vector<double> merged = merged_override(config, "filament_retraction_length", machine, filament);
    REQUIRE(merged.size() == 3);
    CHECK(merged[0] == 0.2); // A/std -> extruder 0
    CHECK(merged[1] == 0.5); // A/high override kept
    CHECK(merged[2] == 0.8); // B/std -> extruder 1
}

// F2 support: the stored map is normalized to the filament count and clamped to the
// physical extruder count when slots or nozzles change.
TEST_CASE("Map normalization tracks filament and extruder counts", "[FilamentNozzleMap][F2]")
{
    DynamicPrintConfig config;
    config.set_key_value("filament_nozzle_map", new ConfigOptionInts{1, 0, 2});

    SECTION("shrinking filaments truncates, growing defaults to extruder 0")
    {
        CHECK(FilamentNozzleMap::normalize(config, 2, 3));
        REQUIRE(FilamentNozzleMap::map(config) == std::vector<int>{1, 0});
        CHECK(FilamentNozzleMap::normalize(config, 4, 3));
        REQUIRE(FilamentNozzleMap::map(config) == std::vector<int>{1, 0, 0, 0});
    }
    SECTION("fewer nozzles clamps out-of-range entries")
    {
        FilamentNozzleMap::normalize(config, 3, 1);
        REQUIRE(FilamentNozzleMap::map(config) == std::vector<int>{0, 0, 0});
    }
    SECTION("an empty map stays empty (first-extruder fallback for every slot)")
    {
        config.set_key_value("filament_nozzle_map", new ConfigOptionInts{});
        CHECK_FALSE(FilamentNozzleMap::normalize(config, 3, 2));
        REQUIRE(FilamentNozzleMap::map(config).empty());
    }
}
