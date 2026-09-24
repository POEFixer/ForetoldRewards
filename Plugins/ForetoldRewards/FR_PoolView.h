// FR_PoolView.h — pure join of the eligible pool (FR_Calc::BuildPool output) with prices,
// then the tiered value sort: Mageblood/Headhunter first, Very Rare Unique below them, then
// every priced reward by chaos value (×qty) descending, unpriced rewards last. The price
// source is injected as a callback so this stays free of WinINet / price-loading (and unit-testable).
#pragma once

#include "FR_Calc.h"
#include "FR_RewardText.h"
#include <functional>
#include <algorithm>
#include <string>
#include <vector>

namespace FR {

struct PoolRow {
    std::string id;
    std::string label;
    std::string group;
    double      chaos = 0.0;   // unit price * qty, in chaos (0 if unpriceable / not found)
    int         qty = 1;
    bool        priced = false;
    bool        potential = false;  // chaos is a fixed PLACEHOLDER (Very Rare Unique), not a real price
    PriceTier   tier = PriceTier::Normal;
    double      probability = 0.0;
};

// priceLookup(poe2scoutName) -> chaos value PER UNIT (0 if missing).
inline std::vector<PoolRow> BuildPoolView(
        const std::vector<PoolEntry>& pool,
        const std::function<double(const std::string&)>& priceLookup,
        double divineInChaos = 0.0) {
    std::vector<PoolRow> rows;
    rows.reserve(pool.size());
    for (const auto& e : pool) {
        RewardPriceInfo pi = PriceInfoFor(e.id);
        PoolRow r;
        r.id = e.id;
        r.label = RewardLabel(e.id);
        r.group = RewardGroup(e.id);
        r.qty = pi.qty;
        r.tier = pi.tier;
        r.probability = e.probability;
        double unit = (pi.priceName && pi.priceName[0]) ? priceLookup(pi.priceName) : 0.0;
        r.chaos = unit * pi.qty;
        r.priced = (r.chaos > 0.0);
        if (!r.priced) {   // unpriceable reward: give "Very Rare Unique" a fixed POTENTIAL placeholder
            double pot = PotentialChaos(e.id, divineInChaos);
            if (pot > 0.0) { r.chaos = pot; r.potential = true; }
        }
        rows.push_back(std::move(r));
    }
    std::sort(rows.begin(), rows.end(), [](const PoolRow& a, const PoolRow& b) {
        if (a.tier != b.tier) return static_cast<int>(a.tier) > static_cast<int>(b.tier);
        bool av = a.priced || a.potential, bv = b.priced || b.potential;
        if (av != bv) return av;                           // value-bearing (real or potential) before "-"
        if (a.chaos != b.chaos) return a.chaos > b.chaos;  // value descending
        return a.label < b.label;                          // deterministic tiebreak
    });
    return rows;
}

} // namespace FR
