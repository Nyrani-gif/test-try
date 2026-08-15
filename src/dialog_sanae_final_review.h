// Copyright (c) 2026, Aegisub Sanae contributors

#pragma once

#include <string>

class wxWindow;
namespace agi { struct Context; }

/// Returns true only after a successful server Finalize.
bool ShowSanaeFinalReview(agi::Context *context, wxWindow *parent = nullptr);
bool ShowSanaeTerminologyEntryDialog(agi::Context *context, wxWindow *parent,
	std::string english, std::string russian);
