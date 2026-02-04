#pragma once

#include "../stbtt_codepoints.hpp"

namespace stbtt_codepoints {

    static constexpr CodepointRange kana_ranges[]{
        { 0x30A0, 0x30FF }, // katakana
        { 0x3040, 0x309F }, // hiragana
    };

    static constexpr ScriptDescriptor Kana = {
        /* singles */ nullptr, 0,
        /* ranges */ kana_ranges,
                     sizeof(kana_ranges) / sizeof(CodepointRange)
    };

} // namespace stbtt_codepoints
