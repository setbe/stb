#pragma once

#include <stdint.h> // uint32_t

namespace stbtt_codepoints {
namespace internal {
    using Codepoint = uint32_t;

    struct CodepointRange {
        uint32_t first;
        uint32_t last;   // inclusive
    };

    struct ScriptDescriptor {
        const Codepoint* singles;
        uint32_t        singles_count;

        const CodepointRange* ranges;
        uint32_t     range_count;
    };

} // namespace stb_codepoints
} // namespace internal