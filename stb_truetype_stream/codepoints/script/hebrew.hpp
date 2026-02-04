#pragma once

#include "../stbtt_codepoints.hpp"

namespace stbtt_codepoints {

    static constexpr CodepointRange hebrew_ranges[]{
        { 0x0590, 0x05FF },
    };

    static constexpr ScriptDescriptor Hebrew = {
        /* singles */ nullptr, 0,
        /* ranges */ hebrew_ranges,
                     sizeof(hebrew_ranges) / sizeof(CodepointRange)
    };

} // namespace stbtt_codepoints
