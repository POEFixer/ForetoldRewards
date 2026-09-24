// FR_Core.h — Foretold Rewards plugin: cached memory model + offline reward calc.
//
// Mechanic (RE-confirmed 2026-06-14, PoE2 0.5.2):
//   reward(map M revealed by anchor A at path-step k) =
//       g(lineSeed, a3=k, a4 = rank of M in sorted(A.neighbors \ already-selected))
//   a3 = selected-anchor count (selVecB size), a4 in 0..4 (<=5 neighbour slots),
//   path = connected non-repeating chain. Only record marker != 0x47B maps show a reward.
//
// All atlas panel data (nodes / per-anchor adjacency / selection vectors / Rite
// line seed / eligibility weights / node names) is read through the host Atlas
// service (ctx->Atlas) — this plugin carries no raw atlas memory offsets.
//
// Perf: connTable adjacency, per-map markers and the path cap are STATIC per
// atlas line — cached and only rebuilt when lineSeed changes. Update() re-reads
// only the volatile parts (node positions, selection) each throttled tick; the
// per-node record/UI-state detail pass runs only on marker-cache misses, so the
// steady-state tick stays at the cheap BASIC enumeration (one bulk read).
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <chrono>

#include "plugin_sdk/PluginSDK.h"
#include "FR_Calc.h"

namespace FR {

// Rewards are computed offline by the reimplementation in FR_Calc.h (validated
// 25/25 against the game's own results); the plugin reads game state only
// through the SDK services.

constexpr int       WEIGHT_CAP_KEY   = 26381;   // map_ritual_rite_additional_maps, runtime Stats row + 1 (0.5.5)

constexpr uint32_t  MARKER_PROLIF    = 0x461;   // historical; reward maps span several markers
constexpr uint32_t  MARKER_NONREWARD = 0x47B;   // line map type that does NOT display a foretold reward

constexpr int A4_MAX     = 4;
constexpr int A3_HARDMAX = 16;

struct Grid {
    int x = 0, y = 0;
    bool operator==(const Grid& o) const { return x == o.x && y == o.y; }
    bool nonzero() const { return x != 0 || y != 0; }
};
inline uint64_t GridKey(int x, int y) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(y);
}
inline uint64_t GridKey(const Grid& g) { return GridKey(g.x, g.y); }

// screen_x = ax*grid_x + bx ; screen_y = ay*grid_y + by ; icon size (w,h) const per zoom.
struct AtlasTransform {
    bool   valid = false;
    double ax = 0, bx = 0, ay = 0, by = 0;
    float  w = 0, h = 0;
};

// Ordinary least squares for s = m*g + c over paired samples. Returns false if
// g has no variance (cannot separate slope). maxResidual = worst |pred - s|.
inline bool FitLinear(const std::vector<double>& g, const std::vector<double>& s,
                      double& m, double& c, double& maxResidual) {
    size_t n = g.size();
    if (n < 2 || s.size() != n) return false;
    double sg = 0, ss = 0, sgg = 0, sgs = 0;
    for (size_t i = 0; i < n; ++i) { sg += g[i]; ss += s[i]; sgg += g[i]*g[i]; sgs += g[i]*s[i]; }
    double denom = (double)n * sgg - sg * sg;
    if (denom > -1e-9 && denom < 1e-9) return false;   // no variance in g
    m = ((double)n * sgs - sg * ss) / denom;
    c = (ss - m * sg) / (double)n;
    maxResidual = 0;
    for (size_t i = 0; i < n; ++i) {
        double r = std::fabs((m * g[i] + c) - s[i]);
        if (r > maxResidual) maxResidual = r;
    }
    return true;
}

struct ConnEntry { Grid key; std::vector<Grid> neighbors; };

struct NodeInfo {
    Grid      grid;
    uintptr_t uiPtr = 0;
    uint32_t  marker = 0;
    bool isProlif()  const { return marker == MARKER_PROLIF; }
    // A clickable line map (part of the Rite line / a valid path anchor).
    bool isLineMap() const { return marker != 0; }
    // A map that DISPLAYS a foretold reward when a neighbour is selected.
    bool isReward()  const { return marker != 0 && marker != MARKER_NONREWARD; }
};

struct Reward { std::vector<std::string> mods; bool empty() const { return mods.empty(); } };

inline double MsSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
        std::chrono::steady_clock::now() - t0).count();
}

// Volatile per-tick state (positions + selection). Static line data lives in Core.
struct AtlasSnapshot {
    bool      valid = false;
    uint32_t  lineSeed = 0;
    int       pathCap = 6;
    std::vector<NodeInfo>               nodes;
    std::map<uint64_t, const NodeInfo*> byGrid;
    std::vector<Grid>                   selectedB;
    std::unordered_set<uint64_t>        revealedA;   // selVecA grids = revealed Rite line ("purple")
};

class Core {
public:
    void Bind(const PluginSDK::Context* ctx) { m_ctx = ctx; }
    bool Bound() const { return m_ctx != nullptr; }

    // --- perf telemetry (read by the plugin Performance tab; benign cross-thread reads) ---
    double perfUpdateMs = 0, perfConnMs = 0, perfNodesMs = 0, perfSelMs = 0;
    int    perfNodeCount = 0, perfConnCount = 0, perfRevealedCount = 0;
    mutable double perfInjectMs = 0;
    mutable int    perfWeightCount = 0, perfComboCount = 0;

    std::string NodeName(uintptr_t uiPtr) const {
        if (!uiPtr || !m_ctx) return {};
        return m_ctx->Atlas.GetNodeName(uiPtr);   // "" when unresolved (same as before)
    }

    // Biome of a grid from the last detail pass; -1 = unknown. Biome is static
    // per map, so a cached value never goes stale within a line (the cache is
    // cleared together with the marker cache on line change).
    int CachedBiome(uint64_t grid) const {
        auto it = m_biomeCache.find(grid);
        return it != m_biomeCache.end() ? it->second : -1;
    }

    // Refresh volatile state; rebuild static caches only on line change.
    bool Update() {
        auto _u0 = std::chrono::steady_clock::now();
        m_snap = AtlasSnapshot{};
        if (!m_ctx) { perfUpdateMs = FR::MsSince(_u0); return false; }

        // GATE: only load/compute when a Rite of the Nameless line is active — i.e. the reward-
        // selection atlas ("Select N maps") is open, where the panel's lineSeed is non-zero.
        // GetLineSeed() returns 0 both when the panel is absent and when no line is applied.
        // On a normal atlas, in the hideout, or before the line is applied there are no foretold
        // rewards, so skip ALL heavy work (conn table + ~thousands of node reads + selVecA/B) and,
        // crucially, never cache markers/names from a non-Rite or half-loaded transient state.
        // snap stays invalid -> hover / Ctrl+Alt-lock / search are all no-ops until the Rite atlas
        // is open. This is also why starting the plugin in the hideout and opening the atlas later
        // now works: nothing is read or cached until the real reward-selection atlas appears.
        m_snap.lineSeed = m_ctx->Atlas.GetLineSeed();
        if (m_snap.lineSeed == 0) { perfUpdateMs = FR::MsSince(_u0); return false; }

        // connTable is VIEWPORT-DEPENDENT (it grows/shrinks with the loaded atlas
        // nodes — 388 zoomed-in vs 876 zoomed-out), so rebuild it every update.
        auto _c0 = std::chrono::steady_clock::now();
        BuildConnCache();
        perfConnMs = FR::MsSince(_c0);
        // per-line caches: path cap (per line) and markers (per grid, stable; they
        // accumulate as you pan and reset only when the line changes).
        if (m_snap.lineSeed != 0 && (!m_haveCache || m_snap.lineSeed != m_cacheSeed)) {
            m_pathCapCache = std::clamp(Weight(WEIGHT_CAP_KEY) + 5, 1, A3_HARDMAX);
            m_markerCache.clear();
            m_biomeCache.clear();
            m_cacheSeed = m_snap.lineSeed;
            m_haveCache = true;
        }
        m_snap.pathCap = m_pathCapCache;

        // volatile: atlas nodes — BASIC enumeration (one bulk read host-side) + cached markers.
        auto _n0 = std::chrono::steady_clock::now();
        std::vector<PluginSDK::AtlasNode> nodes = m_ctx->Atlas.Nodes(PSDK_ATLAS_NODE_BASIC);

        // Marker-cache misses that need a live record read (extra != 0 — an
        // extra==0 miss resolves to marker 0 without any read, exactly as the
        // old per-miss read did). Any such miss triggers ONE transient detail
        // pass below; steady state (all hits) stays BASIC-only.
        bool needDetail = false;
        for (const auto& n : nodes) {
            if (!n.extraAddress) continue;
            auto mit = m_markerCache.find(GridKey(n.gridX, n.gridY));
            if (mit == m_markerCache.end() ||
                (mit->second.first == 0 && mit->second.second != n.extraAddress)) {
                needDetail = true;
                break;
            }
        }
        // Transient detail pass: record markers + UI biome (the biome rides along
        // to feed CachedBiome — it is static per map, so filling it here keeps
        // hover eligibility off the per-frame read path). Markers are keyed by
        // {grid, extra} so a duplicate grid entry resolves against the exact
        // record it was read from (same invariant as the old inline read).
        std::unordered_map<uint64_t, std::vector<std::pair<uintptr_t, uint32_t>>> freshMarkers;
        if (needDetail) {
            std::vector<PluginSDK::AtlasNode> det = m_ctx->Atlas.Nodes(
                PSDK_ATLAS_NODE_BASIC | PSDK_ATLAS_NODE_RECORD | PSDK_ATLAS_NODE_UISTATE);
            freshMarkers.reserve(det.size());
            for (const auto& d : det) {
                uint64_t gk = GridKey(d.gridX, d.gridY);
                if (d.extraAddress) freshMarkers[gk].emplace_back(d.extraAddress, d.marker);
                if (d.uiAddress)    m_biomeCache[gk] = d.biome;
            }
        }
        m_snap.nodes.reserve(nodes.size());
        for (const auto& n : nodes) {
            NodeInfo ni;
            ni.grid.x = n.gridX;
            ni.grid.y = n.gridY;
            ni.uiPtr  = n.uiAddress;
            uintptr_t extra = n.extraAddress;
            uint64_t gk = GridKey(ni.grid);
            auto mit = m_markerCache.find(gk);
            // {marker, extraPtr}. A non-zero marker is the map's stable line-map type ->
            // keep it cached. A zero is transient: the same grid first appears as an
            // OFF-REGION / not-yet-loaded duplicate entry (null/stale ExtraPtr -> 0)
            // before you pan to it. Re-read a cached zero when the entry's ExtraPtr
            // changed (off-region -> real node) so the real marker resolves on travel.
            // Caching the 0 forever (old bug) left far maps as non-line-maps until a
            // restart -> no hover/Ctrl+Alt rect. A stable zero (same ExtraPtr) isn't re-read.
            if (mit != m_markerCache.end() &&
                (mit->second.first != 0 || mit->second.second == extra)) {
                ni.marker = mit->second.first;
            } else if (!extra) {
                ni.marker = 0;
                m_markerCache[gk] = { 0, 0 };
            } else {
                // Miss with a live record: take the detail-pass marker read from
                // THIS extra. A between-pass vanish leaves the miss UNcached so
                // it re-resolves next tick (never freeze a transient zero).
                bool resolved = false;
                auto fit = freshMarkers.find(gk);
                if (fit != freshMarkers.end()) {
                    for (const auto& pr : fit->second) {
                        if (pr.first == extra) {
                            ni.marker = pr.second;
                            m_markerCache[gk] = { pr.second, extra };
                            resolved = true;
                            break;
                        }
                    }
                }
                if (!resolved) ni.marker = 0;
            }
            m_snap.nodes.push_back(ni);
        }
        for (auto& n : m_snap.nodes) m_snap.byGrid[GridKey(n.grid)] = &n;
        perfNodesMs = FR::MsSince(_n0);

        // volatile: selVecA = revealed Rite-line maps (the "purple" set)
        auto _s0 = std::chrono::steady_clock::now();
        for (const auto& g : m_ctx->Atlas.Selection(0))
            m_snap.revealedA.insert(GridKey(g.x, g.y));

        // volatile: selVecB (picked anchors, in pick order)
        for (const auto& g : m_ctx->Atlas.Selection(1))
            m_snap.selectedB.push_back(Grid{ g.x, g.y });

        perfSelMs = FR::MsSince(_s0);

        perfNodeCount     = (int)m_snap.nodes.size();
        perfConnCount     = (int)m_connCache.size();
        perfRevealedCount = (int)m_snap.revealedA.size();
        perfUpdateMs      = FR::MsSince(_u0);
        m_snap.valid = true;
        return true;
    }

    const AtlasSnapshot& Snap() const { return m_snap; }
    const std::map<uint64_t, ConnEntry>& Conn() const { return m_connCache; }

    static std::vector<Grid> SortedNeighbors(const ConnEntry& anchor, const std::vector<Grid>& excluded) {
        std::vector<Grid> v;
        for (const Grid& g : anchor.neighbors) {
            bool drop = false;
            for (const Grid& ex : excluded) if (ex == g) { drop = true; break; }
            if (!drop) v.push_back(g);
        }
        std::sort(v.begin(), v.end(), [](const Grid& a, const Grid& b) {
            if (a.x != b.x) return a.x < b.x; return a.y < b.y;
        });
        return v;
    }

    // ---- OFFLINE reward calc (NO injection): bit-exact reimplementation in FR_Calc.h.
    // Reads the live eligibility weights (the v57 data the game builds server-side),
    // dedup-sums them, then runs the WELL512 weighted reservoir per (a3,a4) over the
    // baked pool. Validated 25/25 vs the game's own fn. ----
    bool ComputeRewardTable(uint32_t lineSeed, int a3max,
                            std::vector<std::vector<Reward>>& out) const {
        auto _i0 = std::chrono::steady_clock::now();
        perfInjectMs = 0; perfWeightCount = 0; perfComboCount = 0;
        out.assign(static_cast<size_t>(a3max) + 1, std::vector<Reward>(A4_MAX + 1));
        if (!m_ctx) return false;

        std::vector<PluginSDK::AtlasWeight> rows = m_ctx->Atlas.Weights();   // one bulk read host-side
        if (rows.empty()) return false;
        std::map<int32_t, int32_t> wmap;
        for (const auto& w : rows) wmap[w.key] += w.value;
        perfWeightCount = (int)wmap.size();
        perfComboCount  = a3max * (A4_MAX + 1);

        RewardCalc calc;
        calc.SetWeights(std::move(wmap));
        for (int a3 = 1; a3 <= a3max; ++a3)
            for (int a4 = 0; a4 <= A4_MAX; ++a4)
                out[a3][a4].mods = calc.Compute(lineSeed, a3, a4);

        perfInjectMs = FR::MsSince(_i0);
        return true;
    }

    // ---- OFFLINE pool readout (NO injection): the route-independent set of rewards this
    // line can drop. Reads the live weights (one bulk read) and runs the pure FR::BuildPool
    // gate over the baked pool. Independent of (a3,a4)/selection — fixed per Head of the King. ----
    bool ComputePool(uint32_t lineSeed, std::vector<FR::PoolEntry>& out) const {
        out.clear();
        if (!m_ctx || lineSeed == 0) return false;
        std::vector<PluginSDK::AtlasWeight> rows = m_ctx->Atlas.Weights();
        if (rows.empty()) return false;
        std::map<int32_t, int32_t> wmap;
        for (const auto& w : rows) wmap[w.key] += w.value;
        FR::BuildPool(wmap, out);
        return true;
    }

    // Deduped reward ids OBTAINABLE on this line: every distinct g(lineSeed,a3,a4) over
    // a3 in [1..a3max], a4 in [0..a4max]. In-game a4 is NOT a 0..4 neighbour rank — it is a
    // reveal-running-index that grows with a3 (CE-proven: g(5,*) hit the live reward at a4=6/8/11/14),
    // so a4max must be wide (~40) to cover the obtainable set. a3max should be the line's pathCap
    // (deeper isn't reachable). Map exclusions (completed/biome) constrain ROUTING, not the reward
    // space, so they don't apply here. One bulk weights read. Exact for the first ring; approximate at
    // depth (the reward fn diverges from the live game for deep multi-anchor reveals).
    bool EnumerateObtainableRewards(uint32_t lineSeed, int a3max, int a4max,
                                    std::vector<std::string>& outIds) const {
        outIds.clear();
        if (!m_ctx || lineSeed == 0) return false;
        std::vector<PluginSDK::AtlasWeight> rows = m_ctx->Atlas.Weights();
        if (rows.empty()) return false;
        std::map<int32_t, int32_t> wmap;
        for (const auto& w : rows) wmap[w.key] += w.value;
        FR::RewardCalc calc;
        calc.SetWeights(std::move(wmap));
        if (a3max < 1) a3max = 1;
        if (a3max > A3_HARDMAX) a3max = A3_HARDMAX;
        if (a4max < 0) a4max = 0;
        std::unordered_set<std::string> seen;
        for (int a3 = 1; a3 <= a3max; ++a3)
            for (int a4 = 0; a4 <= a4max; ++a4)
                for (const auto& id : calc.Compute(lineSeed, a3, a4))
                    if (seen.insert(id).second) outIds.push_back(id);
        return true;
    }

private:
    // The game sums every raw row for a stat before deriving the path cap. Two
    // separate +1 sources must therefore allow START plus six reward maps.
    int32_t Weight(int32_t key) const {
        if (!m_ctx) return 0;
        int32_t value = 0;
        for (const auto& w : m_ctx->Atlas.Weights())
            if (w.key == key) value += w.value;
        return value;
    }
    void BuildConnCache() {
        m_connCache.clear();
        if (!m_ctx) return;
        for (const auto& c : m_ctx->Atlas.Connections()) {   // zero slots already dropped, slot order preserved
            ConnEntry ce2;
            ce2.key.x = c.x;
            ce2.key.y = c.y;
            ce2.neighbors.reserve(c.neighbors.size());
            for (const auto& nb : c.neighbors) ce2.neighbors.push_back(Grid{ nb.first, nb.second });
            m_connCache[GridKey(ce2.key)] = std::move(ce2);
        }
    }

    const PluginSDK::Context* m_ctx = nullptr;
    AtlasSnapshot m_snap;
    std::map<uint64_t, ConnEntry> m_connCache;
    // grid -> {marker, extraPtr}; see Update(). Non-zero markers cached permanently (stable line-map
    // type); a zero is re-read when the entry's ExtraPtr changes, so a far map resolves on travel
    // instead of staying a non-line-map until restart (the off-region stale-duplicate bug). Cleared on
    // line change.
    std::unordered_map<uint64_t, std::pair<uint32_t, uintptr_t>> m_markerCache;
    // grid -> biome id (static per map); filled by Update()'s detail pass, cleared with the marker
    // cache on line change. Feeds CachedBiome() for hover eligibility.
    std::unordered_map<uint64_t, int> m_biomeCache;
    int m_pathCapCache = 6;
    uint32_t m_cacheSeed = 0;
    bool m_haveCache = false;
};

} // namespace FR
