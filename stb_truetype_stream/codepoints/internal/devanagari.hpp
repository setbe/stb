#pragma once

#include "stbtt_codepoints_internal.hpp"

namespace stbtt_codepoints {
namespace internal {

    static constexpr CodepointRange devanagari_ranges[]{
        { 0x0900, 0x097F },
    };

    static constexpr ScriptDescriptor Devanagari = {
        /* singles */ nullptr, 0,
        /* ranges */ devanagari_ranges,
                     sizeof(devanagari_ranges) / sizeof(CodepointRange)
    };

} // namespace internal
} // namespace stbtt_codepoints