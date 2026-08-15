// Copyright (c) 2026, Aegisub Sanae contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#pragma once

#include <stdexcept>
#include <string>

struct SanaeHttpResponse {
	long status = 0;
	std::string body;
	std::string etag;
};

class SanaeApiError final : public std::runtime_error {
	long http_status = 0;
	std::string error_code;

public:
	SanaeApiError(long status, std::string code, std::string message);
	long Status() const { return http_status; }
	std::string const& Code() const { return error_code; }
};

/// Thin client for the authoritative Sanae Server v0.2 HTTP contract.
///
/// It deliberately returns raw JSON so the project model owns schema parsing
/// and cache merging. No request is issued from paint, playback or text-edit
/// callbacks.
class SanaeApiClient final {
	std::string base_url;

	SanaeHttpResponse Perform(char const *method, std::string const& path,
		std::string const& body, char const *content_type, bool authenticated,
		std::string const& idempotency_key, std::string const& if_none_match,
		std::string const& multipart_field, std::string const& multipart_filename,
		std::string const& multipart_data) const;

public:
	explicit SanaeApiClient(std::string base_url);

	SanaeHttpResponse Get(std::string const& path, bool authenticated = true,
		std::string const& if_none_match = {}) const;
	SanaeHttpResponse PostJson(std::string const& path, std::string const& json,
		bool authenticated = true, std::string const& idempotency_key = {}) const;
	SanaeHttpResponse PostMultipart(std::string const& path, std::string const& metadata_json,
		std::string const& file_field, std::string const& filename,
		std::string const& file_data, std::string const& idempotency_key) const;
	SanaeHttpResponse Delete(std::string const& path, std::string const& idempotency_key) const;

	static bool HasStoredDeviceToken();
	static std::string ReadStoredDeviceToken();
	static void StoreDeviceToken(std::string const& token);
	static void DeleteStoredDeviceToken();
};
