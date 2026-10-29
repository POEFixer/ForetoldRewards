// FR_RewardText.h — map RitualAtlasLineMods stat/mod ids to their label keys.
// Ids are the wide strings read from the reward fn output (entry+0x28 -> row -> +0).
// "OtherMaps*" = Foretold Proliferation; "Contains*" = Foretold Bounty.
//
// The full pool is the 67 rows of Data/Balance/ritualatlaslinemods.datc64, each row's
// col0 referencing a Mods.datc64 row whose Id is one of the RitualRite* strings below
// (6 proliferation + 61 bounty). Extracted offline 2026-06-15; AllRewards() is the single
// source of truth (the label keys + the search picker both build from it).
//
// Texts follow the program language: a reward's label is the plugin text "reward_<Mods Id>"
// (lang/<code>.json, the prefix listed in lang/dynamic_prefixes.txt; the English is the
// plugin's own wording, calibrated against in-game screenshots). This header stays free of
// PluginSDK.h: it hands out keys, and ForetoldRewards.cpp draws them through I18n::Tr.
// test/test_view.cpp checks that every key built here is in lang/en.json.
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cctype>

namespace FR {

enum class RewardCat { Unknown, Proliferation, Bounty };

// One reward in the foretold pool. `group` drives the picker's section headers (a group id:
// ForetoldRewards.cpp draws it as the plugin's text for that group).
struct RewardDef {
    const char* id;       // Mods.datc64 Id (the wide string the reward fn returns)
    const char* labelId;  // the Mods Id whose label is shown: key "reward_<labelId>" (= id but for the swap below)
    RewardCat   cat;      // Proliferation (other maps) | Bounty (this map contains)
    const char* group;    // "Proliferation" | "Unique" | "Omen" | "Currency" | "Crafting"
};

// The plugin text key of a label id: "reward_" + the Mods Id (lang/dynamic_prefixes.txt).
inline constexpr const char* kRewardKeyPrefix = "reward_";
inline std::string RewardKey(const std::string& labelId) { return kRewardKeyPrefix + labelId; }

// ============================ KOSTYL / WORKAROUND (2026-06-16) ============================
// The reward fn / ritualatlaslinemods source reports BASE Chaos Orb and Exalted Orb SWAPPED: a
// cell whose id is RitualRiteContainsChaosOrbs* is actually an Exalted Orb reward in-game, and a
// RitualRiteContainsExaltedOrbs* is actually a Chaos Orb — for every quantity (x2/x4/x8). We could
// NOT fix the root cause (the id<->item mapping is swapped at the data source itself), so we swap
// the two at the DATA layer below: once for labels (AllRewards) and once for prices
// (RewardPriceTable). Doing it here keeps EVERY surface consistent automatically — overlay labels,
// the atlas/pool lists, prices + value sort, AND the search picker (which reads d.labelId directly).
// Only the BASE orbs are affected; Greater/Perfect Chaos/Exalt are NOT swapped. If the source is
// ever corrected, delete kChaosExaltSwapPairs and its two uses below. (Quantity is never swapped —
// only the orb TYPE.)
inline const char* const kChaosExaltSwapPairs[][2] = {
    { "RitualRiteContainsChaosOrbs2", "RitualRiteContainsExaltedOrbs2" },
    { "RitualRiteContainsChaosOrbs4", "RitualRiteContainsExaltedOrbs4" },
    { "RitualRiteContainsChaosOrbs8", "RitualRiteContainsExaltedOrbs8" },
};
inline void ApplyChaosExaltLabelKostyl(std::vector<RewardDef>& v) {
    auto find = [&](const char* id) -> RewardDef* { for (auto& d : v) if (std::string(d.id) == id) return &d; return nullptr; };
    for (auto& p : kChaosExaltSwapPairs) {
        RewardDef* a = find(p[0]); RewardDef* b = find(p[1]);
        if (a && b) { const char* t = a->labelId; a->labelId = b->labelId; b->labelId = t; }
    }
}

// All 67 foretold rewards (source of truth). Grouped for the picker.
inline const std::vector<RewardDef>& AllRewards() {
    static const std::vector<RewardDef> k = [] {
        std::vector<RewardDef> v = {
        // --- Proliferation: applies to OTHER maps in the line (6) ---
        {"RitualRiteOtherMapsIncreasedTribute",             "RitualRiteOtherMapsIncreasedTribute",             RewardCat::Proliferation,  "Proliferation"},
        {"RitualRiteOtherMapsRewardsAmount",                "RitualRiteOtherMapsRewardsAmount",                RewardCat::Proliferation,  "Proliferation"},
        {"RitualRiteOtherMapsAdditionalPacks",              "RitualRiteOtherMapsAdditionalPacks",              RewardCat::Proliferation,  "Proliferation"},
        {"RitualRiteOtherMapsFreeRerolls",                  "RitualRiteOtherMapsFreeRerolls",                  RewardCat::Proliferation,  "Proliferation"},
        {"RitualRiteOtherMapsAdditionalRerolls",            "RitualRiteOtherMapsAdditionalRerolls",            RewardCat::Proliferation,  "Proliferation"},
        {"RitualRiteOtherMapsRerollCost",                   "RitualRiteOtherMapsRerollCost",                   RewardCat::Proliferation,  "Proliferation"},

        // --- Bounty: this map's Ritual contains a Unique item (11) ---
        {"RitualRiteContainsVeryRareUnique",                "RitualRiteContainsVeryRareUnique",                RewardCat::Bounty,         "Unique"},
        {"RitualRiteContainsMageblood",                     "RitualRiteContainsMageblood",                     RewardCat::Bounty,         "Unique"},
        {"RitualRiteContainsHeadhunter",                    "RitualRiteContainsHeadhunter",                    RewardCat::Bounty,         "Unique"},
        {"RitualRiteContainsKalandrasTouch",                "RitualRiteContainsKalandrasTouch",                RewardCat::Bounty,         "Unique"},
        {"RitualRiteContainsQueenOfTheForest",              "RitualRiteContainsQueenOfTheForest",              RewardCat::Bounty,         "Unique"},
        {"RitualRiteContainsAlphasHowl",                    "RitualRiteContainsAlphasHowl",                    RewardCat::Bounty,         "Unique"},
        {"RitualRiteContainsDefianceOfDestiny",             "RitualRiteContainsDefianceOfDestiny",             RewardCat::Bounty,         "Unique"},
        {"RitualRiteContainsYokeOfSuffering",               "RitualRiteContainsYokeOfSuffering",               RewardCat::Bounty,         "Unique"},
        {"RitualRiteContainsAstramentis",                   "RitualRiteContainsAstramentis",                   RewardCat::Bounty,         "Unique"},
        {"RitualRiteContainsDreamFragments",                "RitualRiteContainsDreamFragments",                RewardCat::Bounty,         "Unique"},
        {"RitualRiteContainsOriginalSin",                   "RitualRiteContainsOriginalSin",                   RewardCat::Bounty,         "Unique"},

        // --- Bounty: this map's Ritual contains an Omen (15) ---
        {"RitualRiteContainsOmenDextralExaltation",         "RitualRiteContainsOmenDextralExaltation",         RewardCat::Bounty,         "Omen"},
        {"RitualRiteContainsOmenSinistralExaltation",       "RitualRiteContainsOmenSinistralExaltation",       RewardCat::Bounty,         "Omen"},
        {"RitualRiteContainsOmenDextralAnnulment",          "RitualRiteContainsOmenDextralAnnulment",          RewardCat::Bounty,         "Omen"},
        {"RitualRiteContainsOmenSinistralAnnulment",        "RitualRiteContainsOmenSinistralAnnulment",        RewardCat::Bounty,         "Omen"},
        {"RitualRiteContainsOmenDextralErasure",            "RitualRiteContainsOmenDextralErasure",            RewardCat::Bounty,         "Omen"},
        {"RitualRiteContainsOmenSinistralErasure",          "RitualRiteContainsOmenSinistralErasure",          RewardCat::Bounty,         "Omen"},
        {"RitualRiteContainsOmenDextralCrystallisation",    "RitualRiteContainsOmenDextralCrystallisation",    RewardCat::Bounty,         "Omen"},
        {"RitualRiteContainsOmenSinistralCrystallisation",  "RitualRiteContainsOmenSinistralCrystallisation",  RewardCat::Bounty,         "Omen"},
        {"RitualRiteContainsOmenGreaterAnnulment",          "RitualRiteContainsOmenGreaterAnnulment",          RewardCat::Bounty,         "Omen"},
        {"RitualRiteContainsOmenAmelioration",              "RitualRiteContainsOmenAmelioration",              RewardCat::Bounty,         "Omen"},
        {"RitualRiteContainsOmenWhittling",                 "RitualRiteContainsOmenWhittling",                 RewardCat::Bounty,         "Omen"},
        {"RitualRiteContainsOmenCorruption",                "RitualRiteContainsOmenCorruption",                RewardCat::Bounty,         "Omen"},
        {"RitualRiteContainsOmenBlessed",                   "RitualRiteContainsOmenBlessed",                   RewardCat::Bounty,         "Omen"},
        {"RitualRiteContainsOmenChance",                    "RitualRiteContainsOmenChance",                    RewardCat::Bounty,         "Omen"},
        {"RitualRiteContainsOmenSanctification",            "RitualRiteContainsOmenSanctification",            RewardCat::Bounty,         "Omen"},

        // --- Bounty: this map's Ritual contains currency orbs (13) ---
        {"RitualRiteContainsDivineOrbs",                    "RitualRiteContainsDivineOrbs",                    RewardCat::Bounty,         "Currency"},
        {"RitualRiteContainsDivineOrbs2",                   "RitualRiteContainsDivineOrbs2",                   RewardCat::Bounty,         "Currency"},
        {"RitualRiteContainsDivineOrbs5",                   "RitualRiteContainsDivineOrbs5",                   RewardCat::Bounty,         "Currency"},
        {"RitualRiteContainsChaosOrbs2",                    "RitualRiteContainsChaosOrbs2",                    RewardCat::Bounty,         "Currency"},
        {"RitualRiteContainsChaosOrbs4",                    "RitualRiteContainsChaosOrbs4",                    RewardCat::Bounty,         "Currency"},
        {"RitualRiteContainsChaosOrbs8",                    "RitualRiteContainsChaosOrbs8",                    RewardCat::Bounty,         "Currency"},
        {"RitualRiteContainsExaltedOrbs2",                  "RitualRiteContainsExaltedOrbs2",                  RewardCat::Bounty,         "Currency"},
        {"RitualRiteContainsExaltedOrbs4",                  "RitualRiteContainsExaltedOrbs4",                  RewardCat::Bounty,         "Currency"},
        {"RitualRiteContainsExaltedOrbs8",                  "RitualRiteContainsExaltedOrbs8",                  RewardCat::Bounty,         "Currency"},
        {"RitualRiteContainsOrbsOfAnnulment",               "RitualRiteContainsOrbsOfAnnulment",               RewardCat::Bounty,         "Currency"},
        {"RitualRiteContainsOrbsOfAnnulment2",              "RitualRiteContainsOrbsOfAnnulment2",              RewardCat::Bounty,         "Currency"},
        {"RitualRiteContainsOrbsOfChance",                  "RitualRiteContainsOrbsOfChance",                  RewardCat::Bounty,         "Currency"},
        {"RitualRiteContainsOrbsOfChance2",                 "RitualRiteContainsOrbsOfChance2",                 RewardCat::Bounty,         "Currency"},

        // --- Bounty: this map's Ritual contains Greater/Perfect crafting orbs (22) ---
        {"RitualRiteContainsGreaterAugmentation2",          "RitualRiteContainsGreaterAugmentation2",          RewardCat::Bounty,         "Crafting"},
        {"RitualRiteContainsGreaterAugmentation4",          "RitualRiteContainsGreaterAugmentation4",          RewardCat::Bounty,         "Crafting"},
        {"RitualRiteContainsGreaterAugmentation8",          "RitualRiteContainsGreaterAugmentation8",          RewardCat::Bounty,         "Crafting"},
        {"RitualRiteContainsGreaterTransmutation2",         "RitualRiteContainsGreaterTransmutation2",         RewardCat::Bounty,         "Crafting"},
        {"RitualRiteContainsGreaterTransmutation4",         "RitualRiteContainsGreaterTransmutation4",         RewardCat::Bounty,         "Crafting"},
        {"RitualRiteContainsGreaterTransmutation8",         "RitualRiteContainsGreaterTransmutation8",         RewardCat::Bounty,         "Crafting"},
        {"RitualRiteContainsGreaterChaos",                  "RitualRiteContainsGreaterChaos",                  RewardCat::Bounty,         "Crafting"},
        {"RitualRiteContainsGreaterChaos2",                 "RitualRiteContainsGreaterChaos2",                 RewardCat::Bounty,         "Crafting"},
        {"RitualRiteContainsGreaterChaos5",                 "RitualRiteContainsGreaterChaos5",                 RewardCat::Bounty,         "Crafting"},
        {"RitualRiteContainsGreaterRegal",                  "RitualRiteContainsGreaterRegal",                  RewardCat::Bounty,         "Crafting"},
        {"RitualRiteContainsGreaterRegal2",                 "RitualRiteContainsGreaterRegal2",                 RewardCat::Bounty,         "Crafting"},
        {"RitualRiteContainsGreaterRegal5",                 "RitualRiteContainsGreaterRegal5",                 RewardCat::Bounty,         "Crafting"},
        {"RitualRiteContainsGreaterExalt",                  "RitualRiteContainsGreaterExalt",                  RewardCat::Bounty,         "Crafting"},
        {"RitualRiteContainsGreaterExalt2",                 "RitualRiteContainsGreaterExalt2",                 RewardCat::Bounty,         "Crafting"},
        {"RitualRiteContainsGreaterExalt5",                 "RitualRiteContainsGreaterExalt5",                 RewardCat::Bounty,         "Crafting"},
        {"RitualRiteContainsPerfectAugmentation",           "RitualRiteContainsPerfectAugmentation",           RewardCat::Bounty,         "Crafting"},
        {"RitualRiteContainsPerfectAugmentation2",          "RitualRiteContainsPerfectAugmentation2",          RewardCat::Bounty,         "Crafting"},
        {"RitualRiteContainsPerfectTransmutation",          "RitualRiteContainsPerfectTransmutation",          RewardCat::Bounty,         "Crafting"},
        {"RitualRiteContainsPerfectTransmutation2",         "RitualRiteContainsPerfectTransmutation2",         RewardCat::Bounty,         "Crafting"},
        {"RitualRiteContainsPerfectChaos",                  "RitualRiteContainsPerfectChaos",                  RewardCat::Bounty,         "Crafting"},
        {"RitualRiteContainsPerfectRegal",                  "RitualRiteContainsPerfectRegal",                  RewardCat::Bounty,         "Crafting"},
        {"RitualRiteContainsPerfectExalt",                  "RitualRiteContainsPerfectExalt",                  RewardCat::Bounty,         "Crafting"},
        };
        ApplyChaosExaltLabelKostyl(v);   // KOSTYL: swap base Chaos<->Exalted labels (see note above)
        return v;
    }();
    return k;
}

inline RewardCat CategoryOf(const std::string& modId) {
    if (modId.find("OtherMaps") != std::string::npos) return RewardCat::Proliferation;
    if (modId.find("Contains")  != std::string::npos) return RewardCat::Bounty;
    return RewardCat::Unknown;
}

// The text key of a reward's short overlay/picker label ("reward_<Mods Id>", the base
// Chaos/Exalted orbs swapped as above); "" for a mod the catalog does not know (draw
// RewardFallbackLabel then).
inline const std::unordered_map<std::string, const char*>& RewardLabelIds() {
    static const std::unordered_map<std::string, const char*> kMap = [] {
        std::unordered_map<std::string, const char*> m;
        for (const auto& d : AllRewards()) m.emplace(d.id, d.labelId);
        return m;
    }();
    return kMap;
}
inline std::string RewardLabelKey(const std::string& modId) {
    const auto& m = RewardLabelIds();
    auto it = m.find(modId);
    return it != m.end() ? RewardKey(it->second) : std::string();
}
inline bool IsCatalogReward(const std::string& modId) { return RewardLabelIds().count(modId) != 0; }

// A readable label for a mod the catalog does not know (a future game-added reward): the id
// spelled out in English, never translated.
inline std::string RewardFallbackLabel(const std::string& modId) {
    // Strip known prefixes and space-out CamelCase.
    std::string s = modId;
    for (const char* pfx : { "RitualRiteOtherMaps", "RitualRiteContains", "RitualRite" }) {
        size_t len = std::char_traits<char>::length(pfx);
        if (s.rfind(pfx, 0) == 0) { s = s.substr(len); break; }
    }
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (i > 0 && std::isupper((unsigned char)c) && !std::isupper((unsigned char)s[i - 1]))
            out.push_back(' ');
        out.push_back(c);
    }
    return out.empty() ? modId : out;
}

// Ranking tier for the pool sort. Higher value = sorted higher.
enum class PriceTier { Normal = 0, VeryRareUnique = 1, TopUnique = 2 };

// poe2scout lookup name + stack qty + sort tier for a reward id. priceName == "" =>
// unpriceable (proliferation modifiers, or "Very Rare Unique" which has no single price).
struct RewardPriceInfo { const char* priceName; int qty; PriceTier tier; };

// id -> price info. All names reconciled against the live poe2scout "Runes of Aldur" dump
// (2026-06-16); LookupPrice's whole-word contains fallback covers any minor wording drift.
inline const std::unordered_map<std::string, RewardPriceInfo>& RewardPriceTable() {
    static const std::unordered_map<std::string, RewardPriceInfo> k = [] {
        std::unordered_map<std::string, RewardPriceInfo> m = {
        // Uniques
        {"RitualRiteContainsMageblood",        {"Mageblood",            1, PriceTier::TopUnique}},
        {"RitualRiteContainsHeadhunter",       {"Headhunter",           1, PriceTier::TopUnique}},
        {"RitualRiteContainsVeryRareUnique",   {"",                     1, PriceTier::VeryRareUnique}},
        {"RitualRiteContainsKalandrasTouch",   {"Kalandra's Touch",     1, PriceTier::Normal}},
        {"RitualRiteContainsQueenOfTheForest", {"Queen of the Forest",  1, PriceTier::Normal}},
        {"RitualRiteContainsAlphasHowl",       {"Alpha's Howl",         1, PriceTier::Normal}},
        {"RitualRiteContainsDefianceOfDestiny",{"Defiance of Destiny",  1, PriceTier::Normal}},
        {"RitualRiteContainsYokeOfSuffering",  {"Yoke of Suffering",    1, PriceTier::Normal}},
        {"RitualRiteContainsAstramentis",      {"Astramentis",          1, PriceTier::Normal}},
        {"RitualRiteContainsDreamFragments",   {"Dream Fragments",      1, PriceTier::Normal}},
        {"RitualRiteContainsOriginalSin",      {"Original Sin",         1, PriceTier::Normal}},
        // Omens (poe2scout "ritual" category)
        {"RitualRiteContainsOmenDextralExaltation",      {"Omen of Dextral Exaltation",       1, PriceTier::Normal}},
        {"RitualRiteContainsOmenSinistralExaltation",    {"Omen of Sinistral Exaltation",     1, PriceTier::Normal}},
        {"RitualRiteContainsOmenDextralAnnulment",       {"Omen of Dextral Annulment",        1, PriceTier::Normal}},
        {"RitualRiteContainsOmenSinistralAnnulment",     {"Omen of Sinistral Annulment",      1, PriceTier::Normal}},
        {"RitualRiteContainsOmenDextralErasure",         {"Omen of Dextral Erasure",          1, PriceTier::Normal}},
        {"RitualRiteContainsOmenSinistralErasure",       {"Omen of Sinistral Erasure",        1, PriceTier::Normal}},
        {"RitualRiteContainsOmenDextralCrystallisation", {"Omen of Dextral Crystallisation",  1, PriceTier::Normal}},
        {"RitualRiteContainsOmenSinistralCrystallisation",{"Omen of Sinistral Crystallisation",1, PriceTier::Normal}},
        {"RitualRiteContainsOmenGreaterAnnulment",       {"Omen of Greater Annulment",        1, PriceTier::Normal}},
        {"RitualRiteContainsOmenAmelioration",           {"Omen of Amelioration",             1, PriceTier::Normal}},
        {"RitualRiteContainsOmenWhittling",              {"Omen of Whittling",                1, PriceTier::Normal}},
        {"RitualRiteContainsOmenCorruption",             {"Omen of Corruption",               1, PriceTier::Normal}},
        {"RitualRiteContainsOmenBlessed",                {"Omen of the Blessed",              1, PriceTier::Normal}},
        {"RitualRiteContainsOmenChance",                 {"Omen of Chance",   1, PriceTier::Normal}},
        {"RitualRiteContainsOmenSanctification",         {"Omen of Sanctification",           1, PriceTier::Normal}},
        // Currency
        {"RitualRiteContainsDivineOrbs",       {"Divine Orb",       1, PriceTier::Normal}},
        {"RitualRiteContainsDivineOrbs2",      {"Divine Orb",       2, PriceTier::Normal}},
        {"RitualRiteContainsDivineOrbs5",      {"Divine Orb",       5, PriceTier::Normal}},
        {"RitualRiteContainsChaosOrbs2",       {"Chaos Orb",        2, PriceTier::Normal}},
        {"RitualRiteContainsChaosOrbs4",       {"Chaos Orb",        4, PriceTier::Normal}},
        {"RitualRiteContainsChaosOrbs8",       {"Chaos Orb",        8, PriceTier::Normal}},
        {"RitualRiteContainsExaltedOrbs2",     {"Exalted Orb",      2, PriceTier::Normal}},
        {"RitualRiteContainsExaltedOrbs4",     {"Exalted Orb",      4, PriceTier::Normal}},
        {"RitualRiteContainsExaltedOrbs8",     {"Exalted Orb",      8, PriceTier::Normal}},
        {"RitualRiteContainsOrbsOfAnnulment",  {"Orb of Annulment", 1, PriceTier::Normal}},
        {"RitualRiteContainsOrbsOfAnnulment2", {"Orb of Annulment", 2, PriceTier::Normal}},
        {"RitualRiteContainsOrbsOfChance",     {"Orb of Chance",    1, PriceTier::Normal}},
        {"RitualRiteContainsOrbsOfChance2",    {"Orb of Chance",    2, PriceTier::Normal}},
        // Greater/Perfect crafting orbs (names)
        {"RitualRiteContainsGreaterAugmentation2",  {"Greater Orb of Augmentation",  2, PriceTier::Normal}},
        {"RitualRiteContainsGreaterAugmentation4",  {"Greater Orb of Augmentation",  4, PriceTier::Normal}},
        {"RitualRiteContainsGreaterAugmentation8",  {"Greater Orb of Augmentation",  8, PriceTier::Normal}},
        {"RitualRiteContainsGreaterTransmutation2", {"Greater Orb of Transmutation", 2, PriceTier::Normal}},
        {"RitualRiteContainsGreaterTransmutation4", {"Greater Orb of Transmutation", 4, PriceTier::Normal}},
        {"RitualRiteContainsGreaterTransmutation8", {"Greater Orb of Transmutation", 8, PriceTier::Normal}},
        {"RitualRiteContainsGreaterChaos",          {"Greater Chaos Orb",            1, PriceTier::Normal}},
        {"RitualRiteContainsGreaterChaos2",         {"Greater Chaos Orb",            2, PriceTier::Normal}},
        {"RitualRiteContainsGreaterChaos5",         {"Greater Chaos Orb",            5, PriceTier::Normal}},
        {"RitualRiteContainsGreaterRegal",          {"Greater Regal Orb",            1, PriceTier::Normal}},
        {"RitualRiteContainsGreaterRegal2",         {"Greater Regal Orb",            2, PriceTier::Normal}},
        {"RitualRiteContainsGreaterRegal5",         {"Greater Regal Orb",            5, PriceTier::Normal}},
        {"RitualRiteContainsGreaterExalt",          {"Greater Exalted Orb",          1, PriceTier::Normal}},
        {"RitualRiteContainsGreaterExalt2",         {"Greater Exalted Orb",          2, PriceTier::Normal}},
        {"RitualRiteContainsGreaterExalt5",         {"Greater Exalted Orb",          5, PriceTier::Normal}},
        {"RitualRiteContainsPerfectAugmentation",   {"Perfect Orb of Augmentation",  1, PriceTier::Normal}},
        {"RitualRiteContainsPerfectAugmentation2",  {"Perfect Orb of Augmentation",  2, PriceTier::Normal}},
        {"RitualRiteContainsPerfectTransmutation",  {"Perfect Orb of Transmutation", 1, PriceTier::Normal}},
        {"RitualRiteContainsPerfectTransmutation2", {"Perfect Orb of Transmutation", 2, PriceTier::Normal}},
        {"RitualRiteContainsPerfectChaos",          {"Perfect Chaos Orb",            1, PriceTier::Normal}},
        {"RitualRiteContainsPerfectRegal",          {"Perfect Regal Orb",            1, PriceTier::Normal}},
        {"RitualRiteContainsPerfectExalt",          {"Perfect Exalted Orb",          1, PriceTier::Normal}},
        };
        // KOSTYL: swap base Chaos<->Exalted PRICE names so a swapped-label orb is priced as the item
        // it now displays as (quantity unchanged — only the orb TYPE is swapped). See note above AllRewards.
        for (auto& p : kChaosExaltSwapPairs) {
            auto a = m.find(p[0]); auto b = m.find(p[1]);
            if (a != m.end() && b != m.end()) { const char* t = a->second.priceName; a->second.priceName = b->second.priceName; b->second.priceName = t; }
        }
        return m;
    }();
    return k;
}

inline RewardPriceInfo PriceInfoFor(const std::string& id) {
    const auto& m = RewardPriceTable();
    auto it = m.find(id);
    return (it != m.end()) ? it->second : RewardPriceInfo{ "", 1, PriceTier::Normal };
}

// "Very Rare Unique" (RitualRiteContainsVeryRareUnique) has no single price — the unique you
// actually receive is random — so poe2scout can't price it. It would otherwise count as 0:
// shown as "-" in the value tables and IGNORED by "Find best path" (a route ending in it is
// "worth" nothing and never wins). We assign it a fixed POTENTIAL value so such routes are
// ranked/shown, but it is ALWAYS surfaced to the user as a potential estimate, never a real
// price. Single source of truth for the placeholder value + the id check.
inline constexpr double kVeryRareUniquePotentialDivine = 20.0;
inline bool IsPotentialValueReward(const std::string& id) {
    return id == "RitualRiteContainsVeryRareUnique";
}
// Placeholder chaos value for a "potential" reward (0 if `id` isn't one, or the divine rate is
// unknown — e.g. prices not loaded yet, in which case it stays 0 like before).
inline double PotentialChaos(const std::string& id, double divineInChaos) {
    return (IsPotentialValueReward(id) && divineInChaos > 0.0)
         ? kVeryRareUniquePotentialDivine * divineInChaos : 0.0;
}

// Group name ("Proliferation"/"Unique"/"Omen"/"Currency"/"Crafting") for colouring; "" if unknown.
inline const char* RewardGroup(const std::string& id) {
    static const std::unordered_map<std::string, const char*> g = [] {
        std::unordered_map<std::string, const char*> m;
        for (const auto& d : AllRewards()) m.emplace(d.id, d.group);
        return m;
    }();
    auto it = g.find(id);
    return (it != g.end()) ? it->second : "";
}

} // namespace FR
