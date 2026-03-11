#pragma once

#include <stdint.h>

#include "stb_image_internal.hpp"

namespace stbi { namespace detail { namespace core {

struct ImageBackend {
    static inline bool InfoFromMemory(const uint8_t* bytes, int byte_count,
                                      int* x, int* y, int* comp) noexcept {
        return stbi::detail::InternalImageBackend::InfoFromMemory(bytes, byte_count, x, y, comp);
    }

    static inline bool IsHdrFromMemory(const uint8_t* bytes, int byte_count) noexcept {
        return stbi::detail::InternalImageBackend::IsHdrFromMemory(bytes, byte_count);
    }

    static inline bool Is16BitFromMemory(const uint8_t* bytes, int byte_count) noexcept {
        return stbi::detail::InternalImageBackend::Is16BitFromMemory(bytes, byte_count);
    }

    static inline void* LoadU8FromMemory(const uint8_t* bytes, int byte_count,
                                         int* x, int* y, int* comp, int req_comp) noexcept {
        return stbi::detail::InternalImageBackend::LoadU8FromMemory(bytes, byte_count, x, y, comp, req_comp);
    }

    static inline void* LoadU16FromMemory(const uint8_t* bytes, int byte_count,
                                          int* x, int* y, int* comp, int req_comp) noexcept {
        return stbi::detail::InternalImageBackend::LoadU16FromMemory(bytes, byte_count, x, y, comp, req_comp);
    }

    static inline void* LoadF32FromMemory(const uint8_t* bytes, int byte_count,
                                          int* x, int* y, int* comp, int req_comp) noexcept {
        return stbi::detail::InternalImageBackend::LoadF32FromMemory(bytes, byte_count, x, y, comp, req_comp);
    }

    static inline void ImageFree(void* p) noexcept {
        stbi::detail::InternalImageBackend::ImageFree(p);
    }

    static inline const char* FailureReason() noexcept {
        return stbi::detail::InternalImageBackend::FailureReason();
    }
};

} // namespace core
} // namespace detail
} // namespace stbi
