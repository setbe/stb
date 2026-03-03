#pragma once
#include "stbtt_codepoints_internal.hpp"

namespace stbtt_codepoints {
namespace internal {

    static constexpr CodepointRange misc_ranges[] = {

        // --- General punctuation (common in all languages) ---
        { 0x2000, 0x206F },

        // --- Currency symbols ---
        { 0x20A0, 0x20CF },

        // --- Arrows (UI, navigation, diagrams) ---
        { 0x2190, 0x21FF },

        // --- Miscellaneous technical / UI symbols ---
        { 0x2300, 0x23FF },

        // --- Geometric shapes (UI, diagrams) ---
        { 0x25A0, 0x25FF },

        // --- Misc symbols (weather, UI icons, etc.) ---
        { 0x2600, 0x26FF },

        // --- Dingbats ---
        { 0x2700, 0x27BF },

        // --- Fullwidth / halfwidth forms used in CJK text ---
        { 0xFF00, 0xFFEF },
    };

    static constexpr ScriptDescriptor Misc = {
        /* singles */ nullptr, 0,
        /* ranges  */ misc_ranges,
                      sizeof(misc_ranges) / sizeof(CodepointRange)
    };

} // namespace internal
} // namespace stbtt_codepoints