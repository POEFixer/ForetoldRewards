// FR_Search.h — the reward search: case folding, term parsing and matching a reward by id.
//
// A search term matches a catalog reward when the reward's label in the program language OR its
// English label holds the term (case-folded), so an English search (the shipped default
// "Mageblood" in config/search.txt, or any search typed in English) finds the same rewards in
// every language; a search typed in another language matches while that language is shown. The
// matched ids are recorded per term at search time: the result list is ranked and coloured by id,
// so a later language switch keeps its order and colours. A reward the catalog does not know (a
// future game-added one) matches through its English fallback label.
//
// Free of PluginSDK.h: ForetoldRewards.cpp hands in the labels in the program language and its
// embedded English (lang_en.generated.h's kPluginLangEn); test/test_search.cpp and
// test/test_english.cpp test it.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "FR_RewardText.h"

namespace FR {

inline std::string AsciiLower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

// Case folding for a search: Unicode lowercase (LCMapStringEx, invariant locale) over UTF-16,
// so Cyrillic, Greek and accented Latin fold like ASCII. Invalid UTF-8 folds its ASCII only.
inline std::string FoldCase(std::string_view utf8) {
    bool ascii = true;
    for (char c : utf8)
        if (static_cast<unsigned char>(c) >= 0x80) { ascii = false; break; }
    if (ascii || utf8.size() > 0x7FFFFFF) return AsciiLower(utf8);

    const int srcLen = static_cast<int>(utf8.size());
    const int wideLen = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), srcLen, nullptr, 0);
    if (wideLen <= 0) return AsciiLower(utf8);
    std::wstring wide(static_cast<size_t>(wideLen), L'\0');
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), srcLen, wide.data(), wideLen);

    const int lowLen = ::LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, wide.c_str(), wideLen,
                                       nullptr, 0, nullptr, nullptr, 0);
    if (lowLen <= 0) return AsciiLower(utf8);
    std::wstring lower(static_cast<size_t>(lowLen), L'\0');
    ::LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, wide.c_str(), wideLen,
                    lower.data(), lowLen, nullptr, nullptr, 0);

    const int outLen = ::WideCharToMultiByte(CP_UTF8, 0, lower.c_str(), lowLen, nullptr, 0, nullptr, nullptr);
    if (outLen <= 0) return AsciiLower(utf8);
    std::string out(static_cast<size_t>(outLen), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, lower.c_str(), lowLen, out.data(), outLen, nullptr, nullptr);
    return out;
}

// The search box's terms: comma-separated, trimmed, folded; empty terms dropped. term[0] is the
// most important (it ranks and colours first).
inline std::vector<std::string> ParseSearchTerms(std::string_view raw) {
    std::vector<std::string> terms;
    auto flush = [&](std::string_view part) {
        size_t b = 0, e = part.size();
        while (b < e && (part[b] == ' ' || part[b] == '\t' || part[b] == '\r' || part[b] == '\n')) ++b;
        while (e > b && (part[e - 1] == ' ' || part[e - 1] == '\t' || part[e - 1] == '\r' || part[e - 1] == '\n')) --e;
        if (e > b) terms.push_back(FoldCase(part.substr(b, e - b)));
    };
    size_t start = 0;
    for (size_t i = 0; i <= raw.size(); ++i)
        if (i == raw.size() || raw[i] == ',') { flush(raw.substr(start, i - start)); start = i + 1; }
    return terms;
}

// What a search matched, recorded when it started.
struct SearchMatch {
    std::vector<std::string> terms;                         // folded, term[0] = most important
    std::vector<std::unordered_set<std::string>> perTerm;   // per term: the catalog reward ids it matched
    std::unordered_set<std::string> all;                    // every catalog reward id any term matched
};

// Matches `terms` (folded, ParseSearchTerms) against every catalog reward: `shown(id)` gives the
// reward's label in the program language, `english(id)` its English label (std::string or
// convertible). A term matches a reward when either folded label holds it.
template <class Shown, class English>
SearchMatch MatchRewards(std::vector<std::string> terms, Shown&& shown, English&& english) {
    SearchMatch m;
    m.terms = std::move(terms);
    m.perTerm.resize(m.terms.size());
    if (m.terms.empty()) return m;
    for (const auto& d : AllRewards()) {
        const std::string id = d.id;
        const std::string a = FoldCase(std::string(shown(id)));
        const std::string b = FoldCase(std::string(english(id)));
        for (size_t i = 0; i < m.terms.size(); ++i) {
            const std::string& t = m.terms[i];
            if (t.empty()) continue;
            if (a.find(t) != std::string::npos || b.find(t) != std::string::npos) {
                m.perTerm[i].insert(id);
                m.all.insert(id);
            }
        }
    }
    return m;
}

// The text of `key` in an embedded table of {key, text} entries (the plugin's English:
// lang_en.generated.h's kPluginLangEn); "" when absent.
template <class Entry, size_t N>
const char* EmbeddedText(const Entry (&entries)[N], std::string_view key) {
    for (const Entry& e : entries)
        if (key == e.key) return e.text;
    return "";
}

// A reward's English label in the embedded English: the text of its label key (the Chaos <-> Exalt
// swap applied, RewardLabelKey), or the id spelled out for a reward the catalog does not know.
template <class Entry, size_t N>
std::string RewardEnglishLabel(const Entry (&english)[N], const std::string& modId) {
    const std::string key = RewardLabelKey(modId);
    return key.empty() ? RewardFallbackLabel(modId) : std::string(EmbeddedText(english, key));
}

// The plugin's reward search: the labels in the program language (`shown(id)`) and the English
// labels of the embedded English table.
template <class Shown, class Entry, size_t N>
SearchMatch MatchRewardsWithEnglish(std::vector<std::string> terms, Shown&& shown, const Entry (&english)[N]) {
    return MatchRewards(std::move(terms), std::forward<Shown>(shown),
                        [&english](const std::string& id) { return RewardEnglishLabel(english, id); });
}

inline constexpr int kNoPriority = 1 << 30;

// The index of the first term that matches reward `id` (0 = most important), kNoPriority when
// none: a catalog reward by the ids recorded at search time, an unknown one by its English
// fallback label.
inline int RewardPriority(const SearchMatch& m, const std::string& id) {
    if (m.terms.empty() || id.empty()) return kNoPriority;
    if (IsCatalogReward(id)) {
        for (size_t i = 0; i < m.perTerm.size(); ++i)
            if (m.perTerm[i].count(id)) return static_cast<int>(i);
        return kNoPriority;
    }
    const std::string label = FoldCase(RewardFallbackLabel(id));
    for (size_t i = 0; i < m.terms.size(); ++i)
        if (!m.terms[i].empty() && label.find(m.terms[i]) != std::string::npos) return static_cast<int>(i);
    return kNoPriority;
}

// The best (lowest) priority of a map step's rewards.
inline int RewardsPriority(const SearchMatch& m, const std::vector<std::string>& ids) {
    int best = kNoPriority;
    for (const auto& id : ids) {
        const int p = RewardPriority(m, id);
        if (p < best) best = p;
    }
    return best;
}

// True when reward `id` matches the search: a catalog reward by `matchIds` (SearchMatch::all),
// an unknown one by its English fallback label holding one of `terms` (worker-safe).
inline bool RewardMatches(const std::unordered_set<std::string>& matchIds, const std::vector<std::string>& terms,
                          const std::string& id) {
    if (matchIds.count(id)) return true;
    if (IsCatalogReward(id)) return false;
    const std::string label = FoldCase(RewardFallbackLabel(id));
    for (const auto& t : terms)
        if (!t.empty() && label.find(t) != std::string::npos) return true;
    return false;
}

// Copies `text` into out[size] NUL-terminated, cut on a UTF-8 character boundary (a fixed ImGui
// buffer never ends in half a character).
inline void CopyUtf8(std::string_view text, char* out, size_t size) {
    if (!out || size == 0) return;
    size_t n = text.size() < size - 1 ? text.size() : size - 1;
    if (n < text.size())
        while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80) --n;
    if (n) std::memcpy(out, text.data(), n);
    out[n] = '\0';
}

}  // namespace FR
