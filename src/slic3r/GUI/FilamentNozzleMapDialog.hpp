#pragma once

// Snapmaker: 料箱耗材管理 (Filament-Nozzle Mapping) dialog.
//
// Assigns each filament slot to a physical extruder (nozzle). The mapping is written to the
// project config (filament_nozzle_map) through FilamentNozzleMapManager, so it follows the
// project save / load lifecycle; submitting a change goes through the standard config-change
// pipeline, which reslices and marks the project dirty.

#include "GUI_Utils.hpp"
#include "libslic3r/FilamentNozzleMap.hpp"

#include <string>
#include <vector>

class wxStaticText;
class CheckBox;
class ComboBox;

namespace Slic3r { namespace GUI {

class FilamentNozzleMapDialog : public DPIDialog
{
public:
    FilamentNozzleMapDialog(wxWindow* parent);

    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    void build_ui();
    void on_use_map_toggled();
    void apply_use_map_state();
    void update_shared_extruder_warning();

    // Inputs captured at construction (row i = filament slot i).
    std::vector<std::string> m_filament_colours; // project_config filament_colour
    std::vector<wxString>    m_filament_names;   // filament preset name per slot
    std::vector<double>      m_nozzle_diameters; // one entry per physical extruder

    std::vector<ComboBox*>   m_extruder_combos;  // per slot; selection = 0-based extruder id
    CheckBox*                m_use_map_checkbox  = nullptr; // off = no mapping, everything on extruder 1
    std::vector<int>         m_selections_backup;          // per-slot selections stashed while the switch is off
    wxStaticText*            m_shared_warning    = nullptr;
};

}} // namespace Slic3r::GUI
