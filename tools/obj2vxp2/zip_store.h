#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace obj2vxp2 {

uint32_t Crc32(const void *data, size_t size);

class ZipStoreWriter {
  public:
    void Add(std::string name, std::vector<uint8_t> data);
    bool Write(const std::filesystem::path &path, std::string *error) const;

  private:
    struct Entry {
        std::string name;
        std::vector<uint8_t> data;
    };
    std::vector<Entry> entries_;
};

} // namespace obj2vxp2
