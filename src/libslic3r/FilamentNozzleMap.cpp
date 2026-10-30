#include "FilamentNozzleMap.hpp"

#include <algorithm>
#include <cassert>

namespace Slic3r {

namespace FilamentNozzleMap {

const std::string OPTION_KEY = "filament_nozzle_map";

namespace {

const ConfigOptionInts *ints_option(const ConfigOptionResolver &config, const std::string &key)
{
    return dynamic_cast<const ConfigOptionInts *>(config.option(key));
}

const ConfigOptionStrings *strings_option(const ConfigOptionResolver &config, const std::string &key)
{
    return dynamic_cast<const ConfigOptionStrings *>(config.option(key));
}

const ConfigOptionFloats *floats_option(const ConfigOptionResolver &config, const std::string &key)
{
    return dynamic_cast<const ConfigOptionFloats *>(config.option(key));
}

// Core resolution over an explicit mapping + extruder count, shared by the single-config and
// split-source overloads.
unsigned int resolve_filament(const ConfigOptionInts *mapping, size_t num_extruders, unsigned int filament_id)
{
    if (mapping != nullptr && filament_id < mapping->values.size()) {
        const int extruder = mapping->values[filament_id];
        if (extruder >= 0 && size_t(extruder) < num_extruders)
            return unsigned(extruder);
    }
    // default
    return 0;
}

} // namespace

size_t physical_extruder_count(const ConfigOptionResolver &config)
{
    // Primary source: nozzle_diameter (printer hardware key). It is absent from GCodeConfig --
    // the config the GCode writer / Extruder per-filament lookups run against -- where the
    // equivalent carrier of the extruder count is nozzle_volume_type: one entry per physical
    // nozzle, also part of GCodeConfig, and never variant-expanded (unlike the retract arrays,
    // which the filament-override merge can resize). Returning 1 here would clamp every map
    // entry >= 1 to extruder 0 in resolve_filament().
    if (const ConfigOptionFloats *nozzle_diameter = floats_option(config, "nozzle_diameter");
        nozzle_diameter != nullptr && !nozzle_diameter->values.empty())
        return std::max<size_t>(1, nozzle_diameter->values.size());
    if (const ConfigOption *volumes = config.option("nozzle_volume_type");
        volumes != nullptr && volumes->type() == coEnums) {
        const auto *types = static_cast<const ConfigOptionEnumsGeneric *>(volumes);
        if (!types->values.empty())
            return types->values.size();
    }
    assert(false);
    BOOST_LOG_TRIVIAL(error) << "physical_extruder_count: neither nozzle_diameter nor nozzle_volume_type "
                                "available in config, assuming 1 extruder";
    return 1;
}

size_t filament_count(const ConfigOptionResolver &config)
{
    if (const ConfigOptionStrings *colour = strings_option(config, "filament_colour");
        colour != nullptr && !colour->values.empty())
        return colour->values.size();
    if (const ConfigOptionFloats *diameter = floats_option(config, "filament_diameter");
        diameter != nullptr && !diameter->values.empty())
        return diameter->values.size();
    return 1;
}

bool is_active(const ConfigOptionResolver &config)
{
    const ConfigOptionInts *mapping = ints_option(config, OPTION_KEY);
    return mapping != nullptr && !mapping->values.empty();
}

std::vector<int> map(const ConfigOptionResolver &config)
{
    const ConfigOptionInts *mapping = ints_option(config, OPTION_KEY);
    return mapping == nullptr ? std::vector<int>{} : mapping->values;
}

unsigned int physical_extruder_for_filament(const ConfigOptionResolver &config, unsigned int filament_id)
{
    return resolve_filament(ints_option(config, OPTION_KEY), physical_extruder_count(config), filament_id);
}

unsigned int physical_extruder_for_filament(const ConfigOptionResolver &project_config, const ConfigOptionResolver &printer_config, unsigned int filament_id)
{
    return resolve_filament(ints_option(project_config, OPTION_KEY), physical_extruder_count(printer_config), filament_id);
}

size_t filament_of_variant_position(const ConfigOptionResolver &config, size_t position)
{
    const ConfigOptionInts *step_sizes = ints_option(config, "filament_flow_step_size");
    if (step_sizes == nullptr || step_sizes->values.empty())
        return position;

    // Mirrors the segment_start accumulation of get_config_idx() (ConfigFlowDomain::Filament):
    // filament f owns the positions [segment_start(f), segment_start(f) + step_size_of(f)).
    auto step_size_of = [step_sizes](size_t filament_idx) -> size_t {
        if (filament_idx >= step_sizes->values.size())
            return 1;
        return size_t(std::max(1, step_sizes->values[filament_idx]));
    };

    size_t filament_idx = 0;
    while (position >= step_size_of(filament_idx)) {
        position -= step_size_of(filament_idx);
        ++filament_idx;
    }
    return filament_idx;
}

std::vector<size_t> variant_positions_to_filaments(const ConfigOptionResolver &config, size_t positions)
{
    std::vector<size_t> out(positions);
    for (size_t p = 0; p < positions; ++p)
        out[p] = filament_of_variant_position(config, p);
    return out;
}

unsigned int physical_extruder_for_variant_position(const ConfigOptionResolver &config, size_t position)
{
    return physical_extruder_for_filament(config, unsigned(filament_of_variant_position(config, position)));
}

std::vector<unsigned int> filaments_for_physical_extruder(const ConfigOptionResolver &config, unsigned int extruder_id)
{
    std::vector<unsigned int> out;
    const size_t num_filaments = filament_count(config);
    for (unsigned int i = 0; i < num_filaments; ++i)
        if (physical_extruder_for_filament(config, i) == extruder_id)
            out.push_back(i);
    return out;
}

std::vector<unsigned int> filaments_for_physical_extruder(const ConfigOptionResolver &project_config, const ConfigOptionResolver &printer_config, unsigned int extruder_id)
{
    std::vector<unsigned int> out;
    const size_t num_filaments = filament_count(project_config);
    for (unsigned int i = 0; i < num_filaments; ++i)
        if (physical_extruder_for_filament(project_config, printer_config, i) == extruder_id)
            out.push_back(i);
    return out;
}

bool normalize(DynamicPrintConfig &config, size_t num_filaments, size_t num_extruders)
{
    auto *mapping = config.option<ConfigOptionInts>(OPTION_KEY, false);
    if (mapping == nullptr || mapping->values.empty())
        return false;

    num_extruders = std::max<size_t>(1, num_extruders);
    bool changed = false;

    if (mapping->values.size() != num_filaments) {
        // New filament slots default to extruder 0 (design doc D2).
        mapping->values.resize(num_filaments, 0);
        changed = true;
    }
    for (int &extruder : mapping->values) {
        const int clamped = std::clamp(extruder, 0, int(num_extruders - 1));
        if (extruder != clamped) {
            extruder = clamped;
            changed = true;
        }
    }
    return changed;
}

} // namespace FilamentNozzleMap

// ==== FilamentNozzleMapManager ===================================================================

ConfigOptionInts *FilamentNozzleMapManager::map_option(bool create)
{
    return m_config->option<ConfigOptionInts>(FilamentNozzleMap::OPTION_KEY, create);
}

const ConfigOptionInts *FilamentNozzleMapManager::map_option() const
{
    return m_config->option<ConfigOptionInts>(FilamentNozzleMap::OPTION_KEY);
}

bool FilamentNozzleMapManager::is_active() const { return FilamentNozzleMap::is_active(*m_config); }

bool FilamentNozzleMapManager::is_identity() const
{
    const ConfigOptionInts *mapping = map_option();
    if (mapping == nullptr || mapping->values.empty())
        return true;
    // With the first-extruder fallback a stored map is redundant exactly when every entry maps
    // to extruder 0 -- the same resolution an empty vector produces.
    for (int extruder : mapping->values)
        if (extruder != 0)
            return false;
    return true;
}

std::vector<int> FilamentNozzleMapManager::map() const { return FilamentNozzleMap::map(*m_config); }

unsigned int FilamentNozzleMapManager::physical_extruder_of(unsigned int filament_id) const
{
    return FilamentNozzleMap::physical_extruder_for_filament(*m_config, *m_printer_config, filament_id);
}

std::vector<unsigned int> FilamentNozzleMapManager::filaments_of(unsigned int extruder_id) const
{
    return FilamentNozzleMap::filaments_for_physical_extruder(*m_config, *m_printer_config, extruder_id);
}

bool FilamentNozzleMapManager::set_filament_physical_extruder(unsigned int filament_id, unsigned int extruder_id)
{
    const size_t num_extruders = FilamentNozzleMap::physical_extruder_count(*m_printer_config);
    if (extruder_id >= num_extruders)
        return false;
    ConfigOptionInts *mapping = map_option(true);
    if (mapping == nullptr)
        return false;

    if (mapping->values.empty()) {
        // Growing an empty map: seed the other slots with the fallback (first extruder).
        const size_t num_filaments = std::max<size_t>(FilamentNozzleMap::filament_count(*m_config), size_t(filament_id) + 1);
        mapping->values.resize(num_filaments, 0);
    }
    if (filament_id >= mapping->values.size())
        mapping->values.resize(size_t(filament_id) + 1, 0);
    mapping->values[filament_id] = int(extruder_id);
    return true;
}

void FilamentNozzleMapManager::set_map(std::vector<int> map)
{
    if (ConfigOptionInts *mapping = map_option(true); mapping != nullptr)
        mapping->values = std::move(map);
}

void FilamentNozzleMapManager::clear()
{
    if (ConfigOptionInts *mapping = map_option(false); mapping != nullptr)
        mapping->values.clear();
}

bool FilamentNozzleMapManager::on_filaments_changed(size_t num_filaments)
{
    ConfigOptionInts *mapping = map_option(false);
    if (mapping == nullptr || mapping->values.empty())
        return false;
    if (mapping->values.size() == num_filaments)
        return false;
    // New slots default to extruder 0 (design doc D2).
    mapping->values.resize(num_filaments, 0);
    return true;
}

bool FilamentNozzleMapManager::on_physical_extruders_changed(size_t num_extruders)
{
    ConfigOptionInts *mapping = map_option(false);
    if (mapping == nullptr || mapping->values.empty())
        return false;
    num_extruders = std::max<size_t>(1, num_extruders);
    bool changed = false;
    for (int &extruder : mapping->values) {
        const int clamped = std::clamp(extruder, 0, int(num_extruders - 1));
        if (extruder != clamped) {
            extruder = clamped;
            changed = true;
        }
    }
    return changed;
}

} // namespace Slic3r
