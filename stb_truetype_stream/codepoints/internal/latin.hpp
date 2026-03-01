#pragma once

#include "stbtt_codepoints_internal.hpp"

namespace stbtt_codepoints {
namespace internal {

    static constexpr CodepointRange latin_ranges[]{
        { 0x0020, 0x007E }, // Basic Latin
        { 0x00A0, 0x00FF }, // Latin-1 Supplement
    };

    static constexpr ScriptDescriptor Latin = {
        /* singles */ nullptr, 0,
        /* ranges */ latin_ranges,
                     sizeof(latin_ranges) / sizeof(CodepointRange)
    };

} // namespace internal
} // namespace stbtt_codepoints
