#include "FilamentNozzleMapDialog.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Plater.hpp"
#include "PresetBundle.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/StateColor.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>


namespace Slic3r { namespace GUI {

static constexpr int SWATCH_SIZE  = 16;
static constexpr int ROW_HEIGHT   = 30;
static constexpr int DIALOG_WIDTH = 420;

FilamentNozzleMapDialog::FilamentNozzleMapDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, _L("Filament-Nozzle Mapping"), wxDefaultPosition, wxDefaultSize,
                wxDEFAULT_DIALOG_STYLE)
{
    PresetBundle* pb = wxGetApp().preset_bundle;

    if (const auto* colours = pb->project_config.option<ConfigOptionStrings>("filament_colour"))
        m_filament_colours = colours->values;
    if (m_filament_colours.empty())
        m_filament_colours.emplace_back("#FFFFFF");

    for (size_t i = 0; i < m_filament_colours.size(); ++i) {
        wxString name = wxString::Format(_L("Filament %d"), int(i + 1));
        if (i < pb->filament_presets.size()) {
            const Preset* preset = pb->filaments.find_preset(pb->filament_presets[i]);
            if (preset != nullptr)
                name = from_u8(preset->name);
        }
        m_filament_names.push_back(name);
    }

    if (const auto* nozzles = pb->printers.get_edited_preset().config.option<ConfigOptionFloats>("nozzle_diameter"))
        m_nozzle_diameters = nozzles->values;
    if (m_nozzle_diameters.empty())
        m_nozzle_diameters.emplace_back(0.4);

    build_ui();
}

void FilamentNozzleMapDialog::build_ui()
{
    SetBackgroundColour(StateColor::darkModeColorFor(wxColour("#F8F7F7")));

    auto*       top_sizer = new wxBoxSizer(wxVERTICAL);
    const int   M         = FromDIP(12);
    const int   width     = FromDIP(DIALOG_WIDTH);
    const auto& map       = FilamentNozzleMap::map(wxGetApp().preset_bundle->project_config);

    auto* intro = new wxStaticText(this, wxID_ANY,
                                   _L("Assign each filament slot to the physical extruder (nozzle) that feeds it."));
    intro->Wrap(width);
    top_sizer->Add(intro, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, M);

    // Master switch: off = no mapping (filament_nozzle_map stays empty and every slot resolves to
    // extruder 1); on = the per-slot combos below become editable.
    auto* toggle_row = new wxBoxSizer(wxHORIZONTAL);
    m_use_map_checkbox = new CheckBox(this, wxID_ANY);
    auto* toggle_label = new wxStaticText(this, wxID_ANY, _L("Use filament-nozzle mapping"));
    toggle_row->Add(m_use_map_checkbox, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    toggle_row->Add(toggle_label, 0, wxALIGN_CENTER_VERTICAL);
    top_sizer->Add(toggle_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, M);
    m_use_map_checkbox->SetValue(FilamentNozzleMap::is_active(wxGetApp().preset_bundle->project_config));
    // Skip() lets CheckBox's own constructor-bound handler run update() and swap the checked bitmap.
    m_use_map_checkbox->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& e) { on_use_map_toggled(); e.Skip(); });

    const size_t num_filaments = m_filament_colours.size();
    const size_t num_extruders = m_nozzle_diameters.size();

    for (size_t i = 0; i < num_filaments; ++i) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);

        auto* swatch = new wxPanel(this, wxID_ANY, wxDefaultPosition,
                                   wxSize(FromDIP(SWATCH_SIZE), FromDIP(SWATCH_SIZE)));
        const wxColour colour(from_u8(m_filament_colours[i]));
        swatch->SetBackgroundColour(colour.IsOk() ? colour : *wxWHITE);
        row->Add(swatch, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

        auto* name = new wxStaticText(this, wxID_ANY, m_filament_names[i]);
        row->Add(name, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

        auto* combo = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                   wxSize(FromDIP(170), FromDIP(ROW_HEIGHT)), 0, nullptr, wxCB_READONLY);
        for (size_t e = 0; e < num_extruders; ++e)
            combo->Append(wxString::Format(_L("Extruder %d (%.1f)"), int(e + 1), m_nozzle_diameters[e]));
        int selection = 0; // no stored mapping: fall back to the first extruder
        if (i < map.size() && map[i] >= 0 && size_t(map[i]) < num_extruders)
            selection = map[i];
        combo->SetSelection(selection);
        combo->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { update_shared_extruder_warning(); });
        m_extruder_combos.push_back(combo);
        row->Add(combo, 0, wxALIGN_CENTER_VERTICAL);

        top_sizer->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, M);
    }

    m_shared_warning = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_shared_warning->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#FF6F00")));
    m_shared_warning->Wrap(width);
    m_shared_warning->Hide();
    top_sizer->Add(m_shared_warning, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, M);
    apply_use_map_state();

    // OK / Cancel (styling follows MixedFilamentDialog).
    auto* btn_panel = new wxPanel(this, wxID_ANY);
    btn_panel->SetBackgroundColour(StateColor::darkModeColorFor(wxColour("#FFFFFF")));
    auto* panel_sizer = new wxBoxSizer(wxVERTICAL);
    auto* top_line = new wxPanel(btn_panel, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    top_line->SetBackgroundColour(StateColor::darkModeColorFor(wxColour("#F0F0F0")));
    panel_sizer->Add(top_line, 0, wxEXPAND);
    panel_sizer->AddSpacer(FromDIP(12));

    auto* btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto* btn_cancel = new Button(btn_panel, _L("Cancel"));
    btn_cancel->SetMinSize(wxSize(-1, FromDIP(38)));
    btn_cancel->SetCornerRadius(FromDIP(4));
    btn_cancel->SetBorderWidth(FromDIP(1));
    btn_cancel->SetBorderColorNormal(wxColour("#D1D5DC"));
    btn_cancel->SetBackgroundColorNormal(wxColour("#FFFFFF"));
    btn_cancel->SetTextColorNormal(wxColour("#242424"));

    auto* btn_confirm = new Button(btn_panel, _L("OK"));
    btn_confirm->SetMinSize(wxSize(-1, FromDIP(38)));
    btn_confirm->SetCornerRadius(FromDIP(4));
    btn_confirm->SetBorderWidth(0);
    btn_confirm->SetBackgroundColor(StateColor(
        std::pair(wxColour("#DFDFDF"), (int) StateColor::Disabled),
        std::pair(wxColour("#019687"), (int) StateColor::Normal)));
    btn_confirm->SetTextColor(StateColor(
        std::pair(wxColour("#6B6A6A"), (int) StateColor::Disabled),
        std::pair(wxColour("#FEFEFE"), (int) StateColor::Normal)));

    btn_sizer->Add(btn_cancel, 1, wxRIGHT, FromDIP(8));
    btn_sizer->Add(btn_confirm, 1);
    panel_sizer->Add(btn_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));
    panel_sizer->AddSpacer(FromDIP(12));
    btn_panel->SetSizer(panel_sizer);
    top_sizer->Add(btn_panel, 0, wxEXPAND | wxTOP, FromDIP(16));

    btn_cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });
    btn_confirm->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        PresetBundle* pb = wxGetApp().preset_bundle;

        // Single source of truth: write the config option through the manager. The printer
        // preset supplies the extruder count (nozzle_diameter is not in the project config).
        FilamentNozzleMapManager manager(pb->project_config, pb->printers.get_edited_preset().config);
        if (m_use_map_checkbox->GetValue()) {
            std::vector<int> new_map;
            new_map.reserve(m_extruder_combos.size());
            for (const ComboBox* combo : m_extruder_combos)
                new_map.push_back(combo->GetSelection());
            manager.set_map(std::move(new_map));
            // No automatic mode: the explicitly chosen map is stored as-is (an all-zero map behaves
            // the same as an empty one; re-enable below to collapse it back to the empty vector).
            //if (manager.is_identity())
            //    manager.clear();
        } else {
            // Switch off: clear the stored map so every slot falls back to extruder 1.
            manager.clear();
        }

        // Standard config-change pipeline: applies the diff, reslices and marks the project dirty.
        wxGetApp().plater()->on_config_change(pb->full_config());
        EndModal(wxID_OK);
    });
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { EndModal(wxID_CANCEL); });

    SetSizer(top_sizer);
    Fit();
}

void FilamentNozzleMapDialog::on_use_map_toggled()
{
    // Stash the current selections before the off-state forces every row to extruder 1, so
    // flipping the switch back on restores what the user had picked.
    if (!m_use_map_checkbox->GetValue()) {
        m_selections_backup.clear();
        for (const ComboBox* combo : m_extruder_combos)
            m_selections_backup.push_back(combo->GetSelection());
    }
    apply_use_map_state();
}

void FilamentNozzleMapDialog::apply_use_map_state()
{
    const bool use_map = m_use_map_checkbox->GetValue();
    for (size_t i = 0; i < m_extruder_combos.size(); ++i) {
        ComboBox* combo = m_extruder_combos[i];
        if (use_map) {
            combo->Enable();
            if (i < m_selections_backup.size() && m_selections_backup[i] >= 0 &&
                m_selections_backup[i] < int(combo->GetCount()))
                combo->SetSelection(m_selections_backup[i]);
        } else {
            // Mapping off: every slot shows (and resolves to) the first extruder.
            combo->SetSelection(0);
            combo->Disable();
        }
    }
    update_shared_extruder_warning();
}

void FilamentNozzleMapDialog::update_shared_extruder_warning()
{
    bool shared = false;
    if (m_use_map_checkbox == nullptr || m_use_map_checkbox->GetValue()) {
        std::vector<int> per_extruder(m_nozzle_diameters.size(), 0);
        for (const ComboBox* combo : m_extruder_combos) {
            const int selection = combo->GetSelection();
            if (selection >= 0 && size_t(selection) < per_extruder.size() && ++per_extruder[selection] > 1)
                shared = true;
        }
    }
    // With the mapping off every slot sits on extruder 1 by design -- that is not a conflict.

    if (shared) {
        m_shared_warning->SetLabel(_L("Multiple filaments share an extruder. Enable \"Setting Overrides\" for the parameters that differ between them."));
        m_shared_warning->Wrap(FromDIP(DIALOG_WIDTH)); // Wrap() must be redone after SetLabel().
    }
    m_shared_warning->Show(shared);
    // Refit so the buttons stay inside the window when the warning line appears or disappears.
    Layout();
    Fit();
}

void FilamentNozzleMapDialog::on_dpi_changed(const wxRect& /*suggested_rect*/)
{
    // All sizes are FromDIP-based; re-layout on a DPI change.
    Layout();
    Refresh();
}

}} // namespace Slic3r::GUI
