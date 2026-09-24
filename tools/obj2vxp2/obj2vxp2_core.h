#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace obj2vxp2 {

constexpr const char *kVersion = "6";

enum class AssetKind {
    Prop,
    Actor,
};

enum class TextureVMode {
    Auto,
    Flip,
    Preserve,
};

struct Stats {
    size_t sourceVertices = 0;
    size_t sourceFaces = 0;
    size_t bmdlParts = 0;
    size_t bmdlVertices = 0;
    size_t triangles = 0;
    size_t textures = 0;
    size_t fourcnBytes = 0;
    std::string sourceFormat;
};

using LogFn = std::function<void(const std::string &)>;
using ImageConvertFn = std::function<bool(const std::filesystem::path &, std::vector<uint8_t> *, std::string *)>;
using ImageConvertMemoryFn = std::function<bool(const std::vector<uint8_t> &, std::vector<uint8_t> *, std::string *)>;

struct ConvertOptions {
    std::filesystem::path inputPath;
    std::filesystem::path outputPath;
    std::string name;
    AssetKind kind = AssetKind::Prop;
    double scale = 1.0;
    TextureVMode textureVMode = TextureVMode::Auto;
    bool twoSidedFaces = false;
    bool centerBottom = true;
    ImageConvertFn imageConverter;
    ImageConvertMemoryFn imageMemoryConverter;
    LogFn log;
};

bool Convert(const ConvertOptions &options, Stats *stats, std::string *error);

} // namespace obj2vxp2
