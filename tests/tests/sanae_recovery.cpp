#include "../../src/sanae_recovery.h"

#include <gtest/gtest.h>

TEST(SanaeRecovery, Sha256KnownVectors) {
	EXPECT_EQ("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
		SanaeSha256(""));
	EXPECT_EQ("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
		SanaeSha256("abc"));
	EXPECT_EQ("d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592",
		SanaeSha256("The quick brown fox jumps over the lazy dog"));
	EXPECT_EQ("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
		SanaeSha256(std::string(1000000, 'a')));
}

TEST(SanaeRecovery, PayloadShaSkipsIdenticalState) {
	EXPECT_EQ(SanaeRecoveryPayloadDecision::AlreadyStored,
		SanaeClassifyRecoveryPayload(100, "same", "same"));
	EXPECT_EQ(SanaeRecoveryPayloadDecision::Upload,
		SanaeClassifyRecoveryPayload(100, "new", "old"));
	EXPECT_EQ(SanaeRecoveryPayloadDecision::TooLarge,
		SanaeClassifyRecoveryPayload(SANAE_RECOVERY_MAX_BYTES + 1, "new", "old"));
}

TEST(SanaeRecovery, EditThenUndoSkipsByteEquivalentState) {
	auto original = SanaeSha256("full production ass");
	EXPECT_EQ(SanaeRecoveryPayloadDecision::AlreadyStored,
		SanaeClassifyRecoveryPayload(19, SanaeSha256("full production ass"), original));
}

TEST(SanaeRecovery, BaselineIsNewestCurrentDeviceAndSource) {
	std::vector<SanaeRecoverySnapshotInfo> snapshots{
		{.source_file_id = "source-a", .sha256 = "old", .created_at = "2026-08-15T01:00:00Z", .device_id = "device-a"},
		{.source_file_id = "source-a", .sha256 = "new", .created_at = "2026-08-15T02:00:00Z", .device_id = "device-a"},
		{.source_file_id = "source-b", .sha256 = "wrong-source", .created_at = "2026-08-15T03:00:00Z", .device_id = "device-a"},
		{.source_file_id = "source-a", .sha256 = "other-device", .created_at = "2026-08-15T04:00:00Z", .device_id = "device-b"}};
	EXPECT_EQ("new", SanaeNewestRecoveryBaseline(snapshots, "source-a", "device-a"));
	EXPECT_FALSE(SanaeNewestRecoveryBaseline(snapshots, "missing", "device-a"));
}

TEST(SanaeRecovery, CheckIntervalIsGenerationGate) {
	SanaeRecoveryState state;
	state.Bind({"episode", "source"});
	EXPECT_FALSE(state.BeginCheck(false));
	state.NoteDocumentChange();
	EXPECT_TRUE(state.BeginCheck(false));
	EXPECT_FALSE(state.BeginCheck(false));
	EXPECT_TRUE(state.BeginCheck(true));
}

TEST(SanaeRecovery, RetryKeepsImmutableRequestAndIdempotencyKey) {
	SanaeRecoveryState state;
	SanaeRecoveryBinding binding{"episode", "source"};
	state.Bind(binding);
	state.NoteDocumentChange();
	ASSERT_TRUE(state.BeginCheck(false));
	SanaeRecoveryUpload upload{binding, state.Generation(), "stable-key", "sha", "bytes"};
	state.StartUpload(upload);
	EXPECT_FALSE(state.FinishFailure(upload, true, false));
	auto retry = state.BeginRetry();
	ASSERT_TRUE(retry);
	EXPECT_EQ("stable-key", retry->idempotency_key);
	EXPECT_EQ("bytes", retry->full_ass);
	EXPECT_EQ(binding, retry->binding);
}

TEST(SanaeRecovery, UploadCoalescesTimerTicksToFreshestGeneration) {
	SanaeRecoveryState state;
	SanaeRecoveryBinding binding{"episode", "source"};
	state.Bind(binding);
	state.NoteDocumentChange();
	ASSERT_TRUE(state.BeginCheck(false));
	SanaeRecoveryUpload upload{binding, state.Generation(), "key", "sha-a", "state-a"};
	state.StartUpload(upload);
	state.NoteDocumentChange();
	state.NoteDocumentChange();
	EXPECT_FALSE(state.BeginCheck(false));
	EXPECT_TRUE(state.FinishSuccess(upload, "sha-a"));
	EXPECT_TRUE(state.BeginCheck(false));
}

TEST(SanaeRecovery, EpisodeSwitchNeverRebindsOldPayload) {
	SanaeRecoveryState state;
	SanaeRecoveryBinding old_binding{"episode-5", "source-1"};
	state.Bind(old_binding);
	state.NoteDocumentChange();
	ASSERT_TRUE(state.BeginCheck(false));
	SanaeRecoveryUpload old_upload{old_binding, state.Generation(), "key", "sha", "ep5"};
	state.StartUpload(old_upload);
	state.Bind({"episode-6", "source-2"});
	EXPECT_FALSE(state.FinishSuccess(old_upload, "sha"));
	EXPECT_EQ("episode-6", state.Binding().episode_id);
	EXPECT_TRUE(state.LastSuccessfulSha256().empty());
}

TEST(SanaeRecovery, SourceChangedPausesOnlyMatchingBinding) {
	SanaeRecoveryState state;
	SanaeRecoveryBinding binding{"episode", "source-1"};
	state.Bind(binding);
	state.NoteDocumentChange();
	ASSERT_TRUE(state.BeginCheck(false));
	SanaeRecoveryUpload upload{binding, state.Generation(), "key", "sha", "data"};
	state.StartUpload(upload);
	state.FinishFailure(upload, false, true);
	EXPECT_TRUE(state.IsPaused());
	EXPECT_FALSE(state.BeginCheck(true));
	state.Bind({"episode", "source-2"});
	EXPECT_FALSE(state.IsPaused());
	EXPECT_TRUE(state.BeginCheck(true));
}
