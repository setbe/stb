#pragma once
#include "stbtt_codepoints_internal.hpp"

namespace stbtt_codepoints {
namespace internal {

    static constexpr CodepointRange hangul_ranges[] = {
        // --- Minimum that is really needed for Korean ---
        { 0xAC00, 0xD7A3 }, // Hangul Syllables (가..힣)

        // --- Not necessary for 99% of UI/texts ---
        { 0x1100, 0x11FF },  // Hangul Jamo
        { 0x3130, 0x318F },  // Hangul Compatibility Jamo
        { 0xA960, 0xA97F },  // Hangul Jamo Extended-A
        { 0xD7B0, 0xD7FF },  // Hangul Jamo Extended-B
    };

    static constexpr ScriptDescriptor Hangul = {
        /* singles */ nullptr, 0,
        /* ranges  */ hangul_ranges,
                      sizeof(hangul_ranges) / sizeof(CodepointRange)
    };

} // namespace internal
} // namespace stbtt_codepoints