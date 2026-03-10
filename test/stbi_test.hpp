#pragma once

#include <stdint.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "catch.hpp"

#include "../stb_image/stb_image.hpp"

extern "C" {
unsigned char* stbi_ref_load_u8_from_memory(const unsigned char* bytes, int byte_count,
                                            int* x, int* y, int* channels_in_file, int req_channels);
const char* stbi_ref_failure_reason();
void stbi_ref_image_free(void* p);
}

namespace {

struct DecodedRef {
    int x{};
    int y{};
    int channels_in_file{};
    std::vector<uint8_t> pixels_rgba;
};

struct DecodedCpp {
    uint32_t x{};
    uint32_t y{};
    uint8_t channels_in_file{};
    std::vector<uint8_t> pixels_rgba;
};

static bool read_file_bytes(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    if (n <= 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize((size_t)n);
    f.read(reinterpret_cast<char*>(out.data()), n);
    return f.good();
}

static bool read_cat_image(const char* ext, std::vector<uint8_t>& out, std::string& used_path) {
    const std::string rel = std::string("cat.") + ext;
    const std::string candidates[] = {
        std::string("img/") + rel,
        std::string("../img/") + rel,
        std::string("../../img/") + rel,
    };
    for (const std::string& p : candidates) {
        if (read_file_bytes(p, out)) {
            used_path = p;
            return true;
        }
    }
    return false;
}

static bool decode_ref_rgba_u8(const std::vector<uint8_t>& file, DecodedRef& out) {
    int x = 0, y = 0, n = 0;
    unsigned char* pixels = stbi_ref_load_u8_from_memory(
        file.data(),
        static_cast<int>(file.size()),
        &x, &y, &n,
        4
    );
    if (!pixels) return false;

    out.x = x;
    out.y = y;
    out.channels_in_file = n;
    out.pixels_rgba.resize((size_t)x * (size_t)y * 4u);
    std::memcpy(out.pixels_rgba.data(), pixels, out.pixels_rgba.size());
    stbi_ref_image_free(pixels);
    return true;
}

static bool decode_cpp_rgba_u8(const std::vector<uint8_t>& file, DecodedCpp& out, std::string& fail_reason) {
    stbi::Decoder dec;
    if (!dec.ReadBytes(file.data(), file.size())) {
        fail_reason = "ReadBytes failed";
        return false;
    }

    stbi::DecodeOptions opt{};
    opt.desired_channels = 4;
    opt.sample_type = stbi::SampleType::U8;

    stbi::ImagePlan plan{};
    if (!dec.Plan(opt, plan)) {
        fail_reason = dec.FailureReason() ? dec.FailureReason() : "Plan failed";
        return false;
    }

    std::vector<uint8_t> scratch(plan.scratch_bytes ? plan.scratch_bytes : 1u);
    out.pixels_rgba.resize(plan.pixel_bytes);

    if (!dec.Decode(plan,
                    scratch.data(), scratch.size(),
                    out.pixels_rgba.data(), out.pixels_rgba.size())) {
        fail_reason = dec.FailureReason() ? dec.FailureReason() : "Decode failed";
        return false;
    }

    out.x = plan.width;
    out.y = plan.height;
    out.channels_in_file = plan.channels_in_file;
    return true;
}

static ptrdiff_t first_diff_index(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if (a.size() != b.size()) return 0;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) return static_cast<ptrdiff_t>(i);
    }
    return -1;
}

} // namespace

TEST_CASE("stbi byte diff: fork matches original stb_image.h on cat.*", "[stbi][byte-diff]") {
    const char* exts[] = { "jpg", "pnm", "psd", "hdr", "bmp", "tga", "gif", "png" };

    for (const char* ext : exts) {
        DYNAMIC_SECTION(std::string("cat.") + ext) {
            std::vector<uint8_t> file;
            std::string used_path;
            REQUIRE(read_cat_image(ext, file, used_path));

            DecodedRef ref{};
            REQUIRE(decode_ref_rgba_u8(file, ref));

            DecodedCpp cpp{};
            std::string cpp_fail;
            REQUIRE(decode_cpp_rgba_u8(file, cpp, cpp_fail));

            INFO("path: " << used_path);
            INFO("fork fail reason: " << cpp_fail);
            INFO("ref failure reason: " << (stbi_ref_failure_reason() ? stbi_ref_failure_reason() : "<null>"));

            REQUIRE(cpp.x == static_cast<uint32_t>(ref.x));
            REQUIRE(cpp.y == static_cast<uint32_t>(ref.y));
            REQUIRE(cpp.channels_in_file == static_cast<uint8_t>(ref.channels_in_file));
            REQUIRE(cpp.pixels_rgba.size() == ref.pixels_rgba.size());

            const ptrdiff_t diff = first_diff_index(cpp.pixels_rgba, ref.pixels_rgba);
            if (diff >= 0) {
                INFO("first diff index: " << diff);
                INFO("fork byte: " << static_cast<int>(cpp.pixels_rgba[(size_t)diff]));
                INFO("ref byte: " << static_cast<int>(ref.pixels_rgba[(size_t)diff]));
            }
            REQUIRE(diff < 0);
        }
    }
}

