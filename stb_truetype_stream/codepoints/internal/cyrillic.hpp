#pragma once

#include "stbtt_codepoints_internal.hpp"

namespace stbtt_codepoints {
namespace internal {

    static constexpr CodepointRange cyrillic_ranges[]{
        { 0x0400, 0x04FF },
    };

    static constexpr ScriptDescriptor Cyrillic = {
        /* singles */ nullptr, 0,
        /* ranges */ cyrillic_ranges,
                     sizeof(cyrillic_ranges) / sizeof(CodepointRange)
    };

} // namespace internal
} // namespace stbtt_codepoints
