// Copyright (c) 2026, Aegisub Sanae contributors

#pragma once

#include <cstddef>

class AssFile;

struct SanaeCompactStats {
	std::size_t input_events = 0;
	std::size_t output_events = 0;
	std::size_t drawings_removed = 0;
	std::size_t comments_removed = 0;
	std::size_t empty_events_removed = 0;
	std::size_t technical_duplicates_collapsed = 0;
};

/// Build the server-safe compact RUSUB without mutating the production ASS.
AssFile BuildSanaeCompactRusub(AssFile const& source, SanaeCompactStats *stats = nullptr);
