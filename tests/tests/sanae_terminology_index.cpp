// Copyright (c) 2026, Aegisub Sanae contributors
//
// Tests for SanaeTerminologyIndex (Aho-Corasick, V1).
// Verifies SANAE_REVAMP_PLAN.md §3.3 requirements:
//   - exact word / phrase matching
//   - word boundary for single-word terms
//   - V1 ranking: longer phrase > shorter phrase > single word
//   - NO partial/fuzzy
//   - usage: exact RU → CorrectlyUsed; absent → NotUsed (NEUTRAL)
//   - NO auto-misused

#include <main.h>

#include "../../src/sanae_terminology_index.h"
#include "../../src/sanae_text.h"

#include <string>
#include <vector>

using namespace sanae;

static TerminologyIndexEntry make_term(const std::string& en, const std::string& ru,
                                        const std::string& id = "",
                                        std::vector<std::string> aliases = {}) {
    TerminologyIndexEntry e;
    e.term_id = id;
    e.english = en;
    e.english_normalized = SanaeNormalizeSource(en);
    e.russian = ru;
    e.is_phrase = e.english_normalized.find(' ') != std::string::npos;
    for (const auto& a : aliases)
        e.aliases_normalized.push_back(SanaeNormalizeSource(a));
    return e;
}

TEST(sanae_terminology_index, single_word_match) {
    SanaeTerminologyIndex idx;
    idx.Rebuild({ make_term("protect", "защищать", "t1") });
    auto m = idx.Search(SanaeNormalizeSource("i will protect them"));
    EXPECT_EQ(1u, m.size());
    if (!m.empty()) {
        EXPECT_EQ("protect", m[0].english);
        EXPECT_EQ("защищать", m[0].russian);
        EXPECT_EQ(TerminologyMatchKind::ExactWord, m[0].kind);
        EXPECT_EQ(1, m[0].word_count);
    }
}

TEST(sanae_terminology_index, word_boundary_prevents_substring_match) {
    SanaeTerminologyIndex idx;
    idx.Rebuild({ make_term("he", "он", "t-he") });
    auto m = idx.Search(SanaeNormalizeSource("the hero is here"));
    // "he" must NOT match inside "the", "hero", "here"
    EXPECT_EQ(0u, m.size());
}

TEST(sanae_terminology_index, phrase_ranks_above_single_word) {
    SanaeTerminologyIndex idx;
    idx.Rebuild({
        make_term("protect them", "защити их", "t-phrase"),
        make_term("protect", "защищать", "t-word"),
    });
    auto m = idx.Search(SanaeNormalizeSource("i will protect them now"));
    EXPECT_GE(m.size(), 2u);
    if (m.size() >= 2) {
        EXPECT_EQ(TerminologyMatchKind::ExactPhrase, m[0].kind);
        EXPECT_EQ(2, m[0].word_count);
        EXPECT_EQ(TerminologyMatchKind::ExactWord, m[1].kind);
    }
}

TEST(sanae_terminology_index, longer_phrase_ranks_first) {
    SanaeTerminologyIndex idx;
    idx.Rebuild({
        make_term("magic sword", "магический меч", "t-long"),
        make_term("magic", "магия", "t-short"),
        make_term("sword", "меч", "t-word"),
    });
    auto m = idx.Search(SanaeNormalizeSource("he drew his magic sword slowly"));
    EXPECT_GE(m.size(), 3u);
    if (m.size() >= 3) {
        EXPECT_EQ("magic sword", m[0].english);
    }
}

TEST(sanae_terminology_index, no_match_when_absent) {
    SanaeTerminologyIndex idx;
    idx.Rebuild({ make_term("dragon", "дракон", "t") });
    auto m = idx.Search(SanaeNormalizeSource("nothing here but words"));
    EXPECT_TRUE(m.empty());
}

TEST(sanae_terminology_index, empty_index) {
    SanaeTerminologyIndex idx;
    idx.Rebuild({});
    EXPECT_TRUE(idx.Empty());
    auto m = idx.Search("any text");
    EXPECT_TRUE(m.empty());
}

TEST(sanae_terminology_index, usage_exact_ru_match) {
    SanaeTerminologyIndex idx;
    idx.Rebuild({ make_term("protect", "защищать", "t1") });
    auto m = idx.Search(SanaeNormalizeSource("i will protect them"));
    ASSERT_FALSE(m.empty());
    SanaeTerminologyIndex::UpdateUsage(m, "я буду защищать их");
    EXPECT_EQ(TerminologyUsage::CorrectlyUsed, m[0].usage);
}

TEST(sanae_terminology_index, usage_inflected_ru_neutral_not_misused) {
    // "Я защищу их" is correct, but literal "защищать" absent.
    // V1: NEUTRAL (NotUsed), NOT warning/error.
    SanaeTerminologyIndex idx;
    idx.Rebuild({ make_term("protect", "защищать", "t1") });
    auto m = idx.Search(SanaeNormalizeSource("i will protect them"));
    SanaeTerminologyIndex::UpdateUsage(m, "я защищу их");
    EXPECT_EQ(TerminologyUsage::NotUsed, m[0].usage);
}

TEST(sanae_terminology_index, usage_alias_hit) {
    SanaeTerminologyIndex idx;
    idx.Rebuild({ make_term("protect", "защищать", "t1",
                             {"защищу", "защитил", "защитить"}) });
    auto m = idx.Search(SanaeNormalizeSource("i will protect them"));
    SanaeTerminologyIndex::UpdateUsage(m, "я защищу их");
    EXPECT_EQ(TerminologyUsage::CorrectlyUsed, m[0].usage);
}

TEST(sanae_terminology_index, rebuild_invalidates) {
    SanaeTerminologyIndex idx;
    idx.Rebuild({ make_term("cat", "кошка", "t1") });
    EXPECT_EQ(1u, idx.Size());
    idx.Rebuild({
        make_term("cat", "кошка", "t1"),
        make_term("dog", "собака", "t2"),
        make_term("bird", "птица", "t3"),
    });
    EXPECT_EQ(3u, idx.Size());
    auto m = idx.Search(SanaeNormalizeSource("a dog and a cat"));
    EXPECT_EQ(2u, m.size());
}

TEST(sanae_terminology_index, duplicate_normalized_keeps_first) {
    SanaeTerminologyIndex idx;
    idx.Rebuild({
        make_term("protect", "защищать", "t-first"),
        make_term("Protect", "ограждать", "t-second"),
    });
    EXPECT_EQ(1u, idx.Size());
    auto m = idx.Search(SanaeNormalizeSource("i will protect them"));
    EXPECT_EQ(1u, m.size());
    if (!m.empty()) {
        EXPECT_EQ("t-first", m[0].term_id);
    }
}

TEST(sanae_terminology_index, top_n_truncation_by_caller) {
    SanaeTerminologyIndex idx;
    idx.Rebuild({
        make_term("alpha", "альфа", "t1"),
        make_term("beta", "бета", "t2"),
        make_term("gamma", "гамма", "t3"),
        make_term("delta", "дельта", "t4"),
        make_term("epsilon", "эпсилон", "t5"),
        make_term("zeta", "дзета", "t6"),
        make_term("eta", "эта", "t7"),
    });
    auto m = idx.Search(SanaeNormalizeSource("alpha beta gamma delta epsilon zeta eta"));
    EXPECT_EQ(7u, m.size());
    // Caller truncates to top 3:
    std::vector<TerminologyMatch> top3(m.begin(), m.begin() + std::min<size_t>(3, m.size()));
    EXPECT_EQ(3u, top3.size());
}

// ---- Unicode-safe boundary verification (Phase 2 gap 1) ----

TEST(sanae_terminology_index, boundary_term_at_beginning_of_line) {
    SanaeTerminologyIndex idx;
    idx.Rebuild({ make_term("hello", "привет", "t") });
    auto m = idx.Search(SanaeNormalizeSource("hello world"));
    EXPECT_EQ(1u, m.size());
}

TEST(sanae_terminology_index, boundary_term_at_end_of_line) {
    SanaeTerminologyIndex idx;
    idx.Rebuild({ make_term("world", "мир", "t") });
    auto m = idx.Search(SanaeNormalizeSource("hello world"));
    EXPECT_EQ(1u, m.size());
}

TEST(sanae_terminology_index, boundary_ascii_punctuation_around_term) {
    SanaeTerminologyIndex idx;
    idx.Rebuild({ make_term("cat", "кошка", "t") });
    // After SanaeNormalizeSource, punctuation is preserved (NFKC doesn't remove it).
    auto m = idx.Search(SanaeNormalizeSource("the cat, dog! (cat) cat."));
    EXPECT_EQ(3u, m.size());  // three occurrences, all bounded by punctuation
}

TEST(sanae_terminology_index, boundary_unicode_punctuation_around_term) {
    SanaeTerminologyIndex idx;
    idx.Rebuild({ make_term("cat", "кошка", "t") });
    // Unicode punctuation: « cat » (guillemets U+00AB, U+00BB)
    auto m = idx.Search(SanaeNormalizeSource("« cat »"));
    EXPECT_EQ(1u, m.size());
}

TEST(sanae_terminology_index, boundary_apostrophe) {
    SanaeTerminologyIndex idx;
    idx.Rebuild({ make_term("cat", "кошка", "t") });
    // "cat's" — the apostrophe is a boundary, so "cat" should match.
    auto m = idx.Search(SanaeNormalizeSource("the cat's toy"));
    EXPECT_EQ(1u, m.size());
    if (!m.empty()) {
        EXPECT_EQ(4, m[0].matched_start);  // "cat" starts at position 4
    }
}

TEST(sanae_terminology_index, boundary_hyphen) {
    SanaeTerminologyIndex idx;
    idx.Rebuild({ make_term("cat", "кошка", "t") });
    // "cat-dog" — hyphen is a boundary, so "cat" should match.
    auto m = idx.Search(SanaeNormalizeSource("cat-dog"));
    EXPECT_EQ(1u, m.size());
}

TEST(sanae_terminology_index, boundary_term_inside_larger_word_rejected) {
    SanaeTerminologyIndex idx;
    idx.Rebuild({ make_term("he", "он", "t") });
    // "the" — "he" is inside "the" without boundary, must NOT match.
    auto m = idx.Search(SanaeNormalizeSource("the theater"));
    EXPECT_EQ(0u, m.size());
}

TEST(sanae_terminology_index, boundary_adjacent_to_cyrillic_letter) {
    SanaeTerminologyIndex idx;
    idx.Rebuild({ make_term("cat", "кошка", "t") });
    // "catкошка" — adjacent Cyrillic letter is NOT a boundary.
    // "cat" should NOT match here because it's part of a larger word.
    auto m = idx.Search(SanaeNormalizeSource("catкошка"));
    EXPECT_EQ(0u, m.size());
}

TEST(sanae_terminology_index, boundary_adjacent_to_unicode_punctuation_matches) {
    SanaeTerminologyIndex idx;
    idx.Rebuild({ make_term("cat", "кошка", "t") });
    // "cat — dog" (em-dash U+2014)
    auto m = idx.Search(SanaeNormalizeSource("cat — dog"));
    EXPECT_EQ(1u, m.size());
}

TEST(sanae_terminology_index, boundary_normalized_unicode_input) {
    // SanaeNormalizeSource does NFKC + fold_case + whitespace collapse.
    // After normalization, fullwidth characters become ASCII.
    SanaeTerminologyIndex idx;
    idx.Rebuild({ make_term("cat", "кошка", "t") });
    // Fullwidth "CAT" (U+FF21 U+FF22 U+FF23) normalizes to "cat".
    auto m = idx.Search(SanaeNormalizeSource("ＣＡＴ dog"));
    EXPECT_EQ(1u, m.size());
}

TEST(sanae_terminology_index, boundary_digit_adjacent_to_term) {
    SanaeTerminologyIndex idx;
    idx.Rebuild({ make_term("cat", "кошка", "t") });
    // "cat2" — digit is NOT a boundary (isalnum), so "cat" should NOT match.
    auto m = idx.Search(SanaeNormalizeSource("cat2 dog"));
    EXPECT_EQ(0u, m.size());
    // "cat 2" — space is boundary, so "cat" should match.
    m = idx.Search(SanaeNormalizeSource("cat 2"));
    EXPECT_EQ(1u, m.size());
}

TEST(sanae_terminology_index, phrase_matching_not_affected_by_boundary) {
    // Phrases use substring matching, not word-boundary.
    SanaeTerminologyIndex idx;
    idx.Rebuild({ make_term("protect them", "защити их", "t") });
    // Phrase inside larger text — should still match (substring for phrases).
    auto m = idx.Search(SanaeNormalizeSource("i will protect them now"));
    EXPECT_EQ(1u, m.size());
}
