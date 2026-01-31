#pragma once

#include "stbtt_codepoints.hpp"

#include "script/latin.hpp"
#include "script/cyrillic.hpp"
#include "script/greek.hpp"
#include "script/arabic.hpp"
#include "script/hebrew.hpp"
#include "script/devanagari.hpp"
#include "script/cjk.hpp"
#include "script/kana.hpp"
#include "script/jouyou_kanji.hpp"

namespace stbtt_codepoints {
    // ========================================================================
    // Script discpatcher
    // ========================================================================

    enum class Script : uint8_t {
        Latin,
        Cyrillic,
        Greek,
        Arabic,
        Hebrew,
        Devanagari,
        CJK,        // attention: large
        Kana,       // Japanese: Hiragana + Katakana
        JouyouKanji, // Japanese: common-use kanji
    };

    static constexpr ScriptDescriptor GetScriptDescriptor(Script s) noexcept {
        switch (s)
        {
        case Script::Latin:       return Latin;
        case Script::Cyrillic:    return Cyrillic;
        case Script::Greek:       return Greek;
        case Script::Arabic:      return Arabic;
        case Script::Hebrew:      return Hebrew;
        case Script::Devanagari:  return Devanagari;
        case Script::CJK:         return Cjk;
        case Script::Kana:        return Kana;
        case Script::JouyouKanji: return JouyouKanji;
        default:                  return Latin;
        }
    }

    // ========================================================================
    // PASS 1: PLAN
    // ========================================================================

    template<class FontT>
    static inline uint32_t PlanGlyphs(const FontT& font,
        Script script) noexcept {
        const ScriptDescriptor& d = GetScriptDescriptor(script);
        uint32_t count = 0;

        // ranges
        for (uint32_t i = 0; i < d.range_count; ++i) {
            Codepoint cp = d.ranges[i].first;
            Codepoint end = d.ranges[i].last;
            for (; cp <= end; ++cp) {
                if (font.FindGlyphIndex(cp))
                    ++count;
            }
        }

        // singles
        for (uint32_t i = 0; i < d.singles_count; ++i) {
            if (font.FindGlyphIndex(d.singles[i]))
                ++count;
        }

        return count;
    }

    // helper for variadic usage (OUTSIDE core API)
    template<class FontT>
    static inline uint32_t PlanGlyphs(const FontT& font,
                                             Script s0,
                                             Script s1) noexcept {
        return PlanGlyphs(font, s0)
            + PlanGlyphs(font, s1);
    }

    template<class FontT, class... Rest>
    static inline uint32_t PlanGlyphs(const FontT& font,
                                             Script s0,
                                             Script s1,
                                             Rest... rest) noexcept {
        return PlanGlyphs(font, s0)
            + PlanGlyphs(font, s1, rest...);
    }

    // ========================================================================
    // PASS 2: BUILD
    // ========================================================================

    template<class FontT, class SinkT>
    static inline void BuildGlyphs(const FontT& font,
                                         Script script,
                                         SinkT& sink) noexcept {
        const ScriptDescriptor& d = GetScriptDescriptor(script);

        // ranges
        for (uint32_t i = 0; i < d.range_count; ++i) {
            Codepoint cp = d.ranges[i].first;
            Codepoint end = d.ranges[i].last;

            for (; cp <= end; ++cp) {
                int g = font.FindGlyphIndex(cp);
                if (g)
                    sink(cp, g);
            }
        }

        // singles
        for (uint32_t i = 0; i < d.singles_count; ++i) {
            Codepoint cp = d.singles[i];
            int g = font.FindGlyphIndex(cp);
            if (g)
                sink(cp, g);
        }
    }
} // namespace stbtt_codepoints