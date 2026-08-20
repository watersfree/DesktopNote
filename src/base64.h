#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace desktopnote {

std::string EncodeBase64(const std::vector<std::uint8_t>& bytes);
std::vector<std::uint8_t> DecodeBase64(const std::string& text);

}  // namespace desktopnote
