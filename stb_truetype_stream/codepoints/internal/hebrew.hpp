#pragma once

#include "stbtt_codepoints_internal.hpp"

namespace stbtt_codepoints {
namespace internal {

    static constexpr CodepointRange hebrew_ranges[]{
        { 0x0590, 0x05FF },
    };

    static constexpr ScriptDescriptor Hebrew = {
        /* singles */ nullptr, 0,
        /* ranges */ hebrew_ranges,
                     sizeof(hebrew_ranges) / sizeof(CodepointRange)
    };

} // namespace internal
} // namespace stbtt_codepoints
