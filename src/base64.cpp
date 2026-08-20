#include "base64.h"

#include <windows.h>
#include <wincrypt.h>

#include <stdexcept>

namespace desktopnote {

std::string EncodeBase64(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) return {};
    DWORD size = 0;
    if (!CryptBinaryToStringA(bytes.data(), static_cast<DWORD>(bytes.size()),
                              CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &size)) {
        throw std::runtime_error("CryptBinaryToStringA size query failed");
    }
    std::string result(size, '\0');
    if (!CryptBinaryToStringA(bytes.data(), static_cast<DWORD>(bytes.size()),
                              CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, result.data(), &size)) {
        throw std::runtime_error("CryptBinaryToStringA failed");
    }
    if (!result.empty() && result.back() == '\0') result.pop_back();
    return result;
}

std::vector<std::uint8_t> DecodeBase64(const std::string& text) {
    if (text.empty()) return {};
    DWORD size = 0;
    constexpr DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT;
    if (!CryptStringToBinaryA(text.c_str(), static_cast<DWORD>(text.size()),
                              flags, nullptr, &size, nullptr, nullptr)) {
        throw std::runtime_error("invalid base64 input");
    }
    std::vector<std::uint8_t> result(size);
    if (!CryptStringToBinaryA(text.c_str(), static_cast<DWORD>(text.size()),
                              flags, result.data(), &size, nullptr, nullptr)) {
        throw std::runtime_error("base64 decode failed");
    }
    result.resize(size);
    return result;
}

}  // namespace desktopnote
