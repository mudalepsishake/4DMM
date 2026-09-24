#include "obj2vxp2_scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define CGLTF_IMPLEMENTATION
#include "../../brender14/core/fmt/cgltf.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace obj2vxp2 {
namespace {

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

std::string ResultName(cgltf_result result)
{
    switch (result) {
    case cgltf_result_success: return "success";
    case cgltf_result_data_too_short: return "data_too_short";
    case cgltf_result_unknown_format: return "unknown_format";
    case cgltf_result_invalid_json: return "invalid_json";
    case cgltf_result_invalid_gltf: return "invalid_gltf";
    case cgltf_result_invalid_options: return "invalid_options";
    case cgltf_result_file_not_found: return "file_not_found";
    case cgltf_result_io_error: return "io_error";
    case cgltf_result_out_of_memory: return "out_of_memory";
    case cgltf_result_legacy_gltf: return "legacy_gltf";
    default: return "unknown";
    }
}

const cgltf_attribute *FindAttribute(const cgltf_primitive &primitive, cgltf_attribute_type type, int index)
{
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
        const cgltf_attribute &attribute = primitive.attributes[i];
        if (attribute.type == type && attribute.index == index)
            return &attribute;
    }
    return nullptr;
}

std::array<double, 3> TransformPoint(const cgltf_float m[16], const cgltf_float p[3])
{
    return {
        static_cast<double>(m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]),
        static_cast<double>(m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]),
        static_cast<double>(m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]),
    };
}

double Determinant3(const cgltf_float m[16])
{
    return static_cast<double>(m[0]) * (static_cast<double>(m[5]) * m[10] - static_cast<double>(m[9]) * m[6]) -
           static_cast<double>(m[4]) * (static_cast<double>(m[1]) * m[10] - static_cast<double>(m[9]) * m[2]) +
           static_cast<double>(m[8]) * (static_cast<double>(m[1]) * m[6] - static_cast<double>(m[5]) * m[2]);
}

std::array<double, 2> ApplyTextureTransform(double u, double v, const cgltf_texture_view *view)
{
    if (view == nullptr || !view->has_transform)
        return {u, v};
    const cgltf_texture_transform &t = view->transform;
    const double su = u * static_cast<double>(t.scale[0]);
    const double sv = v * static_cast<double>(t.scale[1]);
    const double c = std::cos(static_cast<double>(t.rotation));
    const double s = std::sin(static_cast<double>(t.rotation));
    return {
        c * su - s * sv + static_cast<double>(t.offset[0]),
        s * su + c * sv + static_cast<double>(t.offset[1]),
    };
}

int ByteColor(float value)
{
    const double scaled = std::max(0.0, std::min(1.0, static_cast<double>(value))) * 255.0;
    return static_cast<int>(std::lround(scaled));
}

bool CopyImageBytes(const cgltf_image *image, Material *material)
{
    if (image == nullptr || material == nullptr)
        return false;
    if (image->buffer_view != nullptr) {
        const void *ptr = cgltf_buffer_view_data(image->buffer_view);
        if (ptr == nullptr || image->buffer_view->size == 0)
            return false;
        const auto *bytes = static_cast<const uint8_t *>(ptr);
        material->imageBytes.assign(bytes, bytes + image->buffer_view->size);
        if (image->name != nullptr && *image->name != '\0')
            material->imageName = image->name;
        else if (image->mime_type != nullptr && std::string(image->mime_type) == "image/png")
            material->imageName = material->name + ".png";
        else
            material->imageName = material->name + ".jpg";
        material->hasImageBytes = true;
        return true;
    }
    return false;
}

std::string MaterialName(const cgltf_data *data, const cgltf_material *material,
                         std::map<std::string, size_t> *seen)
{
    if (material == nullptr)
        return "default";
    const size_t index = static_cast<size_t>(cgltf_material_index(data, material));
    std::string base = material->name != nullptr && *material->name != '\0'
                           ? std::string(material->name)
                           : ("material_" + std::to_string(index));
    size_t &count = (*seen)[base];
    const std::string result = count == 0 ? base : (base + "_" + std::to_string(count + 1));
    ++count;
    return result;
}

} // namespace

bool LoadGltfScene(const std::filesystem::path &path, ParsedScene *scene, std::string *error)
{
    if (scene == nullptr)
        return false;
    *scene = ParsedScene{};

    std::vector<uint8_t> bytes;
    if (!ReadAll(path, &bytes)) {
        if (error)
            *error = "Could not read GLB: " + path.string();
        return false;
    }

    cgltf_options options{};
    cgltf_data *data = nullptr;
    cgltf_result result = cgltf_parse(&options, bytes.data(), bytes.size(), &data);
    if (result != cgltf_result_success) {
        if (error)
            *error = "GLB parse failed: " + ResultName(result);
        return false;
    }
    struct Guard {
        cgltf_data *data;
        ~Guard() { if (data != nullptr) cgltf_free(data); }
    } guard{data};

    if (data->file_type != cgltf_file_type_glb) {
        if (error)
            *error = "This milestone supports binary GLB input; use .glb rather than external .gltf";
        return false;
    }
    result = cgltf_load_buffers(&options, data, "");
    if (result != cgltf_result_success) {
        if (error)
            *error = "GLB buffer load failed: " + ResultName(result);
        return false;
    }
    result = cgltf_validate(data);
    if (result != cgltf_result_success) {
        if (error)
            *error = "GLB validation failed: " + ResultName(result);
        return false;
    }

    std::map<const cgltf_material *, std::string> materialNames;
    std::map<std::string, size_t> seenNames;
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        const cgltf_material *gm = &data->materials[i];
        const std::string name = MaterialName(data, gm, &seenNames);
        materialNames[gm] = name;
        Material material{name};
        material.twoSided = gm->double_sided != 0;
        const cgltf_texture_view *baseView = nullptr;
        if (gm->has_pbr_metallic_roughness) {
            material.kd = {ByteColor(gm->pbr_metallic_roughness.base_color_factor[0]),
                           ByteColor(gm->pbr_metallic_roughness.base_color_factor[1]),
                           ByteColor(gm->pbr_metallic_roughness.base_color_factor[2])};
            baseView = &gm->pbr_metallic_roughness.base_color_texture;
        }
        if (baseView != nullptr && baseView->texture != nullptr) {
            const cgltf_texture *texture = baseView->texture;
            const cgltf_image *image = texture->image != nullptr ? texture->image :
                                       (texture->has_webp ? texture->webp_image : nullptr);
            CopyImageBytes(image, &material);
        }
        scene->materials[name] = std::move(material);
    }

    for (cgltf_size nodeIndex = 0; nodeIndex < data->nodes_count; ++nodeIndex) {
        const cgltf_node &node = data->nodes[nodeIndex];
        if (node.mesh == nullptr)
            continue;
        cgltf_float world[16];
        cgltf_node_transform_world(&node, world);
        const bool mirrored = Determinant3(world) < 0.0;

        for (cgltf_size primitiveIndex = 0; primitiveIndex < node.mesh->primitives_count; ++primitiveIndex) {
            const cgltf_primitive &primitive = node.mesh->primitives[primitiveIndex];
            const cgltf_attribute *positionAttr = FindAttribute(primitive, cgltf_attribute_type_position, 0);
            if (positionAttr == nullptr || positionAttr->data == nullptr)
                continue;
            if (primitive.has_draco_mesh_compression) {
                if (error)
                    *error = "GLB uses Draco-compressed geometry, which this native converter does not decode";
                return false;
            }

            const cgltf_texture_view *baseView = nullptr;
            if (primitive.material != nullptr && primitive.material->has_pbr_metallic_roughness)
                baseView = &primitive.material->pbr_metallic_roughness.base_color_texture;
            int texcoordSet = 0;
            if (baseView != nullptr) {
                texcoordSet = baseView->texcoord;
                if (baseView->has_transform && baseView->transform.has_texcoord)
                    texcoordSet = baseView->transform.texcoord;
            }
            const cgltf_attribute *uvAttr = FindAttribute(primitive, cgltf_attribute_type_texcoord, texcoordSet);

            std::string materialName = "default";
            bool twoSided = false;
            if (primitive.material != nullptr) {
                auto it = materialNames.find(primitive.material);
                if (it != materialNames.end())
                    materialName = it->second;
                twoSided = primitive.material->double_sided != 0;
            }
            if (scene->materials.find(materialName) == scene->materials.end())
                scene->materials.emplace(materialName, Material{materialName});

            const size_t vertexBase = scene->positions.size();
            const size_t uvBase = scene->texcoords.size();
            const cgltf_size vertexCount = positionAttr->data->count;
            if (vertexCount > static_cast<cgltf_size>(std::numeric_limits<int>::max())) {
                if (error)
                    *error = "GLB primitive has too many vertices";
                return false;
            }
            scene->positions.reserve(scene->positions.size() + static_cast<size_t>(vertexCount));
            scene->texcoords.reserve(scene->texcoords.size() + static_cast<size_t>(vertexCount));
            for (cgltf_size i = 0; i < vertexCount; ++i) {
                cgltf_float p[3] = {};
                if (!cgltf_accessor_read_float(positionAttr->data, i, p, 3)) {
                    if (error)
                        *error = "GLB POSITION accessor could not be decoded";
                    return false;
                }
                scene->positions.push_back(TransformPoint(world, p));
                cgltf_float uv[2] = {};
                if (uvAttr != nullptr && uvAttr->data != nullptr) {
                    if (!cgltf_accessor_read_float(uvAttr->data, i, uv, 2)) {
                        if (error)
                            *error = "GLB TEXCOORD accessor could not be decoded";
                        return false;
                    }
                    const auto transformed = ApplyTextureTransform(uv[0], uv[1], baseView);
                    scene->texcoords.push_back(transformed);
                }
                else {
                    scene->texcoords.push_back({0.0, 0.0});
                }
            }

            const cgltf_size indexCount = primitive.indices != nullptr ? primitive.indices->count : vertexCount;
            std::vector<size_t> indices;
            indices.reserve(static_cast<size_t>(indexCount));
            for (cgltf_size i = 0; i < indexCount; ++i) {
                const size_t index = primitive.indices != nullptr ?
                                         static_cast<size_t>(cgltf_accessor_read_index(primitive.indices, i)) :
                                         static_cast<size_t>(i);
                if (index >= static_cast<size_t>(vertexCount)) {
                    if (error)
                        *error = "GLB index is outside the primitive vertex range";
                    return false;
                }
                indices.push_back(index);
            }

            auto addTriangle = [&](size_t a, size_t b, size_t c) {
                if (mirrored)
                    std::swap(b, c);
                SceneFace face;
                face.material = materialName;
                face.twoSided = twoSided;
                const size_t local[3] = {a, b, c};
                for (size_t localIndex : local) {
                    const size_t vi = vertexBase + localIndex;
                    const size_t ti = uvBase + localIndex;
                    if (vi > static_cast<size_t>(std::numeric_limits<int>::max()) ||
                        ti > static_cast<size_t>(std::numeric_limits<int>::max()))
                        throw std::runtime_error("GLB scene exceeds converter index range");
                    face.corners.push_back({static_cast<int>(vi), static_cast<int>(ti)});
                }
                scene->faces.push_back(std::move(face));
            };

            switch (primitive.type) {
            case cgltf_primitive_type_triangles:
                if (indices.size() % 3 != 0) {
                    if (error)
                        *error = "GLB triangle primitive has an invalid index count";
                    return false;
                }
                for (size_t i = 0; i < indices.size(); i += 3)
                    addTriangle(indices[i], indices[i + 1], indices[i + 2]);
                break;
            case cgltf_primitive_type_triangle_strip:
                for (size_t i = 0; i + 2 < indices.size(); ++i) {
                    if ((i & 1u) == 0)
                        addTriangle(indices[i], indices[i + 1], indices[i + 2]);
                    else
                        addTriangle(indices[i + 1], indices[i], indices[i + 2]);
                }
                break;
            case cgltf_primitive_type_triangle_fan:
                for (size_t i = 1; i + 1 < indices.size(); ++i)
                    addTriangle(indices[0], indices[i], indices[i + 1]);
                break;
            default:
                break;
            }
        }
    }

    if (scene->faces.empty()) {
        if (error)
            *error = "GLB contains no triangle mesh geometry";
        return false;
    }
    scene->format = "glb";
    return true;
}

} // namespace obj2vxp2
