// Copyright (c) 2026, Aegisub Sanae contributors
//
// Tests for SanaeReviewIssue state machine.
// Verifies SANAE_REVAMP_PLAN.md §3.5 + server req §3.

#include <main.h>

#include "../../src/sanae_review_issue.h"

using namespace sanae;

TEST(sanae_review_issue, initial_state_is_open) {
    SanaeReviewIssue issue;
    EXPECT_EQ(ReviewIssueState::Open, issue.state);
    EXPECT_TRUE(issue.IsOpen());
    EXPECT_EQ(1, issue.version);
}

TEST(sanae_review_issue, open_to_ready_for_review) {
    SanaeReviewIssue issue;
    auto r = issue.ApplyTransition(ReviewIssueState::ReadyForReview, "2026-01-01T00:00:00Z", "dev1");
    EXPECT_EQ(TransitionResult::Ok, r);
    EXPECT_EQ(ReviewIssueState::ReadyForReview, issue.state);
    EXPECT_EQ(2, issue.version);
}

TEST(sanae_review_issue, open_to_resolved) {
    SanaeReviewIssue issue;
    auto r = issue.ApplyTransition(ReviewIssueState::Resolved, "2026-01-01T00:00:00Z", "dev1");
    EXPECT_EQ(TransitionResult::Ok, r);
    EXPECT_TRUE(issue.IsResolved());
    EXPECT_EQ("2026-01-01T00:00:00Z", issue.resolved_at);
    EXPECT_EQ("dev1", issue.resolved_by_device_id);
}

TEST(sanae_review_issue, open_to_wont_fix_requires_resolution_note) {
    SanaeReviewIssue issue;
    auto r = issue.ApplyTransition(ReviewIssueState::WontFix, "2026-01-01T00:00:00Z", "dev1", "");
    EXPECT_EQ(TransitionResult::MissingResolutionNote, r);
    EXPECT_EQ(ReviewIssueState::Open, issue.state);  // unchanged
    EXPECT_EQ(1, issue.version);  // unchanged
}

TEST(sanae_review_issue, open_to_wont_fix_with_note) {
    SanaeReviewIssue issue;
    auto r = issue.ApplyTransition(ReviewIssueState::WontFix, "2026-01-01T00:00:00Z", "dev1",
                                    "Not a real issue");
    EXPECT_EQ(TransitionResult::Ok, r);
    EXPECT_TRUE(issue.IsWontFix());
    EXPECT_EQ("Not a real issue", issue.resolution_note);
}

TEST(sanae_review_issue, reopen_from_resolved_clears_resolved_fields) {
    SanaeReviewIssue issue;
    issue.ApplyTransition(ReviewIssueState::Resolved, "2026-01-01T00:00:00Z", "dev1");
    EXPECT_FALSE(issue.resolved_at.empty());

    auto r = issue.ApplyTransition(ReviewIssueState::Open, "2026-01-02T00:00:00Z", "dev2");
    EXPECT_EQ(TransitionResult::Ok, r);
    EXPECT_TRUE(issue.resolved_at.empty());
    EXPECT_TRUE(issue.resolved_by_device_id.empty());
}

TEST(sanae_review_issue, reopen_from_wont_fix_clears_resolution_note) {
    SanaeReviewIssue issue;
    issue.ApplyTransition(ReviewIssueState::WontFix, "2026-01-01T00:00:00Z", "dev1",
                           "Not a real issue");
    EXPECT_FALSE(issue.resolution_note.empty());

    auto r = issue.ApplyTransition(ReviewIssueState::Open, "2026-01-02T00:00:00Z", "dev2");
    EXPECT_EQ(TransitionResult::Ok, r);
    EXPECT_TRUE(issue.resolution_note.empty());
}

TEST(sanae_review_issue, resolved_to_ready_for_review_forbidden) {
    SanaeReviewIssue issue;
    issue.ApplyTransition(ReviewIssueState::Resolved, "2026-01-01T00:00:00Z", "dev1");
    auto r = issue.ApplyTransition(ReviewIssueState::ReadyForReview, "2026-01-02T00:00:00Z", "dev2");
    EXPECT_EQ(TransitionResult::InvalidTransition, r);
}

TEST(sanae_review_issue, resolved_to_wont_fix_forbidden) {
    SanaeReviewIssue issue;
    issue.ApplyTransition(ReviewIssueState::Resolved, "2026-01-01T00:00:00Z", "dev1");
    auto r = issue.ApplyTransition(ReviewIssueState::WontFix, "2026-01-02T00:00:00Z", "dev2", "note");
    EXPECT_EQ(TransitionResult::InvalidTransition, r);
}

TEST(sanae_review_issue, wont_fix_to_resolved_forbidden) {
    SanaeReviewIssue issue;
    issue.ApplyTransition(ReviewIssueState::WontFix, "2026-01-01T00:00:00Z", "dev1", "note");
    auto r = issue.ApplyTransition(ReviewIssueState::Resolved, "2026-01-02T00:00:00Z", "dev2");
    EXPECT_EQ(TransitionResult::InvalidTransition, r);
}

TEST(sanae_review_issue, wont_fix_to_ready_for_review_forbidden) {
    SanaeReviewIssue issue;
    issue.ApplyTransition(ReviewIssueState::WontFix, "2026-01-01T00:00:00Z", "dev1", "note");
    auto r = issue.ApplyTransition(ReviewIssueState::ReadyForReview, "2026-01-02T00:00:00Z", "dev2");
    EXPECT_EQ(TransitionResult::InvalidTransition, r);
}

TEST(sanae_review_issue, resolution_note_not_allowed_in_non_wont_fix) {
    SanaeReviewIssue issue;
    // Trying to set resolution_note when transitioning to Resolved should
    // be ignored (auto-cleared). The transition itself succeeds.
    auto r = issue.ApplyTransition(ReviewIssueState::Resolved, "2026-01-01T00:00:00Z", "dev1",
                                    "should be cleared");
    EXPECT_EQ(TransitionResult::Ok, r);
    EXPECT_TRUE(issue.resolution_note.empty());
}

TEST(sanae_review_issue, immutable_baseline_hash_change_rejected) {
    SanaeReviewIssue issue;
    issue.baseline_text_hash = "abc123";
    auto r = issue.ApplyTransition(ReviewIssueState::Resolved, "2026-01-01T00:00:00Z", "dev1",
                                    "", "", "different_hash", "");
    EXPECT_EQ(TransitionResult::ImmutableFieldChanged, r);
}

TEST(sanae_review_issue, same_baseline_hash_accepted) {
    SanaeReviewIssue issue;
    issue.baseline_text_hash = "abc123";
    auto r = issue.ApplyTransition(ReviewIssueState::Resolved, "2026-01-01T00:00:00Z", "dev1",
                                    "", "", "abc123", "");
    EXPECT_EQ(TransitionResult::Ok, r);
}

TEST(sanae_review_issue, body_edit_alongside_transition) {
    SanaeReviewIssue issue;
    issue.body = "old body";
    auto r = issue.ApplyTransition(ReviewIssueState::Resolved, "2026-01-01T00:00:00Z", "dev1",
                                    "", "new body");
    EXPECT_EQ(TransitionResult::Ok, r);
    EXPECT_EQ("new body", issue.body);
}

TEST(sanae_review_issue, version_increments_on_each_transition) {
    SanaeReviewIssue issue;
    EXPECT_EQ(1, issue.version);
    issue.ApplyTransition(ReviewIssueState::ReadyForReview, "t1", "d1");
    EXPECT_EQ(2, issue.version);
    issue.ApplyTransition(ReviewIssueState::Resolved, "t2", "d1");
    EXPECT_EQ(3, issue.version);
    issue.ApplyTransition(ReviewIssueState::Open, "t3", "d2");  // Reopen
    EXPECT_EQ(4, issue.version);
}

TEST(sanae_review_issue, soft_delete_increments_version) {
    SanaeReviewIssue issue;
    EXPECT_EQ(1, issue.version);
    bool ok = issue.SoftDelete("2026-01-01T00:00:00Z");
    EXPECT_TRUE(ok);
    EXPECT_EQ(2, issue.version);
    EXPECT_FALSE(issue.deleted_at.empty());
    EXPECT_TRUE(issue.IsDeleted());
}

TEST(sanae_review_issue, double_soft_delete_fails) {
    SanaeReviewIssue issue;
    issue.SoftDelete("2026-01-01T00:00:00Z");
    bool ok = issue.SoftDelete("2026-01-02T00:00:00Z");
    EXPECT_FALSE(ok);
}

TEST(sanae_review_issue, add_comment_to_deleted_fails) {
    SanaeReviewIssue issue;
    issue.SoftDelete("2026-01-01T00:00:00Z");
    SanaeComment c{"c1", issue.id, "test", "dev1", "now"};
    bool ok = issue.AddComment(c);
    EXPECT_FALSE(ok);
}

TEST(sanae_review_issue, add_empty_comment_fails) {
    SanaeReviewIssue issue;
    SanaeComment c{"c1", issue.id, "", "dev1", "now"};
    bool ok = issue.AddComment(c);
    EXPECT_FALSE(ok);
}

TEST(sanae_review_issue, add_comment_succeeds) {
    SanaeReviewIssue issue;
    SanaeComment c{"c1", issue.id, "test comment", "dev1", "now"};
    bool ok = issue.AddComment(c);
    EXPECT_TRUE(ok);
    EXPECT_EQ(1u, issue.comments.size());
}

TEST(sanae_review_issue, blocks_episode_done) {
    SanaeReviewIssue issue;
    EXPECT_TRUE(issue.BlocksEpisodeDone());  // Open

    issue.ApplyTransition(ReviewIssueState::ReadyForReview, "t", "d");
    EXPECT_TRUE(issue.BlocksEpisodeDone());  // ReadyForReview

    issue.ApplyTransition(ReviewIssueState::Resolved, "t", "d");
    EXPECT_FALSE(issue.BlocksEpisodeDone());  // Resolved

    issue.ApplyTransition(ReviewIssueState::Open, "t", "d");  // Reopen
    issue.ApplyTransition(ReviewIssueState::WontFix, "t", "d", "note");
    EXPECT_FALSE(issue.BlocksEpisodeDone());  // WontFix
}

TEST(sanae_review_issue, deleted_does_not_block) {
    SanaeReviewIssue issue;
    issue.SoftDelete("now");
    EXPECT_FALSE(issue.BlocksEpisodeDone());
}

TEST(sanae_review_issue, state_string_roundtrip) {
    EXPECT_EQ("open", SanaeReviewIssue::StateToString(ReviewIssueState::Open));
    EXPECT_EQ("ready_for_review", SanaeReviewIssue::StateToString(ReviewIssueState::ReadyForReview));
    EXPECT_EQ("resolved", SanaeReviewIssue::StateToString(ReviewIssueState::Resolved));
    EXPECT_EQ("wont_fix", SanaeReviewIssue::StateToString(ReviewIssueState::WontFix));

    EXPECT_EQ(ReviewIssueState::Open, SanaeReviewIssue::StateFromString("open"));
    EXPECT_EQ(ReviewIssueState::ReadyForReview, SanaeReviewIssue::StateFromString("ready_for_review"));
    EXPECT_EQ(ReviewIssueState::Resolved, SanaeReviewIssue::StateFromString("resolved"));
    EXPECT_EQ(ReviewIssueState::WontFix, SanaeReviewIssue::StateFromString("wont_fix"));
}
