// Copyright (c) 2026, Aegisub Sanae contributors

#include <main.h>
#include <util.h>

#include "../../src/sanae_batch_import.h"

#include <libaegisub/fs.h>

TEST(sanae_batch_import, extracts_common_episode_codes) {
	EXPECT_EQ("03", SanaeBatchExtractEpisodeCode(agi::fs::path("Show.S02E03.1080p.ass")));
	EXPECT_EQ("03", SanaeBatchExtractEpisodeCode(agi::fs::path("Show 03.1080p.ass")));
	EXPECT_EQ("03", SanaeBatchExtractEpisodeCode(agi::fs::path("Show Episode 03.1080p.ass")));
	EXPECT_EQ("03", SanaeBatchExtractEpisodeCode(agi::fs::path("[Group 1] Show - 03v2 [1080p][ABC12345].ass")));
	EXPECT_EQ("03", SanaeBatchExtractEpisodeCode(agi::fs::path("Show - 03 [5.1].ass")));
	EXPECT_EQ("12", SanaeBatchExtractEpisodeCode(agi::fs::path("Show Episode 12 [RU].ass")));
	EXPECT_EQ("07", SanaeBatchExtractEpisodeCode(agi::fs::path("Show 2024 - 07 1080p.ass")));
	EXPECT_EQ("12.5", SanaeBatchExtractEpisodeCode(agi::fs::path("Show - 12.5 [EN].ass")));
	EXPECT_EQ("01", SanaeBatchExtractEpisodeCode(agi::fs::path("01.ass")));
	EXPECT_EQ("2", SanaeBatchExtractEpisodeCode(agi::fs::path("dara2.ass")));
	EXPECT_EQ("5", SanaeBatchExtractEpisodeCode(agi::fs::path("dara5.ass")));
	EXPECT_EQ("05", SanaeBatchExtractEpisodeCode(agi::fs::path("dara05.ass")));
	EXPECT_EQ("06", SanaeBatchExtractEpisodeCode(agi::fs::path("mao_06.ass")));
	EXPECT_EQ("07", SanaeBatchExtractEpisodeCode(agi::fs::path("episode-07.ass")));
	EXPECT_EQ("08", SanaeBatchExtractEpisodeCode(agi::fs::path("Anime.1080p.HEVC.08.ass")));
	EXPECT_EQ("OVA", SanaeBatchExtractEpisodeCode(agi::fs::path("OVA.ass")));
	EXPECT_TRUE(SanaeBatchExtractEpisodeCode(agi::fs::path("x264.ass")).empty());
	EXPECT_TRUE(SanaeBatchExtractEpisodeCode(agi::fs::path("Show NCOP.ass")).empty());
}

TEST(sanae_batch_import, canonicalizes_numeric_codes) {
	EXPECT_EQ(SanaeBatchCanonicalEpisodeCode("01"), SanaeBatchCanonicalEpisodeCode("1"));
	EXPECT_EQ(SanaeBatchCanonicalEpisodeCode("01.50"), SanaeBatchCanonicalEpisodeCode("1.5"));
	EXPECT_EQ("s:ova 1", SanaeBatchCanonicalEpisodeCode(" OVA-1 "));
	EXPECT_EQ("s:ОВА 1", SanaeBatchCanonicalEpisodeCode("ОВА-1"));
}

TEST(sanae_batch_import, pairs_ensub_and_rusub_by_episode) {
	auto rows = SanaeBatchPairFiles(
		{agi::fs::path("Show 01 EN.ass"), agi::fs::path("Show 02 EN.ass")},
		{agi::fs::path("Show 1 RU.ass"), agi::fs::path("Show 02 RU.ass")});
	ASSERT_EQ(2U, rows.size());
	EXPECT_FALSE(rows[0].ensub_path.empty());
	EXPECT_FALSE(rows[0].rusub_path.empty());
	EXPECT_FALSE(rows[1].ensub_path.empty());
	EXPECT_FALSE(rows[1].rusub_path.empty());
}

TEST(sanae_batch_import, pairs_different_working_names_by_episode_code) {
	auto rows = SanaeBatchPairFiles(
		{agi::fs::path("[Group] Anime - 05 English.ass")},
		{agi::fs::path("dara5.ass")});
	ASSERT_EQ(1U, rows.size());
	EXPECT_EQ("05", rows[0].episode_code);
	EXPECT_EQ("[Group] Anime - 05 English.ass", rows[0].ensub_path.filename().string());
	EXPECT_EQ("dara5.ass", rows[0].rusub_path.filename().string());
}

TEST(sanae_batch_import, marks_ambiguous_duplicate_files) {
	auto rows = SanaeBatchPairFiles(
		{agi::fs::path("Show 01 EN.ass"), agi::fs::path("Alternate 01 EN.ass")}, {});
	ASSERT_EQ(1U, rows.size());
	EXPECT_TRUE(rows[0].duplicate_ensub);
}

TEST(sanae_batch_import, persists_resume_state_and_converts_interrupted_rows) {
	// setup-test-data creates this writable directory in the Meson build tree.
	// Do not write temporary state into the source checkout via test_data_dir().
	auto path = agi::fs::path("data/sanae_batch_import_job_tmp.json");
	agi::fs::Remove(path);

	SanaeBatchImportJob input;
	input.project_id = "project-id";
	input.ensub_directory = agi::fs::path("C:/ENSUB");
	input.rusub_directory = agi::fs::path("C:/RUSUB");
	input.skip_finalized = false;
	input.continue_after_error = false;
	input.sync_after_import = false;
	SanaeBatchImportRow row;
	row.episode_code = "03";
	row.sort_order = 3.0;
	row.ensub_path = agi::fs::path("C:/ENSUB/03.ass");
	row.rusub_path = agi::fs::path("C:/RUSUB/03.ass");
	row.create_idempotency_key = "create-key";
	row.existing_source_action = "replace";
	row.replace_idempotency_key = "replace-key";
	row.finalize_idempotency_key = "finalize-key";
	row.state = SanaeBatchRowState::Running;
	input.rows.push_back(row);

	ASSERT_NO_THROW(SanaeBatchSaveJob(path, input));
	SanaeBatchImportJob output;
	ASSERT_TRUE(SanaeBatchLoadJob(path, output));
	ASSERT_EQ(1U, output.rows.size());
	EXPECT_EQ("project-id", output.project_id);
	EXPECT_FALSE(output.skip_finalized);
	EXPECT_FALSE(output.continue_after_error);
	EXPECT_FALSE(output.sync_after_import);
	EXPECT_EQ("03", output.rows[0].episode_code);
	EXPECT_EQ("create-key", output.rows[0].create_idempotency_key);
	EXPECT_EQ("replace", output.rows[0].existing_source_action);
	EXPECT_EQ("replace-key", output.rows[0].replace_idempotency_key);
	EXPECT_EQ("finalize-key", output.rows[0].finalize_idempotency_key);
	EXPECT_EQ(SanaeBatchRowState::Failed, output.rows[0].state);
	EXPECT_EQ("Interrupted; safe to retry", output.rows[0].status);

	agi::fs::Remove(path);
}

TEST(sanae_batch_import, creates_uuid_shaped_idempotency_keys) {
	auto key = SanaeBatchNewIdempotencyKey();
	ASSERT_EQ(36U, key.size());
	EXPECT_EQ('-', key[8]);
	EXPECT_EQ('-', key[13]);
	EXPECT_EQ('-', key[18]);
	EXPECT_EQ('-', key[23]);
	EXPECT_EQ('4', key[14]);
}
