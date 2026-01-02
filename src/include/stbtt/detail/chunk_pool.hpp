#pragma once

// ------------------- Freestanding-friendly Includes -------------------------
#include <stddef.h> // size_t
#include <stdint.h> // uint32_t

namespace stbtt {
    namespace detail {

        struct ChunkPool {

            struct Chunk {
                Chunk* next; /* linked list of memory chunks */
            };

            Chunk* head{ nullptr }; // head of chunk list
            void* first_free{ nullptr }; // free list pointer
            int num_remaining_in_head_chunk{ 0 }; // remaining blocks in current chunk

            void* Alloc(size_t size, void* userdata) noexcept;
            void Free(void* p) noexcept;
            void Cleanup(void* userdata) noexcept;
        };



        void* ChunkPool::Alloc(size_t size, void* userdata) noexcept {
            if (first_free) {
                void* p = first_free;
                first_free = *(void**)p;
                return p;
            }
            else { // no free space left
                if (num_remaining_in_head_chunk == 0) {
                    // smaller objects -> more per chunk, larger -> fewer
                    int count = (size < 32 ? 2000 : size < 128 ? 800 : 100);
                    Chunk* c = reinterpret_cast<Chunk*>(
                        STBTT_malloc(sizeof(Chunk) + size * count, userdata));
                    if (c == nullptr)
                        return nullptr;
                    c->next = head;
                    head = c;
                    num_remaining_in_head_chunk = count;
                }
                // return a block from the current chunk
                --this->num_remaining_in_head_chunk;
                return reinterpret_cast<char*>(head) + sizeof(Chunk)
                    + size * num_remaining_in_head_chunk;
            }
        } // Alloc


        void ChunkPool::Free(void* p) noexcept {
            *(void**)p = first_free;
            first_free = p;
        }


        void ChunkPool::Cleanup(void* userdata) noexcept {
            while (head) {
                Chunk* n = head->next;
                STBTT_free(head, userdata);
                head = n;
            }
        }

    } // namespace detail
} // namespace stbtt