// decode_heart.cpp
// C++17: read the BMP created by generate_heart.cpp, extract the embedded
// payload, verify its CRC-32, decrypt it with AES-256-GCM, and print it.
// This decoder does not execute the payload.

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/evp.h>

// 默认密码：与 generate_heart.cpp 保持一致。源码公开即等于公开此密码。
const char* kDefaultPassword = "heart-default-password";

namespace {
constexpr char kMagic[] = "HRTSAFE1";
using Pixel = std::array<std::uint8_t, 3>; // B, G, R

std::uint16_t readLe16(const std::vector<std::uint8_t>& b, std::size_t p) {
    return static_cast<std::uint16_t>(b[p]) |
           (static_cast<std::uint16_t>(b[p + 1]) << 8);
}
std::uint32_t readLe32(const std::vector<std::uint8_t>& b, std::size_t p) {
    return static_cast<std::uint32_t>(b[p]) |
           (static_cast<std::uint32_t>(b[p + 1]) << 8) |
           (static_cast<std::uint32_t>(b[p + 2]) << 16) |
           (static_cast<std::uint32_t>(b[p + 3]) << 24);
}
std::uint32_t crc32(const std::string& data) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char c : data) {
        crc ^= c;
        for (int i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

std::string decryptAesGcm(const std::string& blob, const std::string& password) {
    constexpr int kSaltLen = 16;
    constexpr int kNonceLen = 12;
    constexpr int kKeyLen = 32;
    constexpr int kTagLen = 16;
    constexpr int kIterations = 100000;

    if (blob.size() < static_cast<std::size_t>(kSaltLen + kNonceLen + kTagLen)) {
        std::cerr << "Error: ciphertext too short.\n";
        return "";
    }

    const auto* salt = reinterpret_cast<const unsigned char*>(blob.data());
    const auto* nonce = salt + kSaltLen;
    const auto* tag = reinterpret_cast<const unsigned char*>(blob.data() + blob.size() - kTagLen);
    const std::size_t ctLen = blob.size() - kSaltLen - kNonceLen - kTagLen;
    const auto* ciphertext = nonce + kNonceLen;

    unsigned char key[kKeyLen];
    if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()),
                          salt, kSaltLen, kIterations, EVP_sha256(), kKeyLen, key) != 1) {
        std::cerr << "Error: key derivation failed.\n";
        return "";
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return "";

    std::vector<unsigned char> plaintext(ctLen);
    int len = 0;
    int total = 0;

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kNonceLen, nullptr) != 1 ||
        EVP_DecryptInit_ex(ctx, nullptr, nullptr, key, nonce) != 1 ||
        EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext,
                          static_cast<int>(ctLen)) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagLen,
                            const_cast<unsigned char*>(tag)) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        std::cerr << "Error: decryption failed.\n";
        return "";
    }
    total = len;
    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + total, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        std::cerr << "Error: authentication failed (wrong password or tampered data).\n";
        return "";
    }
    total += len;
    EVP_CIPHER_CTX_free(ctx);

    return std::string(reinterpret_cast<char*>(plaintext.data()), total);
}

bool loadBmp(const std::string& path, int& width, int& height, std::vector<Pixel>& pixels) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "Error: cannot open image: " << path << "\n";
        return false;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    if (bytes.size() < 54 || bytes[0] != 'B' || bytes[1] != 'M') {
        std::cerr << "Error: not a valid BMP file.\n";
        return false;
    }

    const std::uint32_t pixelOffset = readLe32(bytes, 10);
    const std::uint32_t dibSize = readLe32(bytes, 14);
    const std::int32_t signedWidth = static_cast<std::int32_t>(readLe32(bytes, 18));
    const std::int32_t signedHeight = static_cast<std::int32_t>(readLe32(bytes, 22));
    const std::uint16_t planes = readLe16(bytes, 26);
    const std::uint16_t bpp = readLe16(bytes, 28);
    const std::uint32_t compression = readLe32(bytes, 30);

    if (dibSize < 40 || signedWidth <= 0 || signedHeight == 0 ||
        planes != 1 || bpp != 24 || compression != 0) {
        std::cerr << "Error: only uncompressed 24-bit BMP images are supported.\n";
        return false;
    }
    if (signedHeight == std::numeric_limits<std::int32_t>::min()) return false;

    width = signedWidth;
    height = signedHeight > 0 ? signedHeight : -signedHeight;
    const std::uint64_t rowBytes = static_cast<std::uint64_t>(width) * 3u;
    const std::uint64_t stride = (rowBytes + 3u) & ~std::uint64_t(3u);
    const std::uint64_t required = static_cast<std::uint64_t>(pixelOffset) + stride * height;
    if (required > bytes.size() || static_cast<std::uint64_t>(width) * height > 100000000u) {
        std::cerr << "Error: BMP pixel data is truncated or dimensions are unreasonable.\n";
        return false;
    }

    pixels.assign(static_cast<std::size_t>(width) * height, Pixel{0, 0, 0});
    const bool bottomUp = signedHeight > 0;
    for (int fileY = 0; fileY < height; ++fileY) {
        const int y = bottomUp ? (height - 1 - fileY) : fileY;
        const std::size_t rowStart = static_cast<std::size_t>(pixelOffset + stride * fileY);
        for (int x = 0; x < width; ++x) {
            const std::size_t pos = rowStart + static_cast<std::size_t>(x) * 3;
            pixels[static_cast<std::size_t>(y) * width + x] =
                Pixel{bytes[pos], bytes[pos + 1], bytes[pos + 2]};
        }
    }
    return true;
}

bool extractPayload(const std::vector<Pixel>& pixels, std::string& payload) {
    const std::size_t capacityBits = pixels.size() * 3;
    std::size_t bitIndex = 0;

    auto readByte = [&](std::uint8_t& value) -> bool {
        if (bitIndex + 8 > capacityBits) return false;
        value = 0;
        for (int i = 0; i < 8; ++i) {
            const std::size_t pixelIndex = bitIndex / 3;
            const std::size_t channelIndex = bitIndex % 3;
            const std::uint8_t bit = pixels[pixelIndex][channelIndex] & 1u;
            value = static_cast<std::uint8_t>((value << 1) | bit);
            ++bitIndex;
        }
        return true;
    };

    std::array<std::uint8_t, 12> header{};
    for (auto& b : header) {
        if (!readByte(b)) return false;
    }
    for (int i = 0; i < 8; ++i) {
        if (header[i] != static_cast<std::uint8_t>(kMagic[i])) {
            std::cerr << "Error: no supported embedded payload found.\n";
            return false;
        }
    }

    const std::uint32_t length =
        static_cast<std::uint32_t>(header[8]) |
        (static_cast<std::uint32_t>(header[9]) << 8) |
        (static_cast<std::uint32_t>(header[10]) << 16) |
        (static_cast<std::uint32_t>(header[11]) << 24);

    const std::uint64_t remainingBytes = (capacityBits - bitIndex) / 8;
    if (length > remainingBytes || length > 10u * 1024u * 1024u) {
        std::cerr << "Error: embedded payload length is invalid.\n";
        return false;
    }

    payload.clear();
    payload.reserve(length);
    for (std::uint32_t i = 0; i < length; ++i) {
        std::uint8_t b = 0;
        if (!readByte(b)) return false;
        payload.push_back(static_cast<char>(b));
    }

    std::array<std::uint8_t, 4> crcBytes{};
    for (auto& b : crcBytes) {
        if (!readByte(b)) return false;
    }
    const std::uint32_t storedCrc =
        static_cast<std::uint32_t>(crcBytes[0]) |
        (static_cast<std::uint32_t>(crcBytes[1]) << 8) |
        (static_cast<std::uint32_t>(crcBytes[2]) << 16) |
        (static_cast<std::uint32_t>(crcBytes[3]) << 24);

    if (crc32(payload) != storedCrc) {
        std::cerr << "Error: CRC-32 verification failed; image data may have changed.\n";
        return false;
    }
    return true;
}
} // namespace

int main(int argc, char** argv) {
    const std::string inputPath = (argc >= 2) ? argv[1] : "heart.bmp";
    const std::string password = (argc >= 3) ? argv[2] : kDefaultPassword;

    int width = 0, height = 0;
    std::vector<Pixel> pixels;
    if (!loadBmp(inputPath, width, height, pixels)) return 1;

    std::string ciphertext;
    if (!extractPayload(pixels, ciphertext)) return 1;

    const std::string plaintext = decryptAesGcm(ciphertext, password);
    if (plaintext.empty()) return 1;

    std::cout << "Decoded & decrypted from " << inputPath
              << " (" << width << "x" << height << ").\n\n";
    std::cout << plaintext;
    return 0;
}
