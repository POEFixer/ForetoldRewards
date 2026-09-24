// FR_RewardText.h — map RitualAtlasLineMods stat/mod ids to readable labels.
// Ids are the wide strings read from the reward fn output (entry+0x28 -> row -> +0).
// "OtherMaps*" = Foretold Proliferation; "Contains*" = Foretold Bounty.
//
// The full pool is the 67 rows of Data/Balance/ritualatlaslinemods.datc64, each row's
// col0 referencing a Mods.datc64 row whose Id is one of the RitualRite* strings below
// (6 proliferation + 61 bounty). Extracted offline 2026-06-15; AllRewards() is the single
// source of truth (the label map + the search picker both build from it). Labels are
// derived from the mod ids and can be calibrated against in-game screenshots.
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cctype>

namespace FR {

enum class RewardCat { Unknown, Proliferation, Bounty };

// One reward in the foretold pool. `group` drives the picker's section headers.
struct RewardDef {
    const char* id;       // Mods.datc64 Id (the wide string the reward fn returns)
    const char* label;    // short overlay/picker label
    RewardCat   cat;      // Proliferation (other maps) | Bounty (this map contains)
    const char* group;    // "Proliferation" | "Unique" | "Omen" | "Currency" | "Crafting"
};

// ============================ KOSTYL / WORKAROUND (2026-06-16) ============================
// The reward fn / ritualatlaslinemods source reports BASE Chaos Orb and Exalted Orb SWAPPED: a
// cell whose id is RitualRiteContainsChaosOrbs* is actually an Exalted Orb reward in-game, and a
// RitualRiteContainsExaltedOrbs* is actually a Chaos Orb — for every quantity (x2/x4/x8). We could
// NOT fix the root cause (the id<->item mapping is swapped at the data source itself), so we swap
// the two at the DATA layer below: once for labels (AllRewards) and once for prices
// (RewardPriceTable). Doing it here keeps EVERY surface consistent automatically — overlay labels,
// the atlas/pool lists, prices + value sort, AND the search picker (which reads d.label directly).
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
        if (a && b) { const char* t = a->label; a->label = b->label; b->label = t; }
    }
}

// All 67 foretold rewards (source of truth). Grouped for the picker.
inline const std::vector<RewardDef>& AllRewards() {
    static const std::vector<RewardDef> k = [] {
        std::vector<RewardDef> v = {
        // --- Proliferation: applies to OTHER maps in the line (6) ---
        {"RitualRiteOtherMapsIncreasedTribute",  "+25% Tribute",        RewardCat::Proliferation, "Proliferation"},
        {"RitualRiteOtherMapsRewardsAmount",     "+Ritual Rewards",     RewardCat::Proliferation, "Proliferation"},
        {"RitualRiteOtherMapsAdditionalPacks",   "+Monster Packs",      RewardCat::Proliferation, "Proliferation"},
        {"RitualRiteOtherMapsFreeRerolls",       "+Free Rerolls",       RewardCat::Proliferation, "Proliferation"},
        {"RitualRiteOtherMapsAdditionalRerolls", "+Additional Rerolls", RewardCat::Proliferation, "Proliferation"},
        {"RitualRiteOtherMapsRerollCost",        "-Reroll Cost",        RewardCat::Proliferation, "Proliferation"},

        // --- Bounty: this map's Ritual contains a Unique item (11) ---
        {"RitualRiteContainsVeryRareUnique",   "Very Rare Unique",     RewardCat::Bounty, "Unique"},
        {"RitualRiteContainsMageblood",        "Mageblood",            RewardCat::Bounty, "Unique"},
        {"RitualRiteContainsHeadhunter",       "Headhunter",           RewardCat::Bounty, "Unique"},
        {"RitualRiteContainsKalandrasTouch",   "Kalandra's Touch",     RewardCat::Bounty, "Unique"},
        {"RitualRiteContainsQueenOfTheForest", "Queen of the Forest",  RewardCat::Bounty, "Unique"},
        {"RitualRiteContainsAlphasHowl",       "Alpha's Howl",         RewardCat::Bounty, "Unique"},
        {"RitualRiteContainsDefianceOfDestiny","Defiance of Destiny",  RewardCat::Bounty, "Unique"},
        {"RitualRiteContainsYokeOfSuffering",  "Yoke of Suffering",    RewardCat::Bounty, "Unique"},
        {"RitualRiteContainsAstramentis",      "Astramentis",          RewardCat::Bounty, "Unique"},
        {"RitualRiteContainsDreamFragments",   "Dream Fragments",      RewardCat::Bounty, "Unique"},
        {"RitualRiteContainsOriginalSin",      "Original Sin",         RewardCat::Bounty, "Unique"},

        // --- Bounty: this map's Ritual contains an Omen (15) ---
        {"RitualRiteContainsOmenDextralExaltation",      "Omen: Dextral Exaltation",       RewardCat::Bounty, "Omen"},
        {"RitualRiteContainsOmenSinistralExaltation",    "Omen: Sinistral Exaltation",     RewardCat::Bounty, "Omen"},
        {"RitualRiteContainsOmenDextralAnnulment",       "Omen: Dextral Annulment",        RewardCat::Bounty, "Omen"},
        {"RitualRiteContainsOmenSinistralAnnulment",     "Omen: Sinistral Annulment",      RewardCat::Bounty, "Omen"},
        {"RitualRiteContainsOmenDextralErasure",         "Omen: Dextral Erasure",          RewardCat::Bounty, "Omen"},
        {"RitualRiteContainsOmenSinistralErasure",       "Omen: Sinistral Erasure",        RewardCat::Bounty, "Omen"},
        {"RitualRiteContainsOmenDextralCrystallisation", "Omen: Dextral Crystallisation",  RewardCat::Bounty, "Omen"},
        {"RitualRiteContainsOmenSinistralCrystallisation","Omen: Sinistral Crystallisation",RewardCat::Bounty, "Omen"},
        {"RitualRiteContainsOmenGreaterAnnulment",       "Omen: Greater Annulment",        RewardCat::Bounty, "Omen"},
        {"RitualRiteContainsOmenAmelioration",           "Omen: Amelioration",             RewardCat::Bounty, "Omen"},
        {"RitualRiteContainsOmenWhittling",              "Omen: Whittling",                RewardCat::Bounty, "Omen"},
        {"RitualRiteContainsOmenCorruption",             "Omen: Corruption",               RewardCat::Bounty, "Omen"},
        {"RitualRiteContainsOmenBlessed",                "Omen: the Blessed",              RewardCat::Bounty, "Omen"},
        {"RitualRiteContainsOmenChance",                 "Omen: Chance",                   RewardCat::Bounty, "Omen"},
        {"RitualRiteContainsOmenSanctification",         "Omen: Sanctification",           RewardCat::Bounty, "Omen"},

        // --- Bounty: this map's Ritual contains currency orbs (13) ---
        {"RitualRiteContainsDivineOrbs",       "Divine Orb",          RewardCat::Bounty, "Currency"},
        {"RitualRiteContainsDivineOrbs2",      "Divine Orbs x2",      RewardCat::Bounty, "Currency"},
        {"RitualRiteContainsDivineOrbs5",      "Divine Orbs x5",      RewardCat::Bounty, "Currency"},
        {"RitualRiteContainsChaosOrbs2",       "Chaos Orbs x2",       RewardCat::Bounty, "Currency"},
        {"RitualRiteContainsChaosOrbs4",       "Chaos Orbs x4",       RewardCat::Bounty, "Currency"},
        {"RitualRiteContainsChaosOrbs8",       "Chaos Orbs x8",       RewardCat::Bounty, "Currency"},
        {"RitualRiteContainsExaltedOrbs2",     "Exalted Orbs x2",     RewardCat::Bounty, "Currency"},
        {"RitualRiteContainsExaltedOrbs4",     "Exalted Orbs x4",     RewardCat::Bounty, "Currency"},
        {"RitualRiteContainsExaltedOrbs8",     "Exalted Orbs x8",     RewardCat::Bounty, "Currency"},
        {"RitualRiteContainsOrbsOfAnnulment",  "Orb of Annulment",    RewardCat::Bounty, "Currency"},
        {"RitualRiteContainsOrbsOfAnnulment2", "Orbs of Annulment x2",RewardCat::Bounty, "Currency"},
        {"RitualRiteContainsOrbsOfChance",     "Orb of Chance",       RewardCat::Bounty, "Currency"},
        {"RitualRiteContainsOrbsOfChance2",    "Orbs of Chance x2",   RewardCat::Bounty, "Currency"},

        // --- Bounty: this map's Ritual contains Greater/Perfect crafting orbs (22) ---
        {"RitualRiteContainsGreaterAugmentation2",  "Greater Augmentation x2",  RewardCat::Bounty, "Crafting"},
        {"RitualRiteContainsGreaterAugmentation4",  "Greater Augmentation x4",  RewardCat::Bounty, "Crafting"},
        {"RitualRiteContainsGreaterAugmentation8",  "Greater Augmentation x8",  RewardCat::Bounty, "Crafting"},
        {"RitualRiteContainsGreaterTransmutation2", "Greater Transmutation x2", RewardCat::Bounty, "Crafting"},
        {"RitualRiteContainsGreaterTransmutation4", "Greater Transmutation x4", RewardCat::Bounty, "Crafting"},
        {"RitualRiteContainsGreaterTransmutation8", "Greater Transmutation x8", RewardCat::Bounty, "Crafting"},
        {"RitualRiteContainsGreaterChaos",          "Greater Chaos Orb",        RewardCat::Bounty, "Crafting"},
        {"RitualRiteContainsGreaterChaos2",         "Greater Chaos x2",         RewardCat::Bounty, "Crafting"},
        {"RitualRiteContainsGreaterChaos5",         "Greater Chaos x5",         RewardCat::Bounty, "Crafting"},
        {"RitualRiteContainsGreaterRegal",          "Greater Regal",            RewardCat::Bounty, "Crafting"},
        {"RitualRiteContainsGreaterRegal2",         "Greater Regal x2",         RewardCat::Bounty, "Crafting"},
        {"RitualRiteContainsGreaterRegal5",         "Greater Regal x5",         RewardCat::Bounty, "Crafting"},
        {"RitualRiteContainsGreaterExalt",          "Greater Exalt",            RewardCat::Bounty, "Crafting"},
        {"RitualRiteContainsGreaterExalt2",         "Greater Exalt x2",         RewardCat::Bounty, "Crafting"},
        {"RitualRiteContainsGreaterExalt5",         "Greater Exalt x5",         RewardCat::Bounty, "Crafting"},
        {"RitualRiteContainsPerfectAugmentation",   "Perfect Augmentation",     RewardCat::Bounty, "Crafting"},
        {"RitualRiteContainsPerfectAugmentation2",  "Perfect Augmentation x2",  RewardCat::Bounty, "Crafting"},
        {"RitualRiteContainsPerfectTransmutation",  "Perfect Transmutation",    RewardCat::Bounty, "Crafting"},
        {"RitualRiteContainsPerfectTransmutation2", "Perfect Transmutation x2", RewardCat::Bounty, "Crafting"},
        {"RitualRiteContainsPerfectChaos",          "Perfect Chaos Orb",        RewardCat::Bounty, "Crafting"},
        {"RitualRiteContainsPerfectRegal",          "Perfect Regal Orb",        RewardCat::Bounty, "Crafting"},
        {"RitualRiteContainsPerfectExalt",          "Perfect Exalt Orb",        RewardCat::Bounty, "Crafting"},
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

// Short label for the overlay/picker. Falls back to a prettified id for unknown mods
// (so a future game-added reward still renders readably).
inline std::string RewardLabel(const std::string& modId) {
    static const std::unordered_map<std::string, std::string> kMap = [] {
        std::unordered_map<std::string, std::string> m;
        for (const auto& d : AllRewards()) m.emplace(d.id, d.label);
        return m;
    }();
    auto it = kMap.find(modId);
    if (it != kMap.end()) return it->second;

    // Fallback: strip known prefixes and space-out CamelCase.
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
