// ForetoldRewards — POE2Fixer plugin (SDK v6).
// "Rite of the Nameless" (Head of the King) foretold atlas rewards.
//
// LOCK & ROUTE: hover an atlas map to highlight it + see the Ctrl+Alt hint; press
// Ctrl+Alt to LOCK it as the START (no click). A background worker calculates the per-line
// reward table (once/line) and enumerates all linear chains (START + up to 6 reward maps)
// from the locked start. The window lists them (flat, filterable); clicking a row draws
// that chain on the live atlas (numbered lines 1->7 + a reward label above each map).
// Ctrl+Alt again / Esc unlocks.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>
#include <cmath>
#include <cctype>
#include <chrono>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <fstream>
#include <filesystem>
#include <set>

#include "plugin_sdk/PluginSDK.h"
#include "imgui/imgui.h"

#include "FR_Core.h"
#include "FR_RewardText.h"
#include "FR_PoolView.h"

using namespace PluginSDK;

// ---------------------------------------------------------------------------
// Minimal price-display helpers (previously from src/IPriceSource.h).
// Defined locally so the five deleted src/ headers aren't needed.
// ---------------------------------------------------------------------------
namespace PriceApi {
enum class DisplayCurrency : int { Divine = 0, Chaos = 2 };
inline std::string FormatPrice(float value, DisplayCurrency currency) {
    char buf[32];
    const char* suffix = (currency == DisplayCurrency::Divine) ? "D" : "C";
    if      (value >= 10000.0f) { float k = value / 1000.0f; if (k >= 10.0f) snprintf(buf, sizeof(buf), "%.0fk %s", k, suffix); else snprintf(buf, sizeof(buf), "%.1fk %s", k, suffix); }
    else if (value >= 1000.0f)  snprintf(buf, sizeof(buf), "%.1fk %s", value / 1000.0f, suffix);
    else if (value >= 100.0f)   snprintf(buf, sizeof(buf), "%.0f %s",  value, suffix);
    else if (value >= 1.0f)     snprintf(buf, sizeof(buf), "%.1f %s",  value, suffix);
    else if (value >= 0.01f)    snprintf(buf, sizeof(buf), "%.2f %s",  value, suffix);
    else                        snprintf(buf, sizeof(buf), "%.3f %s",  value, suffix);
    return buf;
}
} // namespace PriceApi

namespace {
FR::Grid FromKey(uint64_t k) {
    return FR::Grid{ (int)(int32_t)(uint32_t)(k >> 32), (int)(int32_t)(uint32_t)(k & 0xFFFFFFFF) };
}
constexpr int   PATH_HARD_CAP   = 200000;   // DFS exploration backstop
constexpr int   CHAIN_STORE_CAP = 60000;    // max chains kept for display
constexpr int   SEARCH_EXPLORE_CAP = 4000000; // search: DFS exploration backstop across all starts
constexpr int   SEARCH_RESULT_CAP  = 3000;    // search: max matching chains kept
constexpr int   SEARCH_BUDGET_MS   = 12000;   // search: wall-clock budget (ms)
constexpr int   OB_EXPLORE_CAP     = 4000000; // obtainable list scan: DFS exploration backstop (>= search, for completeness)
constexpr int   OB_BUDGET_MS       = 12000;   // obtainable list scan: wall-clock budget (ms) — matches search
constexpr int   OB_SETTLE_MS       = 1000;    // wait for the loaded-start set to stop growing before a (re)scan
constexpr int   OB_MAXWAIT_MS      = 6000;    // ...but (re)scan anyway this long after new starts first appear
constexpr float HIT_RADIUS      = 22.0f;    // node hit radius (rects can be ~0-size)
constexpr int   SETTLE_MS       = 90;       // rebuild node rects only after the view settles
constexpr int   MAX_RECTS        = 400;     // cap on-screen hover/coord rects (debug widens to 100s -> FPS)
constexpr int   MAX_COORD_LABELS = 250;     // cap per-frame "all coords" labels in debug
constexpr int   PALETTE_N   = 24;     // distinct overlay colors; cycles past this
constexpr int   MAX_STACK   = 6;      // max stacked reward chips per shared map (+K more)
constexpr float RIBBON_STEP = 2.0f;   // px perpendicular offset per path so shared edges fan out
const ImU32 kPalette[PALETTE_N] = {
    IM_COL32(235, 70, 70,255), IM_COL32(250,150, 40,255), IM_COL32(240,225, 60,255),
    IM_COL32(140,225, 70,255), IM_COL32( 70,205,110,255), IM_COL32( 50,205,200,255),
    IM_COL32( 80,160,255,255), IM_COL32(150,130,255,255), IM_COL32(200,110,255,255),
    IM_COL32(255,100,200,255), IM_COL32(255,150,150,255), IM_COL32(180,185,195,255),
    IM_COL32(160, 30, 30,255), IM_COL32(190, 95, 20,255), IM_COL32(170,160, 30,255),
    IM_COL32( 90,160, 40,255), IM_COL32( 30,140, 80,255), IM_COL32( 25,140,140,255),
    IM_COL32( 40,100,190,255), IM_COL32(100, 80,200,255), IM_COL32(140, 60,180,255),
    IM_COL32(200, 50,140,255), IM_COL32(120,200,255,255), IM_COL32(120,120,120,255),
};
// Priority palette for SEARCH results: term[0] is the most important / most expensive item, so it
// gets the hottest colour; the list cools down the spectrum. Only the first kPriorityCount search
// terms get a highlight colour — used both to COLOUR the matched reward label in the chain list and
// to ORDER results (term[0] matches shown first). Rewards past 12 / non-matches use the default colour.
constexpr int kPriorityCount = 12;
const ImU32 kPriorityPalette[kPriorityCount] = {
    IM_COL32(255, 75, 75,255), IM_COL32(255,140, 55,255), IM_COL32(255,195, 60,255),
    IM_COL32(240,230, 90,255), IM_COL32(175,230, 80,255), IM_COL32( 95,220,110,255),
    IM_COL32( 60,215,190,255), IM_COL32( 75,200,235,255), IM_COL32( 95,165,245,255),
    IM_COL32(135,135,245,255), IM_COL32(185,120,245,255), IM_COL32(240,110,210,255),
};
const ImU32 kRewardDefaultCol = IM_COL32(150,200,255,255);
// node UI map-state value (Atlas service UISTATE detail; GameLibrary MapStateByte):
// low 2 bits = map state
//   0=unavailable, 1=available, 2=completed+available, 3=completed+unavailable.
constexpr int       MAPSTATE_COMPLETED = 0x2;   // bit1 => completed / "Map Attempted"
constexpr int       BIOME_COUNT        = 13;
// Breach Stronghold (BiomeName[9]) is special in routing: a path may START on it (when revealed /
// purple) but may NOT pass THROUGH it as a reward node. Every other excluded biome blocks both.
// See ClassifyMapForRouting + IsEligible. (User-required rule, 2026-06-16.)
constexpr int       BIOME_BREACH_STRONGHOLD = 9;

const char* BiomeName(int id) {
    static const char* k[] = { "Water", "Mountain", "Grass", "Forest", "Swamp", "Desert",
        "City (6)", "City (7)", "City (8)", "Breach Stronghold", "Ocean", "Island", "Oriath" };
    return (id >= 0 && id < BIOME_COUNT) ? k[id] : "Unknown";
}

// Default seed for config/excluded_maps.txt on first run (Citadels, Halls, unique maps, Towers).
const char* const kDefaultExclExact[] = {
    "The Copper Citadel", "The Iron Citadel", "The Stone Citadel",
    "The Patriarch Halls", "The Matriarch Halls",
    "Untainted Paradise", "Vaults of Kamasa", "Moment of Zen", "Crux of Nothingness",
    "The Ezomyte Megaliths", "The Viridian Wildwood", "Castaway", "The Fractured Lake",
    "Bluff", "Mesa", "Sinking Spire", "Lost Towers", "Alpine Ridge", "Precursor Tower",
};

// name excluded iff exact-match in `exact` OR contains any substring in `contains`.
bool NameExcludedIn(const std::string& n, const std::unordered_set<std::string>& exact,
                    const std::vector<std::string>& contains) {
    if (n.empty()) return false;
    if (exact.count(n)) return true;
    for (const auto& s : contains) if (!s.empty() && n.find(s) != std::string::npos) return true;
    return false;
}

// ---- lightweight profiler: one bucket per hot path; atomic (worker + render threads) ----
struct ProfStat {
    std::atomic<uint64_t> calls{ 0 }, totalNs{ 0 }, maxNs{ 0 }, lastNs{ 0 };
    void add(uint64_t ns) {
        calls.fetch_add(1, std::memory_order_relaxed);
        totalNs.fetch_add(ns, std::memory_order_relaxed);
        lastNs.store(ns, std::memory_order_relaxed);
        uint64_t cur = maxNs.load(std::memory_order_relaxed);
        while (ns > cur && !maxNs.compare_exchange_weak(cur, ns, std::memory_order_relaxed)) {}
    }
    void     reset()  { calls.store(0); totalNs.store(0); maxNs.store(0); lastNs.store(0); }
    uint64_t n()      const { return calls.load(std::memory_order_relaxed); }
    double   lastMs() const { return lastNs.load(std::memory_order_relaxed) / 1e6; }
    double   maxMs()  const { return maxNs.load(std::memory_order_relaxed) / 1e6; }
    double   avgMs()  const { uint64_t c = n(); return c ? (double)totalNs.load(std::memory_order_relaxed) / c / 1e6 : 0.0; }
    double   avgUs()  const { uint64_t c = n(); return c ? (double)totalNs.load(std::memory_order_relaxed) / c / 1e3 : 0.0; }
    double   maxUs()  const { return maxNs.load(std::memory_order_relaxed) / 1e3; }
};
struct ScopedT {
    ProfStat* s; std::chrono::steady_clock::time_point t0;
    explicit ScopedT(ProfStat& st) : s(&st), t0(std::chrono::steady_clock::now()) {}
    ~ScopedT() { s->add((uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now() - t0).count()); }
};
struct Prof {
    ProfStat drawUI, coreUpdate, rebuildRects, csr, drawRoute, drawAllCoords,
             windowFilter, handleHover, pollResult, buildChains;
    void resetAll() {
        drawUI.reset(); coreUpdate.reset(); rebuildRects.reset(); csr.reset(); drawRoute.reset();
        drawAllCoords.reset(); windowFilter.reset(); handleHover.reset(); pollResult.reset(); buildChains.reset();
    }
};
} // namespace

class ForetoldRewardsPlugin : public Plugin {
public:
    const char* GetName() const override { return "Foretold Rewards"; }

    void OnEnable(bool) override { SetImGui(); m_core.Bind(ctx()); LoadExclusions(); ctx()->Log.Info("[Foretold] enabled"); }
    void OnDisable() override { if (ctx()) ctx()->Overlay.SetWantsOverlayInput(false); StopWorker(); ctx()->Log.Info("[Foretold] disabled"); }
    ~ForetoldRewardsPlugin() { StopWorker(); }

    void DrawSettings() override {
        if (ImGui::BeginTabBar("##fr_tabs")) {
            if (ImGui::BeginTabItem("Settings")) {
                ImGui::Checkbox("Show window", &m_showWindow);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("The window only appears on the Head of the King reward-selection\n"
                                      "atlas (the purple-glow Rite line). It stays hidden in the hideout,\n"
                                      "on a normal atlas, and before the line is applied.");
                ImGui::Checkbox("Hover mode", &m_hoverMode);
                ImGui::Checkbox("Debug overlay", &m_debug);
                if (m_debug) ImGui::Checkbox("Debug: show ALL map coords", &m_debugAllCoords);
                ImGui::TextDisabled("Hover an atlas map, press Ctrl+Alt to lock it as START.");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Prices")) {
                DrawPricesTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Exclusions")) {
                DrawExclusionsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Performance")) {
                DrawPerfTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    void DrawUI() override {
        SetImGui();
        if (!m_core.Bound()) m_core.Bind(ctx());
        bool inGame = ctx()->Game.IsInGame();
        if (!inGame) { ctx()->Overlay.SetWantsOverlayInput(false); return; }

        m_csrThisFrame = 0;
        {
            ScopedT _t(m_prof.drawUI);
            RefreshTick();                 // throttled snapshot; Snap().valid == Rite reward-selection atlas open
            // GATE all interaction on the Rite atlas being open. Snap().valid is true only when a Rite
            // of the Nameless line is active (Core::Update bails on lineSeed==0). So in the hideout, on
            // a normal atlas, or before the line is applied: NO hover, NO "Loading..." over the cursor,
            // NO route — the plugin is fully idle. The hover/route block (incl. the "Loading..." indicator
            // inside HandleHover) only runs here.
            bool riteOpen = m_core.Snap().valid;
            // The window only appears on the Head of the King reward-selection atlas (the "purple glow"
            // Rite of the Nameless line; Snap().valid is true only when lineSeed != 0). Off that atlas it
            // is fully hidden — no "open the Atlas" placeholder, and we don't grab overlay input just to
            // show an empty status window.
            bool windowVisible = m_showWindow && riteOpen;
            ctx()->Overlay.SetWantsOverlayInput(riteOpen && (m_showWindow || m_hoverMode));

            if (riteOpen) {
                AutoUnlockOnLineChange();      // drop the lock if the atlas line changed
                { ScopedT _p(m_prof.pollResult); PollResult(); }                 // collect a finished worker result
                if (m_hoverMode) { ScopedT _h(m_prof.handleHover); HandleHover(); } // overlay hover + Ctrl+Alt/Esc edges
                if (m_lockState == LockState::Ready && m_selectedChains.size() == 1) { ScopedT _r(m_prof.drawRoute); DrawRoute(m_selectedChains[0]); }
                else if (m_lockState == LockState::Ready && m_selectedChains.size() >= 2) { ScopedT _r(m_prof.drawRoute); DrawAllVariants(m_selectedChains); }
                if (m_debug && m_debugAllCoords && m_rectsFresh) { ScopedT _a(m_prof.drawAllCoords); DrawAllCoords(); }
            } else if (m_lockState != LockState::Idle) {
                Unlock();                      // left the Rite atlas -> drop any stale lock / route / overlay
            }
            if (windowVisible) DrawWindow();   // only on the Head of the King (Rite) atlas
        }
        m_csrLastFrame = m_csrThisFrame;
    }

    bool WantsOverlay() const override { return m_hoverMode || m_debug || m_lockState != LockState::Idle; }

private:
    enum class LockState { Idle, Computing, Ready };
    struct Rect { float x = 0, y = 0, w = 0, h = 0; };
    struct ChainNode { uint64_t grid = 0; std::string name; std::string reward; double value = 0.0; bool potential = false; };
    using Chain = std::vector<ChainNode>;
    struct VarLbl { ImU32 col = 0; std::string reward; };   // one stacked reward chip
    struct WorkerInput {
        uint64_t startGrid = 0;
        uint32_t lineSeed = 0;
        int      pathCap = 6;
        std::map<uint64_t, FR::ConnEntry>       conn;
        std::unordered_map<uint64_t, uint32_t>  marker;
        std::unordered_map<uint64_t, uintptr_t> uiPtr;
        std::unordered_set<uint64_t>            excluded;     // grids excluded by state (completed) or biome
        std::unordered_set<std::string>         exclExact;    // excluded map names (exact)
        std::vector<std::string>                exclContains; // excluded name substrings
        bool                                    search = false;  // search mode: loop `starts`, keep chains matching `terms`
        bool                                    bestValue = false; // keep only the max total-value chain(s) (ties shown together)
        bool                                    obtainable = false; // collect the route-reachable reward set (background list scan)
        bool                                    append = false;   // true => merge results into existing chains (live scan of NEW starts)
        std::vector<std::string>                terms;           // lowercased reward search terms (any-match)
        std::vector<uint64_t>                   starts;          // candidate start grids (purple, not excluded)
        std::unordered_set<uint64_t>            purple;          // selVecA (revealed Rite maps): the authoritative reward-map set for the DFS — record markers are unreliable when the atlas is opened in-map via the crown
    };

    void SetImGui() {
        if (ctx()->ImGuiContext)
            ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx()->ImGuiContext));
    }

    ImVec2 CursorClient() {
        POINT pt{};
        if (!::GetCursorPos(&pt)) return ImVec2(-1, -1);
        HWND hwnd = ctx()->Game.GetGameWindow();
        if (hwnd) ::ScreenToClient(hwnd, &pt);
        return ImVec2(static_cast<float>(pt.x), static_cast<float>(pt.y));
    }

    void RefreshTick() {
        auto now = std::chrono::steady_clock::now();
        if (m_haveTime &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastSnap).count() < m_throttleMs)
            return;
        m_lastSnap = now; m_haveTime = true;
        { ScopedT _u(m_prof.coreUpdate);   m_core.Update(); }
        { ScopedT _r(m_prof.rebuildRects); RebuildRects(); }
        StartSearchAppend();   // live search: append paths from newly-loaded starts (cheap early-out inside; runs even with hover off / rects not fresh)
        // NOTE (2026-06-17): the "Atlas rewards - obtainable this line" table was removed at user request
        // (it could never reliably equal the live Search result on a viewport-dependent connTable). The
        // background obtainable scan is therefore no longer kicked here; only the on-demand "Find best
        // path" (and text Search) run now. StartObtainableScan/CollectRoutableRewardIds/RefreshAtlasView
        // are left in place but dormant (uncalled) for easy revival.
    }

    // Timed + counted wrapper around the host UI->screen bridge (the suspected hot path).
    bool CSR(uintptr_t uiPtr, float& x, float& y, float& w, float& h) {
        ScopedT _t(m_prof.csr);
        ++m_csrThisFrame;
        return ctx()->Ui.ComputeScreenRect(uiPtr, x, y, w, h);
    }

    bool RawRect(uint64_t grid, float& x, float& y, float& w, float& h) {
        const auto& snap = m_core.Snap();
        auto nit = snap.byGrid.find(grid);
        if (nit == snap.byGrid.end()) return false;
        return CSR(nit->second->uiPtr, x, y, w, h);
    }

    // Sample ~16 spread nodes via the host batch, fit screen = A*grid + b.
    // Sets m_xform.valid=false when the fit is poor (caller then batch-computes).
    void DeriveTransform(const FR::AtlasSnapshot& snap) {
        m_xform = FR::AtlasTransform{};
        int total = (int)snap.nodes.size();
        if (total < 2) return;
        int want = 16, step = (total > want) ? total / want : 1;
        std::vector<uintptr_t> addrs;
        std::vector<const FR::NodeInfo*> picked;
        for (int i = 0; i < total && (int)addrs.size() < want; i += step) {
            if (snap.nodes[i].uiPtr) { addrs.push_back(snap.nodes[i].uiPtr); picked.push_back(&snap.nodes[i]); }
        }
        if (addrs.size() < 2) return;
        std::vector<PluginSDK::UiService::ScreenRect> rects = ctx()->Ui.ComputeScreenRects(addrs);
        std::vector<double> gx, sx, gy, sy; float w = 0, h = 0;
        for (size_t i = 0; i < rects.size(); ++i) {
            if (!rects[i].ok) continue;
            gx.push_back(picked[i]->grid.x); sx.push_back(rects[i].x);
            gy.push_back(picked[i]->grid.y); sy.push_back(rects[i].y);
            if (rects[i].w > 0) { w = rects[i].w; h = rects[i].h; }
        }
        double rX = 0, rY = 0;
        bool okX = FR::FitLinear(gx, sx, m_xform.ax, m_xform.bx, rX);
        bool okY = FR::FitLinear(gy, sy, m_xform.ay, m_xform.by, rY);
        if (okX && okY && rX <= XFORM_MAX_RESIDUAL && rY <= XFORM_MAX_RESIDUAL && w > 0) {
            m_xform.w = w; m_xform.h = h; m_xform.valid = true;
        }
    }

    // Resolve on-screen PURPLE (reward) node screen-rects. ComputeScreenRect is a per-node
    // parent-chain walk (expensive) AND the atlas UI rebuilds every frame while moving, so we
    // sample ONLY the reference node each tick and do the full rebuild once the view settles.
    void RebuildRects() {
        const auto& snap = m_core.Snap();
        if ((!m_hoverMode && !m_debug) || !snap.valid || snap.nodes.empty()) { m_rectByGrid.clear(); m_rectsFresh = false; return; }
        if (m_debug != m_lastRebuildDebug || m_debugAllCoords != m_lastRebuildAllCoords) {  // mode change widens/narrows the set
            m_rectsFresh = false; m_lastRebuildDebug = m_debug; m_lastRebuildAllCoords = m_debugAllCoords;
        }
        // Movement reference. The old code used snap.nodes.front() blindly; after panning to a
        // new area that node is usually off-screen, so RawRect fails -> posChanged is stuck true
        // -> rects never rebuild -> hover dies for many maps until the program is restarted.
        // Fix: keep tracking the current reference while it stays computable; only when it can't
        // be computed do we re-pick the first computable node (bounded probe).
        float rx = 0, ry = 0, rw = 0, rh = 0; bool haveRef = false; uint64_t refGrid = 0;
        if (m_refGrid && RawRect(m_refGrid, rx, ry, rw, rh)) { refGrid = m_refGrid; haveRef = true; }
        else {
            int probe = 0;
            for (const auto& n : snap.nodes) {
                if (++probe > 128) break;                       // bound the probe cost
                if (RawRect(FR::GridKey(n.grid), rx, ry, rw, rh)) { refGrid = FR::GridKey(n.grid); haveRef = true; break; }
            }
        }
        auto now = std::chrono::steady_clock::now();
        if (!haveRef) { m_refGrid = 0; m_rectsFresh = false; return; }   // nothing computable yet -> retry next tick, don't jam
        bool posChanged = refGrid != m_refGrid ||
                          std::fabs(rx - m_lastRefPos.x) > 0.5f || std::fabs(ry - m_lastRefPos.y) > 0.5f;
        if (posChanged) {                       // moving / re-anchored / first build: cheap path, defer rebuild
            m_refGrid = refGrid;
            m_lastRefPos = ImVec2(rx, ry);
            m_lastMove = now;
            m_rectsFresh = false;
            return;
        }
        if (m_rectsFresh) return;                // settled and already built
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastMove).count() < SETTLE_MS) return;

        DeriveTransform(snap);
        m_rectByGrid.clear();
        int csrBefore = m_csrThisFrame;
        ImVec2 disp = ImGui::GetIO().DisplaySize;

        // Collect kept nodes once (normal=purple reward; debug=line maps; debug+all=every node).
        std::vector<const FR::NodeInfo*> kept;
        for (const auto& n : snap.nodes) {
            // Normal mode keeps every REVEALED (purple / selVecA) map — NOT gated on the per-map
            // record marker. The marker reads inconsistent values across atlas contexts
            // (0x30000691 vs 0x461 when the atlas is opened in-map via the crown) and flickers with
            // off-region duplicate entries, which dropped revealed maps from the hover set. selVecA
            // is read straight off the panel and is reliable, so all purple maps stay hoverable.
            bool keep = (m_debug && m_debugAllCoords) ? true
                      : m_debug ? n.isLineMap()
                      : (snap.revealedA.count(FR::GridKey(n.grid)) != 0);
            if (keep) kept.push_back(&n);
        }

        if (m_xform.valid) {
            for (const FR::NodeInfo* n : kept) {
                float x = (float)(m_xform.ax * n->grid.x + m_xform.bx);
                float y = (float)(m_xform.ay * n->grid.y + m_xform.by);
                if (x + m_xform.w < -120 || y + m_xform.h < -120 || x > disp.x + 120 || y > disp.y + 120) continue;
                m_rectByGrid[FR::GridKey(n->grid)] = Rect{ x, y, m_xform.w, m_xform.h };
                if ((int)m_rectByGrid.size() >= MAX_RECTS) break;
            }
        } else {
            // fallback: one batch call for all kept nodes (Task 2 batch ABI), then cull.
            std::vector<uintptr_t> addrs; addrs.reserve(kept.size());
            for (const FR::NodeInfo* n : kept) addrs.push_back(n->uiPtr);
            auto rects = ctx()->Ui.ComputeScreenRects(addrs);
            m_csrThisFrame += (int)addrs.size();   // count batch items as bridge work
            for (size_t i = 0; i < kept.size(); ++i) {
                if (!rects[i].ok) continue;
                float x = rects[i].x, y = rects[i].y, w = rects[i].w, h = rects[i].h;
                if (x + w < -120 || y + h < -120 || x > disp.x + 120 || y > disp.y + 120) continue;
                m_rectByGrid[FR::GridKey(kept[i]->grid)] = Rect{ x, y, w, h };
                if ((int)m_rectByGrid.size() >= MAX_RECTS) break;
            }
        }
        m_csrInLastRebuild = m_csrThisFrame - csrBefore;
        ++m_rebuildCount;
        m_rectsFresh = true;
    }

    uint64_t HoveredNode(const ImVec2& mp) {
        uint64_t best = 0; float bestD = 1e30f;
        for (const auto& kv : m_rectByGrid) {
            const Rect& r = kv.second;
            float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
            bool inside = (r.w > 1 && r.h > 1 && mp.x >= r.x && mp.x <= r.x + r.w && mp.y >= r.y && mp.y <= r.y + r.h);
            float dx = mp.x - cx, dy = mp.y - cy; float d = std::sqrt(dx * dx + dy * dy);
            if (inside || d <= HIT_RADIUS) { if (d < bestD) { bestD = d; best = kv.first; } }
        }
        return best;
    }

    // Fresh per-grid node UI state (biome + map-state) via ONE transient
    // BASIC|UISTATE service enumeration — the kick-time replacement for the old
    // per-node byte reads (lock/search/scan read these fresh, not from the
    // throttled snapshot). Entries without a live UI element are skipped: the
    // call sites gate on n.uiPtr, matching the old behavior.
    struct UiState { int biome = -1; int mapState = 0; };
    std::unordered_map<uint64_t, UiState> ReadUiStates() {
        std::unordered_map<uint64_t, UiState> out;
        for (const auto& n : ctx()->Atlas.Nodes(PSDK_ATLAS_NODE_BASIC | PSDK_ATLAS_NODE_UISTATE))
            if (n.uiAddress) out[FR::GridKey(n.gridX, n.gridY)] = UiState{ n.biome, n.mapState };
        return out;
    }
    // Unknown grid (snapshot/pass churn race) -> defaults: not completed, no
    // biome exclusion — the old fresh read's common outcome for such nodes.
    static UiState UiStateOf(const std::unordered_map<uint64_t, UiState>& m, uint64_t k) {
        auto it = m.find(k);
        return it != m.end() ? it->second : UiState{};
    }

    // Routing classification for one loaded map (values from a fresh kick-time UI-state pass).
    //   exNode  = excluded as a mid-path reward NODE (completed OR any excluded biome).
    //   startOk = NOT blocked from being a START (not completed, and not excluded by a NON-Breach biome).
    // SPECIAL CASE (Breach Stronghold, biome 9): node-excluded but still allowed as a START when
    // revealed (purple) — the only biome with this asymmetry. Every other excluded biome blocks both.
    void ClassifyMapForRouting(const UiState& u, bool& exNode, bool& startOk) const {
        bool completed = (u.mapState & MAPSTATE_COMPLETED) != 0;
        bool biomeExcl = !m_exclBiomes.empty() && m_exclBiomes.count(u.biome) != 0;
        exNode  = completed || biomeExcl;
        startOk = !completed && !(biomeExcl && u.biome != BIOME_BREACH_STRONGHOLD);
    }

    bool IsEligible(uint64_t grid) {   // any revealed Rite map (selVecA "purple"), not excluded (name/biome)
        const auto& snap = m_core.Snap();
        auto it = snap.byGrid.find(grid);
        if (it == snap.byGrid.end()) return false;
        // Gated on selVecA (purple) membership, NOT the per-map record marker. The marker
        // reads inconsistent values across atlas contexts (e.g. 0x30000691 vs 0x461 when the atlas is
        // opened in-map via the crown) and flickers with off-region duplicate entries, which made
        // revealed maps un-lockable. selVecA is read straight off the panel and is reliable.
        if (!snap.revealedA.count(grid)) return false;
        if (NameExcludedIn(NameOf(FromKey(grid)), m_exclExact, m_exclContains)) return false;
        int biome = m_core.CachedBiome(grid);   // static per map; filled by Core's detail passes
        if (!m_exclBiomes.empty() && m_exclBiomes.count(biome) && biome != BIOME_BREACH_STRONGHOLD)
            return false;   // Breach Stronghold may still START a path (node-excluded, start-allowed)
        return true;
    }

    std::string NameOf(const FR::Grid& g) {
        uint64_t k = FR::GridKey(g);
        auto it = m_nameCache.find(k);
        if (it != m_nameCache.end()) return it->second;
        const auto& snap = m_core.Snap();
        auto nit = snap.byGrid.find(k);
        std::string nm = (nit != snap.byGrid.end()) ? m_core.NodeName(nit->second->uiPtr) : "";
        if (nm.empty()) {
            // Unresolved (off-region / not-yet-loaded duplicate node): return a coord fallback but
            // do NOT cache it, so the real name resolves once you pan to the map. Same off-region
            // stale-duplicate trap as the marker cache (fix 518775ff) — caching the "(x,y)" fallback
            // froze far maps as coords until a restart.
            char b[32]; std::snprintf(b, sizeof(b), "(%d,%d)", g.x, g.y);
            return b;
        }
        m_nameCache[k] = nm;   // cache only a resolved, real name (stable per grid)
        return nm;
    }

    static std::string ToLower(const std::string& s) {
        std::string r = s; for (char& c : r) c = (char)std::tolower((unsigned char)c); return r;
    }

    // ---------- exclusions config (editable files + UI) ----------
    std::filesystem::path ExclDir()        const { return DirectoryPath() / "config"; }
    std::filesystem::path ExclMapsPath()   const { return ExclDir() / "excluded_maps.txt"; }
    std::filesystem::path ExclBiomesPath() const { return ExclDir() / "excluded_biomes.txt"; }

    static std::string Trim(std::string s) {
        while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
        size_t i = 0; while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        return s.substr(i);
    }

    void LoadExclusions() {
        m_exclExact.clear(); m_exclContains.clear(); m_exclBiomes.clear();
        std::ifstream mf(ExclMapsPath());
        if (mf.is_open()) {
            std::string line;
            while (std::getline(mf, line)) {
                line = Trim(line);
                if (line.empty() || line[0] == '#') continue;
                if (line.size() >= 2 && line.front() == '*' && line.back() == '*')
                    m_exclContains.push_back(line.substr(1, line.size() - 2));
                else m_exclExact.insert(line);
            }
        } else {
            for (const char* d : kDefaultExclExact) m_exclExact.insert(d);
            m_exclContains.push_back("Hideout");
            SaveExclusionsMaps();   // create the file with defaults on first run
        }
        std::ifstream bf(ExclBiomesPath());
        if (bf.is_open()) {
            std::string line;
            while (std::getline(bf, line)) {
                line = Trim(line);
                if (line.empty() || line[0] == '#') continue;
                try { m_exclBiomes.insert(std::stoi(line)); } catch (...) {}
            }
        } else {
            m_exclBiomes.insert(9);   // Breach Stronghold excluded by default on a fresh install (id 9)
            SaveExclusionsBiomes();
        }
        std::ifstream sf(ExclDir() / "search.txt");
        if (sf.is_open()) {
            std::string line;
            if (std::getline(sf, line)) { line = Trim(line); std::snprintf(m_searchBuf, sizeof(m_searchBuf), "%s", line.c_str()); }
        } else { std::snprintf(m_searchBuf, sizeof(m_searchBuf), "Mageblood"); SaveSearch(); }
        std::ifstream pf(ExclDir() / "prefs.txt");
        if (pf.is_open()) {
            std::string key; float val;
            while (pf >> key >> val) { if (key == "alpha") m_windowAlpha = std::clamp(val, 0.20f, 1.0f); }
        }
        m_exclLoaded = true;
    }

    void SaveExclusionsMaps() {
        std::error_code ec; std::filesystem::create_directories(ExclDir(), ec);
        std::ofstream f(ExclMapsPath(), std::ios::trunc);
        if (!f.is_open()) return;
        f << "# ForetoldRewards excluded maps. One entry per line. '#' starts a comment.\n";
        f << "# Exact name (e.g. The Iron Citadel) or *substring* (e.g. *Hideout*).\n";
        std::vector<std::string> ex(m_exclExact.begin(), m_exclExact.end()); std::sort(ex.begin(), ex.end());
        for (const auto& s : ex) f << s << "\n";
        for (const auto& s : m_exclContains) f << "*" << s << "*\n";
    }

    void SaveExclusionsBiomes() {
        std::error_code ec; std::filesystem::create_directories(ExclDir(), ec);
        std::ofstream f(ExclBiomesPath(), std::ios::trunc);
        if (!f.is_open()) return;
        f << "# ForetoldRewards excluded biomes, one id per line.\n";
        f << "# 0=Water 1=Mountain 2=Grass 3=Forest 4=Swamp 5=Desert 6-8=City\n";
        f << "# 9=Breach Stronghold 10=Ocean 11=Island 12=Oriath\n";
        for (int b : m_exclBiomes) f << b << "\n";
    }

    void SaveSearch() {   // persist the search box text to config/search.txt
        std::error_code ec; std::filesystem::create_directories(ExclDir(), ec);
        std::ofstream f(ExclDir() / "search.txt", std::ios::trunc);
        if (f.is_open()) f << m_searchBuf << "\n";
    }

    void SavePrefs() {   // persist window prefs (opacity) to config/prefs.txt
        std::error_code ec; std::filesystem::create_directories(ExclDir(), ec);
        std::ofstream f(ExclDir() / "prefs.txt", std::ios::trunc);
        if (f.is_open()) f << "alpha " << m_windowAlpha << "\n";
    }

    void DrawExclusionsTab() {
        if (!m_exclLoaded) LoadExclusions();
        ImGui::TextDisabled("Excluded maps/biomes never appear as a start or a path node.");
        ImGui::TextWrapped("Saved to config/excluded_maps.txt + config/excluded_biomes.txt next to the DLL. Edit there or below.");
        if (ImGui::Button("Reload from files")) LoadExclusions();
        ImGui::Separator();

        ImGui::Text("Maps (exact name, or *substring*):");
        ImGui::SetNextItemWidth(240);
        bool add = ImGui::InputText("##fr_excl_add", m_exclAddBuf, sizeof(m_exclAddBuf), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        add |= ImGui::Button("Add");
        if (add && m_exclAddBuf[0]) {
            std::string s = Trim(m_exclAddBuf);
            if (!s.empty()) {
                if (s.size() >= 2 && s.front() == '*' && s.back() == '*') m_exclContains.push_back(s.substr(1, s.size() - 2));
                else m_exclExact.insert(s);
                SaveExclusionsMaps();
            }
            m_exclAddBuf[0] = 0;
        }
        if (ImGui::BeginChild("##fr_excl_list", ImVec2(0, 180), true)) {
            // exact names (sorted) then *substring* entries, laid out 6 per row.
            std::vector<std::string> ex(m_exclExact.begin(), m_exclExact.end()); std::sort(ex.begin(), ex.end());
            std::string removeExact; int removeContains = -1;
            if (ImGui::BeginTable("##fr_maps", 6, ImGuiTableFlags_SizingStretchSame)) {
                int col = 0;
                auto cell = [&](const std::string& label, bool isContains, const std::string& key, int idx) {
                    ImGui::TableNextColumn();
                    ImGui::PushID(col++);
                    if (ImGui::SmallButton("x")) { if (isContains) removeContains = idx; else removeExact = key; }
                    ImGui::SameLine(); ImGui::TextUnformatted(label.c_str());
                    ImGui::PopID();
                };
                for (const auto& s : ex) cell(s, false, s, -1);
                for (size_t i = 0; i < m_exclContains.size(); ++i) cell("*" + m_exclContains[i] + "*", true, "", (int)i);
                ImGui::EndTable();
            }
            if (!removeExact.empty()) { m_exclExact.erase(removeExact); SaveExclusionsMaps(); }
            else if (removeContains >= 0 && removeContains < (int)m_exclContains.size()) {
                m_exclContains.erase(m_exclContains.begin() + removeContains); SaveExclusionsMaps();
            }
            ImGui::EndChild();
        }
        ImGui::Separator();
        ImGui::Text("Biomes:");
        if (ImGui::BeginTable("##fr_biomes", 3, ImGuiTableFlags_SizingStretchSame)) {
            for (int b = 0; b < BIOME_COUNT; ++b) {
                ImGui::TableNextColumn();
                bool on = m_exclBiomes.count(b) != 0;
                char lbl[48]; std::snprintf(lbl, sizeof(lbl), "%s##bi%d", BiomeName(b), b);
                if (ImGui::Checkbox(lbl, &on)) {
                    if (on) m_exclBiomes.insert(b); else m_exclBiomes.erase(b);
                    SaveExclusionsBiomes();
                }
            }
            ImGui::EndTable();
        }
    }

    // ---------- performance tab (live instrumentation) ----------
    void DrawPerfTab() {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("Host FPS: %.1f   (%.2f ms / frame)", io.Framerate, io.Framerate > 0 ? 1000.0f / io.Framerate : 0.0f);
        ImGui::SameLine();
        if (ImGui::Button("Reset stats")) { m_prof.resetAll(); m_rebuildCount = 0; }
        ImGui::TextDisabled("last = most recent call | avg = mean since reset | max = worst | n = samples");

        ImGui::SeparatorText("Per-frame (render thread)");
        if (ImGui::BeginTable("##fr_perf_frame", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Section");  ImGui::TableSetupColumn("last ms");
            ImGui::TableSetupColumn("avg ms");   ImGui::TableSetupColumn("max ms");
            ImGui::TableSetupColumn("n");        ImGui::TableHeadersRow();
            auto row = [&](const char* name, const ProfStat& s) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(name);
                ImGui::TableNextColumn(); ImGui::Text("%.3f", s.lastMs());
                ImGui::TableNextColumn(); ImGui::Text("%.3f", s.avgMs());
                ImGui::TableNextColumn(); ImGui::Text("%.3f", s.maxMs());
                ImGui::TableNextColumn(); ImGui::Text("%llu", (unsigned long long)s.n());
            };
            row("DrawUI (whole plugin frame)", m_prof.drawUI);
            row("  RebuildRects (on settle)",  m_prof.rebuildRects);
            row("  DrawRoute",                 m_prof.drawRoute);
            row("  DrawAllCoords",             m_prof.drawAllCoords);
            row("  HandleHover",               m_prof.handleHover);
            row("  Window filter",             m_prof.windowFilter);
            row("  PollResult",                m_prof.pollResult);
            row("Core.Update (~8 Hz tick)",    m_prof.coreUpdate);
            ImGui::EndTable();
        }

        ImGui::SeparatorText("ComputeScreenRect (host UI->screen bridge)");
        ImGui::Text("per call:  avg %.1f us   |   max %.1f us   |   total calls %llu",
                    m_prof.csr.avgUs(), m_prof.csr.maxUs(), (unsigned long long)m_prof.csr.n());
        ImGui::Text("calls last frame: %d", m_csrLastFrame);
        ImGui::Text("calls in last full rebuild: %d   (full rebuilds: %d)", m_csrInLastRebuild, m_rebuildCount);
        ImGui::Text("affine transform: %s   (residual gate %.1f px)",
                    m_xform.valid ? "VALID (math, ~0 bridge calls)" : "fallback -> batch", XFORM_MAX_RESIDUAL);
        ImGui::TextDisabled("Each call = ABI hop + SEH + parent-chain walk. High per-frame count or avg us = top FPS suspect.");

        ImGui::SeparatorText("Core.Update breakdown (last tick)");
        ImGui::Text("total %.3f ms  =  conn %.3f  +  nodes %.3f  +  sel %.3f ms",
                    m_core.perfUpdateMs, m_core.perfConnMs, m_core.perfNodesMs, m_core.perfSelMs);
        ImGui::Text("nodes %d   connTable %d   revealed/purple %d",
                    m_core.perfNodeCount, m_core.perfConnCount, m_core.perfRevealedCount);
        ImGui::TextDisabled("connTable + byGrid std::map are rebuilt EVERY tick (viewport-dependent).");

        ImGui::SeparatorText("Reward table + enumeration (worker thread)");
        ImGui::Text("reward table: %.1f ms   (weights %d, combos %d, %s)",
                    m_core.perfInjectMs, m_core.perfWeightCount, m_core.perfComboCount,
                    m_lastInjectFromCache.load(std::memory_order_relaxed) ? "from cache" : "computed");
        ImGui::Text("BuildChains:  last %.1f ms  |  avg %.1f ms  |  max %.1f ms",
                    m_prof.buildChains.lastMs(), m_prof.buildChains.avgMs(), m_prof.buildChains.maxMs());
        ImGui::Text("last run: explored %d nodes, emitted %d chains",
                    m_lastExplored.load(std::memory_order_relaxed), m_lastEmitted.load(std::memory_order_relaxed));
        int wf = m_workerFaults.load(std::memory_order_relaxed);
        if (wf > 0) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "worker faults caught: %d (job dropped, host kept alive)", wf);
        else        ImGui::TextDisabled("worker faults caught: 0");

        ImGui::SeparatorText("Live state");
        const auto& snap = m_core.Snap();
        const char* st = m_lockState == LockState::Idle ? "Idle"
                       : m_lockState == LockState::Computing ? "Computing" : "Ready";
        ImGui::Text("state %s   chains stored %d   rects %d", st, (int)m_chains.size(), (int)m_rectByGrid.size());
        ImGui::Text("seed %08X   pathCap %d   valid %d", snap.lineSeed, snap.pathCap, snap.valid ? 1 : 0);
        ImGui::Text("flags: hover=%d debug=%d allCoords=%d window=%d rectsFresh=%d",
                    m_hoverMode, m_debug, m_debugAllCoords, m_showWindow, m_rectsFresh ? 1 : 0);
        if (ImGui::SliderInt("Update throttle (ms)", &m_throttleMs, 16, 500)) {}
        ImGui::TextDisabled("Lower = fresher hover/rects but more Core.Update cost per second. Default 120.");
    }

    // ---------- lock / worker ----------
    void StartWorker() {
        if (m_worker.joinable()) return;
        m_stop = false;
        m_worker = std::thread(&ForetoldRewardsPlugin::WorkerLoop, this);
    }
    void StopWorker() {
        { std::lock_guard<std::mutex> lk(m_jobMutex); m_stop = true; }
        m_jobCv.notify_one();
        if (m_worker.joinable()) m_worker.join();
    }

    void StartLock(uint64_t startGrid) {
        const auto& snap = m_core.Snap();
        WorkerInput in;
        in.startGrid = startGrid;
        in.purple    = snap.revealedA;   // reward-map set for the DFS (markers unreliable in-map/crown)
        in.lineSeed  = snap.lineSeed;
        in.pathCap   = snap.pathCap;
        in.conn      = m_core.Conn();                       // copy (worker owns its data)
        in.exclExact    = m_exclExact;
        in.exclContains = m_exclContains;
        auto uiStates = ReadUiStates();   // fresh biome/map-state at lock time (one transient pass)
        for (const auto& n : snap.nodes) {
            uint64_t k = FR::GridKey(n.grid);
            in.marker[k] = n.marker;
            in.uiPtr[k]  = n.uiPtr;
            // exclude (read fresh on the render thread at lock): completed ("Map Attempted") via
            // the map-state bit, or an excluded biome. Names are handled in the DFS.
            if (in.conn.count(k) && n.uiPtr) {   // edge-graph maps: mark completed / biome-excluded so the DFS won't path THROUGH them
                UiState u = UiStateOf(uiStates, k);
                bool ex = (u.mapState & MAPSTATE_COMPLETED) != 0;
                if (!ex && !m_exclBiomes.empty())
                    ex = m_exclBiomes.count(u.biome) != 0;
                if (ex) in.excluded.insert(k);
            }
        }
        uint64_t gen = m_curGen.fetch_add(1) + 1;
        { std::lock_guard<std::mutex> lk(m_jobMutex); m_job = std::move(in); m_jobGen = gen; m_jobPending = true; m_routeWaiting.store(true, std::memory_order_relaxed); }
        m_jobCv.notify_one();
        { std::lock_guard<std::mutex> lk(m_resultMutex); m_resultReady = false; }
        StartWorker();
        m_lockedStart = startGrid; m_lockedSeed = snap.lineSeed; m_isSearchResult = false;
        m_lockState = LockState::Computing; m_chains.clear(); m_selectedChains.clear(); m_selAnchor = -1; m_shownDirty = true;
        m_searchSession = false; m_appendPending = false;   // locking ends any search session
        m_priorityTerms.clear();   // lock mode has no search terms -> no priority colour/sort
        m_searchedStarts.clear(); m_chainKeys.clear();
    }

    // Search every purple (selVecA) start for chains whose rewards match `termsRaw` (comma-separated,
    // partial, any-match). Heavy -> runs on the worker; the window shows progress. Limited to maps
    // currently loaded in the connTable (zoom out to cover more of the line).
    void StartSearch(const std::string& termsRaw, bool bestValue = false, bool oneShot = false) {
        const auto& snap = m_core.Snap();
        if (!snap.valid || snap.lineSeed == 0) return;
        if (!bestValue && !oneShot) SaveSearch();   // don't persist one-shot / best-value searches to search.txt
        WorkerInput in;
        in.search   = true;
        in.bestValue = bestValue;
        in.purple   = snap.revealedA;
        in.lineSeed = snap.lineSeed;
        in.pathCap  = snap.pathCap;
        in.conn     = m_core.Conn();
        in.exclExact    = m_exclExact;
        in.exclContains = m_exclContains;
        { std::string cur;
          auto flush = [&]() { std::string t = ToLower(Trim(cur)); if (!t.empty()) in.terms.push_back(t); cur.clear(); };
          for (char c : termsRaw) { if (c == ',') flush(); else cur += c; } flush(); }
        m_priorityTerms = in.terms;   // ordered (term[0] = most important) -> drives result sort + colour
        auto uiStates = ReadUiStates();   // fresh biome/map-state at search time (one transient pass)
        for (const auto& n : snap.nodes) {
            uint64_t k = FR::GridKey(n.grid);
            in.marker[k] = n.marker;
            in.uiPtr[k]  = n.uiPtr;
            if (in.conn.count(k) && n.uiPtr) {   // edge-graph maps: mark completed / biome-excluded so the DFS won't path THROUGH them
                bool exNode, startOk; ClassifyMapForRouting(UiStateOf(uiStates, k), exNode, startOk);
                if (exNode) in.excluded.insert(k);                          // completed/excluded-biome: not a mid-path node
                if (startOk && snap.revealedA.count(k)) in.starts.push_back(k);   // purple + start-eligible (Breach may START)
            }
        }
        m_searchTotal.store((int)in.starts.size());
        m_searchProgress.store(0);
        m_searchTermsDisplay = bestValue ? std::string("most valuable path") : termsRaw;
        m_searchSession     = !bestValue && !oneShot;   // best-value / per-reward Path are one-shot (no live-append)
        m_searchTermsActive = termsRaw;
        m_searchedStarts.clear();
        m_chainKeys.clear();
        for (uint64_t s : in.starts) m_searchedStarts.insert(s);   // initial pass covers these
        m_appendPending = false;
        uint64_t gen = m_curGen.fetch_add(1) + 1;
        { std::lock_guard<std::mutex> lk(m_jobMutex); m_job = std::move(in); m_jobGen = gen; m_jobPending = true; m_routeWaiting.store(true, std::memory_order_relaxed); }
        m_jobCv.notify_one();
        { std::lock_guard<std::mutex> lk(m_resultMutex); m_resultReady = false; }
        StartWorker();
        m_lockedStart = 0; m_lockedSeed = snap.lineSeed; m_isSearchResult = true;
        m_lockState = LockState::Computing; m_chains.clear(); m_selectedChains.clear(); m_selAnchor = -1; m_shownDirty = true;
    }

    // Scan ONLY newly-loaded purple starts (not yet scanned) with the active terms,
    // appending results. No-op unless a search session is active and live mode is on.
    void StartSearchAppend() {
        if (!m_searchSession || !m_liveSearch || m_appendPending) return;
        if (m_lockState == LockState::Computing) return;     // don't stack on a running job
        const auto& snap = m_core.Snap();
        if (!snap.valid || snap.lineSeed == 0 || snap.lineSeed != m_lockedSeed) return;
        // Cheap early-out (this runs every Update tick now): only build a job when a NEW purple start
        // has actually loaded (in the edge graph, not yet searched). Pure hash lookups, no memory
        // reads -> safe to run per tick; the costly MapState scan below only runs when there IS new work.
        {
            const auto& conn = m_core.Conn();
            bool hasNew = false;
            for (const auto& n : snap.nodes) {
                uint64_t k = FR::GridKey(n.grid);
                if (n.uiPtr && conn.count(k) && snap.revealedA.count(k) && !m_searchedStarts.count(k)) { hasNew = true; break; }
            }
            if (!hasNew) return;
        }
        WorkerInput in;
        in.search = true; in.append = true; in.purple = snap.revealedA;
        in.lineSeed = snap.lineSeed; in.pathCap = snap.pathCap;
        in.conn = m_core.Conn();
        in.exclExact = m_exclExact; in.exclContains = m_exclContains;
        { std::string cur;
          auto flush = [&]() { std::string t = ToLower(Trim(cur)); if (!t.empty()) in.terms.push_back(t); cur.clear(); };
          for (char c : m_searchTermsActive) { if (c == ',') flush(); else cur += c; } flush(); }
        auto uiStates = ReadUiStates();   // fresh biome/map-state (only runs when NEW starts exist — after the early-out above)
        for (const auto& n : snap.nodes) {
            uint64_t k = FR::GridKey(n.grid);
            in.marker[k] = n.marker; in.uiPtr[k] = n.uiPtr;
            if (in.conn.count(k) && n.uiPtr) {   // edge-graph maps: mark completed / biome-excluded so the DFS won't path THROUGH them
                bool exNode, startOk; ClassifyMapForRouting(UiStateOf(uiStates, k), exNode, startOk);
                if (exNode) in.excluded.insert(k);
                if (startOk && snap.revealedA.count(k) && !m_searchedStarts.count(k)) in.starts.push_back(k);
            }
        }
        if (in.starts.empty()) return;                         // nothing new loaded
        for (uint64_t s : in.starts) m_searchedStarts.insert(s);
        uint64_t gen = m_curGen.fetch_add(1) + 1;
        // NOTE: do NOT set m_routeWaiting here. A live-search APPEND is background work (fires as maps
        // load), not a user click. Setting m_routeWaiting made the background obtainable scan ABORT on
        // every append (line 1234) -> during loading with Live on it never COMPLETED -> rewards stayed
        // missing from the list until "Find best path" folded them in. The append still gets worker
        // priority via m_jobPending; it just no longer starves the obtainable scan. (Only user-initiated
        // jobs — Search/Find best path/Path/lock — set m_routeWaiting, so the OB scan still yields to those.)
        { std::lock_guard<std::mutex> lk(m_jobMutex); m_job = std::move(in); m_jobGen = gen; m_jobPending = true; }
        m_jobCv.notify_one();
        m_appendPending = true;
    }

    // Kick a background scan that enumerates all complete routes from the loaded purple starts and
    // collects the reward set actually reachable (the route-DEPENDENT obtainable list). Uses a
    // separate worker slot + generation so it never disturbs a user lock/search. Triggered by
    // StartObtainableIfNeeded; the start/exclusion logic mirrors StartSearch.
    void StartObtainableScan() {
        const auto& snap = m_core.Snap();
        if (!snap.valid || snap.lineSeed == 0) return;
        WorkerInput in;
        in.obtainable = true;
        in.purple    = snap.revealedA;
        in.lineSeed  = snap.lineSeed;
        in.pathCap   = snap.pathCap;
        in.conn      = m_core.Conn();
        in.exclExact = m_exclExact; in.exclContains = m_exclContains;
        auto uiStates = ReadUiStates();   // fresh biome/map-state at scan-kick time (one transient pass)
        for (const auto& n : snap.nodes) {
            uint64_t k = FR::GridKey(n.grid);
            in.marker[k] = n.marker; in.uiPtr[k] = n.uiPtr;
            if (in.conn.count(k) && n.uiPtr) {
                bool exNode, startOk; ClassifyMapForRouting(UiStateOf(uiStates, k), exNode, startOk);
                if (exNode) in.excluded.insert(k);
                if (startOk && snap.revealedA.count(k)) in.starts.push_back(k);
            }
        }
        if (in.starts.empty()) { m_obScanConnMax = m_obScanConnInFlight; return; }   // no starts at this size -> mark handled, don't spin
        uint64_t gen = m_obGen.fetch_add(1) + 1;
        { std::lock_guard<std::mutex> lk(m_jobMutex); m_obJob = std::move(in); m_obJobGen = gen; m_obJobPending = true; }
        m_jobCv.notify_one();
        StartWorker();
        m_obComputing = true;
        m_lastObScan = std::chrono::steady_clock::now(); m_haveObTime = true;
        m_obActiveSince = m_lastObScan;  // reset the max-wait baseline so continuous map-loading can't thrash re-scans
    }

    // Keep the "obtainable this line" list == the rewards every route over the CURRENTLY-LOADED line can
    // yield — exactly what the global Search enumerates. The connTable is viewport-dependent (FR_Core.h: it
    // grows as you pan/zoom in more of the line), so a single early scan misses maps that load later, and
    // "Find best path"/"Path" then surface rewards the frozen list never had. We (re)scan whenever the
    // connTable GROWS and UNION the results in (monotonic — it only ever accumulates, never drops a reward).
    // Trigger on connTable SIZE, NOT on new starts: a reward is g(lineSeed,a3,a4) (path-independent), so a
    // map loading DEEP in the line can complete a chain from an already-seen start and realize a new (a3,a4)
    // cell without any new start appearing — tracking starts alone misses exactly that (the Very Rare Unique
    // case). Reset only on a new Rite line / atlas reopen. (User model 2026-06-16: "build it like Search".)
    void StartObtainableIfNeeded() {
        const auto& snap = m_core.Snap();
        bool active = snap.valid && snap.lineSeed != 0;
        if (!active) {                               // atlas closed -> arm a fresh recompute for the next open
            m_obWasValid = false; m_obScanDue = false;
            if (m_obComputing) { m_obGen.fetch_add(1); m_obComputing = false; }
            return;
        }
        auto now = std::chrono::steady_clock::now();
        bool reform = !m_obWasValid || snap.lineSeed != m_obSeed;   // atlas (re)opened, or a new Rite line
        m_obWasValid = true;
        if (reform) {
            if (snap.lineSeed != m_obSeed) { m_routableIds.clear(); m_routableSet.clear(); m_atlasDirty = true; }   // wrong-line list -> drop now
            m_obSeed = snap.lineSeed;
            m_obScanConnMax = 0;                      // nothing scanned for this open -> first growth triggers a scan
            if (m_obComputing) { m_obGen.fetch_add(1); m_obComputing = false; }   // cancel any stale in-flight scan
            m_obActiveSince = now; m_obStableSince = now; m_obConnSize = -1; m_obScanDue = false;
        }
        if (m_obComputing) return;                   // a scan is in flight
        // (Re)scan whenever the loaded subgraph has grown beyond what a scan has already covered. High-water
        // so zooming back IN (smaller connTable) never re-triggers and never shrinks the list.
        int connSize = (int)m_core.Conn().size();
        if (connSize != m_obConnSize) { m_obConnSize = connSize; m_obStableSince = now; }
        if (connSize <= m_obScanConnMax) { m_obScanDue = false; m_obActiveSince = now; return; }   // nothing new loaded
        m_obScanDue = true;                          // a (re)scan is pending -> drives the "scanning..." hint
        // Debounce: scan once the connTable settles (stops growing) or after a hard max-wait, so a long
        // continuous zoom-out coalesces into a few scans instead of one per loaded map.
        long long stableMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_obStableSince).count();
        long long pendingMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_obActiveSince).count();
        if (stableMs >= OB_SETTLE_MS || pendingMs >= OB_MAXWAIT_MS) {
            m_obScanConnInFlight = connSize;         // size this scan covers -> high-water on completion
            StartObtainableScan();
        }
    }

    void Unlock() {
        m_curGen.fetch_add(1);                              // invalidate any in-flight result
        m_lockState = LockState::Idle; m_lockedStart = 0; m_lockedSeed = 0; m_isSearchResult = false;
        m_chains.clear(); m_selectedChains.clear(); m_selAnchor = -1; m_shownDirty = true;
        m_searchSession = false; m_appendPending = false;
        m_searchedStarts.clear(); m_chainKeys.clear();
        { std::lock_guard<std::mutex> lk(m_resultMutex); m_resultReady = false; }
    }

    void AutoUnlockOnLineChange() {
        if (m_lockState == LockState::Idle) return;
        const auto& snap = m_core.Snap();
        if (snap.valid && snap.lineSeed != 0 && snap.lineSeed != m_lockedSeed) Unlock();
    }

    // Fold reward ids into the accumulated "obtainable this line" set (render thread only). Returns true
    // if anything was new. The set only ever grows within an atlas-open (reset on line change).
    template <class Container>
    bool UnionRoutable(const Container& ids) {
        bool added = false;
        for (const auto& id : ids)
            if (!id.empty() && m_routableSet.insert(id).second) { m_routableIds.push_back(id); added = true; }
        return added;
    }

    void PollResult() {
        std::lock_guard<std::mutex> lk(m_resultMutex);
        if (m_resultReady && m_resultGen == m_curGen.load()) {
            if (m_resultAppend) {
                for (auto& ch : m_resultChains) {
                    std::string key = ChainKey(ch);
                    if (m_chainKeys.insert(key).second) m_chains.push_back(std::move(ch));
                }
                m_appendPending = false;
            } else {
                m_chains = std::move(m_resultChains);
                m_chainKeys.clear();
                for (auto& ch : m_chains) m_chainKeys.insert(ChainKey(ch));
                m_selectedChains.clear(); m_selAnchor = -1;
                m_appendPending = false;   // a replace-result supersedes any pending append
            }
            // Fold every reward a search/best-path result actually contains into the obtainable list, so
            // the top table is ALWAYS a superset of what any displayed route shows (closes the race where
            // a just-loaded map lets "Find best path" reach a reward the background scan hasn't caught yet).
            if (m_isSearchResult && UnionRoutable(m_resultRewardIds)) m_atlasDirty = true;
            m_resultReady = false;
            m_lockState = LockState::Ready;
            m_shownDirty = true;
        }
        if (m_obResultReady) {   // background obtainable scan finished
            if (m_obResultGen == m_obGen.load()) {   // fresh result for the current scan (stale -> drop)
                m_obComputing = false;
                // UNION the confirmed routable rewards into the accumulated list (monotonic, never shrink).
                // A partial (yielded) scan still contributes valid rewards; only a COMPLETE pass marks its
                // starts covered, so a partial result re-scans later instead of leaving the set "done".
                if (UnionRoutable(m_obResultIds)) m_atlasDirty = true;
                if (m_obResultComplete) m_obScanConnMax = m_obScanConnInFlight;   // this connTable size fully covered
            }
            m_obResultReady = false;
        }
    }

    void WorkerLoop() {
        for (;;) {
            WorkerInput job; uint64_t gen;
            {
                std::unique_lock<std::mutex> lk(m_jobMutex);
                m_jobCv.wait(lk, [&] { return m_jobPending || m_obJobPending || m_stop; });
                if (m_stop) return;
                if (m_jobPending) {                  // user route jobs take priority over the background list scan
                    job = std::move(m_job); gen = m_jobGen; m_jobPending = false;
                    m_routeWaiting.store(false, std::memory_order_relaxed);
                } else {
                    job = std::move(m_obJob); gen = m_obJobGen; m_obJobPending = false;
                }
            }
            // This runs on the plugin's OWN thread, which the host's SEH bridge does NOT wrap (it
            // only guards synchronous host->plugin calls). An unhandled C++ exception OR a structured
            // fault (access violation, etc.) here would terminate the whole POEFixer process. Double-
            // guard each job: a faulting job is dropped, the worker and the host stay alive.
            if (!ProcessJobSEH(job, gen))
                m_workerFaults.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // SEH layer — catches structured faults (AV, etc.). Kept free of locals that need C++ unwinding
    // (MSVC C2712: __try cannot coexist with object unwinding), so it only forwards to the C++ layer.
    int ProcessJobSEH(WorkerInput& job, uint64_t gen) {
        __try { return ProcessJobGuarded(job, gen); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }
    // C++ exception layer (bad_alloc, length_error, ...). Returns 1 = ok, 0 = the job threw/faulted.
    int ProcessJobGuarded(WorkerInput& job, uint64_t gen) {
        try { ProcessJob(job, gen); return 1; }
        catch (...) { return 0; }
    }
    void ProcessJob(WorkerInput& job, uint64_t gen) {
        // Staleness is tracked per job kind: route jobs (m_curGen) vs the background obtainable list
        // scan (m_obGen) use independent generation counters, so they never cancel each other.
        auto stale = [&] { return (job.obtainable ? m_obGen.load() : m_curGen.load()) != gen; };

        // 1) reward table (cached per line; computed offline once) — shared by both job kinds
        std::vector<std::vector<FR::Reward>> table; bool tableOk = false;
        {
            std::lock_guard<std::mutex> lk(m_resultMutex);
            if (m_tableCacheOk && m_tableCacheSeed == job.lineSeed) { table = m_tableCache; tableOk = true; }
        }
        if (!job.obtainable) m_lastInjectFromCache.store(tableOk, std::memory_order_relaxed);  // true => no remote thread this run
        if (!tableOk) {
            // The Atlas service resolves the panel internally (stateless, worker-thread safe);
            // an unresolvable panel yields empty weights -> ComputeRewardTable returns false.
            tableOk = m_core.ComputeRewardTable(job.lineSeed, job.pathCap, table);
            if (tableOk) {
                std::lock_guard<std::mutex> lk(m_resultMutex);
                m_tableCache = table; m_tableCacheSeed = job.lineSeed; m_tableCacheOk = true;
            }
        }
        if (stale()) return;             // stale (re-locked/unlocked / line changed) -> drop

        if (job.obtainable) {            // background list scan: route-reachable reward set
            std::vector<std::string> ids; bool aborted = false;
            CollectRoutableRewardIds(job, table, tableOk, ids, aborted);
            if (stale()) return;
            std::lock_guard<std::mutex> lk(m_resultMutex);
            m_obResultIds = std::move(ids); m_obResultGen = gen;
            m_obResultComplete = !aborted;   // partial (yielded to a route job) -> don't freeze; re-scan later
            m_obResultReady = true;
            return;
        }

        // 2) enumerate chains
        std::vector<Chain> chains;
        std::unordered_set<std::string> rewardIds;   // every reward id appearing in an emitted chain
        BuildChains(job, table, tableOk, chains, &rewardIds);
        if (stale()) return;

        // 3) publish
        { std::lock_guard<std::mutex> lk(m_resultMutex);
          m_resultChains = std::move(chains); m_resultGen = gen; m_resultReady = true;
          m_resultAppend = job.append; m_resultRewardIds = std::move(rewardIds); }
    }

    void BuildChains(const WorkerInput& in, const std::vector<std::vector<FR::Reward>>& table,
                     bool tableOk, std::vector<Chain>& out,
                     std::unordered_set<std::string>* outIds = nullptr) {
        ScopedT _t(m_prof.buildChains);
        auto markerOf = [&](uint64_t k) -> uint32_t {
            auto it = in.marker.find(k); return it != in.marker.end() ? it->second : 0;
        };
        auto isReward = [&](uint64_t k) {
            // Traverse every connected map; skip ONLY a confirmed non-reward (marker == 0x47B).
            // The edge graph (connTable / selVecA edges) already constrains traversal to the Rite line,
            // so it is the authoritative connectivity. We must NOT additionally require:
            //   - a positive reward marker -> skipped marker-0 neighbours ("explored 1 node, 0 chains");
            //   - purple/selVecA membership -> far too narrow: a Rite path threads through non-revealed
            //     maps (CE-verified: Sinter Rift's purple component is only 2 maps, but its edge-connected
            //     line is 60+; the in-map/crown markers read 0x30000691 / 0x100009xx, none == 0x47B).
            return markerOf(k) != FR::MARKER_NONREWARD;
        };
        std::unordered_map<uint64_t, std::string> nameCache;   // resolve each unique grid once
        auto nameOf = [&](uint64_t k) -> std::string {
            auto cached = nameCache.find(k);
            if (cached != nameCache.end()) return cached->second;
            auto it = in.uiPtr.find(k);
            std::string nm = (it != in.uiPtr.end()) ? m_core.NodeName(it->second) : "";
            if (nm.empty()) { FR::Grid g = FromKey(k); char b[32]; std::snprintf(b, sizeof(b), "(%d,%d)", g.x, g.y); nm = b; }
            nameCache[k] = nm;
            return nm;
        };
        auto rewardLabel = [&](int a3, int a4) -> std::string {
            if (!tableOk || a3 < 1 || a3 >= (int)table.size() || a4 < 0 || a4 > FR::A4_MAX) return {};
            std::string s; for (const auto& m : table[a3][a4].mods) { if (!s.empty()) s += " + "; s += FR::RewardLabel(m); }
            return s;
        };
        // Raw reward ids for a step (parallel to rewardLabel) -> outIds union, so every reward a
        // displayed route contains is guaranteed to appear in the "obtainable this line" table.
        auto rewardIdsAt = [&](int a3, int a4) -> std::vector<std::string> {
            if (!tableOk || a3 < 1 || a3 >= (int)table.size() || a4 < 0 || a4 > FR::A4_MAX) return {};
            return table[a3][a4].mods;
        };
        // Total chaos value of a step's reward(s) — for the "most valuable path" ranking (bestValue).
        // Unpriceable rewards normally count as 0; "Very Rare Unique" instead gets a fixed POTENTIAL
        // placeholder (FR::PotentialChaos) so routes that yield it aren't ignored by "Find best path".
        auto rewardValue = [&](int a3, int a4) -> double {
            if (!tableOk || a3 < 1 || a3 >= (int)table.size() || a4 < 0 || a4 > FR::A4_MAX) return 0.0;
            double div = ctx()->Prices.GetRates().divineInChaos;
            double v = 0.0;
            for (const auto& m : table[a3][a4].mods) {
                FR::RewardPriceInfo pi = FR::PriceInfoFor(m);
                if (pi.priceName && pi.priceName[0]) v += ctx()->Prices.LookupPrice(pi.priceName).chaos * (double)pi.qty;
                else v += FR::PotentialChaos(m, div);   // unpriced "Very Rare Unique" -> ~20 div placeholder
            }
            return v;
        };
        // True if a step's reward(s) include a "potential"-valued reward (Very Rare Unique) — drives
        // the "(incl. potential)" marker on the route total so the user knows it isn't a real price.
        auto rewardPotential = [&](int a3, int a4) -> bool {
            if (!tableOk || a3 < 1 || a3 >= (int)table.size() || a4 < 0 || a4 > FR::A4_MAX) return false;
            for (const auto& m : table[a3][a4].mods) if (FR::IsPotentialValueReward(m)) return true;
            return false;
        };

        std::vector<FR::Grid> path;
        std::vector<std::string> rew;
        std::vector<double> rewVal;          // per-node reward chaos value (parallel to rew); bestValue ranking
        std::vector<bool>   rewPot;          // per-node "is potential placeholder" flag (parallel to rew)
        std::vector<std::vector<std::string>> rewIds;   // per-node reward ids (parallel to rew) -> outIds union
        long long bestCval = -1;             // best total chain value seen (centi-chaos), for bestValue mode
        int explored = 0;
        bool stop = false;
        const int exploreCap = in.search ? SEARCH_EXPLORE_CAP : PATH_HARD_CAP;
        const int resultCap  = in.search ? SEARCH_RESULT_CAP  : CHAIN_STORE_CAP;
        const auto t0 = std::chrono::steady_clock::now();

        // search match: a chain is kept iff ANY reward (rew[1..]) contains ANY search term.
        auto matches = [&]() -> bool {
            if (in.terms.empty()) return true;
            for (size_t i = 1; i < rew.size(); ++i) {
                std::string lr = ToLower(rew[i]);
                for (const auto& t : in.terms) if (!t.empty() && lr.find(t) != std::string::npos) return true;
            }
            return false;
        };
        auto emit = [&]() {
            if ((int)out.size() >= resultCap) { stop = true; return; }
            if (in.search && !matches()) return;
            if (in.bestValue) {   // keep only the highest total-value chain(s); equal-value ties accumulate
                long long cval = 0;
                for (size_t i = 1; i < rewVal.size(); ++i) cval += std::llround(rewVal[i] * 100.0);
                if (out.empty() || cval > bestCval) { out.clear(); bestCval = cval; }
                else if (cval < bestCval) return;
            }
            if (outIds) for (const auto& ids : rewIds) for (const auto& id : ids) if (!id.empty()) outIds->insert(id);
            Chain ch; ch.reserve(path.size());
            for (size_t i = 0; i < path.size(); ++i) {
                ChainNode nd; nd.grid = FR::GridKey(path[i]); nd.name = nameOf(nd.grid); nd.reward = rew[i]; nd.value = rewVal[i]; nd.potential = rewPot[i];
                ch.push_back(std::move(nd));
            }
            out.push_back(std::move(ch));
            if (!in.bestValue && (int)out.size() >= resultCap) stop = true;
        };

        // Only COMPLETE chains (START + pathCap-1 reward maps) are emitted; partial/dead-end chains
        // are dropped (in-game you cannot realise a foretold reward unless the full path is buildable).
        std::function<void()> dfs = [&]() {
            if (stop) return;
            if (explored >= exploreCap) { stop = true; return; }
            ++explored;
            if ((int)path.size() >= in.pathCap) { emit(); return; }     // full-length chain reached
            auto cit = in.conn.find(FR::GridKey(path.back()));
            if (cit == in.conn.end()) return;                           // dead end -> drop (incomplete)
            // a4 = index in the anchor's neighbours sorted (x, then y), EXCLUDING already-selected
            // anchors (= the path). Confirmed against the game: non-reward neighbours still occupy
            // a slot, only selected anchors drop out. NOTE: the computed reward only matches the game
            // at shallow depth — the reward fn carries internal state tied to the game's in-context
            // call sequence and diverges at high a3, so deep values are "~" estimates and the real
            // in-game label (once revealed) overrides them via RewardForDisplay.
            std::vector<FR::Grid> sorted = FR::Core::SortedNeighbors(cit->second, path);
            int a3 = (int)path.size();                                  // 1..(pathCap-1)
            for (int r = 0; r < (int)sorted.size() && r <= FR::A4_MAX; ++r) {
                uint64_t ck = FR::GridKey(sorted[r]);
                if (!isReward(ck) || in.excluded.count(ck)) continue;
                if (NameExcludedIn(nameOf(ck), in.exclExact, in.exclContains)) continue;
                path.push_back(sorted[r]); rew.push_back(rewardLabel(a3, r)); rewVal.push_back(rewardValue(a3, r)); rewPot.push_back(rewardPotential(a3, r)); rewIds.push_back(rewardIdsAt(a3, r));
                dfs();
                path.pop_back(); rew.pop_back(); rewVal.pop_back(); rewPot.pop_back(); rewIds.pop_back();
                if (stop) return;
            }
        };

        // lock mode = one start; search mode = every purple candidate start.
        std::vector<uint64_t> starts = in.search ? in.starts : std::vector<uint64_t>{ in.startGrid };
        for (size_t si = 0; si < starts.size() && !stop; ++si) {
            uint64_t s = starts[si];
            if (!(in.search && NameExcludedIn(nameOf(s), in.exclExact, in.exclContains))) {
                path.assign(1, FromKey(s));
                rew.assign(1, std::string());
                rewVal.assign(1, 0.0);
                rewPot.assign(1, false);
                rewIds.assign(1, std::vector<std::string>{});
                dfs();
            }
            if (in.search) {
                m_searchProgress.store((int)si + 1);
                auto el = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
                if (el > SEARCH_BUDGET_MS) stop = true;
            }
        }
        m_lastExplored.store(explored, std::memory_order_relaxed);
        m_lastEmitted.store((int)out.size(), std::memory_order_relaxed);
        if ((int)out.size() >= resultCap)
            ctx()->Log.Info(in.search ? "[Foretold] search results truncated at cap" : "[Foretold] chain list truncated at cap");
        if (in.search) {   // diagnostic: compare against the OB scan log (same conn/starts => list must be a superset)
            char lg[192];
            std::snprintf(lg, sizeof(lg), "[Foretold] %s: conn=%d starts=%d purple=%d explored=%d emitted=%d",
                          in.bestValue ? "bestpath" : "search", (int)in.conn.size(), (int)starts.size(),
                          (int)in.purple.size(), explored, (int)out.size());
            ctx()->Log.Info(lg);
        }
    }

    // Enumerate every COMPLETE route (length = pathCap) from the loaded purple starts, honoring the
    // SAME edge graph + exclusions + impassable-map rules as BuildChains, and collect the distinct
    // reward ids that land on a realizable (depth a3, neighbour-rank a4) cell. This is the route-
    // DEPENDENT obtainable set: every id here has a findable "Path" by construction. Limited to
    // currently-loaded maps (connTable is viewport-dependent) — the list grows as the line loads.
    void CollectRoutableRewardIds(const WorkerInput& in,
                                  const std::vector<std::vector<FR::Reward>>& table,
                                  bool tableOk, std::vector<std::string>& out, bool& aborted) {
        out.clear(); aborted = false;
        if (!tableOk || in.pathCap < 2 || in.pathCap > 60) return;
        auto markerOf = [&](uint64_t k) -> uint32_t { auto it = in.marker.find(k); return it != in.marker.end() ? it->second : 0; };
        auto isReward = [&](uint64_t k) { return markerOf(k) != FR::MARKER_NONREWARD; };
        std::unordered_map<uint64_t, std::string> nameCache;
        auto nameOf = [&](uint64_t k) -> std::string {
            auto c = nameCache.find(k); if (c != nameCache.end()) return c->second;
            auto it = in.uiPtr.find(k);
            std::string nm = (it != in.uiPtr.end()) ? m_core.NodeName(it->second) : "";
            if (nm.empty()) { FR::Grid g = FromKey(k); char b[32]; std::snprintf(b, sizeof(b), "(%d,%d)", g.x, g.y); nm = b; }
            nameCache[k] = nm; return nm;
        };

        const int D = in.pathCap;                  // chain length (START + D-1 reward maps)
        const int R = FR::A4_MAX + 1;              // neighbour ranks 0..A4_MAX
        std::vector<uint8_t> cellSeen((size_t)D * R, 0);     // realizable (depth a3, rank a4)
        auto seen = [&](int a3, int a4) -> uint8_t& { return cellSeen[(size_t)a3 * R + a4]; };
        int unseenAtDepth[64] = { 0 };
        for (int d = 1; d < D; ++d) unseenAtDepth[d] = R;
        auto anyUnseenDeeper = [&](int from) -> bool {
            for (int d = from; d < D; ++d) if (unseenAtDepth[d] > 0) return true;
            return false;
        };

        std::vector<FR::Grid> path;
        std::vector<int> ranks;                    // rank chosen at each depth (ranks[0] = START, unused)
        int explored = 0; bool stop = false; bool budget = false;
        const auto t0 = std::chrono::steady_clock::now();

        std::function<void()> dfs = [&]() {
            if (stop) return;
            if (++explored >= OB_EXPLORE_CAP) { stop = true; return; }
            if ((explored & 0xFFF) == 0 && m_routeWaiting.load(std::memory_order_relaxed)) { aborted = true; stop = true; return; }  // yield to a user route job
            if ((int)path.size() >= D) {           // complete chain -> confirm its (depth,rank) cells
                for (int i = 1; i < D; ++i) {
                    uint8_t& s = seen(i, ranks[i]);
                    if (!s) { s = 1; if (unseenAtDepth[i] > 0) --unseenAtDepth[i]; }
                }
                return;
            }
            auto cit = in.conn.find(FR::GridKey(path.back()));
            if (cit == in.conn.end()) return;      // dead end -> incomplete, drop
            std::vector<FR::Grid> sorted = FR::Core::SortedNeighbors(cit->second, path);
            int a3 = (int)path.size();
            for (int r = 0; r < (int)sorted.size() && r <= FR::A4_MAX; ++r) {
                if (seen(a3, r) && !anyUnseenDeeper(a3 + 1)) continue;   // prune: nothing new down this branch
                uint64_t ck = FR::GridKey(sorted[r]);
                if (!isReward(ck) || in.excluded.count(ck)) continue;
                if (NameExcludedIn(nameOf(ck), in.exclExact, in.exclContains)) continue;
                path.push_back(sorted[r]); ranks.push_back(r);
                dfs();
                path.pop_back(); ranks.pop_back();
                if (stop) return;
            }
        };

        // Explore EVERY start within the budget — parity with Search. (No convergence cut-off: it
        // could stop before a LATER start's cell was confirmed, e.g. the Very Rare Unique cell, which
        // is exactly the "Search finds it but the table doesn't" bug.)
        for (size_t si = 0; si < in.starts.size() && !stop; ++si) {
            uint64_t s = in.starts[si];
            if (NameExcludedIn(nameOf(s), in.exclExact, in.exclContains)) continue;
            path.assign(1, FromKey(s)); ranks.assign(1, 0);
            dfs();
            if (aborted) break;                    // a user route job is waiting -> yield (result is partial)
            auto el = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            if (el > OB_BUDGET_MS) { stop = true; budget = true; }
            if (!anyUnseenDeeper(1)) break;        // every (depth,rank) cell confirmed -> done early
        }

        // realizable cells -> distinct reward ids
        std::unordered_set<std::string> idset;
        for (int a3 = 1; a3 < D && a3 < (int)table.size(); ++a3)
            for (int a4 = 0; a4 <= FR::A4_MAX; ++a4)
                if (seen(a3, a4))
                    for (const auto& id : table[a3][a4].mods)
                        if (!id.empty() && idset.insert(id).second) out.push_back(id);

        const char* reason = aborted ? "YIELD" : (explored >= OB_EXPLORE_CAP) ? "CAP" : budget ? "BUDGET" : "done";
        char lg[192];
        std::snprintf(lg, sizeof(lg), "[Foretold] OB scan: conn=%d starts=%d purple=%d explored=%d rewards=%d stop=%s",
                      (int)in.conn.size(), (int)in.starts.size(), (int)in.purple.size(), explored, (int)out.size(), reason);
        ctx()->Log.Info(lg);
    }

    // ---------- drawing ----------
    void DrawBox(const ImVec2& at, const std::vector<std::string>& lines, ImU32 titleCol) {
        if (lines.empty()) return;
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        float pad = 6.0f, lh = ImGui::GetTextLineHeight() + 3.0f, w = 0;
        for (const auto& l : lines) w = (std::max)(w, ImGui::CalcTextSize(l.c_str()).x);
        ImVec2 disp = ImGui::GetIO().DisplaySize;
        float boxW = w + pad * 2, boxH = lines.size() * lh + pad * 2;
        ImVec2 p0(at.x + 12, at.y - boxH - 16);      // above the cursor so it never covers the text
        if (p0.y < 4) p0.y = at.y + 20;              // flip below only if no room above
        if (p0.x + boxW > disp.x) p0.x = disp.x - boxW - 4;
        if (p0.x < 4) p0.x = 4;
        ImVec2 p1(p0.x + boxW, p0.y + boxH);
        dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 225), 4.0f);
        dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 60), 4.0f);
        float y = p0.y + pad;
        for (size_t i = 0; i < lines.size(); ++i) {
            dl->AddText(ImVec2(p0.x + pad, y), i == 0 ? titleCol : IM_COL32(210, 220, 235, 255), lines[i].c_str());
            y += lh;
        }
    }

    void DrawHighlightRing(uint64_t grid, ImU32 col) {
        float x, y, w, h;
        if (!RawRect(grid, x, y, w, h)) return;          // fresh rect -> tracks during movement
        ImVec2 c(x + w * 0.5f, y + h * 0.5f);
        float rad = (std::max)(14.0f, (std::max)(w, h) * 0.6f);
        ImGui::GetForegroundDrawList()->AddCircle(c, rad, col, 0, 2.5f, ImDrawFlags_StrokeCenter);
    }

    void DrawChip(ImDrawList* dl, ImVec2 anchorBottomCenter, const std::string& text, bool start) {
        ImVec2 sz = ImGui::CalcTextSize(text.c_str());
        float pad = 3.0f;
        ImVec2 p0(anchorBottomCenter.x - sz.x * 0.5f - pad, anchorBottomCenter.y - sz.y - pad * 2);
        ImVec2 p1(anchorBottomCenter.x + sz.x * 0.5f + pad, anchorBottomCenter.y);
        dl->AddRectFilled(p0, p1, start ? IM_COL32(15, 55, 75, 235) : IM_COL32(25, 25, 30, 235), 3.0f);
        dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 45), 3.0f);
        dl->AddText(ImVec2(anchorBottomCenter.x - sz.x * 0.5f, p0.y + pad),
                    start ? IM_COL32(120, 230, 255, 255) : IM_COL32(255, 225, 140, 255), text.c_str());
    }

    // Vertical stack of color-attributed reward chips above a node (border+text = path color).
    void DrawStackedChips(ImDrawList* dl, ImVec2 nodeCenter, const std::vector<VarLbl>& labels) {
        int n = (int)labels.size();
        int shown = (std::min)(n, MAX_STACK);
        float lh = ImGui::GetTextLineHeight() + 4.0f;
        int   extra  = (n > shown) ? 1 : 0;              // the "+K more" line
        float totalH = (shown + extra) * lh;
        float y = nodeCenter.y - 14.0f - totalH;         // prefer stacking above the node
        if (y < 2.0f) y = nodeCenter.y + 14.0f;          // no room above -> flip below the node
        for (int i = 0; i < shown; ++i) {
            const VarLbl& L = labels[i];
            const char* txt = L.reward.c_str();
            ImVec2 ts = ImGui::CalcTextSize(txt);
            ImVec2 q0(nodeCenter.x - ts.x * 0.5f - 4, y);
            ImVec2 q1(nodeCenter.x + ts.x * 0.5f + 4, y + lh - 2);
            dl->AddRectFilled(q0, q1, IM_COL32(20, 20, 25, 235), 3.0f);
            dl->AddRect(q0, q1, L.col, 3.0f, 2.0f, ImDrawFlags_StrokeCenter);
            dl->AddText(ImVec2(nodeCenter.x - ts.x * 0.5f, y + 1), L.col, txt);
            y += lh;
        }
        if (n > shown) {
            char b[16]; std::snprintf(b, sizeof(b), "+%d more", n - shown);
            ImVec2 ts = ImGui::CalcTextSize(b);
            dl->AddText(ImVec2(nodeCenter.x - ts.x * 0.5f, y + 1), IM_COL32(200, 200, 200, 255), b);
        }
    }

    void HandleHover() {
        ImVec2 mp = CursorClient();

        bool ctrl = (::GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool alt  = (::GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
        bool combo = ctrl && alt;
        bool comboEdge = combo && !m_comboPrev; m_comboPrev = combo;
        bool esc = (::GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
        bool escEdge = esc && !m_escPrev; m_escPrev = esc;

        uint64_t hov = m_rectsFresh ? HoveredNode(mp) : 0;

        if (m_debug) {
            const char* st = m_lockState == LockState::Idle ? "idle"
                           : m_lockState == LockState::Computing ? "computing" : "ready";
            char dbg[192];
            std::snprintf(dbg, sizeof(dbg), "FR: state=%s fresh=%d rects=%d revA=%d chains=%d sel=%d seed=%08X",
                          st, m_rectsFresh ? 1 : 0, (int)m_rectByGrid.size(), (int)m_core.Snap().revealedA.size(),
                          (int)m_chains.size(), (int)m_selectedChains.size(), m_core.Snap().lineSeed);
            ImGui::GetForegroundDrawList()->AddText(ImVec2(12, 36), IM_COL32(255, 240, 120, 255), dbg);
            if (hov) {                                   // coords of the hovered map, fixed top-left (cursor never covers it)
                const auto& snap = m_core.Snap();
                auto it = snap.byGrid.find(hov);
                uint32_t mk = (it != snap.byGrid.end()) ? it->second->marker : 0;
                FR::Grid g = FromKey(hov);
                char cb[112];
                std::snprintf(cb, sizeof(cb), "HOVER  (%d, %d)   mk=0x%X   %s", g.x, g.y, mk,
                              snap.revealedA.count(hov) ? "PURPLE" : "not-purple");
                ImDrawList* fdl = ImGui::GetForegroundDrawList();
                ImVec2 ts = ImGui::CalcTextSize(cb);
                ImVec2 p(12.0f, 58.0f);
                fdl->AddRectFilled(ImVec2(p.x - 5, p.y - 3), ImVec2(p.x + ts.x + 7, p.y + ts.y + 3), IM_COL32(0, 0, 0, 230), 4.0f);
                fdl->AddRect(ImVec2(p.x - 5, p.y - 3), ImVec2(p.x + ts.x + 7, p.y + ts.y + 3), IM_COL32(255, 255, 255, 90), 4.0f);
                fdl->AddText(p, IM_COL32(255, 255, 255, 255), cb);
            }
        }

        // Esc unlocks any active display (lock or search). Works while the atlas moves.
        if (m_lockState != LockState::Idle && escEdge) { Unlock(); return; }

        // Hover interaction is ALWAYS available, regardless of lock/search display state.
        // (Previously gated on lockState==Idle, which made hover die after a Search.)
        if (ImGui::GetIO().WantCaptureMouse) return;   // cursor over an ImGui window -> don't lock a map under it
        if (!m_rectsFresh) {                     // node positions still settling -> wait indicator
            int d = (int)(ImGui::GetTime() * 3) % 4;
            char b[20]; std::snprintf(b, sizeof(b), "loading%.*s", d, "...");
            DrawBox(mp, { b }, IM_COL32(255, 220, 120, 255));
            return;
        }
        // Keep the locked-start ring visible while a single START is locked (not for search results).
        if (m_lockState == LockState::Ready && !m_isSearchResult && m_lockedStart)
            DrawHighlightRing(m_lockedStart, IM_COL32(190, 130, 255, 220));
        if (!hov) return;
        if (IsEligible(hov)) {                   // only purple (selVecA) Rite reward maps can start a path
            DrawHighlightRing(hov, IM_COL32(190, 130, 255, 255));
            DrawBox(mp, { NameOf(FromKey(hov)), "Ctrl+Alt: lock as START" }, IM_COL32(190, 130, 255, 255));
            if (comboEdge && m_core.Snap().lineSeed != 0) StartLock(hov);   // replaces the panel with this start
        }
    }

    void DrawChainRow(int shownPos) {
        int ci = m_shownIdx[shownPos];
        const Chain& ch = m_chains[ci];
        // pre-click selection rank (index in m_selectedChains, or -1)
        int selRank = -1;
        for (int k = 0; k < (int)m_selectedChains.size(); ++k) if (m_selectedChains[k] == ci) { selRank = k; break; }

        float rowH = ImGui::GetTextLineHeight() * 2.0f + 6.0f;
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::PushID(ci);
        if (ImGui::Selectable("##row", selRank >= 0, ImGuiSelectableFlags_None, ImVec2(0, rowH))) {
            ImGuiIO& io = ImGui::GetIO();
            if (io.KeyShift && m_selAnchor >= 0) {                       // range add
                int a = (std::min)(m_selAnchor, shownPos), b = (std::max)(m_selAnchor, shownPos);
                for (int p = a; p <= b && p < (int)m_shownIdx.size(); ++p) {
                    int cc = m_shownIdx[p];
                    if (std::find(m_selectedChains.begin(), m_selectedChains.end(), cc) == m_selectedChains.end())
                        m_selectedChains.push_back(cc);
                }
            } else if (io.KeyCtrl) {                                     // toggle one
                auto it = std::find(m_selectedChains.begin(), m_selectedChains.end(), ci);
                if (it != m_selectedChains.end()) m_selectedChains.erase(it);
                else m_selectedChains.push_back(ci);
                m_selAnchor = shownPos;
            } else {                                                     // select only this
                m_selectedChains.assign(1, ci);
                m_selAnchor = shownPos;
            }
        }
        ImGui::PopID();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        float textX = p0.x + 4;
        if (selRank >= 0) {
            // swatch = the color this chain is actually drawn with:
            // sole selection -> the rich-route yellow; otherwise its palette color.
            ImU32 sc = (m_selectedChains.size() == 1) ? IM_COL32(255, 210, 90, 255)
                                                      : kPalette[selRank % PALETTE_N];
            dl->AddRectFilled(ImVec2(p0.x + 3, p0.y + 3), ImVec2(p0.x + 13, p0.y + 13), sc, 2.0f);
            textX = p0.x + 18;
        }
        std::string route; for (size_t i = 0; i < ch.size(); ++i) { if (i) route += "  >  "; route += ch[i].name; }
        // total route value (sum of all reward values) so the user sees the route's total profit
        double cvChaos = 0.0; bool hasPotential = false;
        for (size_t i = 1; i < ch.size(); ++i) { cvChaos += ch[i].value; if (ch[i].potential) hasPotential = true; }
        if (cvChaos > 0.0) {
            float div = ctx()->Prices.GetRates().divineInChaos;
            float tv = (m_poolCurrency == PriceApi::DisplayCurrency::Divine) ? (float)(cvChaos / (div > 0 ? div : 1.0f)) : (float)cvChaos;
            route += "    | Total: " + PriceApi::FormatPrice(tv, m_poolCurrency);
            if (hasPotential) route += " (incl. potential)";
        }
        dl->AddText(ImVec2(textX, p0.y + 2), IM_COL32(235, 235, 245, 255), route.c_str());
        // Reward line: colour each reward by the priority of the search term it matches (term[0] =
        // most important -> hottest colour). Non-matching / beyond-12 rewards use the default colour.
        float rx = textX + 14, ry = p0.y + 2 + ImGui::GetTextLineHeight() + 2;
        for (size_t i = 1; i < ch.size(); ++i) {
            if (i > 1) { const char* sep = "  -  "; dl->AddText(ImVec2(rx, ry), IM_COL32(120, 120, 130, 255), sep); rx += ImGui::CalcTextSize(sep).x; }
            std::string r = ch[i].reward.empty() ? "(none)" : ch[i].reward;
            dl->AddText(ImVec2(rx, ry), RewardColour(r), r.c_str());
            rx += ImGui::CalcTextSize(r.c_str()).x;
        }
    }

    static std::string ChainKey(const Chain& ch) {
        std::string k; char b[24];
        for (const auto& nd : ch) { std::snprintf(b, sizeof(b), "%llu;", (unsigned long long)nd.grid); k += b; }
        return k;
    }

    bool ChainMatches(const Chain& ch, const std::string& f) {
        for (const auto& nd : ch) {
            if (ToLower(nd.name).find(f) != std::string::npos) return true;
            if (ToLower(nd.reward).find(f) != std::string::npos) return true;
        }
        return false;
    }

    // --- search-result priority (term[0] = most important) ---
    static constexpr int kNoPriority = 1 << 30;
    // Lowest search-term index whose text appears in `reward` (0 = most important); kNoPriority if none.
    int RewardPriorityIdx(const std::string& reward) const {
        if (m_priorityTerms.empty() || reward.empty()) return kNoPriority;
        std::string lr = ToLower(reward);
        for (int i = 0; i < (int)m_priorityTerms.size(); ++i)
            if (!m_priorityTerms[i].empty() && lr.find(m_priorityTerms[i]) != std::string::npos) return i;
        return kNoPriority;
    }
    // Best (lowest) priority across all of a chain's reward labels — used to order the result list.
    int ChainPriorityIdx(const Chain& ch) const {
        int best = kNoPriority;
        for (size_t i = 1; i < ch.size(); ++i) { int p = RewardPriorityIdx(ch[i].reward); if (p < best) best = p; }
        return best;
    }
    // Colour for a reward label: the priority term's palette colour (first 12) or the default blue.
    ImU32 RewardColour(const std::string& reward) const {
        int p = RewardPriorityIdx(reward);
        return (p < kPriorityCount) ? kPriorityPalette[p] : kRewardDefaultCol;
    }

    // Rebuild m_shownIdx (filtered chain indices) only when the chain list or the
    // filter text changed. Cheap no-op otherwise — safe to call every frame.
    void EnsureShownIdx() {
        std::string f = ToLower(m_filter);
        if (!m_shownDirty && f == m_shownFilter) return;
        m_shownIdx.clear();
        m_shownIdx.reserve(m_chains.size());
        if (m_priorityTerms.empty()) {
            // lock mode (no search terms): keep DFS order
            for (int i = 0; i < (int)m_chains.size(); ++i)
                if (f.empty() || ChainMatches(m_chains[i], f)) m_shownIdx.push_back(i);
        } else {
            // search mode: show chains matching the FIRST-listed term (term[0] = most important)
            // first, then term[1], etc. Stable within a tier -> preserves DFS order. Priority is
            // computed once per chain here (not in the comparator).
            std::vector<std::pair<int, int>> ord;   // {priority, chainIdx}
            ord.reserve(m_chains.size());
            for (int i = 0; i < (int)m_chains.size(); ++i)
                if (f.empty() || ChainMatches(m_chains[i], f)) ord.push_back({ ChainPriorityIdx(m_chains[i]), i });
            std::stable_sort(ord.begin(), ord.end(),
                             [](const std::pair<int, int>& a, const std::pair<int, int>& b) { return a.first < b.first; });
            for (const auto& p : ord) m_shownIdx.push_back(p.second);
        }
        m_shownFilter = f;
        m_shownDirty  = false;
        m_selAnchor = -1;   // positions are invalid after a rebuild; next Shift-click re-anchors
    }

    // ---------- reward picker (autocomplete + browse; both feed the comma-separated search box) ----------
    // Push picked text into the search box. ImGui keeps its OWN edit buffer while an InputText is
    // active, so a plain write to m_searchBuf from a click below the (still-focused) box is
    // discarded when the box deactivates. Re-assert the value for a few frames after the InputText
    // submits (see DrawWindow) to defeat that clobber.
    void SetSearchText(const std::string& s) {
        std::snprintf(m_searchBuf, sizeof(m_searchBuf), "%s", s.c_str());
        m_pendingSearch = s; m_pendingFrames = 3;
        SaveSearch();
    }
    // Replace the term being typed (text after the last comma) with `label`.
    void ReplaceActiveTerm(const std::string& label) {
        std::string s = m_searchBuf;
        size_t c = s.find_last_of(',');
        std::string out = (c == std::string::npos) ? std::string() : s.substr(0, c + 1);
        if (!out.empty()) out += " ";
        out += label;
        SetSearchText(out);
    }
    // Append `label` as a new term (no-op if already present).
    void AppendSearchTerm(const std::string& label) {
        std::string s = Trim(m_searchBuf);
        if (ToLower(s).find(ToLower(label)) != std::string::npos) return;   // already in the box
        if (!s.empty()) s += (s.back() == ',') ? " " : ", ";
        s += label;
        SetSearchText(s);
    }

    // Live autocomplete: match the term after the last comma against all reward labels/ids.
    void DrawRewardAutocomplete(bool inputActive, float width) {
        if (!inputActive) return;
        std::string s = m_searchBuf;
        size_t c = s.find_last_of(',');
        std::string term = ToLower(Trim(c == std::string::npos ? s : s.substr(c + 1)));
        if (term.empty()) return;
        std::vector<const FR::RewardDef*> hits;
        for (const auto& d : FR::AllRewards()) {
            if (ToLower(d.label).find(term) != std::string::npos) {   // match the shown label, not the internal id
                hits.push_back(&d);
                if (hits.size() >= 8) break;
            }
        }
        if (hits.empty()) return;
        float h = hits.size() * ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y * 2.0f;
        ImGui::BeginChild("##fr_autocomplete", ImVec2(width, h), true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        for (const FR::RewardDef* d : hits) {
            ImGui::PushID(d->id);
            if (ImGui::Selectable(d->label)) ReplaceActiveTerm(d->label);
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    // Browse panel: full reward list grouped by section, own filter; click + to append a term.
    void DrawRewardBrowser() {
        if (!m_showBrowse) return;
        ImGui::BeginChild("##fr_browser", ImVec2(0, 220), true);
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##fr_browse_filter", "filter rewards...", m_browseFilter, sizeof(m_browseFilter));
        std::string bf = ToLower(Trim(m_browseFilter));
        std::string curGroup;
        for (const auto& d : FR::AllRewards()) {
            if (!bf.empty() && ToLower(d.label).find(bf) == std::string::npos)   // match the shown label only
                continue;
            if (curGroup != d.group) { curGroup = d.group; ImGui::SeparatorText(d.group); }
            ImGui::PushID(d.id);
            if (ImGui::SmallButton("+")) AppendSearchTerm(d.label);
            ImGui::SameLine();
            ImGui::TextUnformatted(d.label);
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    void DrawPricesTab() {
        ImGui::TextWrapped("Reward values are supplied by the host price service (poe2scout).");
        ImGui::Separator();

        auto st = ctx()->Prices.GetStatus();
        ImGui::Text("Status: %s", st.loaded ? "loaded" : "loading...");
        ImGui::Text("Items: %d", st.totalItems);
        ImGui::Text("Rates: 1 Divine = %.0f Chaos | 1 Exalted = %.1f Chaos",
                    st.divineInChaos, st.exaltedInChaos);

        ImGui::Separator();
        DrawPoolSection();   // full theoretical pool (moved here from the main window)
    }

    static ImU32 PoolGroupColor(const std::string& group) {
        if (group == "Unique")        return IM_COL32(255, 200, 90, 255);   // gold
        if (group == "Omen")          return IM_COL32(200, 130, 255, 255);  // purple
        if (group == "Currency")      return IM_COL32(240, 225, 120, 255);  // yellow
        if (group == "Crafting")      return IM_COL32(120, 210, 255, 255);  // cyan
        if (group == "Proliferation") return IM_COL32(150, 165, 180, 255);  // gray-blue
        return IM_COL32(210, 210, 220, 255);
    }

    // Render the "Value" table cell for a pool/atlas row: a real price, an amber "~N div" POTENTIAL
    // placeholder (Very Rare Unique — random unique, can't be priced), or "-" when truly unpriced.
    void DrawValueCell(const FR::PoolRow& r, float divInChaos) {
        if (r.priced || r.potential) {
            float v = (m_poolCurrency == PriceApi::DisplayCurrency::Divine)
                    ? (float)(r.chaos / (divInChaos > 0 ? divInChaos : 1.0f))
                    : (float)r.chaos;
            std::string s = PriceApi::FormatPrice(v, m_poolCurrency);
            if (r.potential) {
                ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.35f, 1.0f), "~%s", s.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Potential value - not a real price.\n"
                                      "\"Very Rare Unique\" is a random very rare unique, so it can't be priced;\n"
                                      "%g Divine is a fixed placeholder so routes that yield it aren't ignored.",
                                      FR::kVeryRareUniquePotentialDivine);
            } else {
                ImGui::TextUnformatted(s.c_str());
            }
        } else {
            ImGui::TextDisabled("-");
        }
    }

    // Rebuild m_poolView when the line changed or prices just finished loading. Cheap
    // (one bulk v57 read + 64-row join + sort); cached so the table render is a plain draw.
    void RefreshPoolView() {
        const auto& snap = m_core.Snap();
        bool loaded = ctx()->Prices.GetStatus().loaded;
        if (snap.lineSeed == m_poolSeed && loaded == m_poolBuiltLoaded) return;
        std::vector<FR::PoolEntry> entries;
        if (snap.lineSeed != 0 && m_core.ComputePool(snap.lineSeed, entries)) {
            auto look = [this](const std::string& n) { return (double)ctx()->Prices.LookupPrice(n).chaos; };
            m_poolView = FR::BuildPoolView(entries, look, ctx()->Prices.GetRates().divineInChaos);
            if (!loaded)   // prices not in yet -> show by chance until they load
                std::stable_sort(m_poolView.begin(), m_poolView.end(),
                    [](const FR::PoolRow& a, const FR::PoolRow& b) { return a.probability > b.probability; });
        } else {
            m_poolView.clear();
        }
        m_poolSeed = snap.lineSeed;
        m_poolBuiltLoaded = loaded;
    }

    void DrawPoolSection() {
        const auto& snap = m_core.Snap();
        if (snap.lineSeed == 0) return;
        RefreshPoolView();

        char hdr[64];
        std::snprintf(hdr, sizeof(hdr), "Reward Pool - this line (%d)###fr_pool_hdr", (int)m_poolView.size());
        if (!ImGui::CollapsingHeader(hdr, ImGuiTreeNodeFlags_DefaultOpen)) return;

        bool loaded = ctx()->Prices.GetStatus().loaded;
        ImGui::TextDisabled("prices %s", loaded ? "loaded" : "loading...");
        ImGui::SameLine();
        const char* curLbl = (m_poolCurrency == PriceApi::DisplayCurrency::Divine) ? "Divine" : "Chaos";
        if (ImGui::SmallButton(curLbl))
            m_poolCurrency = (m_poolCurrency == PriceApi::DisplayCurrency::Divine)
                           ? PriceApi::DisplayCurrency::Chaos : PriceApi::DisplayCurrency::Divine;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle value currency");

        float divInChaos = ctx()->Prices.GetRates().divineInChaos;
        ImGuiTableFlags tf = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("##fr_pool_tbl", 3, tf, ImVec2(0, 260))) {
            ImGui::TableSetupColumn("Reward", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Value",  ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Chance", ImGuiTableColumnFlags_WidthFixed, 64.0f);
            ImGui::TableHeadersRow();
            for (const auto& r : m_poolView) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(PoolGroupColor(r.group)), "%s", r.label.c_str());
                ImGui::TableNextColumn();
                DrawValueCell(r, divInChaos);
                ImGui::TableNextColumn();
                ImGui::Text("%.2f%%", r.probability * 100.0);
            }
            ImGui::EndTable();
        }
        if (!loaded)
            ImGui::TextDisabled("(prices still loading from poe2scout; sorted by chance until ready)");
    }

    // Rebuild m_atlasView: the deduped set of rewards obtainable by routing THIS line — every
    // distinct reward across g(a3,a4) for a3 in [1..pathCap], a4 in [0..4] (the reward table the
    // line's routes can yield). Depends only on lineSeed + v57, so it's stable as you pan/select
    // (it is the line's route-space, not the theoretical pool). Exact for the first ring,
    // approximate at depth (the known reward-fn limitation). Cached by (lineSeed, prices-loaded).
    void RefreshAtlasView() {
        const auto& snap = m_core.Snap();
        bool loaded = ctx()->Prices.GetStatus().loaded;
        if (!m_atlasDirty && snap.lineSeed == m_atlasSeed && loaded == m_atlasBuiltLoaded) return;
        m_atlasView.clear();
        // Source: the route-DEPENDENT obtainable set collected by the background scan
        // (StartObtainableScan -> CollectRoutableRewardIds). Every id is reachable by a real complete
        // route among currently-loaded maps, so its per-reward "Path" always finds chains. The list
        // grows as more of the line loads (zoom out / pan); see StartObtainableIfNeeded.
        if (snap.lineSeed != 0 && !m_routableIds.empty()) {
            std::vector<FR::PoolEntry> entries; entries.reserve(m_routableIds.size());
            for (const auto& id : m_routableIds) { FR::PoolEntry e; e.id = id; entries.push_back(std::move(e)); }
            auto look = [this](const std::string& n) { return (double)ctx()->Prices.LookupPrice(n).chaos; };
            m_atlasView = FR::BuildPoolView(entries, look, ctx()->Prices.GetRates().divineInChaos);   // tiered value sort
        }
        m_atlasSeed = snap.lineSeed;
        m_atlasBuiltLoaded = loaded;
        m_atlasDirty = false;
    }

    // The old "Atlas rewards - obtainable this line" table was removed (2026-06-17) — it could not be
    // kept reliably equal to the live Search over a viewport-dependent connTable. Replaced by a single
    // big, centered "Find best path" button; results show in the chains list below.
    void DrawFindBestPathBar() {
        const auto& snap = m_core.Snap();
        if (snap.lineSeed == 0) return;
        ImGui::Spacing();
        float avail = ImGui::GetContentRegionAvail().x;
        if (!ctx()->Prices.GetStatus().loaded) {   // best-path ranks by value -> needs prices; show a centered hint until ready
            const char* msg = "(prices loading...)";
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - ImGui::CalcTextSize(msg).x) * 0.5f);
            ImGui::TextDisabled("%s", msg);
            ImGui::Spacing();
            return;
        }
        float btnW = avail;
        if (avail > 240.0f) btnW = std::max(240.0f, avail * 0.6f);   // long, but not edge-to-edge on wide windows
        float btnH = ImGui::GetFrameHeight() * 1.3f;                 // prominent but not too tall
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - btnW) * 0.5f);   // centered
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.25f);   // larger label
        bool go = ImGui::Button("Find best path", ImVec2(btnW, btnH));
        ImGui::PopFont();
        if (go) StartSearch("", /*bestValue=*/true);                 // route(s) with the highest TOTAL reward value
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Show the route(s) with the highest total reward value\n(ties are listed together; deep values are approximate)");
        ImGui::Spacing();
    }

    void DrawWindow() {
        const auto& snap = m_core.Snap();
        ImGui::SetNextWindowSize(ImVec2(640, 560), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(m_windowAlpha);   // window transparency (opacity slider on the controls row)
        if (!ImGui::Begin("Foretold Rewards##FR", &m_showWindow)) { ImGui::End(); return; }

        if (!snap.valid) { ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "Atlas panel not found - open the Atlas."); ImGui::End(); return; }
        if (snap.lineSeed == 0) { ImGui::TextDisabled("No Rite line active (apply Head of the King)."); ImGui::End(); return; }

        { ScopedT _t(m_prof.windowFilter); EnsureShownIdx(); }

        // Single controls row: search input + Search + Clear + Live + Browse toggle, sized so all fit.
        ImGuiStyle& st = ImGui::GetStyle();
        const float alphaW = 64.0f;   // opacity slider width (reserved on the controls row)
        float reserve = ImGui::CalcTextSize("Search").x + ImGui::CalcTextSize("Clear").x
                      + (ImGui::GetFrameHeight() + ImGui::CalcTextSize("Live").x)   // Live checkbox
                      + ImGui::GetFrameHeight()                                     // Browse arrow button (square)
                      + alphaW                                                      // opacity slider
                      + st.FramePadding.x * 4 + st.ItemSpacing.x * 5 + st.ItemInnerSpacing.x + 12.0f;
        float inputW = ImGui::GetContentRegionAvail().x - reserve;
        if (inputW < 140.0f) inputW = 140.0f;
        ImGui::SetNextItemWidth(inputW);
        bool go = ImGui::InputTextWithHint("##fr_search", "rewards to find (comma-sep, partial)", m_searchBuf, sizeof(m_searchBuf), ImGuiInputTextFlags_EnterReturnsTrue);
        bool inputActive = ImGui::IsItemActive();   // gates the autocomplete dropdown below
        if (ImGui::IsItemDeactivatedAfterEdit()) SaveSearch();
        // Re-assert a just-picked reward into the box AFTER the InputText submitted, so ImGui's
        // internal edit buffer (kept while the box was focused) doesn't overwrite the pick.
        if (m_pendingFrames > 0) { std::snprintf(m_searchBuf, sizeof(m_searchBuf), "%s", m_pendingSearch.c_str()); --m_pendingFrames; }
        ImGui::SameLine();
        if ((ImGui::Button("Search") || go) && m_searchBuf[0]) StartSearch(m_searchBuf);
        ImGui::SameLine();
        if (ImGui::Button("Clear")) { m_searchBuf[0] = '\0'; if (m_lockState != LockState::Idle) Unlock(); }   // clear the search box (+ reset any active search/lock)
        ImGui::SameLine();
        ImGui::Checkbox("Live", &m_liveSearch);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keep scanning newly-loaded atlas areas while you pan");
        ImGui::SameLine();
        if (ImGui::ArrowButton("##fr_browse_toggle", m_showBrowse ? ImGuiDir_Up : ImGuiDir_Down)) m_showBrowse = !m_showBrowse;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Browse all rewards");
        ImGui::SameLine();
        int alphaPct = (int)(m_windowAlpha * 100.0f + 0.5f);
        ImGui::SetNextItemWidth(alphaW);
        if (ImGui::SliderInt("##fr_alpha", &alphaPct, 20, 100, "%d%%")) m_windowAlpha = (float)alphaPct / 100.0f;
        if (ImGui::IsItemDeactivatedAfterEdit()) SavePrefs();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Window opacity");

        DrawRewardAutocomplete(inputActive, inputW);   // live dropdown under the box while typing
        DrawRewardBrowser();                            // full grouped list (toggled by the arrow)
        ImGui::Separator();
        DrawFindBestPathBar();                          // big centered "Find best path" (replaced the obtainable table)
        ImGui::Separator();

        if (m_lockState == LockState::Idle) {
            ImGui::TextDisabled("Hover a map + Ctrl+Alt to lock a START, or search rewards above.");
            ImGui::Text("seed=%08X  pathCap=%d  rects=%d", snap.lineSeed, snap.pathCap, (int)m_rectByGrid.size());
            ImGui::End(); return;
        }
        if (m_lockState == LockState::Computing) {
            int dots = (int)(ImGui::GetTime() * 3) % 4;
            if (m_isSearchResult) {
                int done = m_searchProgress.load(), tot = m_searchTotal.load();
                ImGui::Text("Searching%.*s   %d / %d starts", dots, "...", done, tot);
                ImGui::ProgressBar(tot > 0 ? (float)done / (float)tot : 0.0f, ImVec2(-1, 0));
                ImGui::TextDisabled("(all purple starts; only currently-loaded line maps are covered)");
            } else {
                ImGui::Text("Computing rewards%.*s", dots, "...");
                ImGui::TextDisabled("(computing reward table + enumerating routes)");
            }
            ImGui::End(); return;
        }

        if (m_isSearchResult) {
            ImGui::Text("Found %d chains for: %s", (int)m_chains.size(), m_searchTermsDisplay.c_str());
            if (m_liveSearch && m_appendPending) { ImGui::SameLine(); ImGui::TextDisabled("(scanning new areas...)"); }
        }
        else
            ImGui::Text("Start: %s   (Esc to unlock)", NameOf(FromKey(m_lockedStart)).c_str());
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##filter", "Filter shown chains by map or reward...", m_filter, sizeof(m_filter));
        ImGui::Separator();

        ImGui::TextDisabled("Shown: %d", (int)m_shownIdx.size());
        if (ImGui::BeginChild("chains", ImVec2(0, 0), true)) {
            float rowH = ImGui::GetTextLineHeight() * 2.0f + 6.0f;
            ImGuiListClipper clip; clip.Begin((int)m_shownIdx.size(), rowH);
            while (clip.Step())
                for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row)
                    DrawChainRow(row);
            clip.End();
            ImGui::EndChild();
        }
        ImGui::End();
    }

    void DrawAllCoords() {   // debug: grid (x,y) over on-screen nodes (bounded per-frame for FPS)
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImVec2 disp = ImGui::GetIO().DisplaySize;
        int drawn = 0;
        for (const auto& kv : m_rectByGrid) {
            const Rect& r = kv.second;
            float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
            if (cx < 0 || cx > disp.x || cy < 0 || cy > disp.y) continue;   // actual screen only
            if (++drawn > MAX_COORD_LABELS) break;                          // bound per-frame cost
            FR::Grid g = FromKey(kv.first);
            char b[24]; std::snprintf(b, sizeof(b), "%d,%d", g.x, g.y);
            ImVec2 ts = ImGui::CalcTextSize(b);
            ImVec2 p(cx - ts.x * 0.5f, cy - ts.y * 0.5f);
            dl->AddRectFilled(ImVec2(p.x - 2, p.y - 1), ImVec2(p.x + ts.x + 2, p.y + ts.y + 1), IM_COL32(0, 0, 0, 190));
            dl->AddText(p, IM_COL32(170, 255, 170, 255), b);
        }
    }

    void DrawRoute(int ci) {
        if (ci < 0 || ci >= (int)m_chains.size()) return;
        const Chain& ch = m_chains[ci];
        if (ch.empty()) return;
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImVec2 disp = ImGui::GetIO().DisplaySize;

        struct P { bool ok; ImVec2 c; };
        std::vector<P> pts; pts.reserve(ch.size());
        for (const auto& nd : ch) {
            float x, y, w, h;
            if (RawRect(nd.grid, x, y, w, h))            // fresh per node -> tracks during movement
                pts.push_back({ true, ImVec2(x + w * 0.5f, y + h * 0.5f) });
            else
                pts.push_back({ false, ImVec2() });
        }
        for (size_t i = 1; i < pts.size(); ++i)
            if (pts[i - 1].ok && pts[i].ok) dl->AddLine(pts[i - 1].c, pts[i].c, IM_COL32(255, 210, 90, 220), 2.5f);
        for (size_t i = 0; i < pts.size(); ++i) {
            if (!pts[i].ok) continue;
            bool start = (i == 0);
            dl->AddCircleFilled(pts[i].c, 7.0f, start ? IM_COL32(90, 220, 255, 255) : IM_COL32(255, 210, 90, 255));
            dl->AddCircle(pts[i].c, 9.0f, IM_COL32(0, 0, 0, 200), 0, 2.0f, ImDrawFlags_StrokeCenter);
            char num[8]; std::snprintf(num, sizeof(num), "%d", (int)i + 1);
            ImVec2 ns = ImGui::CalcTextSize(num);
            dl->AddText(ImVec2(pts[i].c.x - ns.x * 0.5f, pts[i].c.y - ns.y * 0.5f), IM_COL32(0, 0, 0, 255), num);
            const std::string lbl = start ? std::string("START") : ch[i].reward;
            if (!lbl.empty()) DrawChip(dl, ImVec2(pts[i].c.x, pts[i].c.y - 12.0f), lbl, start);
        }
        // off-screen START guide (a searched chain may be anywhere on the line)
        bool startOnScreen = pts[0].ok && pts[0].c.x >= 0 && pts[0].c.x <= disp.x && pts[0].c.y >= 0 && pts[0].c.y <= disp.y;
        if (!startOnScreen) {
            ImVec2 ctr(disp.x * 0.5f, disp.y * 0.5f);
            if (pts[0].ok) {                                  // loaded but off-screen: guide line + edge marker
                dl->AddLine(ctr, pts[0].c, IM_COL32(90, 220, 255, 220), 2.0f);
                ImVec2 e = pts[0].c; const float m = 26.0f;
                e.x = std::clamp(e.x, m, disp.x - m); e.y = std::clamp(e.y, m, disp.y - m);
                dl->AddCircleFilled(e, 8.0f, IM_COL32(90, 220, 255, 255));
                DrawChip(dl, ImVec2(e.x, e.y - 10.0f), "START >>", true);
            } else {                                          // not loaded: pan-to hint at screen centre
                FR::Grid g = FromKey(ch[0].grid);
                char b[64]; std::snprintf(b, sizeof(b), "START off-screen - pan to (%d, %d)", g.x, g.y);
                ImVec2 ts = ImGui::CalcTextSize(b);
                ImVec2 p(ctr.x - ts.x * 0.5f, ctr.y - 44.0f);
                dl->AddRectFilled(ImVec2(p.x - 5, p.y - 3), ImVec2(p.x + ts.x + 5, p.y + ts.y + 3), IM_COL32(0, 0, 0, 215), 4.0f);
                dl->AddText(p, IM_COL32(120, 220, 255, 255), b);
            }
        }
    }

    void DrawAllVariants(const std::vector<int>& chains) {
        if (chains.empty()) return;
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImVec2 disp = ImGui::GetIO().DisplaySize;

        std::map<uint64_t, std::vector<VarLbl>> perMap;   // grid -> per-path reward chips
        int offscreen = 0;

        for (int k = 0; k < (int)chains.size(); ++k) {
            if (chains[k] < 0 || chains[k] >= (int)m_chains.size()) continue;
            const Chain& ch = m_chains[chains[k]];
            if (ch.empty()) continue;
            ImU32 col = kPalette[k % PALETTE_N];
            float off = (k - ((int)chains.size() - 1) * 0.5f) * RIBBON_STEP;

            struct PT { bool ok; ImVec2 c; };
            std::vector<PT> pts; pts.reserve(ch.size());
            for (const auto& nd : ch) {
                float x, y, w, h;
                if (RawRect(nd.grid, x, y, w, h)) pts.push_back({ true, ImVec2(x + w * 0.5f, y + h * 0.5f) });
                else pts.push_back({ false, ImVec2() });
            }
            if (!pts[0].ok || pts[0].c.x < 0 || pts[0].c.x > disp.x || pts[0].c.y < 0 || pts[0].c.y > disp.y)
                ++offscreen;

            for (size_t i = 1; i < pts.size(); ++i) {
                if (!pts[i - 1].ok || !pts[i].ok) continue;
                ImVec2 a = pts[i - 1].c, b = pts[i].c;
                float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
                if (len > 0.001f) { float nx = -dy / len * off, ny = dx / len * off; a.x += nx; a.y += ny; b.x += nx; b.y += ny; }
                dl->AddLine(a, b, col, 2.5f);
            }
            for (size_t i = 0; i < pts.size(); ++i) {
                if (!pts[i].ok) continue;
                dl->AddCircleFilled(pts[i].c, 4.0f, col);
                if (i >= 1 && !ch[i].reward.empty()) perMap[ch[i].grid].push_back(VarLbl{ col, ch[i].reward }); // i>=1: skip START
            }
        }

        for (auto& kv : perMap) {
            float x, y, w, h;
            if (!RawRect(kv.first, x, y, w, h)) continue;
            float cx = x + w * 0.5f, cy = y + h * 0.5f;
            if (cx < -60 || cx > disp.x + 60 || cy < -60 || cy > disp.y + 60) continue;
            DrawStackedChips(dl, ImVec2(cx, cy), kv.second);
        }

        char hdr[64];
        std::snprintf(hdr, sizeof(hdr), "%d variants selected%s", (int)chains.size(), offscreen ? "  (some off-screen)" : "");
        dl->AddText(ImVec2(12, 86), IM_COL32(220, 220, 235, 255), hdr);
    }

    // ---------- members ----------
    FR::Core m_core;
    // ---- reward pool view (value-sorted, host-priced via ctx()->Prices) ----
    std::vector<FR::PoolRow> m_poolView;       // cached sorted rows for the current line
    uint32_t m_poolSeed = 0;                   // line the cache was built for
    bool     m_poolBuiltLoaded = false;        // price-loaded state the cache was built under
    std::vector<FR::PoolRow> m_atlasView;      // deduped rewards obtainable by routing this line (main window list)
    uint32_t m_atlasSeed = 0;                  // line the atlas-view cache was built for
    bool     m_atlasBuiltLoaded = false;       // price-loaded state the atlas-view cache was built under
    PriceApi::DisplayCurrency m_poolCurrency = PriceApi::DisplayCurrency::Divine;
    bool m_showWindow = true, m_hoverMode = true, m_debug = false, m_debugAllCoords = false;
    float m_windowAlpha = 1.0f;   // Foretold Rewards window background opacity (slider; persisted to config/prefs.txt)
    int  m_throttleMs = 120;

    // perf instrumentation (Performance tab)
    Prof m_prof;
    int  m_csrThisFrame = 0;       // ComputeScreenRect calls accumulated this frame
    int  m_csrLastFrame = 0;       // snapshot of the above at end of last frame
    int  m_csrInLastRebuild = 0;   // CSR calls spent on the last full rect rebuild
    int  m_rebuildCount = 0;       // number of full rect rebuilds since reset
    std::atomic<int> m_lastExplored{ 0 }, m_lastEmitted{ 0 };
    std::atomic<bool> m_lastInjectFromCache{ false };

    FR::AtlasTransform m_xform;          // grid->screen affine; rebuilt on settle
    static constexpr double XFORM_MAX_RESIDUAL = 3.0;  // px; above this -> fall back to batch

    std::map<uint64_t, Rect> m_rectByGrid;
    bool m_rectsFresh = false;
    bool m_lastRebuildDebug = false;
    bool m_lastRebuildAllCoords = false;
    uint64_t m_refGrid = 0;
    ImVec2 m_lastRefPos{};
    std::chrono::steady_clock::time_point m_lastMove{};
    std::unordered_map<uint64_t, std::string> m_nameCache;

    LockState m_lockState = LockState::Idle;
    uint64_t  m_lockedStart = 0;
    uint32_t  m_lockedSeed = 0;
    bool      m_comboPrev = false, m_escPrev = false;
    std::vector<Chain> m_chains;
    std::vector<int> m_selectedChains;    // chain indices, selection order (drives overlay color)
    int              m_selAnchor = -1;    // filtered-list position of the last click (Shift range)
    char      m_filter[256] = { 0 };

    // variant overlay
    std::vector<int> m_shownIdx;          // cached filtered chain indices (into m_chains)
    bool m_shownDirty   = true;           // recompute m_shownIdx when chains change
    std::string m_shownFilter;            // ToLower filter the cache was built for

    // exclusions config (loaded from / saved to config/*.txt next to the DLL)
    std::unordered_set<std::string> m_exclExact;
    std::vector<std::string>        m_exclContains;
    std::set<int>                   m_exclBiomes;
    bool m_exclLoaded = false;
    char m_exclAddBuf[64] = { 0 };

    // reward search (across all purple starts)
    bool m_isSearchResult = false;
    char m_searchBuf[512] = { 0 };
    bool m_showBrowse = false;          // reward Browse panel toggled open (arrow button)
    char m_browseFilter[64] = { 0 };    // filter text inside the Browse panel
    std::string m_pendingSearch;        // reward just picked from autocomplete/browse
    int m_pendingFrames = 0;            // frames left to re-assert it past ImGui's InputText buffer
    std::string m_searchTermsDisplay;
    std::atomic<int> m_searchProgress{ 0 };
    std::atomic<int> m_searchTotal{ 0 };
    bool m_liveSearch = true;                       // UI toggle (default on)
    bool m_searchSession = false;                   // a search is the active display
    std::string m_searchTermsActive;                // terms for incremental scans
    std::vector<std::string> m_priorityTerms;       // ordered lowercased search terms (term[0]=most important) for result sort + colour
    std::unordered_set<uint64_t> m_searchedStarts;  // start grids already scanned this session
    std::unordered_set<std::string> m_chainKeys;    // dedup keys of stored chains
    bool m_appendPending = false;                   // an incremental append job is in flight

    // worker
    std::thread m_worker;
    std::atomic<int> m_workerFaults{ 0 };   // dropped (faulted) worker jobs; surfaced in the Performance tab
    std::mutex  m_jobMutex;
    std::condition_variable m_jobCv;
    bool m_jobPending = false, m_stop = false;
    WorkerInput m_job;
    uint64_t m_jobGen = 0;
    // background "obtainable list" scan — separate pending slot so it never clobbers a route job
    WorkerInput m_obJob;
    bool        m_obJobPending = false;
    uint64_t    m_obJobGen = 0;

    std::mutex m_resultMutex;
    std::vector<Chain> m_resultChains;
    uint64_t m_resultGen = 0;
    bool m_resultReady = false;
    bool m_resultAppend = false;                    // the in-flight result is an append (merge, not replace)
    std::unordered_set<std::string> m_resultRewardIds;  // reward ids in the emitted chains -> obtainable union
    std::vector<std::string> m_obResultIds;         // routable reward ids produced by the obtainable scan
    uint64_t m_obResultGen = 0;
    bool m_obResultReady = false;
    bool m_obResultComplete = false;                // last obtainable result was a full scan (not yielded mid-way)
    std::vector<std::vector<FR::Reward>> m_tableCache;
    uint32_t m_tableCacheSeed = 0;
    bool m_tableCacheOk = false;

    std::atomic<uint64_t> m_curGen{ 0 };
    std::atomic<uint64_t> m_obGen{ 0 };             // generation for the obtainable list scan (independent of m_curGen)
    // obtainable-list render-side state (render thread only). The list is the UNION of every routable
    // reward confirmed since the atlas opened (monotonic), rebuilt as more of the line loads.
    std::vector<std::string> m_routableIds;         // accumulated route-reachable reward ids feeding m_atlasView
    std::unordered_set<std::string> m_routableSet;  // dedup guard for m_routableIds (same lifetime)
    uint32_t m_obSeed = 0;                          // line the routable set was built for
    bool m_obComputing = false;                     // an obtainable scan is in flight
    bool m_obScanDue = false;                       // a (re)scan is pending (connTable grew) -> "scanning..." hint
    bool m_atlasDirty = false;                      // routable set changed -> rebuild m_atlasView
    std::chrono::steady_clock::time_point m_lastObScan{};
    bool m_haveObTime = false;
    bool m_obWasValid = false;                      // last tick's Snap().valid (to detect atlas reopen)
    std::chrono::steady_clock::time_point m_obActiveSince{};   // max-wait baseline for the rescan debounce
    std::chrono::steady_clock::time_point m_obStableSince{};   // connTable-size stable-since (settle detection)
    int  m_obConnSize         = -1;                 // last-seen connTable size (settle detection)
    int  m_obScanConnMax      = 0;                  // largest connTable size a scan has covered (growth high-water)
    int  m_obScanConnInFlight = 0;                  // connTable size the in-flight scan covers -> high-water on done
    std::atomic<bool> m_routeWaiting{ false };      // a user route job is queued -> the obtainable scan should yield

    bool m_haveTime = false;
    std::chrono::steady_clock::time_point m_lastSnap{};
};

extern "C" PLUGIN_API Plugin* CreatePlugin() { return new ForetoldRewardsPlugin(); }
extern "C" PLUGIN_API void DestroyPlugin(Plugin* p) { delete p; }

// Plugin info: the host checks it before CreatePlugin; the publish tool reads it.
POEFIXER_PLUGIN_INFO(
    "ForetoldRewards",
    "Foretold Rewards",
    "1.0.0",
    "POEFixer",
    "Shows the priced rewards the Rite of the Nameless can offer on each atlas map, so you can plan your route before you run it.")
