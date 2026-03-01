#pragma once

#include "stbtt_codepoints_internal.hpp"

namespace stbtt_codepoints {
namespace internal {

    static constexpr CodepointRange kana_ranges[]{
        { 0x30A0, 0x30FF }, // katakana
        { 0x3040, 0x309F }, // hiragana
    };

    static constexpr ScriptDescriptor Kana = {
        /* singles */ nullptr, 0,
        /* ranges */ kana_ranges,
                     sizeof(kana_ranges) / sizeof(CodepointRange)
    };

} // namespace internal 
} // namespace stbtt_codepoints
