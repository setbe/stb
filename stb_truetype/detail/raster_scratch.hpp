#pragma once

// ------------------- Freestanding-friendly Includes -------------------------
#include <stddef.h> // size_t
#include <stdint.h> // uint32_t
#include <cstddef> // std::max_align_t

#include "edges.hpp"

namespace stbtt {
namespace detail {

    static inline uintptr_t align_up(uintptr_t p, size_t a) noexcept {
        return (p + (a-1)) & ~(static_cast<uintptr_t>(a-1));
    }

    struct RasterScratch {
        ActiveEdge* pool = nullptr;
        float* scan = nullptr;      // len = 2*w + 1
        int pool_cap = 0;
        int w = 0;

        // freelist
        ActiveEdge* free_list = nullptr;
        int used = 0;

        inline void reset() noexcept { free_list = nullptr; used = 0; }

        inline ActiveEdge* alloc() noexcept {
            if (free_list) {
                auto* p = free_list;
                free_list = free_list->next;
                return p;
            }
            if (used >= pool_cap)
                return nullptr;
            return &pool[used++];
        }
        inline void free(ActiveEdge* p) noexcept {
            p->next = free_list;
            free_list = p;
        }
    };

    static inline size_t RasterScratchBytes(int w, int n_edges) noexcept {
        const size_t A = alignof(std::max_align_t);
        size_t bytes = 0;
        bytes = align_up(bytes, A) + static_cast<size_t>(n_edges) * sizeof(ActiveEdge);
        bytes = align_up(bytes, A) + static_cast<size_t>(2*w + 1) * sizeof(float);
        return align_up(bytes, A);
    }

    static inline RasterScratch RasterScratchBind(void* mem, size_t cap, int w, int n_edges) noexcept {
        RasterScratch s{};
        s.w = w;
        s.pool_cap = n_edges;

        const size_t A = alignof(std::max_align_t);
        uintptr_t p = reinterpret_cast<uintptr_t>(mem);
        const uintptr_t End = p + cap;

        p = align_up(p, A);
        uintptr_t p_pool = p;
        p += size_t(n_edges) * sizeof(ActiveEdge);
        p = align_up(p, A);

        uintptr_t p_scan = p;
        p += size_t(2 * w + 1) * sizeof(float);
        p = align_up(p, A);

        if (p > End) return {}; // should not happen if cap == RasterScratchBytes()

        s.pool = reinterpret_cast<ActiveEdge*>(p_pool);
        s.scan = reinterpret_cast<float*>(p_scan);
        s.reset();
        return s;
    }

} // namespace detail
} // namespace stbtt
