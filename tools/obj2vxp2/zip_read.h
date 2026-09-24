#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace obj2vxp2 {

bool ReadZipEntries(const std::filesystem::path &path,
                    std::map<std::string, std::vector<uint8_t>> *entries,
                    std::string *error);

bool InflateZlib(const uint8_t *compressed, size_t compressedSize,
                 uint8_t *output, size_t outputSize);
bool InflateRawDeflate(const uint8_t *compressed, size_t compressedSize,
                       uint8_t *output, size_t outputSize);

} // namespace obj2vxp2
