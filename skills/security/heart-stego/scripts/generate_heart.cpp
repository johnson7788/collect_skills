// generate_heart.cpp
// C++17: create a heart-shaped BMP and embed the full environment, encrypted
// with AES-256-GCM (key derived from a password via PBKDF2), in the LSB of
// its pixel channels. No network access.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/evp.h>
#include <openssl/rand.h>

extern char** environ;

// 默认密码：不传命令行密码时使用。源码公开即等于公开此密码，请改成你自己的。
const char* kDefaultPassword = "heart-default-password";

namespace {
constexpr int kWidth = 512;
constexpr int kHeight = 512;
constexpr char kMagic[] = "HRTSAFE1"; // 8 bytes
using Pixel = std::array<std::uint8_t, 3>; // BMP byte order: B, G, R

void putLe16(std::ostream& out, std::uint16_t v) {
    out.put(static_cast<char>(v & 0xFF));
    out.put(static_cast<char>((v >> 8) & 0xFF));
}
void putLe32(std::ostream& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.put(static_cast<char>((v >> (8 * i)) & 0xFF));
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

std::string platformName() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown/other";
#endif
}

std::string collectAllEnv() {
    std::ostringstream out;
    out << "FULL ENVIRONMENT SNAPSHOT\n";
    out << "Platform: " << platformName() << "\n";
    out << "Hardware threads (C++ runtime): " << std::thread::hardware_concurrency() << "\n";
    out << "\n[All environment variables]\n";

    for (char** e = environ; e && *e; ++e) {
        out << *e << "\n";
    }
    return out.str();
}

std::string encryptAesGcm(const std::string& plaintext, const std::string& password) {
    constexpr int kSaltLen = 16;
    constexpr int kNonceLen = 12;
    constexpr int kKeyLen = 32;
    constexpr int kTagLen = 16;
    constexpr int kIterations = 100000;

    unsigned char salt[kSaltLen];
    unsigned char nonce[kNonceLen];
    if (RAND_bytes(salt, kSaltLen) != 1 || RAND_bytes(nonce, kNonceLen) != 1) {
        std::cerr << "Error: random number generation failed.\n";
        return "";
    }

    unsigned char key[kKeyLen];
    if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()),
                          salt, kSaltLen, kIterations, EVP_sha256(), kKeyLen, key) != 1) {
        std::cerr << "Error: key derivation failed.\n";
        return "";
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return "";

    std::vector<unsigned char> ciphertext(plaintext.size() + 16);
    int len = 0;
    int total = 0;
    const auto* in = reinterpret_cast<const unsigned char*>(plaintext.data());

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kNonceLen, nullptr) != 1 ||
        EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, nonce) != 1 ||
        EVP_EncryptUpdate(ctx, ciphertext.data(), &len, in,
                          static_cast<int>(plaintext.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }
    total = len;
    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + total, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }
    total += len;

    unsigned char tag[kTagLen];
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kTagLen, tag) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }
    EVP_CIPHER_CTX_free(ctx);

    std::string blob;
    blob.assign(reinterpret_cast<char*>(salt), kSaltLen);
    blob.append(reinterpret_cast<char*>(nonce), kNonceLen);
    blob.append(reinterpret_cast<char*>(ciphertext.data()), total);
    blob.append(reinterpret_cast<char*>(tag), kTagLen);
    return blob;
}

std::vector<Pixel> makeHeartImage() {
    std::vector<Pixel> pixels(static_cast<std::size_t>(kWidth) * kHeight);
    for (int py = 0; py < kHeight; ++py) {
        for (int px = 0; px < kWidth; ++px) {
            // White background.
            Pixel p{255, 255, 255};

            // Standard implicit heart curve. y is positive upwards.
            const double x = (px - kWidth * 0.5) / 165.0;
            const double y = (kHeight * 0.48 - py) / 165.0;
            const double a = x * x + y * y - 1.0;
            const double f = a * a * a - x * x * y * y * y;
            if (f <= 0.0) {
                // Gentle red gradient, with a slight highlight near the top.
                const double glow = std::max(0.0, 1.0 - (x * x + (y - 0.35) * (y - 0.35)));
                const int red = 205 + static_cast<int>(40.0 * glow);
                const int green = 25 + static_cast<int>(28.0 * glow);
                const int blue = 55 + static_cast<int>(35.0 * glow);
                p = {static_cast<std::uint8_t>(blue),
                     static_cast<std::uint8_t>(green),
                     static_cast<std::uint8_t>(red)};
            }
            pixels[static_cast<std::size_t>(py) * kWidth + px] = p;
        }
    }
    return pixels;
}

std::vector<std::uint8_t> makeFrame(const std::string& payload) {
    std::vector<std::uint8_t> frame;
    frame.reserve(8 + 4 + payload.size() + 4);
    for (int i = 0; i < 8; ++i) frame.push_back(static_cast<std::uint8_t>(kMagic[i]));
    const std::uint32_t len = static_cast<std::uint32_t>(payload.size());
    for (int i = 0; i < 4; ++i) frame.push_back(static_cast<std::uint8_t>((len >> (8 * i)) & 0xFF));
    frame.insert(frame.end(), payload.begin(), payload.end());
    const std::uint32_t crc = crc32(payload);
    for (int i = 0; i < 4; ++i) frame.push_back(static_cast<std::uint8_t>((crc >> (8 * i)) & 0xFF));
    return frame;
}

bool embedPayload(std::vector<Pixel>& pixels, const std::vector<std::uint8_t>& frame) {
    const std::size_t capacityBits = pixels.size() * 3;
    if (frame.size() * 8 > capacityBits) return false;

    std::size_t bitIndex = 0;
    for (std::uint8_t byte : frame) {
        for (int bit = 7; bit >= 0; --bit) {
            const std::size_t pixelIndex = bitIndex / 3;
            const std::size_t channelIndex = bitIndex % 3;
            const std::uint8_t value = static_cast<std::uint8_t>((byte >> bit) & 1u);
            pixels[pixelIndex][channelIndex] =
                static_cast<std::uint8_t>((pixels[pixelIndex][channelIndex] & 0xFEu) | value);
            ++bitIndex;
        }
    }
    return true;
}

bool saveBmp(const std::string& path, const std::vector<Pixel>& pixels) {
    const std::uint32_t rowBytes = static_cast<std::uint32_t>(kWidth) * 3;
    const std::uint32_t stride = (rowBytes + 3u) & ~3u;
    const std::uint32_t imageSize = stride * kHeight;
    const std::uint32_t fileSize = 54u + imageSize;

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    out.put('B'); out.put('M');
    putLe32(out, fileSize);
    putLe16(out, 0); putLe16(out, 0);
    putLe32(out, 54);
    putLe32(out, 40); // BITMAPINFOHEADER
    putLe32(out, kWidth);
    putLe32(out, kHeight); // positive => bottom-up rows
    putLe16(out, 1);
    putLe16(out, 24);
    putLe32(out, 0); // BI_RGB
    putLe32(out, imageSize);
    putLe32(out, 2835); putLe32(out, 2835);
    putLe32(out, 0); putLe32(out, 0);

    const char padding[3] = {0, 0, 0};
    const int padCount = static_cast<int>(stride - rowBytes);
    for (int y = kHeight - 1; y >= 0; --y) {
        for (int x = 0; x < kWidth; ++x) {
            const Pixel& p = pixels[static_cast<std::size_t>(y) * kWidth + x];
            out.put(static_cast<char>(p[0]));
            out.put(static_cast<char>(p[1]));
            out.put(static_cast<char>(p[2]));
        }
        out.write(padding, padCount);
    }
    return static_cast<bool>(out);
}
} // namespace

int main(int argc, char** argv) {
    const std::string outputPath = (argc >= 2) ? argv[1] : "heart.bmp";
    const std::string password = (argc >= 3) ? argv[2] : kDefaultPassword;

    const std::string plaintext = collectAllEnv();
    const std::string payload = encryptAesGcm(plaintext, password);
    if (payload.empty()) return 1;

    auto pixels = makeHeartImage();
    const auto frame = makeFrame(payload);
    if (!embedPayload(pixels, frame)) {
        std::cerr << "Error: payload is too large for this image.\n";
        return 1;
    }
    if (!saveBmp(outputPath, pixels)) {
        std::cerr << "Error: could not write image: " << outputPath << "\n";
        return 1;
    }

    std::cout << "Created: " << outputPath << "\n";
    std::cout << "Encrypted payload (" << plaintext.size() << " -> " << payload.size() << " bytes).\n";
    return 0;
}
