// Copyright (c) 2026, Aegisub Sanae contributors

#pragma once

#include <string>

namespace agi { struct Context; }

void ShowSanaeBatchImportDialog(agi::Context *context,
	std::string const& project_id, std::string const& project_name);

