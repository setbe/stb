#pragma once

#include "stbtt_codepoints_internal.hpp"

namespace stbtt_codepoints {
namespace internal {

    static constexpr CodepointRange greek_ranges[]{
        { 0x0370, 0x03FF },
    };

    static constexpr ScriptDescriptor Greek = {
        /* singles */ nullptr, 0,
        /* ranges */ greek_ranges,
                     sizeof(greek_ranges) / sizeof(CodepointRange)
    };

} // namespace internal
} // namespace stbtt_codepoints
