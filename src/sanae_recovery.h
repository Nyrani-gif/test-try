// Copyright (c) 2026, Aegisub Sanae contributors

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

constexpr std::size_t SANAE_RECOVERY_MAX_BYTES = 64u * 1024u * 1024u;

struct SanaeRecoveryBinding {
	std::string episode_id;
	std::string source_file_id;

	bool IsValid() const { return !episode_id.empty() && !source_file_id.empty(); }
	friend bool operator==(SanaeRecoveryBinding const&, SanaeRecoveryBinding const&) = default;
};

struct SanaeRecoverySnapshotInfo {
	std::string id;
	std::string project_id;
	std::string episode_id;
	std::string source_file_id;
	std::string created_by_device_id;
	std::string sha256;
	std::size_t size_bytes = 0;
	std::string created_at;
	std::string device_id;
	std::string device_display_name;
	std::string device_name;
};

/// An upload is intentionally self-contained. A background worker must never
/// consult the currently active Episode after this object has been created.
struct SanaeRecoveryUpload {
	SanaeRecoveryBinding binding;
	std::uint64_t generation = 0;
	std::string idempotency_key;
	std::string sha256;
	std::string full_ass;
};

enum class SanaeRecoveryPayloadDecision { Upload, AlreadyStored, TooLarge };

std::string SanaeSha256(std::string_view bytes);
std::string SanaeRecoveryBindingKey(SanaeRecoveryBinding const& binding);
SanaeRecoveryPayloadDecision SanaeClassifyRecoveryPayload(std::size_t size,
	std::string_view sha256, std::string_view last_successful_sha256);
std::optional<std::string> SanaeNewestRecoveryBaseline(
	std::vector<SanaeRecoverySnapshotInfo> const& snapshots,
	std::string_view source_file_id, std::string_view device_id);

/// Small UI-thread state machine. It coalesces timer ticks to the latest
/// document generation and retains an immutable failed request for an
/// idempotent retry.
class SanaeRecoveryState {
	SanaeRecoveryBinding binding;
	std::uint64_t generation = 0;
	std::uint64_t last_checked_generation = 0;
	std::optional<SanaeRecoveryUpload> in_flight;
	std::optional<SanaeRecoveryUpload> retry;
	std::string last_successful_sha256;
	bool pending_newer_state = false;
	bool paused_source_changed = false;

public:
	void Bind(SanaeRecoveryBinding value);
	void ClearBinding();
	void NoteDocumentChange();
	bool BeginCheck(bool manual);
	void StartUpload(SanaeRecoveryUpload upload);
	std::optional<SanaeRecoveryUpload> BeginRetry();
	bool FinishSuccess(SanaeRecoveryUpload const& upload, std::string response_sha256);
	bool FinishFailure(SanaeRecoveryUpload upload, bool retryable,
		bool source_changed);
	void FinishWithoutUpload(SanaeRecoveryUpload const& upload);
	void SetBaseline(SanaeRecoveryBinding const& for_binding, std::string sha256);
	void PauseForSourceChange() { paused_source_changed = true; retry.reset(); }

	SanaeRecoveryBinding const& Binding() const { return binding; }
	std::uint64_t Generation() const { return generation; }
	std::string const& LastSuccessfulSha256() const { return last_successful_sha256; }
	bool IsUploading() const { return in_flight.has_value(); }
	bool HasRetry() const { return retry.has_value(); }
	bool IsPaused() const { return paused_source_changed; }
};
