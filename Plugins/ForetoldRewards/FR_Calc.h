// FR_Calc.h — OFFLINE bit-exact reimplementation of the foretold reward fn (NO injection).
//
// The game computes each foretold reward as a PURE function of {lineSeed, a3, a4, count}
// over a weighted pool + an eligibility weights table (v57), reverse-engineered from the
// 0.5.x client (outer loop, selector, seed init, WELL512 draws). This file reproduces it
// EXACTLY: validated 25/25 (a3=1..5 x a4=0..4, incl the compound second-reward cells)
// against the game's own results for lineSeed 0x29A394F2 (2026-06-15).
//
//   * PRNG  = a 4-word WELL512 variant (state words W[0..3]; a counter word exists in the
//             game but is never read, so it is dropped here).
//   * seed  = init_by_array-style: 7 mixing rounds (x1664525, +seed[0..3]) + 4 rounds
//             (x1566083941, -k) + 8 WELL steps (the inline tempering).
//   * pick  = streaming weighted reservoir over the eligible pool rows (draw a uniform in
//             [0, runningTotal); select the row if draw < rowWeight).
//   * elig  = pool row eligible iff it has no condition OR v57.lookup(condKey) != 0.
//   * 2nd   = with prob v57.lookup(26382)% (d100 on a SEPARATE seed), a second reward is
//             picked with count=1, excluding every row whose Mod shares a stat with the primary.
//
// The pool (weights/conditions/reward ids) is STATIC game data baked below from a live dump
// (Data/Balance/RitualAtlasLineMods.datc64, 67 rows). v57 is read live each call (it depends
// on the active atlas/character state). If a balance patch changes RitualAtlasLineMods, the
// baked pool must be re-dumped from Data/Balance/RitualAtlasLineMods.datc64.
//
// 0.5.4 (2026-06-25): the game's stat-id table shifted +3, so the three v57 stat keys this
// plugin depends on all moved +3 — unique-gate condKey 25949->25952, compound% 26377->26380
// (CALC_KEY_COMPOUND below), and pathCap 26376->26379 (FR_Core::WEIGHT_CAP_KEY). Reward
// weights / ids / row order in RitualAtlasLineMods.datc64 were UNCHANGED (diffed vs the
// unpacked 0.5.4 datc64: 0 weight/ref diffs). Confirmed live: 25952/26379/26380 are present
// in v57, the old 25949/26376/26377 are absent.
//
// 0.5.5 (2026-09-08): stat rows moved another +2. Current runtime keys are unique-gate
// 25954, path cap 26381 and compound chance 26382 (Stats.dat row + 1). All 67 pool
// weights, Mod ids and stat groups are unchanged; their rewardStatRid values moved +2.
// Rechecked against today's IDA database and matching live code bytes. The game's
// cap is 5 + the SUM of additional-map stat rows: START plus 4/5/6 reward maps.
#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <map>

namespace FR {

// ---- baked pool: RitualAtlasLineMods.datc64, in datc64 ROW ORDER (NOT the grouped order
// of FR_RewardText::AllRewards). weight = row+0x10; condKey = 0 (always eligible) or the
// row's condition foreign-key (= row index in the condition dat + 1); rewardStatRid is the
// zero-based Stats.datc64 row referenced by the primary Mod. The game prevents a compound
// reward from selecting another Mod that shares this stat. All 67 current Mods have exactly
// one stat; re-dump this table if RitualAtlasLineMods or its referenced Mods change. ----
struct CalcPoolRow { int32_t weight; int32_t condKey; int32_t rewardStatRid; const char* id; };

inline const std::vector<CalcPoolRow>& CalcPool() {
    static const std::vector<CalcPoolRow> k = {
        /* 0*/ {65000,    0, 25999, "RitualRiteOtherMapsIncreasedTribute"},
        /* 1*/ {48750,    0, 26000, "RitualRiteOtherMapsFreeRerolls"},
        /* 2*/ {65000,    0, 26001, "RitualRiteOtherMapsRerollCost"},
        /* 3*/ {0,        0, 26002, "RitualRiteOtherMapsAdditionalRerolls"},
        /* 4*/ {65000,    0, 26735, "RitualRiteOtherMapsAdditionalPacks"},
        /* 5*/ {65000,    0, 26736, "RitualRiteOtherMapsRewardsAmount"},
        /* 6*/ {1800,     0, 26656, "RitualRiteContainsVeryRareUnique"},
        /* 7*/ {30,   25954, 26004, "RitualRiteContainsMageblood"},
        /* 8*/ {30,   25954, 26647, "RitualRiteContainsHeadhunter"},
        /* 9*/ {30,   25954, 26648, "RitualRiteContainsKalandrasTouch"},
        /*10*/ {120,  25954, 26649, "RitualRiteContainsQueenOfTheForest"},
        /*11*/ {120,  25954, 26650, "RitualRiteContainsAlphasHowl"},
        /*12*/ {120,  25954, 26651, "RitualRiteContainsDefianceOfDestiny"},
        /*13*/ {120,  25954, 26652, "RitualRiteContainsYokeOfSuffering"},
        /*14*/ {120,  25954, 26653, "RitualRiteContainsAstramentis"},
        /*15*/ {120,  25954, 26654, "RitualRiteContainsDreamFragments"},
        /*16*/ {120,  25954, 26655, "RitualRiteContainsOriginalSin"},
        /*17*/ {0,        0, 26005, "RitualRiteContainsOmenGreaterAnnulment"},
        /*18*/ {18900,    0, 26669, "RitualRiteContainsOmenAmelioration"},
        /*19*/ {3500,     0, 26670, "RitualRiteContainsOmenWhittling"},
        /*20*/ {700,      0, 26671, "RitualRiteContainsOmenSinistralErasure"},
        /*21*/ {700,      0, 26672, "RitualRiteContainsOmenDextralErasure"},
        /*22*/ {0,        0, 26673, "RitualRiteContainsOmenCorruption"},
        /*23*/ {28350,    0, 26674, "RitualRiteContainsOmenSinistralExaltation"},
        /*24*/ {28350,    0, 26675, "RitualRiteContainsOmenDextralExaltation"},
        /*25*/ {700,      0, 26676, "RitualRiteContainsOmenSinistralAnnulment"},
        /*26*/ {700,      0, 26677, "RitualRiteContainsOmenDextralAnnulment"},
        /*27*/ {3500,     0, 26688, "RitualRiteContainsOmenBlessed"},
        /*28*/ {140,      0, 26689, "RitualRiteContainsOmenChance"},
        /*29*/ {700,      0, 26690, "RitualRiteContainsOmenSanctification"},
        /*30*/ {3500,     0, 26691, "RitualRiteContainsOmenDextralCrystallisation"},
        /*31*/ {3500,     0, 26692, "RitualRiteContainsOmenSinistralCrystallisation"},
        /*32*/ {10000,    0, 26003, "RitualRiteContainsDivineOrbs"},
        /*33*/ {5000,     0, 26003, "RitualRiteContainsDivineOrbs2"},
        /*34*/ {2000,     0, 26003, "RitualRiteContainsDivineOrbs5"},
        /*35*/ {20833,    0, 26006, "RitualRiteContainsChaosOrbs2"},
        /*36*/ {10417,    0, 26006, "RitualRiteContainsChaosOrbs4"},
        /*37*/ {5208,     0, 26006, "RitualRiteContainsChaosOrbs8"},
        /*38*/ {100000,   0, 26007, "RitualRiteContainsExaltedOrbs2"},
        /*39*/ {50000,    0, 26007, "RitualRiteContainsExaltedOrbs4"},
        /*40*/ {25000,    0, 26007, "RitualRiteContainsExaltedOrbs8"},
        /*41*/ {2500,     0, 26657, "RitualRiteContainsOrbsOfAnnulment"},
        /*42*/ {1250,     0, 26657, "RitualRiteContainsOrbsOfAnnulment2"},
        /*43*/ {2500,     0, 26658, "RitualRiteContainsOrbsOfChance"},
        /*44*/ {1250,     0, 26658, "RitualRiteContainsOrbsOfChance2"},
        /*45*/ {20400,    0, 26659, "RitualRiteContainsGreaterAugmentation2"},
        /*46*/ {10200,    0, 26659, "RitualRiteContainsGreaterAugmentation4"},
        /*47*/ {5100,     0, 26659, "RitualRiteContainsGreaterAugmentation8"},
        /*48*/ {11017,    0, 26660, "RitualRiteContainsGreaterTransmutation2"},
        /*49*/ {5508,     0, 26660, "RitualRiteContainsGreaterTransmutation4"},
        /*50*/ {2754,     0, 26660, "RitualRiteContainsGreaterTransmutation8"},
        /*51*/ {1792,     0, 26661, "RitualRiteContainsGreaterChaos"},
        /*52*/ {896,      0, 26661, "RitualRiteContainsGreaterChaos2"},
        /*53*/ {358,      0, 26661, "RitualRiteContainsGreaterChaos5"},
        /*54*/ {5733,     0, 26662, "RitualRiteContainsGreaterRegal"},
        /*55*/ {2867,     0, 26662, "RitualRiteContainsGreaterRegal2"},
        /*56*/ {1147,     0, 26662, "RitualRiteContainsGreaterRegal5"},
        /*57*/ {3480,     0, 26663, "RitualRiteContainsGreaterExalt"},
        /*58*/ {1740,     0, 26663, "RitualRiteContainsGreaterExalt2"},
        /*59*/ {696,      0, 26663, "RitualRiteContainsGreaterExalt5"},
        /*60*/ {6100,     0, 26664, "RitualRiteContainsPerfectAugmentation"},
        /*61*/ {3050,     0, 26664, "RitualRiteContainsPerfectAugmentation2"},
        /*62*/ {3300,     0, 26665, "RitualRiteContainsPerfectTransmutation"},
        /*63*/ {1650,     0, 26665, "RitualRiteContainsPerfectTransmutation2"},
        /*64*/ {438,      0, 26666, "RitualRiteContainsPerfectChaos"},
        /*65*/ {1400,     0, 26667, "RitualRiteContainsPerfectRegal"},
        /*66*/ {420,      0, 26668, "RitualRiteContainsPerfectExalt"},
    };
    return k;
}

// One eligible reward in the current line's pool (route-independent; see BuildPool).
struct PoolEntry {
    std::string id;             // RitualRite* mod id (matches FR_RewardText / CalcPool)
    int32_t     weight = 0;     // static row weight (CalcPoolRow::weight)
    double      probability = 0.0;  // weight / sum(eligible weights) = single-draw odds
};

// Pure: given the live v57 (key -> summed value), return the eligible pool rows + their
// per-draw probability. A row is eligible iff weight>0 AND (condKey==0 OR v57[condKey]!=0)
// — exactly RewardCalc::PickRow's gate. Independent of (a3,a4)/route: this is THE pool.
inline void BuildPool(const std::map<int32_t, int32_t>& v57, std::vector<PoolEntry>& out) {
    out.clear();
    const auto& pool = CalcPool();
    auto lookup = [&](int32_t k) -> int32_t {
        auto it = v57.find(k);
        return (it == v57.end()) ? 0 : it->second;
    };
    uint64_t total = 0;
    for (const auto& r : pool) {
        if (r.weight <= 0) continue;
        bool eligible = (r.condKey == 0) || (lookup(r.condKey) != 0);
        if (!eligible) continue;
        total += static_cast<uint32_t>(r.weight);
        PoolEntry e; e.id = r.id; e.weight = r.weight;
        out.push_back(std::move(e));
    }
    if (total > 0)
        for (auto& e : out)
            e.probability = static_cast<double>(static_cast<uint32_t>(e.weight)) / static_cast<double>(total);
}

constexpr int32_t CALC_KEY_COMPOUND = 26382;          // map_ritual_rite_additional_modifier_chance_%, runtime Stats row + 1 (0.5.5)
constexpr uint32_t CALC_GATE_SEED4  = 0x91DA3AD9u;    // 4th seed for the compound d100 = (uint32)-1847969063

// 4-word WELL512 step (one draw). Unsigned arithmetic is modular (matches the 32-bit game).
struct CalcWell {
    uint32_t w[4];
    uint32_t step() {
        uint32_t s1 = w[0], s2 = w[1], s3 = w[2], s4 = w[3];
        w[0] = s2;
        uint32_t v9  = s3 ^ s2 ^ (s1 & 0x7FFFFFFFu);
        uint32_t v10 = (s4 << 1) ^ s4;
        uint32_t v11 = v10 ^ v9 ^ (v9 >> 1);            w[3] = v11;
        uint32_t v12 = (v11 & 1u) ? 0xFFFFFFFFu : 0u;
        w[1] = s3 ^ (v12 & 0x8F7011EEu);
        uint32_t v13 = v10 ^ (v11 << 10) ^ (v12 & 0xFC78FF1Fu); w[2] = v13;
        uint32_t sum  = s2 + (v13 >> 8);
        uint32_t term = (sum & 1u) ? 0x3793FDFFu : 0u;
        return v11 ^ sum ^ term;
    }
};

inline CalcWell CalcSeedInit(uint32_t lineSeed, uint32_t a3, uint32_t a4, uint32_t count) {
    const uint32_t seed[4] = { lineSeed, a3, a4, count };
    uint32_t W[4] = { 0x40336052u, 0xCFA3723Cu, 0x3CAC5F71u, 0x3793FDFFu };
    int k = 1;
    for (int it = 0; it < 7; ++it) {                    // phase 1: x1664525 + seed[0..3]
        int m1 = (k + 1) & 3, m2 = (k + 2) & 3, m3 = (k + 3) & 3;
        uint32_t v8  = W[m1];
        uint32_t tmp = W[k] ^ W[m3] ^ v8;
        uint32_t v10 = 1664525u * (tmp ^ (tmp >> 27));
        W[m1] = v8 + v10;
        uint32_t t = (uint32_t)k + ((it < 4) ? seed[it] : 0u) + v10;
        W[m2] += t;
        W[k]   = t;
        k = m1;
    }
    for (int it = 0; it < 4; ++it) {                    // phase 2: x1566083941 - k
        int m1 = (k + 1) & 3, m2 = (k + 2) & 3, m3 = (k + 3) & 3;
        uint32_t v19 = W[m1];
        uint32_t s   = W[k] + v19 + W[m3];
        uint32_t p   = 1566083941u * (s ^ (s >> 27));
        uint32_t v22 = p - (uint32_t)k;
        W[m1] = p ^ v19;
        W[m2] ^= v22;
        W[k]   = v22;
        k = m1;
    }
    CalcWell wl{ { W[0], W[1], W[2], W[3] } };
    for (int i = 0; i < 8; ++i) wl.step();              // inline tempering = 8 WELL steps
    return wl;
}

// Unbiased uniform in [0, range) via rejection (matches the game's draw-B).
inline uint32_t CalcDrawB(CalcWell& W, uint32_t range) {
    if (range <= 1) return 0;
    const uint32_t q   = 0xFFFFFFFFu / range;
    const uint32_t rem = 0xFFFFFFFFu % range;
    for (;;) {
        uint32_t v = W.step();
        if (rem == range - 1 || v < q * range) return v % range;
    }
}

// Offline reward calculator. Feed it the live v57 (dedup-summed key->val), then Compute().
class RewardCalc {
public:
    void SetWeights(std::map<int32_t, int32_t> v57) { m_v57 = std::move(v57); }
    int32_t Lookup(int32_t key) const {
        auto it = m_v57.find(key);
        return (it == m_v57.end()) ? 0 : it->second;
    }

    // Reward mod-ids for (lineSeed, a3, a4): 1 entry, or 2 if the compound gate fires.
    std::vector<std::string> Compute(uint32_t lineSeed, int a3, int a4) const {
        std::vector<std::string> out;
        const auto& pool = CalcPool();
        int r0 = PickRow(lineSeed, (uint32_t)a3, (uint32_t)a4, 0u, -1);
        if (r0 < 0) return out;
        out.push_back(pool[(size_t)r0].id);

        int32_t chance = Lookup(CALC_KEY_COMPOUND);
        if (chance > 0) {
            bool second = (chance >= 100);
            if (!second) {
                CalcWell g = CalcSeedInit(lineSeed, (uint32_t)a3, (uint32_t)a4, CALC_GATE_SEED4);
                second = ((int32_t)CalcDrawB(g, 100) < chance);
            }
            if (second) {
                int r1 = PickRow(lineSeed, (uint32_t)a3, (uint32_t)a4, 1u,
                                 pool[(size_t)r0].rewardStatRid);
                if (r1 >= 0) out.push_back(pool[(size_t)r1].id);
            }
        }
        return out;
    }

private:
    // Streaming weighted reservoir over eligible pool rows. For a compound second pick,
    // exclude every row whose reward Mod shares the primary Mod's stat (the game's stat set).
    int PickRow(uint32_t lineSeed, uint32_t a3, uint32_t a4, uint32_t count,
                int32_t excludedStatRid) const {
        CalcWell W = CalcSeedInit(lineSeed, a3, a4, count);
        const auto& pool = CalcPool();
        uint32_t total = 0;
        int sel = -1;
        for (int i = 0; i < (int)pool.size(); ++i) {
            const CalcPoolRow& r = pool[(size_t)i];
            if (excludedStatRid >= 0 && r.rewardStatRid == excludedStatRid) continue;
            if (r.weight <= 0) continue;
            bool eligible = (r.condKey == 0) || (Lookup(r.condKey) != 0);
            if (!eligible) continue;
            total += (uint32_t)r.weight;
            if (CalcDrawB(W, total) < (uint32_t)r.weight) sel = i;
        }
        return sel;
    }

    std::map<int32_t, int32_t> m_v57;
};

} // namespace FR
