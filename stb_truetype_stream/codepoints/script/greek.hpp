#pragma once

#include "../stbtt_codepoints.hpp"

namespace stbtt_codepoints {

    static constexpr CodepointRange greek_ranges[]{
        { 0x0370, 0x03FF },
    };

    static constexpr ScriptDescriptor Greek = {
        /* singles */ nullptr, 0,
        /* ranges */ greek_ranges,
                     sizeof(greek_ranges) / sizeof(CodepointRange)
    };

} // namespace stbtt_codepoints
