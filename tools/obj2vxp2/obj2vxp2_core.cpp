#include "obj2vxp2_core.h"
#include "obj2vxp2_scene.h"
#include "zip_store.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace obj2vxp2 {
namespace {

constexpr uint32_t kMaxBmdlCount = 32767;
constexpr uint32_t kMaxU16 = 65535;
constexpr double kBmdlScale = 163840.0;
constexpr double kUvScale = 65535.0;

class ConvertError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

struct CornerHash {
    size_t operator()(const Corner &value) const noexcept
    {
        const uint64_t a = static_cast<uint32_t>(value.vertex);
        const uint64_t b = static_cast<uint32_t>(value.texcoord + 1);
        return static_cast<size_t>((a << 32) ^ b);
    }
};

struct MeshCorner {
    Corner corner;
    bool backSide = false;

    bool operator==(const MeshCorner &other) const
    {
        return corner == other.corner && backSide == other.backSide;
    }
};

struct MeshCornerHash {
    size_t operator()(const MeshCorner &value) const noexcept
    {
        size_t hash = CornerHash{}(value.corner);
        if (value.backSide)
            hash ^= static_cast<size_t>(0x9e3779b97f4a7c15ULL);
        return hash;
    }
};

struct MeshVertex {
    double x = 0;
    double y = 0;
    double z = 0;
    double u = 0;
    double v = 0;
};

struct MeshPart {
    std::string material;
    std::vector<MeshVertex> vertices;
    std::vector<std::array<uint16_t, 3>> triangles;
    std::unordered_map<MeshCorner, uint16_t, MeshCornerHash> vertexMap;

    size_t NeededNewVertices(const std::array<Corner, 3> &tri, bool backSide) const
    {
        size_t count = 0;
        for (const Corner &corner : tri) {
            if (vertexMap.find({corner, backSide}) == vertexMap.end())
                ++count;
        }
        return count;
    }

    bool CanAdd(const std::array<Corner, 3> &tri, bool backSide) const
    {
        return triangles.size() + 1 <= kMaxBmdlCount &&
               vertices.size() + NeededNewVertices(tri, backSide) <= kMaxBmdlCount;
    }
};

struct Quad;
using QuadPtr = std::shared_ptr<Quad>;

struct Quad {
    std::string type;
    uint32_t cno = 0;
    uint8_t mode = 0;
    std::vector<uint8_t> data;
    std::string name;
    bool hasName = false;
    std::vector<std::pair<uint32_t, QuadPtr>> refs;

    void Ref(const QuadPtr &other, uint32_t chid)
    {
        refs.push_back({chid, other});
    }
};

void Log(const ConvertOptions &options, const std::string &line)
{
    if (options.log)
        options.log(line);
}


const char *TextureVModeName(TextureVMode mode)
{
    switch (mode) {
    case TextureVMode::Auto: return "auto";
    case TextureVMode::Flip: return "flip";
    case TextureVMode::Preserve: return "preserve";
    default: return "unknown";
    }
}

bool EffectiveFlipV(TextureVMode mode, const std::string &format)
{
    if (mode == TextureVMode::Flip)
        return true;
    if (mode == TextureVMode::Preserve)
        return false;

    // OBJ/FBX need the historical 3DMM/BRender V correction used by the
    // proven Python converter. glTF/GLB already defines texture coordinates
    // in the convention that reaches the VXP2 truecolour path correctly, so
    // applying the OBJ correction a second time vertically remaps atlases.
    return format != "glb";
}

std::string PathForLog(const std::filesystem::path &path)
{
#if defined(__cpp_lib_char8_t)
    const auto u8 = path.u8string();
    return std::string(reinterpret_cast<const char *>(u8.data()), u8.size());
#else
    return path.u8string();
#endif
}

std::string Trim(const std::string &value)
{
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string LowerAscii(std::string value)
{
    for (char &ch : value) {
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');
    }
    return value;
}

std::vector<std::string> TokenizeQuoted(const std::string &text)
{
    // Match Python v2's shlex.split(..., posix=False) behavior needed by OBJ/MTL:
    // quotes group whitespace, while Windows path backslashes stay literal.
    std::vector<std::string> out;
    std::string current;
    char quote = 0;
    for (char ch : text) {
        if (quote != 0) {
            if (ch == quote)
                quote = 0;
            else
                current.push_back(ch);
            continue;
        }
        if (ch == '"' || ch == '\'') {
            quote = ch;
            continue;
        }
        if (ch == ' ' || ch == '\t') {
            if (!current.empty()) {
                out.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty())
        out.push_back(current);
    return out;
}

std::filesystem::path Resolve(const std::filesystem::path &base, const std::filesystem::path &child)
{
    std::error_code ec;
    const std::filesystem::path combined = child.is_absolute() ? child : (base / child);
    std::filesystem::path absolute = std::filesystem::absolute(combined, ec);
    if (ec)
        return combined.lexically_normal();
    return absolute.lexically_normal();
}

long long PythonRound(double value)
{
    if (!std::isfinite(value))
        throw ConvertError("Non-finite numeric value in OBJ/MTL");
    const double base = std::floor(value);
    const double fraction = value - base;
    double rounded = base;
    if (fraction > 0.5)
        rounded = base + 1.0;
    else if (fraction == 0.5) {
        const long long ibase = static_cast<long long>(base);
        rounded = (ibase & 1LL) ? (base + 1.0) : base;
    }
    if (rounded < static_cast<double>(std::numeric_limits<long long>::min()) ||
        rounded > static_cast<double>(std::numeric_limits<long long>::max()))
        throw ConvertError("Rounded value is out of range");
    return static_cast<long long>(rounded);
}

std::string StripOuterQuotes(std::string value)
{
    value = Trim(value);
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                              (value.front() == '\'' && value.back() == '\'')))
        return value.substr(1, value.size() - 2);
    return value;
}

void ParseMtl(const std::filesystem::path &path, std::map<std::string, Material> *materials)
{
    std::ifstream in(path);
    if (!in)
        throw ConvertError("MTL not found: " + PathForLog(path));

    Material *current = nullptr;
    std::string raw;
    while (std::getline(in, raw)) {
        const std::string line = Trim(raw);
        if (line.empty() || line[0] == '#')
            continue;
        const size_t space = line.find(' ');
        const std::string head = space == std::string::npos ? line : line.substr(0, space);
        const std::string rest = space == std::string::npos ? std::string() : Trim(line.substr(space + 1));
        const std::string cmd = LowerAscii(head);
        if (cmd == "newmtl") {
            Material mat;
            mat.name = rest;
            (*materials)[rest] = mat;
            current = &(*materials)[rest];
        }
        else if (current && cmd == "kd") {
            std::istringstream ss(rest);
            double r = 1.0, g = 1.0, b = 1.0;
            if (ss >> r >> g >> b) {
                const double values[3] = {r, g, b};
                for (int i = 0; i < 3; ++i) {
                    long long n = PythonRound(values[i] * 255.0);
                    n = std::max<long long>(0, std::min<long long>(255, n));
                    current->kd[static_cast<size_t>(i)] = static_cast<int>(n);
                }
            }
        }
        else if (current && cmd == "map_kd") {
            std::vector<std::string> tokens = TokenizeQuoted(rest);
            if (tokens.empty())
                continue;

            const std::string directText = StripOuterQuotes(rest);
            const std::filesystem::path direct = Resolve(path.parent_path(), std::filesystem::u8path(directText));
            std::error_code ec;
            if (std::filesystem::exists(direct, ec) && !ec) {
                current->mapKd = direct;
                current->hasMapKd = true;
                continue;
            }

            static const std::map<std::string, size_t> optionArgc = {
                {"-blendu", 1}, {"-blendv", 1}, {"-boost", 1}, {"-mm", 2}, {"-o", 3},
                {"-s", 3}, {"-t", 3}, {"-texres", 1}, {"-clamp", 1}, {"-bm", 1},
                {"-imfchan", 1}, {"-type", 1}, {"-cc", 1},
            };
            size_t i = 0;
            while (i < tokens.size()) {
                auto it = optionArgc.find(LowerAscii(tokens[i]));
                if (it == optionArgc.end())
                    break;
                const size_t advance = 1 + it->second;
                if (i + advance > tokens.size()) {
                    i = tokens.size();
                    break;
                }
                i += advance;
            }
            std::string tail;
            if (i < tokens.size()) {
                for (size_t j = i; j < tokens.size(); ++j) {
                    if (!tail.empty())
                        tail.push_back(' ');
                    tail += tokens[j];
                }
            }
            else {
                tail = tokens.back();
            }
            current->mapKd = Resolve(path.parent_path(), std::filesystem::u8path(StripOuterQuotes(tail)));
            current->hasMapKd = true;
        }
    }
}

int ObjIndex(const std::string &token, size_t count, const char *what, size_t line)
{
    long long value = 0;
    try {
        size_t used = 0;
        value = std::stoll(token, &used, 10);
        if (used != token.size())
            throw std::invalid_argument("trailing");
    }
    catch (...) {
        throw ConvertError("OBJ line " + std::to_string(line) + ": bad " + what + " index '" + token + "'");
    }
    if (value == 0)
        throw ConvertError("OBJ line " + std::to_string(line) + ": " + what + " index 0 is invalid");
    const long long index = value > 0 ? value - 1 : static_cast<long long>(count) + value;
    if (index < 0 || index >= static_cast<long long>(count))
        throw ConvertError("OBJ line " + std::to_string(line) + ": " + what + " index " +
                           std::to_string(value) + " out of range");
    return static_cast<int>(index);
}

ParsedScene ParseObj(const std::filesystem::path &path)
{
    std::ifstream in(path);
    if (!in)
        throw ConvertError("OBJ not found: " + PathForLog(path));

    ParsedScene parsed;
    parsed.format = "obj";
    parsed.materials["Default"] = Material{"Default"};
    std::string current = "Default";
    std::string raw;
    size_t lineNumber = 0;
    while (std::getline(in, raw)) {
        ++lineNumber;
        const std::string line = Trim(raw);
        if (line.empty() || line[0] == '#')
            continue;
        const size_t space = line.find(' ');
        const std::string head = space == std::string::npos ? line : line.substr(0, space);
        const std::string rest = space == std::string::npos ? std::string() : Trim(line.substr(space + 1));
        const std::string cmd = LowerAscii(head);
        if (cmd == "v") {
            std::istringstream ss(rest);
            std::array<double, 3> p{};
            if (!(ss >> p[0] >> p[1] >> p[2]))
                throw ConvertError("OBJ line " + std::to_string(lineNumber) + ": vertex needs xyz");
            parsed.positions.push_back(p);
        }
        else if (cmd == "vt") {
            std::istringstream ss(rest);
            std::array<double, 2> uv{};
            if (!(ss >> uv[0] >> uv[1]))
                throw ConvertError("OBJ line " + std::to_string(lineNumber) + ": vt needs uv");
            parsed.texcoords.push_back(uv);
        }
        else if (cmd == "mtllib") {
            for (const std::string &lib : TokenizeQuoted(rest))
                ParseMtl(Resolve(path.parent_path(), std::filesystem::u8path(lib)), &parsed.materials);
        }
        else if (cmd == "usemtl") {
            current = rest.empty() ? "Default" : rest;
            if (parsed.materials.find(current) == parsed.materials.end())
                parsed.materials[current] = Material{current};
        }
        else if (cmd == "f") {
            const std::vector<std::string> tokens = TokenizeQuoted(rest);
            if (tokens.size() < 3)
                continue;
            SceneFace face;
            face.material = current;
            for (const std::string &token : tokens) {
                std::vector<std::string> pieces;
                size_t start = 0;
                for (;;) {
                    const size_t slash = token.find('/', start);
                    pieces.push_back(token.substr(start, slash == std::string::npos ? std::string::npos : slash - start));
                    if (slash == std::string::npos)
                        break;
                    start = slash + 1;
                }
                if (pieces.empty() || pieces[0].empty())
                    throw ConvertError("OBJ line " + std::to_string(lineNumber) + ": face has no vertex index");
                Corner corner;
                corner.vertex = ObjIndex(pieces[0], parsed.positions.size(), "vertex", lineNumber);
                if (pieces.size() >= 2 && !pieces[1].empty())
                    corner.texcoord = ObjIndex(pieces[1], parsed.texcoords.size(), "texture", lineNumber);
                face.corners.push_back(corner);
            }
            parsed.faces.push_back(std::move(face));
        }
    }

    if (parsed.positions.empty() || parsed.faces.empty())
        throw ConvertError("OBJ contains no usable vertices/faces");
    return parsed;
}

void AddTriangle(MeshPart *part, const std::array<Corner, 3> &tri, const ParsedScene &obj,
                 double scale, bool flipV, bool backSide)
{
    std::array<uint16_t, 3> indices{};
    for (size_t i = 0; i < tri.size(); ++i) {
        const Corner &corner = tri[i];
        const MeshCorner meshCorner{corner, backSide};
        auto it = part->vertexMap.find(meshCorner);
        if (it == part->vertexMap.end()) {
            const auto &p = obj.positions[static_cast<size_t>(corner.vertex)];
            double u = 0.0;
            double v = 0.0;
            if (corner.texcoord >= 0) {
                const auto &uv = obj.texcoords[static_cast<size_t>(corner.texcoord)];
                u = uv[0];
                v = flipV ? (1.0 - uv[1]) : uv[1];
            }
            const size_t newIndex = part->vertices.size();
            if (newIndex >= kMaxBmdlCount)
                throw ConvertError("Internal error: BMDL signed 16-bit vertex overflow");
            part->vertices.push_back({p[0] * scale, p[1] * scale, p[2] * scale, u, v});
            const uint16_t stored = static_cast<uint16_t>(newIndex);
            part->vertexMap.emplace(meshCorner, stored);
            indices[i] = stored;
        }
        else {
            indices[i] = it->second;
        }
    }
    part->triangles.push_back(indices);
}

struct MeshBuildInfo {
    size_t sourceTriangles = 0;
    size_t exactOppositeGroups = 0;
    size_t coverageOppositeGroups = 0;
    size_t explicitOppositeTriangles = 0;
    size_t coverageOppositeTriangles = 0;
    size_t generatedBackTriangles = 0;
    size_t suppressedBackTriangles = 0;
};

std::vector<MeshPart> BuildMeshParts(const ParsedScene &obj, double scale, bool flipV, bool twoSidedFaces,
                                     MeshBuildInfo *buildInfo)
{
    if (!(scale > 0.0) || !std::isfinite(scale))
        throw ConvertError("Scale must be greater than zero");

    using Triangle = std::array<Corner, 3>;
    using QuantizedPoint = std::array<int64_t, 3>;
    using GeometryKey = std::array<QuantizedPoint, 3>;
    using PlaneKey = std::array<int64_t, 4>;

    struct SourceTriangle {
        Triangle corners;
        std::string material;
        bool wantsBackSide = false;
        bool hasExplicitOpposite = false;
        bool hasCoverageOpposite = false;
        GeometryKey geometry{};
        PlaneKey plane{};
        std::array<double, 3> normal{};
    };
    struct MeshTriangle {
        Triangle corners;
        bool backSide = false;
    };
    struct MaterialTriangles {
        std::string material;
        std::vector<MeshTriangle> triangles;
    };

    auto quantizedPoint = [&](int vertex) {
        const auto &p = obj.positions[static_cast<size_t>(vertex)];
        QuantizedPoint out{};
        for (size_t axis = 0; axis < 3; ++axis)
            out[axis] = static_cast<int64_t>(PythonRound(p[axis] * scale * kBmdlScale));
        return out;
    };
    auto geometryKey = [&](const Triangle &tri) {
        GeometryKey key = {quantizedPoint(tri[0].vertex), quantizedPoint(tri[1].vertex), quantizedPoint(tri[2].vertex)};
        std::sort(key.begin(), key.end());
        return key;
    };
    auto triangleNormal = [&](const Triangle &tri) {
        const auto &a = obj.positions[static_cast<size_t>(tri[0].vertex)];
        const auto &b = obj.positions[static_cast<size_t>(tri[1].vertex)];
        const auto &c = obj.positions[static_cast<size_t>(tri[2].vertex)];
        const double abx = b[0] - a[0];
        const double aby = b[1] - a[1];
        const double abz = b[2] - a[2];
        const double acx = c[0] - a[0];
        const double acy = c[1] - a[1];
        const double acz = c[2] - a[2];
        return std::array<double, 3>{aby * acz - abz * acy,
                                     abz * acx - abx * acz,
                                     abx * acy - aby * acx};
    };
    auto planeKey = [&](const Triangle &tri, const std::array<double, 3> &rawNormal) {
        PlaneKey key{};
        const double length = std::sqrt(rawNormal[0] * rawNormal[0] + rawNormal[1] * rawNormal[1] + rawNormal[2] * rawNormal[2]);
        if (!(length > 0.0))
            return key;
        std::array<double, 3> unit = {rawNormal[0] / length, rawNormal[1] / length, rawNormal[2] / length};
        for (double component : unit) {
            if (std::abs(component) > 1.0e-12) {
                if (component < 0.0) {
                    unit[0] = -unit[0];
                    unit[1] = -unit[1];
                    unit[2] = -unit[2];
                }
                break;
            }
        }
        // Normal quantization groups numerically identical authored planes
        // after node transforms.  The distance term is quantized at BMDL's
        // actual coordinate precision, so surfaces that are merely close are
        // never collapsed into one plane.
        key[0] = static_cast<int64_t>(PythonRound(unit[0] * 1000000.0));
        key[1] = static_cast<int64_t>(PythonRound(unit[1] * 1000000.0));
        key[2] = static_cast<int64_t>(PythonRound(unit[2] * 1000000.0));
        const auto &p = obj.positions[static_cast<size_t>(tri[0].vertex)];
        const double distance = (unit[0] * p[0] + unit[1] * p[1] + unit[2] * p[2]) * scale;
        key[3] = static_cast<int64_t>(PythonRound(distance * kBmdlScale));
        return key;
    };
    auto pointInTriangle = [&](const std::array<double, 3> &point, const SourceTriangle &tri) {
        const auto &a = obj.positions[static_cast<size_t>(tri.corners[0].vertex)];
        const auto &b = obj.positions[static_cast<size_t>(tri.corners[1].vertex)];
        const auto &c = obj.positions[static_cast<size_t>(tri.corners[2].vertex)];
        const double v0x = b[0] - a[0];
        const double v0y = b[1] - a[1];
        const double v0z = b[2] - a[2];
        const double v1x = c[0] - a[0];
        const double v1y = c[1] - a[1];
        const double v1z = c[2] - a[2];
        const double v2x = point[0] - a[0];
        const double v2y = point[1] - a[1];
        const double v2z = point[2] - a[2];
        const double d00 = v0x * v0x + v0y * v0y + v0z * v0z;
        const double d01 = v0x * v1x + v0y * v1y + v0z * v1z;
        const double d11 = v1x * v1x + v1y * v1y + v1z * v1z;
        const double d20 = v2x * v0x + v2y * v0y + v2z * v0z;
        const double d21 = v2x * v1x + v2y * v1y + v2z * v1z;
        const double denominator = d00 * d11 - d01 * d01;
        if (std::abs(denominator) <= std::numeric_limits<double>::epsilon())
            return false;
        const double v = (d11 * d20 - d01 * d21) / denominator;
        const double w = (d00 * d21 - d01 * d20) / denominator;
        const double u = 1.0 - v - w;
        constexpr double kInsideEpsilon = 1.0e-7;
        return u >= -kInsideEpsilon && v >= -kInsideEpsilon && w >= -kInsideEpsilon;
    };

    // Authored GLB/FBX assets can contain complete front/back surface pairs
    // whose two sides use different triangulations and different UVs.  The
    // old v5 test only recognized an opposite face when all three quantized
    // vertices were identical.  On the abandoned-building GLB that found 44
    // triangles, but hundreds of coplanar front/back triangles overlap with a
    // different diagonal or subdivision.  Generating a second backface for
    // those triangles creates a same-facing coplanar competitor and produces
    // the moving crosshatch/z-fighting seen in 4DMM.
    //
    // Keep every authored triangle.  We only decide whether a synthetic
    // reverse triangle is necessary.  First detect exact opposite geometry,
    // then conservatively detect full authored coverage on the same BMDL
    // plane using seven interior sample points.  If the opposite-winding
    // authored surface covers all samples, it already provides that side and
    // no synthetic reverse face is emitted.
    std::vector<SourceTriangle> sourceTriangles;
    std::map<GeometryKey, std::vector<size_t>> geometryGroups;
    std::map<PlaneKey, std::vector<size_t>> planeGroups;
    for (const SceneFace &face : obj.faces) {
        for (size_t i = 1; i + 1 < face.corners.size(); ++i) {
            SourceTriangle tri;
            tri.corners = {face.corners[0], face.corners[i], face.corners[i + 1]};
            tri.material = face.material;
            tri.wantsBackSide = twoSidedFaces || face.twoSided;
            tri.geometry = geometryKey(tri.corners);
            tri.normal = triangleNormal(tri.corners);
            tri.plane = planeKey(tri.corners, tri.normal);
            const size_t index = sourceTriangles.size();
            sourceTriangles.push_back(std::move(tri));
            geometryGroups[sourceTriangles.back().geometry].push_back(index);
            planeGroups[sourceTriangles.back().plane].push_back(index);
        }
    }

    size_t exactOppositeGroups = 0;
    for (const auto &entry : geometryGroups) {
        const std::vector<size_t> &indices = entry.second;
        bool groupHasOpposite = false;
        for (size_t i = 0; i < indices.size(); ++i) {
            SourceTriangle &a = sourceTriangles[indices[i]];
            const double a2 = a.normal[0] * a.normal[0] + a.normal[1] * a.normal[1] + a.normal[2] * a.normal[2];
            if (!(a2 > 0.0))
                continue;
            for (size_t j = i + 1; j < indices.size(); ++j) {
                SourceTriangle &b = sourceTriangles[indices[j]];
                const double b2 = b.normal[0] * b.normal[0] + b.normal[1] * b.normal[1] + b.normal[2] * b.normal[2];
                if (!(b2 > 0.0))
                    continue;
                const double dot = a.normal[0] * b.normal[0] + a.normal[1] * b.normal[1] + a.normal[2] * b.normal[2];
                if (dot < 0.0) {
                    a.hasExplicitOpposite = true;
                    b.hasExplicitOpposite = true;
                    groupHasOpposite = true;
                }
            }
        }
        if (groupHasOpposite)
            ++exactOppositeGroups;
    }

    const std::array<std::array<double, 3>, 7> coverageWeights = {{
        {{1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0}},
        {{0.60, 0.20, 0.20}}, {{0.20, 0.60, 0.20}}, {{0.20, 0.20, 0.60}},
        {{0.45, 0.45, 0.10}}, {{0.45, 0.10, 0.45}}, {{0.10, 0.45, 0.45}},
    }};
    size_t coverageOppositeGroups = 0;
    for (const auto &entry : planeGroups) {
        const std::vector<size_t> &indices = entry.second;
        bool groupAddedCoverage = false;
        for (size_t sourceIndex : indices) {
            SourceTriangle &source = sourceTriangles[sourceIndex];
            if (!source.wantsBackSide || source.hasExplicitOpposite)
                continue;
            const double source2 = source.normal[0] * source.normal[0] + source.normal[1] * source.normal[1] +
                                   source.normal[2] * source.normal[2];
            if (!(source2 > 0.0))
                continue;

            std::vector<size_t> opposite;
            opposite.reserve(indices.size());
            for (size_t candidateIndex : indices) {
                if (candidateIndex == sourceIndex)
                    continue;
                const SourceTriangle &candidate = sourceTriangles[candidateIndex];
                const double dot = source.normal[0] * candidate.normal[0] + source.normal[1] * candidate.normal[1] +
                                   source.normal[2] * candidate.normal[2];
                if (dot < 0.0)
                    opposite.push_back(candidateIndex);
            }
            if (opposite.empty())
                continue;

            const auto &a = obj.positions[static_cast<size_t>(source.corners[0].vertex)];
            const auto &b = obj.positions[static_cast<size_t>(source.corners[1].vertex)];
            const auto &c = obj.positions[static_cast<size_t>(source.corners[2].vertex)];
            bool covered = true;
            for (const auto &weight : coverageWeights) {
                const std::array<double, 3> sample = {
                    a[0] * weight[0] + b[0] * weight[1] + c[0] * weight[2],
                    a[1] * weight[0] + b[1] * weight[1] + c[1] * weight[2],
                    a[2] * weight[0] + b[2] * weight[1] + c[2] * weight[2],
                };
                bool sampleCovered = false;
                for (size_t candidateIndex : opposite) {
                    if (pointInTriangle(sample, sourceTriangles[candidateIndex])) {
                        sampleCovered = true;
                        break;
                    }
                }
                if (!sampleCovered) {
                    covered = false;
                    break;
                }
            }
            if (covered) {
                source.hasExplicitOpposite = true;
                source.hasCoverageOpposite = true;
                groupAddedCoverage = true;
            }
        }
        if (groupAddedCoverage)
            ++coverageOppositeGroups;
    }

    const size_t explicitOppositeTriangles = static_cast<size_t>(std::count_if(
        sourceTriangles.begin(), sourceTriangles.end(), [](const SourceTriangle &tri) { return tri.hasExplicitOpposite; }));
    const size_t coverageOppositeTriangles = static_cast<size_t>(std::count_if(
        sourceTriangles.begin(), sourceTriangles.end(), [](const SourceTriangle &tri) { return tri.hasCoverageOpposite; }));

    std::vector<MaterialTriangles> grouped;
    std::unordered_map<std::string, size_t> groupedIndex;
    size_t generatedBackTriangles = 0;
    size_t suppressedBackTriangles = 0;
    for (const SourceTriangle &source : sourceTriangles) {
        auto it = groupedIndex.find(source.material);
        if (it == groupedIndex.end()) {
            const size_t index = grouped.size();
            groupedIndex[source.material] = index;
            grouped.push_back({source.material, {}});
            it = groupedIndex.find(source.material);
        }
        auto &triangles = grouped[it->second].triangles;
        triangles.push_back({source.corners, false});
        if (source.wantsBackSide) {
            if (source.hasExplicitOpposite) {
                ++suppressedBackTriangles;
            }
            else {
                // 3DMM's on-disk MTRL wrapper does not serialize BRender
                // two-sided material flags. Emit a second face with reversed
                // winding only when the source does not already contain an
                // authored opposite-winding triangle on the same plane.
                const Triangle back = {source.corners[0], source.corners[2], source.corners[1]};
                triangles.push_back({back, true});
                ++generatedBackTriangles;
            }
        }
    }

    if (buildInfo != nullptr) {
        buildInfo->sourceTriangles = sourceTriangles.size();
        buildInfo->exactOppositeGroups = exactOppositeGroups;
        buildInfo->coverageOppositeGroups = coverageOppositeGroups;
        buildInfo->explicitOppositeTriangles = explicitOppositeTriangles;
        buildInfo->coverageOppositeTriangles = coverageOppositeTriangles;
        buildInfo->generatedBackTriangles = generatedBackTriangles;
        buildInfo->suppressedBackTriangles = suppressedBackTriangles;
    }

    std::vector<MeshPart> out;
    for (const MaterialTriangles &group : grouped) {
        MeshPart part;
        part.material = group.material;
        for (const MeshTriangle &tri : group.triangles) {
            if (!part.CanAdd(tri.corners, tri.backSide)) {
                if (part.triangles.empty())
                    throw ConvertError("A single triangle for material '" + group.material + "' exceeds BMDL limits");
                out.push_back(std::move(part));
                part = MeshPart{};
                part.material = group.material;
            }
            AddTriangle(&part, tri.corners, obj, scale, flipV, tri.backSide);
        }
        if (!part.triangles.empty())
            out.push_back(std::move(part));
    }
    return out;
}

void PutU16(std::vector<uint8_t> *out, uint16_t value)
{
    out->push_back(static_cast<uint8_t>(value & 0xff));
    out->push_back(static_cast<uint8_t>((value >> 8) & 0xff));
}

void PutI16(std::vector<uint8_t> *out, int16_t value)
{
    PutU16(out, static_cast<uint16_t>(value));
}

void PutU24(std::vector<uint8_t> *out, uint32_t value)
{
    if (value > 0xffffffu)
        throw ConvertError("Chunk payload exceeds 24-bit Chunky limit");
    out->push_back(static_cast<uint8_t>(value & 0xff));
    out->push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out->push_back(static_cast<uint8_t>((value >> 16) & 0xff));
}

void PutU32(std::vector<uint8_t> *out, uint32_t value)
{
    out->push_back(static_cast<uint8_t>(value & 0xff));
    out->push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out->push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    out->push_back(static_cast<uint8_t>((value >> 24) & 0xff));
}

void PutI32(std::vector<uint8_t> *out, int32_t value)
{
    PutU32(out, static_cast<uint32_t>(value));
}

void PutFourCCReversed(std::vector<uint8_t> *out, const std::string &type)
{
    if (type.size() != 4)
        throw ConvertError("Bad Chunky type '" + type + "'");
    out->push_back(static_cast<uint8_t>(type[3]));
    out->push_back(static_cast<uint8_t>(type[2]));
    out->push_back(static_cast<uint8_t>(type[1]));
    out->push_back(static_cast<uint8_t>(type[0]));
}

void PutBytes(std::vector<uint8_t> *out, const void *data, size_t size)
{
    const auto *bytes = static_cast<const uint8_t *>(data);
    out->insert(out->end(), bytes, bytes + size);
}

void PutZeros(std::vector<uint8_t> *out, size_t count)
{
    out->insert(out->end(), count, 0);
}

std::vector<uint8_t> Utf8ToCp1252Replace(const std::string &text)
{
    std::vector<uint8_t> out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        uint32_t cp = 0;
        const uint8_t a = static_cast<uint8_t>(text[i]);
        size_t count = 1;
        if (a < 0x80) {
            cp = a;
        }
        else if ((a & 0xe0) == 0xc0 && i + 1 < text.size()) {
            cp = a & 0x1f;
            count = 2;
        }
        else if ((a & 0xf0) == 0xe0 && i + 2 < text.size()) {
            cp = a & 0x0f;
            count = 3;
        }
        else if ((a & 0xf8) == 0xf0 && i + 3 < text.size()) {
            cp = a & 0x07;
            count = 4;
        }
        else {
            out.push_back('?');
            ++i;
            continue;
        }
        bool valid = true;
        for (size_t j = 1; j < count; ++j) {
            const uint8_t b = static_cast<uint8_t>(text[i + j]);
            if ((b & 0xc0) != 0x80) {
                valid = false;
                break;
            }
            cp = (cp << 6) | (b & 0x3f);
        }
        if (!valid) {
            out.push_back('?');
            ++i;
            continue;
        }
        i += count;

        if (cp <= 0x7f || (cp >= 0xa0 && cp <= 0xff)) {
            out.push_back(static_cast<uint8_t>(cp));
            continue;
        }
        static const std::map<uint32_t, uint8_t> special = {
            {0x20ac, 0x80}, {0x201a, 0x82}, {0x0192, 0x83}, {0x201e, 0x84}, {0x2026, 0x85},
            {0x2020, 0x86}, {0x2021, 0x87}, {0x02c6, 0x88}, {0x2030, 0x89}, {0x0160, 0x8a},
            {0x2039, 0x8b}, {0x0152, 0x8c}, {0x017d, 0x8e}, {0x2018, 0x91}, {0x2019, 0x92},
            {0x201c, 0x93}, {0x201d, 0x94}, {0x2022, 0x95}, {0x2013, 0x96}, {0x2014, 0x97},
            {0x02dc, 0x98}, {0x2122, 0x99}, {0x0161, 0x9a}, {0x203a, 0x9b}, {0x0153, 0x9c},
            {0x017e, 0x9e}, {0x0178, 0x9f},
        };
        auto it = special.find(cp);
        out.push_back(it == special.end() ? static_cast<uint8_t>('?') : it->second);
    }
    return out;
}

std::pair<std::vector<uint8_t>, uint32_t> BuildEntry(const QuadPtr &quad, uint32_t offset,
                                                      const std::vector<QuadPtr> &quads)
{
    std::vector<uint8_t> raw;
    PutFourCCReversed(&raw, quad->type);
    PutU32(&raw, quad->cno);
    PutU32(&raw, offset);
    raw.push_back(quad->mode);
    PutU24(&raw, static_cast<uint32_t>(quad->data.size()));
    uint16_t refsTo = 0;
    for (const QuadPtr &parent : quads) {
        for (const auto &ref : parent->refs) {
            if (ref.second.get() == quad.get())
                ++refsTo;
        }
    }
    if (quad->refs.size() > std::numeric_limits<uint16_t>::max())
        throw ConvertError("Too many Chunky child references");
    PutU16(&raw, static_cast<uint16_t>(quad->refs.size()));
    PutU16(&raw, refsTo);
    for (const auto &ref : quad->refs) {
        PutFourCCReversed(&raw, ref.second->type);
        PutU32(&raw, ref.second->cno);
        PutU32(&raw, ref.first);
    }

    uint32_t logicalLength = static_cast<uint32_t>(raw.size());
    if (quad->hasName) {
        const std::vector<uint8_t> encoded = Utf8ToCp1252Replace(quad->name);
        if (encoded.size() > 255)
            throw ConvertError("Object/resource name exceeds 255 bytes");
        raw.push_back(0x03);
        raw.push_back(0x03);
        raw.push_back(static_cast<uint8_t>(encoded.size()));
        raw.insert(raw.end(), encoded.begin(), encoded.end());
        raw.push_back(0);
        const size_t pad = (4 - (encoded.size() & 3)) & 3;
        PutZeros(&raw, pad);
        logicalLength = static_cast<uint32_t>(raw.size() - pad);
    }
    return {std::move(raw), logicalLength};
}

std::vector<uint8_t> BuildChunky(const std::vector<QuadPtr> &quads)
{
    if (quads.size() > std::numeric_limits<uint32_t>::max())
        throw ConvertError("Too many Chunky resources");
    std::vector<QuadPtr> ordered = quads;
    std::sort(ordered.begin(), ordered.end(), [](const QuadPtr &a, const QuadPtr &b) {
        if (a->type != b->type)
            return a->type < b->type;
        return a->cno < b->cno;
    });

    std::unordered_map<const Quad *, uint32_t> offsets;
    uint64_t offset = 128;
    for (const QuadPtr &quad : ordered) {
        if (quad->data.size() > 0xffffffu)
            throw ConvertError("Chunk payload exceeds 24-bit Chunky limit");
        if (offset > std::numeric_limits<uint32_t>::max())
            throw ConvertError("Chunky file exceeds 32-bit offset limit");
        offsets[quad.get()] = static_cast<uint32_t>(offset);
        offset += quad->data.size();
    }
    if (offset > std::numeric_limits<uint32_t>::max())
        throw ConvertError("Chunky file exceeds 32-bit offset limit");
    const uint32_t indexOffset = static_cast<uint32_t>(offset);

    struct EntryInfo {
        uint32_t entryOffset = 0;
        uint32_t logicalLength = 0;
    };
    std::unordered_map<const Quad *, EntryInfo> entries;
    std::vector<uint8_t> entryBlob;
    for (const QuadPtr &quad : quads) {
        const auto built = BuildEntry(quad, offsets.at(quad.get()), quads);
        if (entryBlob.size() > std::numeric_limits<uint32_t>::max())
            throw ConvertError("Chunky index exceeds 32-bit limit");
        entries[quad.get()] = {static_cast<uint32_t>(entryBlob.size()), built.second};
        entryBlob.insert(entryBlob.end(), built.first.begin(), built.first.end());
    }

    std::vector<uint8_t> index;
    index.push_back(1);
    index.push_back(0);
    index.push_back(3);
    index.push_back(3);
    PutU32(&index, static_cast<uint32_t>(quads.size()));
    PutU32(&index, static_cast<uint32_t>(entryBlob.size()));
    PutI32(&index, -1);
    PutI32(&index, 20);
    index.insert(index.end(), entryBlob.begin(), entryBlob.end());
    for (const QuadPtr &quad : ordered) {
        const EntryInfo &info = entries.at(quad.get());
        PutU32(&index, info.entryOffset);
        PutU32(&index, info.logicalLength);
    }

    const uint64_t total64 = static_cast<uint64_t>(indexOffset) + index.size();
    if (total64 > std::numeric_limits<uint32_t>::max())
        throw ConvertError("Chunky file exceeds 32-bit size limit");
    const uint32_t total = static_cast<uint32_t>(total64);

    std::vector<uint8_t> out;
    out.reserve(total);
    const char ident[8] = {'C', 'H', 'N', '4', 'P', 'M', 'H', 'C'};
    PutBytes(&out, ident, sizeof(ident));
    PutU16(&out, 5);
    PutU16(&out, 4);
    out.push_back(1);
    out.push_back(0);
    out.push_back(3);
    out.push_back(3);
    PutU32(&out, total);
    PutU32(&out, indexOffset);
    PutU32(&out, static_cast<uint32_t>(index.size()));
    PutU32(&out, total);
    PutZeros(&out, 96);
    if (out.size() != 128)
        throw ConvertError("Internal error: CHN4 header size is not 128 bytes");
    for (const QuadPtr &quad : ordered)
        out.insert(out.end(), quad->data.begin(), quad->data.end());
    out.insert(out.end(), index.begin(), index.end());
    return out;
}

std::vector<uint8_t> BmdlBytes(const MeshPart &part)
{
    if (part.vertices.size() > kMaxBmdlCount || part.triangles.size() > kMaxBmdlCount)
        throw ConvertError("Internal error: unsplit BMDL exceeds signed 16-bit count");
    std::vector<uint8_t> out;
    out.reserve(48 + part.vertices.size() * 32 + part.triangles.size() * 32);
    out.push_back(1);
    out.push_back(0);
    out.push_back(3);
    out.push_back(3);
    PutU16(&out, static_cast<uint16_t>(part.vertices.size()));
    PutU16(&out, static_cast<uint16_t>(part.triangles.size()));
    PutZeros(&out, 40);
    for (const MeshVertex &vertex : part.vertices) {
        const double values[5] = {vertex.x * kBmdlScale, vertex.y * kBmdlScale, vertex.z * kBmdlScale,
                                  vertex.u * kUvScale, vertex.v * kUvScale};
        for (double value : values) {
            const long long rounded = PythonRound(value);
            if (rounded < std::numeric_limits<int32_t>::min() || rounded > std::numeric_limits<int32_t>::max())
                throw ConvertError("Scaled model coordinate/UV exceeds BMDL int32 range; reduce Scale");
            PutI32(&out, static_cast<int32_t>(rounded));
        }
        PutZeros(&out, 12);
    }
    for (const auto &triangle : part.triangles) {
        PutU16(&out, triangle[0]);
        PutU16(&out, triangle[1]);
        PutU16(&out, triangle[2]);
        PutZeros(&out, 10);
        out.push_back(1);
        PutZeros(&out, 15);
    }
    return out;
}

QuadPtr MakeQuad(const std::string &type, uint32_t cno, uint8_t mode, std::vector<uint8_t> data,
                 const std::string &name = {}, bool hasName = false)
{
    auto quad = std::make_shared<Quad>();
    quad->type = type;
    quad->cno = cno;
    quad->mode = mode;
    quad->data = std::move(data);
    quad->name = name;
    quad->hasName = hasName;
    return quad;
}

std::vector<uint8_t> Hex(const char *text)
{
    std::vector<uint8_t> out;
    int high = -1;
    for (const char *p = text; *p; ++p) {
        char ch = *p;
        int value = -1;
        if (ch >= '0' && ch <= '9')
            value = ch - '0';
        else if (ch >= 'a' && ch <= 'f')
            value = 10 + ch - 'a';
        else if (ch >= 'A' && ch <= 'F')
            value = 10 + ch - 'A';
        else
            continue;
        if (high < 0)
            high = value;
        else {
            out.push_back(static_cast<uint8_t>((high << 4) | value));
            high = -1;
        }
    }
    if (high >= 0)
        throw ConvertError("Internal error: odd hex string");
    return out;
}

std::vector<uint8_t> Build4Cn(const std::string &name, bool isProp, const std::vector<MeshPart> &parts,
                              const std::map<std::string, std::string> &texturePaths, uint32_t baseId)
{
    const size_t n = parts.size();
    if (n == 0)
        throw ConvertError("No mesh sections were produced");
    if (n > 32766)
        throw ConvertError("Too many BMDL sections for the legacy BODY hierarchy");

    std::vector<uint8_t> tmplData = {1, 0, 3, 3, 0, 0, 0, 0x40, 0, 0, 0x14, 0, 0, 0, 0, 0};
    tmplData[12] = isProp ? 4 : 0;
    auto tmpl = MakeQuad("TMPL", baseId, 2, std::move(tmplData), name, true);
    auto actn = MakeQuad("ACTN", baseId, 0, Hex("010003030A000000"), "At Rest", true);

    const uint32_t length = static_cast<uint32_t>(12 + n * 4);
    std::vector<uint8_t> ggclData;
    ggclData.push_back(1); ggclData.push_back(0); ggclData.push_back(3); ggclData.push_back(3);
    PutU32(&ggclData, 1); PutU32(&ggclData, length); PutI32(&ggclData, -1);
    PutU32(&ggclData, 8); PutU32(&ggclData, 0); PutU32(&ggclData, 163840);
    PutU16(&ggclData, 0); PutU16(&ggclData, 0);
    for (size_t i = 0; i < n; ++i) {
        PutU16(&ggclData, static_cast<uint16_t>(i + 1));
        PutU16(&ggclData, 0);
    }
    PutU32(&ggclData, 0); PutU32(&ggclData, length);
    auto ggcl = MakeQuad("GGCL", baseId, 0, std::move(ggclData));

    std::vector<uint8_t> glxfData = Hex("010003033000000001000000FCFF0000");
    PutZeros(&glxfData, 12);
    const std::vector<uint8_t> axis = Hex("FCFF0000");
    glxfData.insert(glxfData.end(), axis.begin(), axis.end()); PutZeros(&glxfData, 12);
    glxfData.insert(glxfData.end(), axis.begin(), axis.end()); PutZeros(&glxfData, 12);
    auto glxf = MakeQuad("GLXF", baseId, 0, std::move(glxfData));

    std::vector<uint8_t> blankData = {1, 0, 3, 3};
    PutZeros(&blankData, 44);
    auto blankBmdl = MakeQuad("BMDL", baseId, 0, std::move(blankData));

    std::vector<uint8_t> cmtlData = {1, 0, 3, 3};
    PutU32(&cmtlData, 0);
    auto cmtl = MakeQuad("CMTL", baseId, 0, std::move(cmtlData));
    const std::vector<uint8_t> mtrlData = Hex("01000303000000000000ffff0000170700003200");
    auto firstMtrl = MakeQuad("MTRL", baseId, 0, mtrlData);
    cmtl->Ref(firstMtrl, 0);

    auto ggcm = MakeQuad("GGCM", baseId, 0, Hex("010003030100000008000000ffffffff0400000001000000000000000000000008000000"));
    std::vector<uint8_t> glbsData = {1, 0, 3, 3};
    PutU32(&glbsData, 2); PutU32(&glbsData, static_cast<uint32_t>(n + 1));
    PutZeros(&glbsData, (n + 1) * 2);
    auto glbs = MakeQuad("GLBS", baseId, 0, std::move(glbsData));

    std::vector<uint8_t> glpiData = {1, 0, 3, 3};
    PutU32(&glpiData, 2); PutU32(&glpiData, static_cast<uint32_t>(n + 1)); PutI16(&glpiData, -1);
    for (size_t i = 0; i < n; ++i)
        PutI16(&glpiData, static_cast<int16_t>(i));
    auto glpi = MakeQuad("GLPI", baseId, 0, std::move(glpiData));

    std::map<std::string, QuadPtr> t24ByMaterial;
    std::vector<QuadPtr> t24Order;
    std::vector<QuadPtr> mtrls;
    std::vector<QuadPtr> bmdls;
    for (size_t i = 0; i < parts.size(); ++i) {
        const MeshPart &part = parts[i];
        auto pathIt = texturePaths.find(part.material);
        if (pathIt == texturePaths.end())
            throw ConvertError("Internal error: missing texture path for material '" + part.material + "'");
        QuadPtr t24m;
        auto t24It = t24ByMaterial.find(part.material);
        if (t24It == t24ByMaterial.end()) {
            std::vector<uint8_t> pathData(pathIt->second.begin(), pathIt->second.end());
            pathData.push_back(0);
            t24m = MakeQuad("T24M", baseId + 0x10000u + static_cast<uint32_t>(t24ByMaterial.size()), 0,
                            std::move(pathData));
            t24ByMaterial[part.material] = t24m;
            t24Order.push_back(t24m);
        }
        else {
            t24m = t24It->second;
        }
        const uint32_t cno = baseId + static_cast<uint32_t>(i + 1);
        auto mtrl = MakeQuad("MTRL", cno, 0, mtrlData);
        mtrl->Ref(t24m, 0);
        auto bmdl = MakeQuad("BMDL", cno, 0, BmdlBytes(part));
        mtrls.push_back(mtrl);
        bmdls.push_back(bmdl);
        cmtl->Ref(mtrl, static_cast<uint32_t>(i + 1));
        tmpl->Ref(bmdl, static_cast<uint32_t>(i + 1));
    }

    tmpl->Ref(actn, 0);
    actn->Ref(ggcl, 0);
    actn->Ref(glxf, 0);
    tmpl->Ref(blankBmdl, 0);
    tmpl->Ref(cmtl, 0);
    tmpl->Ref(ggcm, 0);
    tmpl->Ref(glbs, 0);
    tmpl->Ref(glpi, 0);

    std::vector<QuadPtr> quads = {tmpl, actn, ggcl, glxf, blankBmdl, cmtl, firstMtrl};
    quads.insert(quads.end(), mtrls.begin(), mtrls.end());
    quads.insert(quads.end(), t24Order.begin(), t24Order.end());
    quads.push_back(ggcm);
    quads.push_back(glbs);
    quads.push_back(glpi);
    quads.insert(quads.end(), bmdls.begin(), bmdls.end());
    return BuildChunky(quads);
}

std::vector<uint8_t> Build4Th()
{
    return BuildChunky({});
}

std::string Sanitize(const std::string &text)
{
    std::string out;
    bool lastUnderscore = false;
    for (char ch : Trim(text)) {
        const bool allowed = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                             (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-';
        if (allowed) {
            out.push_back(ch);
            lastUnderscore = false;
        }
        else if (!lastUnderscore) {
            out.push_back('_');
            lastUnderscore = true;
        }
    }
    while (!out.empty() && (out.front() == '.' || out.front() == '_'))
        out.erase(out.begin());
    while (!out.empty() && (out.back() == '.' || out.back() == '_'))
        out.pop_back();
    return out.empty() ? "asset" : out;
}

uint32_t Adler32(const uint8_t *data, size_t size)
{
    constexpr uint32_t mod = 65521;
    uint32_t a = 1;
    uint32_t b = 0;
    for (size_t i = 0; i < size; ++i) {
        a = (a + data[i]) % mod;
        b = (b + a) % mod;
    }
    return (b << 16) | a;
}

void PutBe32(std::vector<uint8_t> *out, uint32_t value)
{
    out->push_back(static_cast<uint8_t>((value >> 24) & 0xff));
    out->push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    out->push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out->push_back(static_cast<uint8_t>(value & 0xff));
}

void PutPngChunk(std::vector<uint8_t> *out, const char type[4], const std::vector<uint8_t> &payload)
{
    PutBe32(out, static_cast<uint32_t>(payload.size()));
    const size_t crcStart = out->size();
    PutBytes(out, type, 4);
    if (!payload.empty())
        PutBytes(out, payload.data(), payload.size());
    const uint32_t crc = Crc32(out->data() + crcStart, 4 + payload.size());
    PutBe32(out, crc);
}

std::vector<uint8_t> SolidPng(const std::array<int, 3> &rgb)
{
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::vector<uint8_t> ihdr;
    PutBe32(&ihdr, 1); PutBe32(&ihdr, 1);
    ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    PutPngChunk(&png, "IHDR", ihdr);

    const uint8_t raw[4] = {0, static_cast<uint8_t>(rgb[0]), static_cast<uint8_t>(rgb[1]), static_cast<uint8_t>(rgb[2])};
    std::vector<uint8_t> zlib;
    zlib.push_back(0x78); zlib.push_back(0x01);
    zlib.push_back(0x01); // final uncompressed DEFLATE block
    PutU16(&zlib, sizeof(raw));
    PutU16(&zlib, static_cast<uint16_t>(~static_cast<uint16_t>(sizeof(raw))));
    zlib.insert(zlib.end(), raw, raw + sizeof(raw));
    PutBe32(&zlib, Adler32(raw, sizeof(raw)));
    PutPngChunk(&png, "IDAT", zlib);
    PutPngChunk(&png, "IEND", {});
    return png;
}

bool ReadFile(const std::filesystem::path &path, std::vector<uint8_t> *data)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0)
        return false;
    in.seekg(0, std::ios::beg);
    data->resize(static_cast<size_t>(size));
    if (size > 0)
        in.read(reinterpret_cast<char *>(data->data()), size);
    return in.good() || in.eof();
}

bool IsPng(const std::vector<uint8_t> &data)
{
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    return data.size() >= sizeof(signature) && std::memcmp(data.data(), signature, sizeof(signature)) == 0;
}

std::string FileStemUtf8(const std::filesystem::path &path)
{
#if defined(_WIN32)
    return PathForLog(path.stem());
#else
    return path.stem().string();
#endif
}

std::pair<std::vector<uint8_t>, std::string> TextureBytes(const ConvertOptions &options, const Material &material)
{
    if (material.hasImageBytes) {
        const std::filesystem::path sourceName = std::filesystem::u8path(
            material.imageName.empty() ? (material.name + ".image") : material.imageName);
        const std::string preferred = Sanitize(material.name) + "_" + Sanitize(FileStemUtf8(sourceName)) + ".png";
        if (IsPng(material.imageBytes))
            return {material.imageBytes, preferred};
        if (!options.imageMemoryConverter)
            throw ConvertError("Embedded texture for material '" + material.name +
                               "' is not PNG and no native memory image converter is available");
        std::vector<uint8_t> converted;
        std::string convertError;
        if (!options.imageMemoryConverter(material.imageBytes, &converted, &convertError) || !IsPng(converted)) {
            if (convertError.empty())
                convertError = "native memory image conversion failed";
            throw ConvertError("Could not convert embedded texture for material '" + material.name + "': " + convertError);
        }
        return {std::move(converted), preferred};
    }

    if (!material.hasMapKd)
        return {SolidPng(material.kd), Sanitize(material.name) + "_solid.png"};

    std::error_code ec;
    if (!std::filesystem::exists(material.mapKd, ec) || ec)
        throw ConvertError("Texture for material '" + material.name + "' not found: " + PathForLog(material.mapKd));
    std::vector<uint8_t> data;
    if (!ReadFile(material.mapKd, &data))
        throw ConvertError("Could not read texture: " + PathForLog(material.mapKd));

    const std::string suffix = LowerAscii(PathForLog(material.mapKd.extension()));
    const std::string preferred = Sanitize(material.name) + "_" + Sanitize(FileStemUtf8(material.mapKd)) + ".png";
    if (suffix == ".png" && IsPng(data))
        return {std::move(data), preferred};

    if (!options.imageConverter)
        throw ConvertError("Texture '" + PathForLog(material.mapKd) + "' is not PNG and no native image converter is available");
    std::vector<uint8_t> converted;
    std::string convertError;
    if (!options.imageConverter(material.mapKd, &converted, &convertError) || !IsPng(converted)) {
        if (convertError.empty())
            convertError = "native image conversion failed";
        throw ConvertError("Could not convert texture " + PathForLog(material.mapKd) + ": " + convertError);
    }
    return {std::move(converted), preferred};
}

struct CenterResult {
    std::array<double, 3> minimum{};
    std::array<double, 3> maximum{};
    std::array<double, 3> offset{};
};

CenterResult CenterBottom(ParsedScene *scene)
{
    if (scene == nullptr || scene->faces.empty())
        throw ConvertError("Scene contains no faces to center");
    std::vector<uint8_t> used(scene->positions.size(), 0);
    bool havePoint = false;
    std::array<double, 3> minimum = {0.0, 0.0, 0.0};
    std::array<double, 3> maximum = {0.0, 0.0, 0.0};
    for (const SceneFace &face : scene->faces) {
        for (const Corner &corner : face.corners) {
            if (corner.vertex < 0 || static_cast<size_t>(corner.vertex) >= scene->positions.size())
                throw ConvertError("Scene face references an invalid vertex");
            const size_t index = static_cast<size_t>(corner.vertex);
            if (used[index])
                continue;
            used[index] = 1;
            const auto &p = scene->positions[index];
            if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]))
                throw ConvertError("Scene contains a non-finite vertex");
            if (!havePoint) {
                minimum = maximum = p;
                havePoint = true;
            }
            else {
                for (size_t axis = 0; axis < 3; ++axis) {
                    minimum[axis] = std::min(minimum[axis], p[axis]);
                    maximum[axis] = std::max(maximum[axis], p[axis]);
                }
            }
        }
    }
    if (!havePoint)
        throw ConvertError("Scene contains no referenced vertices");
    const std::array<double, 3> offset = {
        (minimum[0] + maximum[0]) * 0.5,
        minimum[1],
        (minimum[2] + maximum[2]) * 0.5,
    };
    for (size_t i = 0; i < scene->positions.size(); ++i) {
        if (!used[i])
            continue;
        scene->positions[i][0] -= offset[0];
        scene->positions[i][1] -= offset[1];
        scene->positions[i][2] -= offset[2];
    }
    return {minimum, maximum, offset};
}

ParsedScene LoadScene(const std::filesystem::path &path)
{
    const std::string ext = LowerAscii(PathForLog(path.extension()));
    if (ext == ".obj")
        return ParseObj(path);

    ParsedScene scene;
    std::string loadError;
    if (ext == ".glb") {
        if (!LoadGltfScene(path, &scene, &loadError))
            throw ConvertError(loadError.empty() ? "GLB load failed" : loadError);
        return scene;
    }
    if (ext == ".fbx" || ext == ".zip") {
        if (!LoadFbxScene(path, &scene, &loadError))
            throw ConvertError(loadError.empty() ? "FBX load failed" : loadError);
        return scene;
    }
    throw ConvertError("Unsupported model input type '" + ext + "'. Use OBJ, GLB, FBX, or an FBX ZIP bundle");
}

uint32_t MakeId()
{
    std::random_device random;
    const uint32_t bits = (static_cast<uint32_t>(random()) ^ (static_cast<uint32_t>(random()) << 16)) & 0x3fffffffu;
    return 0x40000000u | bits;
}

std::string StatsLine(const Stats &stats)
{
    std::ostringstream ss;
    ss << "stats format=" << stats.sourceFormat
       << " source_vertices=" << stats.sourceVertices << " source_faces=" << stats.sourceFaces
       << " bmdl_parts=" << stats.bmdlParts << " bmdl_vertices=" << stats.bmdlVertices
       << " triangles=" << stats.triangles << " textures=" << stats.textures
       << " fourcn_bytes=" << stats.fourcnBytes;
    return ss.str();
}

} // namespace

bool Convert(const ConvertOptions &options, Stats *statsOut, std::string *error)
{
    try {
        if (options.inputPath.empty() || options.outputPath.empty() || Trim(options.name).empty())
            throw ConvertError("Model input, output VXP2, and Name are required");
        Log(options, "convert begin version=" + std::string(kVersion) + " input=" + PathForLog(options.inputPath) +
                     " output=" + PathForLog(options.outputPath) + " kind=" +
                     (options.kind == AssetKind::Prop ? "prop" : "actor") + " scale=" + std::to_string(options.scale) +
                     " texture_v_mode=" + TextureVModeName(options.textureVMode) +
                     " two_sided=" + (options.twoSidedFaces ? "1" : "0") +
                     " center_bottom=" + (options.centerBottom ? "1" : "0"));

        const std::filesystem::path inputPath = Resolve(std::filesystem::current_path(), options.inputPath);
        ParsedScene parsed = LoadScene(inputPath);
        Log(options, "parse ok format=" + parsed.format + " positions=" + std::to_string(parsed.positions.size()) +
                     " texcoords=" + std::to_string(parsed.texcoords.size()) +
                     " faces=" + std::to_string(parsed.faces.size()) +
                     " materials=" + std::to_string(parsed.materials.size()));
        const bool effectiveFlipV = EffectiveFlipV(options.textureVMode, parsed.format);
        Log(options, "texture_v mode=" + std::string(TextureVModeName(options.textureVMode)) +
                     " format=" + parsed.format + " effective_flip=" + (effectiveFlipV ? "1" : "0"));

        if (options.centerBottom) {
            const CenterResult centered = CenterBottom(&parsed);
            std::ostringstream line;
            line << std::setprecision(12)
                 << "center_bottom bounds_min=" << centered.minimum[0] << ',' << centered.minimum[1] << ',' << centered.minimum[2]
                 << " bounds_max=" << centered.maximum[0] << ',' << centered.maximum[1] << ',' << centered.maximum[2]
                 << " subtract=" << centered.offset[0] << ',' << centered.offset[1] << ',' << centered.offset[2];
            Log(options, line.str());
        }

        MeshBuildInfo meshBuildInfo;
        const std::vector<MeshPart> parts = BuildMeshParts(parsed, options.scale, effectiveFlipV,
                                                           options.twoSidedFaces, &meshBuildInfo);
        Log(options, "two_sided resolve source_triangles=" + std::to_string(meshBuildInfo.sourceTriangles) +
                     " exact_opposite_groups=" + std::to_string(meshBuildInfo.exactOppositeGroups) +
                     " coverage_opposite_groups=" + std::to_string(meshBuildInfo.coverageOppositeGroups) +
                     " explicit_opposite_triangles=" + std::to_string(meshBuildInfo.explicitOppositeTriangles) +
                     " coverage_opposite_triangles=" + std::to_string(meshBuildInfo.coverageOppositeTriangles) +
                     " generated_backfaces=" + std::to_string(meshBuildInfo.generatedBackTriangles) +
                     " suppressed_backfaces=" + std::to_string(meshBuildInfo.suppressedBackTriangles));
        size_t meshVertices = 0;
        size_t triangles = 0;
        for (size_t partIndex = 0; partIndex < parts.size(); ++partIndex) {
            const MeshPart &part = parts[partIndex];
            meshVertices += part.vertices.size();
            triangles += part.triangles.size();
            Log(options, "mesh part index=" + std::to_string(partIndex) + " material=" + part.material +
                         " vertices=" + std::to_string(part.vertices.size()) +
                         " triangles=" + std::to_string(part.triangles.size()));
        }
        Log(options, "mesh ok parts=" + std::to_string(parts.size()) + " vertices=" + std::to_string(meshVertices) +
                     " triangles=" + std::to_string(triangles));

        std::vector<std::string> usedMaterials;
        for (const MeshPart &part : parts) {
            if (std::find(usedMaterials.begin(), usedMaterials.end(), part.material) == usedMaterials.end())
                usedMaterials.push_back(part.material);
        }

        std::map<std::string, std::vector<uint8_t>> texturePayloads;
        std::map<std::string, std::string> texturePaths;
        std::vector<std::pair<std::vector<uint8_t>, std::string>> payloadDedup;
        std::vector<std::string> usedNamesLower;
        for (const std::string &materialName : usedMaterials) {
            Material fallback{materialName};
            auto materialIt = parsed.materials.find(materialName);
            const Material &material = materialIt == parsed.materials.end() ? fallback : materialIt->second;
            auto loaded = TextureBytes(options, material);

            std::string reused;
            for (const auto &existing : payloadDedup) {
                if (existing.first == loaded.first) {
                    reused = existing.second;
                    break;
                }
            }
            if (!reused.empty()) {
                texturePaths[materialName] = reused;
                Log(options, "texture reuse material=" + materialName + " path=" + reused);
                continue;
            }

            const std::filesystem::path preferredPath(loaded.second);
            const std::string stem = Sanitize(FileStemUtf8(preferredPath));
            std::string candidate = stem + ".png";
            int suffix = 2;
            for (;;) {
                const std::string lower = LowerAscii(candidate);
                if (std::find(usedNamesLower.begin(), usedNamesLower.end(), lower) == usedNamesLower.end()) {
                    usedNamesLower.push_back(lower);
                    break;
                }
                candidate = stem + "_" + std::to_string(suffix++) + ".png";
            }
            const std::string rel = "textures/" + candidate;
            texturePaths[materialName] = rel;
            texturePayloads[rel] = loaded.first;
            payloadDedup.push_back({std::move(loaded.first), rel});
            Log(options, "texture add material=" + materialName + " path=" + rel +
                         " bytes=" + std::to_string(texturePayloads[rel].size()));
        }

        const uint32_t baseId = MakeId();
        const std::vector<uint8_t> fourcn = Build4Cn(Trim(options.name), options.kind == AssetKind::Prop,
                                                     parts, texturePaths, baseId);
        const std::vector<uint8_t> fourth = Build4Th();
        Log(options, "chunky ok base_id=" + std::to_string(baseId) + " fourcn_bytes=" + std::to_string(fourcn.size()) +
                     " fourth_bytes=" + std::to_string(fourth.size()));

        std::string stem = Sanitize(FileStemUtf8(options.outputPath));
        if (stem.empty())
            stem = "asset";
        ZipStoreWriter zip;
        zip.Add(stem + ".4cn", fourcn);
        zip.Add(stem + ".4th", fourth);
        for (const auto &texture : texturePayloads)
            zip.Add(texture.first, texture.second);
        std::string zipError;
        if (!zip.Write(options.outputPath, &zipError))
            throw ConvertError(zipError);

        Stats stats;
        stats.sourceVertices = parsed.positions.size();
        stats.sourceFaces = parsed.faces.size();
        stats.sourceFormat = parsed.format;
        stats.bmdlParts = parts.size();
        stats.bmdlVertices = meshVertices;
        stats.triangles = triangles;
        stats.textures = texturePayloads.size();
        stats.fourcnBytes = fourcn.size();
        if (statsOut)
            *statsOut = stats;
        Log(options, "convert success " + StatsLine(stats));
        return true;
    }
    catch (const std::exception &ex) {
        if (error)
            *error = ex.what();
        Log(options, std::string("convert fail error=") + ex.what());
        return false;
    }
}

} // namespace obj2vxp2
