// Snapmaker: filament-to-nozzle (physical extruder) mapping -- regression tests for the
// retract-override merge behind print_config_diffs() (PrintApply.cpp) and the map resolution
// helpers (FilamentNozzleMap.cpp).
//
// The merge itself is the production function merge_retract_override(), defined in
// PrintApply.cpp right before print_config_diffs(), which calls it unconditionally for every
// non-gated retract key and diffs the result against the previously applied array. Only the
// thin decision (print_diff / store) stays inline in the file-static entry.
#include <catch2/catch_test_macros.hpp>

#include "libslic3r/Config.hpp"
#include "libslic3r/FilamentNozzleMap.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <cmath>
#include <string>
#include <vector>

// Production retract-override merge -- no public header (it lives beside its only production
// caller in PrintApply.cpp), so it is declared here to link the real symbol.
namespace Slic3r {

struct RetractOverrideMerge {
    ConfigOption *merged    = nullptr;
    bool          overriden = false;
    bool          changed   = false;
};

RetractOverrideMerge merge_retract_override(const ConfigOptionResolver &config, const std::string &filament_key,
                                            const ConfigOption *machine, const ConfigOption *filament_override,
                                            const ConfigOption *current_effective);

} // namespace Slic3r

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

std::vector<double> floats_of(const ConfigOption *option)
{
    const auto *floats = static_cast<const ConfigOptionFloats*>(option);
    return {floats->values.begin(), floats->values.end()};
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
// nozzle value; the merge is unconditional, so the untouched machine array as the
// current value counts as changed whenever the map is non-trivial.
TEST_CASE("All-nil retract overrides inherit the mapped nozzle values", "[FilamentNozzleMap][F3]")
{
    const DynamicPrintConfig config = map_test_config({1, 0}, {1, 1});

    ConfigOptionFloats machine;
    machine.values = {0.2, 0.8};
    ConfigOptionFloatsNullable filament;
    filament.values = {ConfigOptionFloatsNullable::nil_value(), ConfigOptionFloatsNullable::nil_value()};
    ConfigOptionFloats current; // state left behind by the identity map [0,1]
    current.values = {0.2, 0.8};

    auto merge = merge_retract_override(config, "filament_retraction_length", &machine, &filament, &current);
    const std::vector<double> merged = floats_of(merge.merged);
    REQUIRE(merged.size() == 2);
    CHECK(merged[0] == 0.8); // slot A -> extruder 1
    CHECK(merged[1] == 0.2); // slot B -> extruder 0
    CHECK_FALSE(merge.overriden);
    CHECK(merge.changed);
    delete merge.merged;
}

// R1 (second-review report, old F3 residue): all overrides nil, machine values untouched and
// the previously applied array still equal to the machine array -- flipping only the map must
// still re-derive the effective array. The old inner gate skipped the merge in exactly this
// state and consumers kept reading the previous extruder order.
TEST_CASE("Flipping only the map re-derives the effective retract array", "[FilamentNozzleMap][F3][R1]")
{
    const DynamicPrintConfig config = map_test_config({1, 0}, {1, 1});

    ConfigOptionFloats machine;
    machine.values = {0.2, 0.8};
    ConfigOptionFloatsNullable filament;
    filament.values = {ConfigOptionFloatsNullable::nil_value(), ConfigOptionFloatsNullable::nil_value()};

    SECTION("old effective array equals the machine array (the R1 counterexample)")
    {
        ConfigOptionFloats current;
        current.values = {0.2, 0.8};
        auto merge = merge_retract_override(config, "filament_retraction_length", &machine, &filament, &current);
        const std::vector<double> merged = floats_of(merge.merged);
        CHECK(merged.size() == 2);
        CHECK(merged[0] == 0.8); // expected A = 0.8
        CHECK(merged[1] == 0.2); // expected B = 0.2
        CHECK(merge.changed);    // the caller must emit the diff and store the array
        delete merge.merged;
    }
    SECTION("old effective array already remapped (steady state converges)")
    {
        ConfigOptionFloats current;
        current.values = {0.8, 0.2};
        auto merge = merge_retract_override(config, "filament_retraction_length", &machine, &filament, &current);
        CHECK_FALSE(merge.changed); // nothing to store or invalidate
        CHECK_FALSE(merge.overriden);
        delete merge.merged;
    }
    SECTION("old effective array differs from the machine array (A06 contrast)")
    {
        ConfigOptionFloats current;
        current.values = {0.3, 0.9};
        auto merge = merge_retract_override(config, "filament_retraction_length", &machine, &filament, &current);
        const std::vector<double> merged = floats_of(merge.merged);
        CHECK(merged[0] == 0.8);
        CHECK(merged[1] == 0.2);
        CHECK(merge.changed);
        delete merge.merged;
    }
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
    ConfigOptionFloats current;
    current.values = {0.1, 0.6};

    auto merge = merge_retract_override(config, "filament_z_hop", &machine, &filament, &current);
    const std::vector<double> merged = floats_of(merge.merged);
    REQUIRE(merged.size() == 2);
    CHECK(merged[0] == 0.7); // slot A override kept
    CHECK(merged[1] == 0.6); // slot B inherits its own nozzle (extruder 1)
    CHECK(merge.overriden);
    delete merge.merged;
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
    ConfigOptionFloats current;
    current.values = {0.2, 0.2, 0.8};

    auto merge = merge_retract_override(config, "filament_retraction_length", &machine, &filament, &current);
    const std::vector<double> merged = floats_of(merge.merged);
    REQUIRE(merged.size() == 3);
    CHECK(merged[0] == 0.2); // A/std -> extruder 0
    CHECK(merged[1] == 0.5); // A/high override kept
    CHECK(merged[2] == 0.8); // B/std -> extruder 1
    CHECK(merge.overriden);
    delete merge.merged;
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
