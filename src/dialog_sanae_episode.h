// Copyright (c) 2026, Aegisub Sanae contributors

#pragma once

#include <libaegisub/fs.h>

#include <string>

class wxWindow;
namespace agi { struct Context; }

enum class SanaeEpisodeDialogResult { None, Changed, Deleted };

SanaeEpisodeDialogResult ShowSanaeEpisodeDetailsDialog(agi::Context *context,
	std::string const& episode_id, wxWindow *parent = nullptr);

bool ShowSanaeReplaceEpisodeSourceDialog(agi::Context *context,
	std::string const& episode_id, wxWindow *parent = nullptr,
	agi::fs::path const& selected_path = {});

bool ConfirmAndDeleteSanaeEpisode(agi::Context *context,
	std::string const& episode_id, wxWindow *parent = nullptr);
