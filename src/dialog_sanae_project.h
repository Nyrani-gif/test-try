// Copyright (c) 2026, Aegisub Sanae contributors

#pragma once

#include <string>

namespace agi { struct Context; }
class wxWindow;

void ShowSanaeProjectDialog(agi::Context *context);
std::string ShowSanaeCreateSeasonDialog(agi::Context *context, wxWindow *parent = nullptr);
std::string ShowSanaeCreateProjectDialog(agi::Context *context, wxWindow *parent = nullptr,
	std::string const& preferred_season_id = {});
void ShowSanaeAddEpisodeDialog(agi::Context *context, wxWindow *parent = nullptr);
void ShowSanaeBatchImportForActiveProject(agi::Context *context, wxWindow *parent = nullptr);
