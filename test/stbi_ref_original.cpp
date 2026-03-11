#include <stdint.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_SIMD
#include "../3rd_party/stb/stb_image.h"

extern "C" {

unsigned char* stbi_ref_load_u8_from_memory(const unsigned char* bytes, int byte_count,
                                            int* x, int* y, int* channels_in_file, int req_channels) {
    return stbi_load_from_memory(bytes, byte_count, x, y, channels_in_file, req_channels);
}

const char* stbi_ref_failure_reason() {
    return stbi_failure_reason();
}

void stbi_ref_image_free(void* p) {
    stbi_image_free(p);
}

} // extern "C"
