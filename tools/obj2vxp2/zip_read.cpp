#include "zip_read.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define STBI_ONLY_ZLIB
#define STBI_SUPPORT_ZLIB
#define STB_IMAGE_IMPLEMENTATION
#include "../../brender14/core/fmt/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace obj2vxp2 {
namespace {

uint16_t U16(const std::vector<uint8_t> &data, size_t off)
{
    if (off + 2 > data.size())
        return 0;
    return static_cast<uint16_t>(data[off]) |
           static_cast<uint16_t>(static_cast<uint16_t>(data[off + 1]) << 8);
}

uint32_t U32(const std::vector<uint8_t> &data, size_t off)
{
    if (off + 4 > data.size())
        return 0;
    return static_cast<uint32_t>(data[off]) |
           (static_cast<uint32_t>(data[off + 1]) << 8) |
           (static_cast<uint32_t>(data[off + 2]) << 16) |
           (static_cast<uint32_t>(data[off + 3]) << 24);
}

bool ReadAll(const std::filesystem::path &path, std::vector<uint8_t> *data)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    in.seekg(0, std::ios::end);
    const std::streamoff end = in.tellg();
    if (end < 0)
        return false;
    in.seekg(0, std::ios::beg);
    data->resize(static_cast<size_t>(end));
    if (!data->empty())
        in.read(reinterpret_cast<char *>(data->data()), static_cast<std::streamsize>(data->size()));
    return static_cast<bool>(in) || data->empty();
}

std::string NormalizeName(std::string name)
{
    std::replace(name.begin(), name.end(), '\\', '/');
    while (!name.empty() && name.front() == '/')
        name.erase(name.begin());
    return name;
}

} // namespace


bool InflateZlib(const uint8_t *compressed, size_t compressedSize,
                 uint8_t *output, size_t outputSize)
{
    if ((compressed == nullptr && compressedSize != 0) || (output == nullptr && outputSize != 0) ||
        compressedSize > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        outputSize > static_cast<size_t>(std::numeric_limits<int>::max()))
        return false;
    const int wrote = stbi_zlib_decode_buffer(reinterpret_cast<char *>(output), static_cast<int>(outputSize),
                                              reinterpret_cast<const char *>(compressed), static_cast<int>(compressedSize));
    return wrote >= 0 && static_cast<size_t>(wrote) == outputSize;
}

bool InflateRawDeflate(const uint8_t *compressed, size_t compressedSize,
                       uint8_t *output, size_t outputSize)
{
    if ((compressed == nullptr && compressedSize != 0) || (output == nullptr && outputSize != 0) ||
        compressedSize > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        outputSize > static_cast<size_t>(std::numeric_limits<int>::max()))
        return false;
    const int wrote = stbi_zlib_decode_noheader_buffer(reinterpret_cast<char *>(output), static_cast<int>(outputSize),
                                                       reinterpret_cast<const char *>(compressed), static_cast<int>(compressedSize));
    return wrote >= 0 && static_cast<size_t>(wrote) == outputSize;
}

bool ReadZipEntries(const std::filesystem::path &path,
                    std::map<std::string, std::vector<uint8_t>> *entries,
                    std::string *error)
{
    if (entries == nullptr)
        return false;
    entries->clear();

    std::vector<uint8_t> data;
    if (!ReadAll(path, &data)) {
        if (error)
            *error = "Could not read ZIP: " + path.string();
        return false;
    }
    if (data.size() < 22) {
        if (error)
            *error = "ZIP is too small";
        return false;
    }

    const size_t searchStart = data.size() > (0xffffu + 22u) ? data.size() - (0xffffu + 22u) : 0;
    size_t eocd = std::numeric_limits<size_t>::max();
    for (size_t pos = data.size() - 22;; --pos) {
        if (U32(data, pos) == 0x06054b50u) {
            eocd = pos;
            break;
        }
        if (pos == searchStart)
            break;
    }
    if (eocd == std::numeric_limits<size_t>::max()) {
        if (error)
            *error = "ZIP end-of-central-directory record not found";
        return false;
    }

    const uint16_t disk = U16(data, eocd + 4);
    const uint16_t centralDisk = U16(data, eocd + 6);
    const uint16_t entriesOnDisk = U16(data, eocd + 8);
    const uint16_t totalEntries = U16(data, eocd + 10);
    const uint32_t centralSize = U32(data, eocd + 12);
    const uint32_t centralOffset = U32(data, eocd + 16);
    if (disk != 0 || centralDisk != 0 || entriesOnDisk != totalEntries ||
        totalEntries == 0xffffu || centralOffset == 0xffffffffu || centralSize == 0xffffffffu) {
        if (error)
            *error = "Multi-disk/ZIP64 input archives are not supported";
        return false;
    }
    if (static_cast<uint64_t>(centralOffset) + centralSize > data.size()) {
        if (error)
            *error = "ZIP central directory is out of range";
        return false;
    }

    size_t pos = centralOffset;
    for (uint16_t index = 0; index < totalEntries; ++index) {
        if (pos + 46 > data.size() || U32(data, pos) != 0x02014b50u) {
            if (error)
                *error = "Invalid ZIP central directory entry";
            return false;
        }
        const uint16_t flags = U16(data, pos + 8);
        const uint16_t method = U16(data, pos + 10);
        const uint32_t compressedSize = U32(data, pos + 20);
        const uint32_t uncompressedSize = U32(data, pos + 24);
        const uint16_t nameLen = U16(data, pos + 28);
        const uint16_t extraLen = U16(data, pos + 30);
        const uint16_t commentLen = U16(data, pos + 32);
        const uint32_t localOffset = U32(data, pos + 42);
        if (flags & 0x0001u) {
            if (error)
                *error = "Encrypted ZIP entries are not supported";
            return false;
        }
        if (pos + 46u + nameLen + extraLen + commentLen > data.size()) {
            if (error)
                *error = "ZIP central directory name is out of range";
            return false;
        }
        std::string name(reinterpret_cast<const char *>(data.data() + pos + 46), nameLen);
        name = NormalizeName(name);
        pos += 46u + nameLen + extraLen + commentLen;
        if (name.empty() || name.back() == '/')
            continue;

        if (static_cast<uint64_t>(localOffset) + 30u > data.size() || U32(data, localOffset) != 0x04034b50u) {
            if (error)
                *error = "ZIP local file header is invalid for " + name;
            return false;
        }
        const uint16_t localNameLen = U16(data, localOffset + 26);
        const uint16_t localExtraLen = U16(data, localOffset + 28);
        const uint64_t payloadOffset = static_cast<uint64_t>(localOffset) + 30u + localNameLen + localExtraLen;
        if (payloadOffset + compressedSize > data.size()) {
            if (error)
                *error = "ZIP payload is out of range for " + name;
            return false;
        }
        const uint8_t *compressed = data.data() + static_cast<size_t>(payloadOffset);
        std::vector<uint8_t> decoded;
        if (method == 0) {
            decoded.assign(compressed, compressed + compressedSize);
            if (decoded.size() != uncompressedSize) {
                if (error)
                    *error = "Stored ZIP size mismatch for " + name;
                return false;
            }
        }
        else if (method == 8) {
            decoded.resize(uncompressedSize);
            if (!InflateRawDeflate(compressed, compressedSize, decoded.data(), decoded.size())) {
                if (error)
                    *error = "DEFLATE decode failed for ZIP entry " + name;
                return false;
            }
        }
        else {
            if (error) {
                std::ostringstream ss;
                ss << "ZIP compression method " << method << " is not supported for " << name;
                *error = ss.str();
            }
            return false;
        }
        (*entries)[name] = std::move(decoded);
    }
    return true;
}

} // namespace obj2vxp2
