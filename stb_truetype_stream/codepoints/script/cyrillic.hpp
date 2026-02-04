#pragma once

#include "../stbtt_codepoints.hpp"

namespace stbtt_codepoints {

    static constexpr CodepointRange cyrillic_ranges[]{
        { 0x0400, 0x04FF },
    };

    static constexpr ScriptDescriptor Cyrillic = {
        /* singles */ nullptr, 0,
        /* ranges */ cyrillic_ranges,
                     sizeof(cyrillic_ranges) / sizeof(CodepointRange)
    };

} // namespace stbtt_codepoints
