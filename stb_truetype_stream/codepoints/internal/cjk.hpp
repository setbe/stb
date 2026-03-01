#pragma once

#include "stbtt_codepoints_internal.hpp"

namespace stbtt_codepoints {
namespace internal {


    static constexpr CodepointRange cjk_ranges[]{
        { 0x4E00, 0x9FFF },
    };

    static constexpr ScriptDescriptor Cjk = {
        /* singles */ nullptr, 0,
        /* ranges */ cjk_ranges,
                     sizeof(cjk_ranges) / sizeof(CodepointRange)
    };

} // namespace internal 
} // namespace stbtt_codepoints
