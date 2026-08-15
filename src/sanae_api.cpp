// Copyright (c) 2026, Aegisub Sanae contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "sanae_api.h"

#include <libaegisub/cajun/elements.h>
#include <libaegisub/cajun/reader.h>

#include <algorithm>
#include <cctype>
#include <curl/curl.h>
#include <memory>
#include <sstream>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include <wincred.h>
#endif

namespace {
constexpr char credential_target[] = "Aegisub Sanae/Device Token";

size_t append_body(char *data, size_t size, size_t count, void *target) {
	auto bytes = size * count;
	static_cast<std::string *>(target)->append(data, bytes);
	return bytes;
}

std::string trim(std::string value) {
	auto not_space = [](unsigned char c) { return !std::isspace(c); };
	value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
	value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
	return value;
}

size_t inspect_header(char *data, size_t size, size_t count, void *target) {
	auto bytes = size * count;
	std::string_view line(data, bytes);
	if (line.size() >= 5) {
		std::string name(line.substr(0, std::min<size_t>(5, line.size())));
		std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		if (name == "etag:")
			*static_cast<std::string *>(target) = trim(std::string(line.substr(5)));
	}
	return bytes;
}

std::pair<std::string, std::string> parse_error(std::string const& body) {
	try {
		std::istringstream input(body);
		json::UnknownElement root;
		json::Reader::Read(root, input);
		auto const& object = static_cast<json::Object const&>(root);
		auto envelope = object.find("error");
		if (envelope == object.end()) return {};
		auto const& error = static_cast<json::Object const&>(envelope->second);
		auto code = error.find("code");
		auto message = error.find("message");
		return {
			code == error.end() ? std::string() : static_cast<std::string>(code->second),
			message == error.end() ? std::string() : static_cast<std::string>(message->second)};
	}
	catch (...) {
		return {};
	}
}

#ifdef _WIN32
std::wstring widen(std::string const& value) {
	if (value.empty()) return {};
	int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
		static_cast<int>(value.size()), nullptr, 0);
	if (length <= 0) throw std::runtime_error("Invalid UTF-8 credential target");
	std::wstring result(static_cast<size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
		static_cast<int>(value.size()), result.data(), length);
	return result;
}
#endif
}

SanaeApiError::SanaeApiError(long status, std::string code, std::string message)
: std::runtime_error(message.empty() ? "Sanae Server request failed" : std::move(message))
, http_status(status)
, error_code(std::move(code))
{
}

SanaeApiClient::SanaeApiClient(std::string url)
: base_url(std::move(url))
{
	while (!base_url.empty() && base_url.back() == '/') base_url.pop_back();
	auto local_http = [&](std::string_view host) {
		std::string prefix = "http://" + std::string(host);
		if (base_url.rfind(prefix, 0) != 0) return false;
		return base_url.size() == prefix.size() || base_url[prefix.size()] == ':'
			|| base_url[prefix.size()] == '/';
	};
	if (base_url.rfind("https://", 0) != 0 && !local_http("127.0.0.1") && !local_http("localhost"))
		throw std::invalid_argument("Sanae Server URL must use HTTPS");
}

SanaeHttpResponse SanaeApiClient::Perform(char const *method, std::string const& path,
	std::string const& body, char const *content_type, bool authenticated,
	std::string const& idempotency_key, std::string const& if_none_match,
	std::string const& multipart_field, std::string const& multipart_filename,
	std::string const& multipart_data) const
{
	static const int curl_initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
	if (curl_initialized != CURLE_OK) throw std::runtime_error("Could not initialize libcurl");

	using CurlPtr = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
	CurlPtr curl(curl_easy_init(), &curl_easy_cleanup);
	if (!curl) throw std::runtime_error("Could not create Sanae HTTP request");

	SanaeHttpResponse response;
	std::string url = base_url + path;
	std::string token;
	if (authenticated) {
		token = ReadStoredDeviceToken();
		if (token.empty()) throw SanaeApiError(401, "authentication_required", "Sanae device is not enrolled");
	}

	struct curl_slist *raw_headers = nullptr;
	auto add_header = [&](std::string const& value) { raw_headers = curl_slist_append(raw_headers, value.c_str()); };
	if (content_type) add_header(std::string("Content-Type: ") + content_type);
	add_header("Accept: application/json");
	if (authenticated) add_header("Authorization: Bearer " + token);
	if (!idempotency_key.empty()) add_header("Idempotency-Key: " + idempotency_key);
	if (!if_none_match.empty()) add_header("If-None-Match: " + if_none_match);
	std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(raw_headers, &curl_slist_free_all);

	curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, method);
	curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
	curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, append_body);
	curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response.body);
	curl_easy_setopt(curl.get(), CURLOPT_HEADERFUNCTION, inspect_header);
	curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, &response.etag);
	curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "Aegisub-Sanae/0.2");
	curl_easy_setopt(curl.get(), CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 120L);
	curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 0L);
	curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);

	using MimePtr = std::unique_ptr<curl_mime, decltype(&curl_mime_free)>;
	MimePtr mime(nullptr, &curl_mime_free);
	if (!multipart_field.empty()) {
		mime.reset(curl_mime_init(curl.get()));
		if (!mime) throw std::runtime_error("Could not create multipart request");
		auto metadata = curl_mime_addpart(mime.get());
		if (!metadata) throw std::runtime_error("Could not create multipart metadata field");
		curl_mime_name(metadata, "metadata");
		curl_mime_type(metadata, "application/json; charset=utf-8");
		curl_mime_data(metadata, body.data(), body.size());

		auto file = curl_mime_addpart(mime.get());
		if (!file) throw std::runtime_error("Could not create multipart file field");
		curl_mime_name(file, multipart_field.c_str());
		curl_mime_filename(file, multipart_filename.c_str());
		curl_mime_type(file, "application/octet-stream");
		curl_mime_data(file, multipart_data.data(), multipart_data.size());
		curl_easy_setopt(curl.get(), CURLOPT_MIMEPOST, mime.get());
	}
	else if (std::string_view(method) != "GET") {
		curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.data());
		curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
	}

	auto result = curl_easy_perform(curl.get());
	if (result != CURLE_OK)
		throw std::runtime_error(std::string("Sanae Server connection failed: ") + curl_easy_strerror(result));
	curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response.status);
	if ((response.status < 200 || response.status >= 300) && response.status != 304) {
		auto [code, message] = parse_error(response.body);
		if (message.empty()) message = "Sanae Server returned HTTP " + std::to_string(response.status);
		throw SanaeApiError(response.status, std::move(code), std::move(message));
	}
	return response;
}

SanaeHttpResponse SanaeApiClient::Get(std::string const& path, bool authenticated,
	std::string const& if_none_match) const
{
	return Perform("GET", path, {}, nullptr, authenticated, {}, if_none_match, {}, {}, {});
}

SanaeHttpResponse SanaeApiClient::PostJson(std::string const& path, std::string const& json,
	bool authenticated, std::string const& idempotency_key) const
{
	return Perform("POST", path, json, "application/json; charset=utf-8", authenticated,
		idempotency_key, {}, {}, {}, {});
}

SanaeHttpResponse SanaeApiClient::PostMultipart(std::string const& path,
	std::string const& metadata_json, std::string const& file_field,
	std::string const& filename, std::string const& file_data,
	std::string const& idempotency_key) const
{
	return Perform("POST", path, metadata_json, nullptr, true, idempotency_key,
		{}, file_field, filename, file_data);
}

SanaeHttpResponse SanaeApiClient::Delete(std::string const& path,
	std::string const& idempotency_key) const
{
	if (idempotency_key.empty())
		throw std::invalid_argument("Sanae DELETE request requires an idempotency key");
	return Perform("DELETE", path, {}, nullptr, true, idempotency_key,
		{}, {}, {}, {});
}

bool SanaeApiClient::HasStoredDeviceToken() {
	return !ReadStoredDeviceToken().empty();
}

std::string SanaeApiClient::ReadStoredDeviceToken() {
#ifdef _WIN32
	PCREDENTIALW credential = nullptr;
	auto target = widen(credential_target);
	if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) return {};
	std::unique_ptr<CREDENTIALW, decltype(&CredFree)> holder(credential, &CredFree);
	if (!credential->CredentialBlob || !credential->CredentialBlobSize) return {};
	return std::string(reinterpret_cast<char const *>(credential->CredentialBlob),
		credential->CredentialBlobSize);
#else
	return {};
#endif
}

void SanaeApiClient::StoreDeviceToken(std::string const& token) {
	if (token.empty()) throw std::invalid_argument("Empty Sanae device token");
#ifdef _WIN32
	CREDENTIALW credential{};
	auto target = widen(credential_target);
	credential.Type = CRED_TYPE_GENERIC;
	credential.TargetName = target.data();
	credential.CredentialBlobSize = static_cast<DWORD>(token.size());
	credential.CredentialBlob = reinterpret_cast<BYTE *>(const_cast<char *>(token.data()));
	credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
	if (!CredWriteW(&credential, 0))
		throw std::runtime_error("Could not save Sanae device token in Windows Credential Manager");
#else
	throw std::runtime_error("Sanae device enrollment is supported on Windows only");
#endif
}

void SanaeApiClient::DeleteStoredDeviceToken() {
#ifdef _WIN32
	auto target = widen(credential_target);
	CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0);
#endif
}
