// sanae_local_project_config.cpp — implementation
// Phase 3 of SANAE_REVAMP_PLAN.md §5.8

#include "sanae_local_project_config.h"

#include <libaegisub/fs.h>
#include <libaegisub/io.h>
#include <libaegisub/json.h>
#include <libaegisub/path.h>
#include <libaegisub/util.h>

#include <fstream>
#include <sstream>

namespace sanae {

SanaeLocalProjectConfig::SanaeLocalProjectConfig() {
    profile_.ApplyPreset(QCProfilePreset::TeamStandard);
}

void SanaeLocalProjectConfig::Load(const std::string& project_id) {
    if (project_id.empty()) return;

    auto base = agi::Path().User / "sanae" / "local-config";
    agi::fs::CreateDirectory(base);
    auto path = base / (project_id + ".json");

    if (!agi::fs::Exists(path)) {
        profile_.ApplyPreset(QCProfilePreset::TeamStandard);
        return;
    }

    try {
        auto content = agi::io::Open(path)->Get();
        std::string line;
        std::string json_text;
        while (std::getline(content, line))
            json_text += line;

        // Minimal JSON parsing for QCProfile preset.
        // Full JSON parsing would use libaegisub::cajun; for simplicity,
        // we parse just the preset field.
        if (json_text.find("\"preset\":") != std::string::npos) {
            if (json_text.find("\"strict\"") != std::string::npos)
                profile_.ApplyPreset(QCProfilePreset::StrictQC);
            else if (json_text.find("\"minimal\"") != std::string::npos)
                profile_.ApplyPreset(QCProfilePreset::MinimalQC);
            else if (json_text.find("\"custom\"") != std::string::npos)
                profile_.ApplyPreset(QCProfilePreset::Custom);
            else
                profile_.ApplyPreset(QCProfilePreset::TeamStandard);
        }
    }
    catch (...) {
        profile_.ApplyPreset(QCProfilePreset::TeamStandard);
    }
}

void SanaeLocalProjectConfig::Save(const std::string& project_id) const {
    if (project_id.empty()) return;

    auto base = agi::Path().User / "sanae" / "local-config";
    agi::fs::CreateDirectory(base);
    auto path = base / (project_id + ".json");

    std::string preset_str = "team_standard";
    switch (profile_.preset) {
        case QCProfilePreset::TeamStandard: preset_str = "team_standard"; break;
        case QCProfilePreset::StrictQC:     preset_str = "strict"; break;
        case QCProfilePreset::MinimalQC:    preset_str = "minimal"; break;
        case QCProfilePreset::Custom:       preset_str = "custom"; break;
    }

    std::ostringstream ss;
    ss << "{\n  \"preset\": \"" << preset_str << "\",\n"
       << "  \"cps_error_threshold\": " << profile_.cps_error_threshold << ",\n"
       << "  \"cps_warning_threshold\": " << profile_.cps_warning_threshold << ",\n"
       << "  \"max_line_length\": " << profile_.max_line_length << "\n"
       << "}\n";

    auto out = agi::io::Save(path, true);
    out.Get().write(ss.str().data(), static_cast<std::streamsize>(ss.str().size()));
}

} // namespace sanae
