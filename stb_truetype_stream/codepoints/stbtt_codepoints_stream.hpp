#pragma once

#include "internal/stbtt_codepoints_internal.hpp"

#include "internal/latin.hpp"
#include "internal/cyrillic.hpp"
#include "internal/greek.hpp"
#include "internal/arabic.hpp"
#include "internal/hebrew.hpp"
#include "internal/devanagari.hpp"
#include "internal/cjk.hpp"
#include "internal/kana.hpp"
#include "internal/jouyou_kanji.hpp"

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

    static constexpr internal::ScriptDescriptor GetScriptDescriptor(Script s) noexcept {
        switch (s)
        {
        case Script::Latin:       return internal::Latin;
        case Script::Cyrillic:    return internal::Cyrillic;
        case Script::Greek:       return internal::Greek;
        case Script::Arabic:      return internal::Arabic;
        case Script::Hebrew:      return internal::Hebrew;
        case Script::Devanagari:  return internal::Devanagari;
        case Script::CJK:         return internal::Cjk;
        case Script::Kana:        return internal::Kana;
        case Script::JouyouKanji: return internal::JouyouKanji;
        default:                  return internal::Latin;
        }
    }

    // ========================================================================
    // PASS 1: PLAN GLYPH COUNT
    // ========================================================================

    template<class FontT>
    static inline uint32_t PlanGlyphs(const FontT& font, Script script) noexcept {
        const internal::ScriptDescriptor& d = GetScriptDescriptor(script);
        uint32_t count = 0;

        // ranges
        for (uint32_t i = 0; i < d.range_count; ++i) {
            internal::Codepoint cp = d.ranges[i].first;
            internal::Codepoint end = d.ranges[i].last;
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

    // 0 scripts => 0 glyphs
    template<class FontT>
    static inline uint32_t PlanGlyphs(const FontT& /*font*/) noexcept {
        return 0u;
    }

    // N scripts => sum
    template<class FontT, class... Scripts>
    static inline uint32_t PlanGlyphs(const FontT& font, Script s0, Scripts... rest) noexcept {
        return PlanGlyphs(font, s0) + PlanGlyphs(font, rest...);
    }

    // ========================================================================
    // PASS 2: COLLECT GLYPHS
    // ========================================================================

    template<class FontT, class SinkT>
    static inline void CollectGlyphs(const FontT& font, SinkT& sink, Script script) noexcept {
        const internal::ScriptDescriptor& d = GetScriptDescriptor(script);

        // ranges
        for (uint32_t i = 0; i < d.range_count; ++i) {
            internal::Codepoint cp = d.ranges[i].first;
            internal::Codepoint end = d.ranges[i].last;

            for (; cp <= end; ++cp) {
                int g = font.FindGlyphIndex(cp);
                if (g)
                    sink(cp, g); // call back
            }
        }

        // singles
        for (uint32_t i = 0; i < d.singles_count; ++i) {
            internal::Codepoint cp = d.singles[i];
            int g = font.FindGlyphIndex(cp);
            if (g)
                sink(cp, g); // call back
        }
    }

    // Build: 0 scripts => nothing
    template<class FontT, class SinkT>
    static inline void CollectGlyphs(const FontT& /*font*/, SinkT& /*sink*/) noexcept {}

    // Build: N scripts => call each
    template<class FontT, class SinkT, class... Scripts>
    static inline void CollectGlyphs(const FontT& font, SinkT& sink, Script s0, Scripts... rest) noexcept {
        CollectGlyphs(font, s0, sink);
        CollectGlyphs(font, sink, rest...);
    }
} // namespace stbtt_codepoints