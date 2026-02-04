#pragma once

#include "../stbtt_codepoints.hpp"

namespace stbtt_codepoints {

    static constexpr CodepointRange arabic_ranges[]{
        { 0x0600, 0x06FF },
    };

    static constexpr ScriptDescriptor Arabic = {
        /* singles */ nullptr, 0,
        /* ranges */ arabic_ranges,
                     sizeof(arabic_ranges) / sizeof(CodepointRange)
    };

} // namespace stbtt_codepoints
