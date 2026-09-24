#include "zip_store.h"

#include <array>
#include <chrono>
#include <ctime>
#include <fstream>
#include <limits>

namespace obj2vxp2 {
namespace {

void PutU16(std::ostream &os, uint16_t value)
{
    const char b[2] = {static_cast<char>(value & 0xff), static_cast<char>((value >> 8) & 0xff)};
    os.write(b, sizeof(b));
}

void PutU32(std::ostream &os, uint32_t value)
{
    const char b[4] = {static_cast<char>(value & 0xff), static_cast<char>((value >> 8) & 0xff),
                       static_cast<char>((value >> 16) & 0xff), static_cast<char>((value >> 24) & 0xff)};
    os.write(b, sizeof(b));
}

bool TellU32(std::ostream &os, uint32_t *value)
{
    const std::streampos pos = os.tellp();
    if (pos < 0 || static_cast<uint64_t>(pos) > std::numeric_limits<uint32_t>::max())
        return false;
    *value = static_cast<uint32_t>(pos);
    return true;
}

void GetDosDateTime(uint16_t *date, uint16_t *time)
{
    const std::time_t now = std::time(nullptr);
    std::tm tmNow{};
#if defined(_WIN32)
    localtime_s(&tmNow, &now);
#else
    localtime_r(&now, &tmNow);
#endif
    int year = tmNow.tm_year + 1900;
    if (year < 1980)
        year = 1980;
    if (year > 2107)
        year = 2107;
    *date = static_cast<uint16_t>(((year - 1980) << 9) | ((tmNow.tm_mon + 1) << 5) | tmNow.tm_mday);
    *time = static_cast<uint16_t>((tmNow.tm_hour << 11) | (tmNow.tm_min << 5) | (tmNow.tm_sec / 2));
}

struct CentralEntry {
    std::string name;
    uint32_t crc = 0;
    uint32_t size = 0;
    uint32_t localOffset = 0;
    uint16_t dosDate = 0;
    uint16_t dosTime = 0;
};

} // namespace

uint32_t Crc32(const void *data, size_t size)
{
    static std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int bit = 0; bit < 8; ++bit)
                c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();

    uint32_t crc = 0xffffffffu;
    const auto *bytes = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; ++i)
        crc = table[(crc ^ bytes[i]) & 0xffu] ^ (crc >> 8);
    return crc ^ 0xffffffffu;
}

void ZipStoreWriter::Add(std::string name, std::vector<uint8_t> data)
{
    entries_.push_back({std::move(name), std::move(data)});
}

bool ZipStoreWriter::Write(const std::filesystem::path &path, std::string *error) const
{
    if (entries_.empty()) {
        if (error)
            *error = "ZIP contains no entries";
        return false;
    }
    if (entries_.size() > std::numeric_limits<uint16_t>::max()) {
        if (error)
            *error = "ZIP has too many entries for classic ZIP";
        return false;
    }

    std::error_code ec;
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        if (error)
            *error = "Could not create output directory: " + ec.message();
        return false;
    }

    std::ofstream os(path, std::ios::binary | std::ios::trunc);
    if (!os) {
        if (error)
            *error = "Could not create ZIP: " + path.string();
        return false;
    }

    std::vector<CentralEntry> central;
    central.reserve(entries_.size());
    for (const Entry &entry : entries_) {
        if (entry.name.empty() || entry.name.size() > std::numeric_limits<uint16_t>::max() ||
            entry.data.size() > std::numeric_limits<uint32_t>::max()) {
            if (error)
                *error = "ZIP entry is too large or has an invalid name: " + entry.name;
            return false;
        }
        CentralEntry ce;
        ce.name = entry.name;
        ce.size = static_cast<uint32_t>(entry.data.size());
        ce.crc = Crc32(entry.data.data(), entry.data.size());
        GetDosDateTime(&ce.dosDate, &ce.dosTime);
        if (!TellU32(os, &ce.localOffset)) {
            if (error)
                *error = "VXP2 exceeded classic ZIP offset limits";
            return false;
        }

        PutU32(os, 0x04034b50u);
        PutU16(os, 20);
        PutU16(os, 0);
        PutU16(os, 0); // Stored. PNG payloads are already compressed.
        PutU16(os, ce.dosTime);
        PutU16(os, ce.dosDate);
        PutU32(os, ce.crc);
        PutU32(os, ce.size);
        PutU32(os, ce.size);
        PutU16(os, static_cast<uint16_t>(entry.name.size()));
        PutU16(os, 0);
        os.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
        if (!entry.data.empty())
            os.write(reinterpret_cast<const char *>(entry.data.data()), static_cast<std::streamsize>(entry.data.size()));
        if (!os) {
            if (error)
                *error = "Failed while writing ZIP entry: " + entry.name;
            return false;
        }
        central.push_back(std::move(ce));
    }

    uint32_t centralOffset = 0;
    if (!TellU32(os, &centralOffset)) {
        if (error)
            *error = "VXP2 exceeded classic ZIP offset limits";
        return false;
    }

    for (const CentralEntry &ce : central) {
        PutU32(os, 0x02014b50u);
        PutU16(os, 20);
        PutU16(os, 20);
        PutU16(os, 0);
        PutU16(os, 0);
        PutU16(os, ce.dosTime);
        PutU16(os, ce.dosDate);
        PutU32(os, ce.crc);
        PutU32(os, ce.size);
        PutU32(os, ce.size);
        PutU16(os, static_cast<uint16_t>(ce.name.size()));
        PutU16(os, 0);
        PutU16(os, 0);
        PutU16(os, 0);
        PutU16(os, 0);
        PutU32(os, 0);
        PutU32(os, ce.localOffset);
        os.write(ce.name.data(), static_cast<std::streamsize>(ce.name.size()));
    }

    uint32_t centralEnd = 0;
    if (!TellU32(os, &centralEnd)) {
        if (error)
            *error = "VXP2 exceeded classic ZIP offset limits";
        return false;
    }
    const uint32_t centralSize = centralEnd - centralOffset;

    PutU32(os, 0x06054b50u);
    PutU16(os, 0);
    PutU16(os, 0);
    PutU16(os, static_cast<uint16_t>(central.size()));
    PutU16(os, static_cast<uint16_t>(central.size()));
    PutU32(os, centralSize);
    PutU32(os, centralOffset);
    PutU16(os, 0);
    os.flush();
    if (!os) {
        if (error)
            *error = "Failed while finalizing ZIP: " + path.string();
        return false;
    }
    return true;
}

} // namespace obj2vxp2
