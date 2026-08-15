// Copyright (c) 2026, Aegisub Sanae contributors

#include "sanae_recovery.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <utility>

namespace {
constexpr std::array<std::uint32_t, 64> k{
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

constexpr std::uint32_t rotr(std::uint32_t value, int bits) {
	return (value >> bits) | (value << (32 - bits));
}

bool same_upload(SanaeRecoveryUpload const& left, SanaeRecoveryUpload const& right) {
	return left.binding == right.binding && left.idempotency_key == right.idempotency_key;
}
}

std::string SanaeSha256(std::string_view bytes) {
	std::vector<unsigned char> message(bytes.begin(), bytes.end());
	auto bit_length = static_cast<std::uint64_t>(message.size()) * 8u;
	message.push_back(0x80);
	while (message.size() % 64 != 56) message.push_back(0);
	for (int shift = 56; shift >= 0; shift -= 8)
		message.push_back(static_cast<unsigned char>((bit_length >> shift) & 0xff));

	std::array<std::uint32_t, 8> hash{
		0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
		0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	for (std::size_t offset = 0; offset < message.size(); offset += 64) {
		std::array<std::uint32_t, 64> words{};
		for (std::size_t i = 0; i < 16; ++i) {
			auto p = offset + i * 4;
			words[i] = (static_cast<std::uint32_t>(message[p]) << 24)
				| (static_cast<std::uint32_t>(message[p + 1]) << 16)
				| (static_cast<std::uint32_t>(message[p + 2]) << 8)
				| static_cast<std::uint32_t>(message[p + 3]);
		}
		for (std::size_t i = 16; i < words.size(); ++i) {
			auto s0 = rotr(words[i - 15], 7) ^ rotr(words[i - 15], 18) ^ (words[i - 15] >> 3);
			auto s1 = rotr(words[i - 2], 17) ^ rotr(words[i - 2], 19) ^ (words[i - 2] >> 10);
			words[i] = words[i - 16] + s0 + words[i - 7] + s1;
		}
		auto [a, b, c, d, e, f, g, h] = hash;
		for (std::size_t i = 0; i < words.size(); ++i) {
			auto s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
			auto choice = (e & f) ^ (~e & g);
			auto temp1 = h + s1 + choice + k[i] + words[i];
			auto s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
			auto majority = (a & b) ^ (a & c) ^ (b & c);
			auto temp2 = s0 + majority;
			h = g; g = f; f = e; e = d + temp1;
			d = c; c = b; b = a; a = temp1 + temp2;
		}
		hash[0] += a; hash[1] += b; hash[2] += c; hash[3] += d;
		hash[4] += e; hash[5] += f; hash[6] += g; hash[7] += h;
	}
	std::ostringstream output;
	output << std::hex << std::setfill('0');
	for (auto value : hash) output << std::setw(8) << value;
	return output.str();
}

std::string SanaeRecoveryBindingKey(SanaeRecoveryBinding const& binding) {
	return binding.episode_id + "\n" + binding.source_file_id;
}

SanaeRecoveryPayloadDecision SanaeClassifyRecoveryPayload(std::size_t size,
	std::string_view sha256, std::string_view last_successful_sha256)
{
	if (size > SANAE_RECOVERY_MAX_BYTES) return SanaeRecoveryPayloadDecision::TooLarge;
	if (!sha256.empty() && sha256 == last_successful_sha256)
		return SanaeRecoveryPayloadDecision::AlreadyStored;
	return SanaeRecoveryPayloadDecision::Upload;
}

std::optional<std::string> SanaeNewestRecoveryBaseline(
	std::vector<SanaeRecoverySnapshotInfo> const& snapshots,
	std::string_view source_file_id, std::string_view device_id)
{
	auto found = snapshots.end();
	for (auto it = snapshots.begin(); it != snapshots.end(); ++it) {
		if (it->source_file_id != source_file_id || it->device_id != device_id) continue;
		if (found == snapshots.end() || found->created_at < it->created_at) found = it;
	}
	if (found == snapshots.end() || found->sha256.empty()) return std::nullopt;
	return found->sha256;
}

void SanaeRecoveryState::Bind(SanaeRecoveryBinding value) {
	if (binding == value) return;
	binding = std::move(value);
	last_checked_generation = generation;
	last_successful_sha256.clear();
	retry.reset();
	paused_source_changed = false;
	pending_newer_state = in_flight.has_value();
}

void SanaeRecoveryState::ClearBinding() {
	Bind({});
}

void SanaeRecoveryState::NoteDocumentChange() {
	++generation;
	if (in_flight) pending_newer_state = true;
}

bool SanaeRecoveryState::BeginCheck(bool manual) {
	if (!binding.IsValid() || paused_source_changed) return false;
	if (in_flight) {
		if (manual || generation != last_checked_generation) pending_newer_state = true;
		return false;
	}
	if (retry) return false;
	if (!manual && generation == last_checked_generation) return false;
	last_checked_generation = generation;
	return true;
}

void SanaeRecoveryState::StartUpload(SanaeRecoveryUpload upload) {
	in_flight = std::move(upload);
}

std::optional<SanaeRecoveryUpload> SanaeRecoveryState::BeginRetry() {
	if (in_flight || !retry || retry->binding != binding || paused_source_changed)
		return std::nullopt;
	auto result = std::move(*retry);
	retry.reset();
	in_flight = SanaeRecoveryUpload{result.binding, result.generation,
		result.idempotency_key, result.sha256, {}};
	return result;
}

bool SanaeRecoveryState::FinishSuccess(SanaeRecoveryUpload const& upload,
	std::string response_sha256)
{
	if (!in_flight || !same_upload(*in_flight, upload)) return false;
	in_flight.reset();
	if (binding == upload.binding) {
		last_successful_sha256 = std::move(response_sha256);
		retry.reset();
	}
	bool newer = binding == upload.binding
		&& (pending_newer_state || generation != upload.generation);
	pending_newer_state = false;
	return newer;
}

bool SanaeRecoveryState::FinishFailure(SanaeRecoveryUpload upload,
	bool retryable, bool source_changed)
{
	if (!in_flight || !same_upload(*in_flight, upload)) return false;
	in_flight.reset();
	bool matching_binding = binding == upload.binding;
	auto upload_generation = upload.generation;
	if (matching_binding) {
		paused_source_changed = source_changed;
		if (retryable && !source_changed) retry = std::move(upload);
		else retry.reset();
	}
	bool newer = matching_binding && !retry
		&& !paused_source_changed && (pending_newer_state || generation != upload_generation);
	pending_newer_state = false;
	return newer;
}

void SanaeRecoveryState::FinishWithoutUpload(SanaeRecoveryUpload const& upload) {
	if (!in_flight || !same_upload(*in_flight, upload)) return;
	in_flight.reset();
	pending_newer_state = binding == upload.binding && generation != upload.generation;
}

void SanaeRecoveryState::SetBaseline(SanaeRecoveryBinding const& for_binding,
	std::string sha256)
{
	if (binding == for_binding) last_successful_sha256 = std::move(sha256);
}
