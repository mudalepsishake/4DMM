#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace obj2vxp2 {

struct Material {
    Material() = default;
    explicit Material(std::string materialName) : name(std::move(materialName)) {}

    std::string name;
    std::array<int, 3> kd = {255, 255, 255};
    std::filesystem::path mapKd;
    bool hasMapKd = false;
    std::vector<uint8_t> imageBytes;
    std::string imageName;
    bool hasImageBytes = false;
    bool twoSided = false;
};

struct Corner {
    int vertex = -1;
    int texcoord = -1;

    bool operator==(const Corner &other) const
    {
        return vertex == other.vertex && texcoord == other.texcoord;
    }
};

struct SceneFace {
    std::string material;
    std::vector<Corner> corners;
    bool twoSided = false;
};

struct ParsedScene {
    std::vector<std::array<double, 3>> positions;
    std::vector<std::array<double, 2>> texcoords;
    std::vector<SceneFace> faces;
    std::map<std::string, Material> materials;
    std::string format;
};

bool LoadGltfScene(const std::filesystem::path &path, ParsedScene *scene, std::string *error);
bool LoadFbxScene(const std::filesystem::path &path, ParsedScene *scene, std::string *error);

} // namespace obj2vxp2
