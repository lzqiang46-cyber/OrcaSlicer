#pragma once

// Snapmaker: filament-to-nozzle (physical extruder) mapping =======================================
//
// filament_nozzle_map (coInts, project-scoped, persisted in project_settings.config) holds one
// entry per filament slot: the 0-based physical extruder (nozzle) that feeds it. There is no
// automatic mode: an empty vector, a missing entry or an invalid/out-of-range value falls back to
// the FIRST extruder (0), matching the legacy behavior of the hardcoded-extruder-1 code paths.
//
// All slot->extruder resolution goes through this module so the remap stays in one place (see the
// extruder-1-hardcode audit): consumers ask physical_extruder_for_filament() instead of using the raw slot
// index. The mapping lives in libslic3r on purpose: it is a slicing-domain concept consumed by
// both the slicing pipeline (PrintApply.cpp, GCode.cpp) and the GUI (料箱耗材管理).

#include "PrintConfig.hpp"

#include <string>
#include <vector>

namespace Slic3r {

namespace FilamentNozzleMap {

extern const std::string OPTION_KEY;

size_t physical_extruder_count(const ConfigOptionResolver &config);

size_t filament_count(const ConfigOptionResolver &config);

bool is_active(const ConfigOptionResolver &config);

std::vector<int> map(const ConfigOptionResolver &config);

unsigned int physical_extruder_for_filament(const ConfigOptionResolver &config, unsigned int filament_id);
unsigned int physical_extruder_for_filament(const ConfigOptionResolver &project_config, const ConfigOptionResolver &printer_config, unsigned int filament_id);

std::vector<unsigned int> filaments_for_physical_extruder(const ConfigOptionResolver &config, unsigned int extruder_id);
std::vector<unsigned int> filaments_for_physical_extruder(const ConfigOptionResolver &project_config, const ConfigOptionResolver &printer_config, unsigned int extruder_id);

size_t filament_of_variant_position(const ConfigOptionResolver &config, size_t position);
std::vector<size_t> variant_positions_to_filaments(const ConfigOptionResolver &config, size_t positions);

unsigned int physical_extruder_for_variant_position(const ConfigOptionResolver &config, size_t position);

bool normalize(DynamicPrintConfig &config, size_t num_filaments, size_t num_extruders);

} // namespace FilamentNozzleMap

class FilamentNozzleMapManager
{
public:
    FilamentNozzleMapManager(DynamicPrintConfig &project_config, const ConfigBase &printer_config)
        : m_config(&project_config), m_printer_config(&printer_config) {}

    bool                      is_active() const;
    bool                      is_identity() const;
    std::vector<int>          map() const;
    unsigned int              physical_extruder_of(unsigned int filament_id) const;
    std::vector<unsigned int> filaments_of(unsigned int extruder_id) const;

    // --- mutations ---
    bool set_filament_physical_extruder(unsigned int filament_id, unsigned int extruder_id);
    void set_map(std::vector<int> map);
    void clear();

    // --- count-change sync hooks ---
    bool on_filaments_changed(size_t num_filaments);
    bool on_physical_extruders_changed(size_t num_extruders);

private:
    ConfigOptionInts       *map_option(bool create);
    const ConfigOptionInts *map_option() const;

    DynamicPrintConfig *m_config;
    const ConfigBase   *m_printer_config;
};

} // namespace Slic3r
