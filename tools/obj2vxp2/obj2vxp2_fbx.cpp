#include "obj2vxp2_scene.h"
#include "zip_read.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace obj2vxp2 {
namespace {

struct Prop {
    enum class Kind { None, Int, Double, String, Raw, Int32Array, Int64Array, DoubleArray } kind = Kind::None;
    int64_t i = 0;
    double d = 0.0;
    std::string s;
    std::vector<uint8_t> raw;
    std::vector<int32_t> i32;
    std::vector<int64_t> i64;
    std::vector<double> doubles;
};

struct Node {
    std::string name;
    std::vector<Prop> props;
    std::vector<std::unique_ptr<Node>> children;
};

struct Reader {
    const std::vector<uint8_t> &data;
    size_t pos = 0;
    uint32_t version = 0;

    bool Need(size_t count) const { return count <= data.size() - std::min(pos, data.size()); }
    uint8_t U8() { if (!Need(1)) throw std::runtime_error("Unexpected end of FBX"); return data[pos++]; }
    uint16_t U16() { if (!Need(2)) throw std::runtime_error("Unexpected end of FBX"); uint16_t v = static_cast<uint16_t>(data[pos]) | static_cast<uint16_t>(data[pos + 1] << 8); pos += 2; return v; }
    uint32_t U32() { if (!Need(4)) throw std::runtime_error("Unexpected end of FBX"); uint32_t v = static_cast<uint32_t>(data[pos]) | (static_cast<uint32_t>(data[pos + 1]) << 8) | (static_cast<uint32_t>(data[pos + 2]) << 16) | (static_cast<uint32_t>(data[pos + 3]) << 24); pos += 4; return v; }
    uint64_t U64() { uint64_t lo = U32(); uint64_t hi = U32(); return lo | (hi << 32); }
    int16_t I16() { return static_cast<int16_t>(U16()); }
    int32_t I32() { return static_cast<int32_t>(U32()); }
    int64_t I64() { return static_cast<int64_t>(U64()); }
    float F32() { uint32_t v = U32(); float f; std::memcpy(&f, &v, sizeof(f)); return f; }
    double F64() { uint64_t v = U64(); double d; std::memcpy(&d, &v, sizeof(d)); return d; }
    std::string String(size_t count) { if (!Need(count)) throw std::runtime_error("Unexpected end of FBX string"); std::string s(reinterpret_cast<const char *>(data.data() + pos), count); pos += count; return s; }
    std::vector<uint8_t> Bytes(size_t count) { if (!Need(count)) throw std::runtime_error("Unexpected end of FBX blob"); std::vector<uint8_t> v(data.begin() + static_cast<std::ptrdiff_t>(pos), data.begin() + static_cast<std::ptrdiff_t>(pos + count)); pos += count; return v; }
};

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

std::string CleanName(std::string value)
{
    const size_t zero = value.find('\0');
    if (zero != std::string::npos)
        value.resize(zero);
    return value;
}

std::string LowerAscii(std::string value)
{
    for (char &c : value) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return value;
}

std::string NormalizedKey(const std::string &value)
{
    std::string out;
    for (char c : LowerAscii(value)) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            out.push_back(c);
    }
    return out;
}

std::string BaseName(std::string value)
{
    std::replace(value.begin(), value.end(), '\\', '/');
    const size_t slash = value.find_last_of('/');
    return slash == std::string::npos ? value : value.substr(slash + 1);
}

Prop ReadProp(Reader *r)
{
    Prop p;
    const char type = static_cast<char>(r->U8());
    switch (type) {
    case 'Y': p.kind = Prop::Kind::Int; p.i = r->I16(); break;
    case 'C': p.kind = Prop::Kind::Int; p.i = r->U8() != 0; break;
    case 'I': p.kind = Prop::Kind::Int; p.i = r->I32(); break;
    case 'L': p.kind = Prop::Kind::Int; p.i = r->I64(); break;
    case 'F': p.kind = Prop::Kind::Double; p.d = r->F32(); break;
    case 'D': p.kind = Prop::Kind::Double; p.d = r->F64(); break;
    case 'S': {
        p.kind = Prop::Kind::String;
        const uint32_t length = r->U32();
        p.s = r->String(length);
        break;
    }
    case 'R': {
        p.kind = Prop::Kind::Raw;
        const uint32_t length = r->U32();
        p.raw = r->Bytes(length);
        break;
    }
    case 'i':
    case 'l':
    case 'f':
    case 'd':
    case 'b':
    case 'c': {
        const uint32_t count = r->U32();
        const uint32_t encoding = r->U32();
        const uint32_t compressedLength = r->U32();
        const size_t elementSize = (type == 'i' || type == 'f') ? 4u :
                                   (type == 'l' || type == 'd') ? 8u : 1u;
        if (count > std::numeric_limits<size_t>::max() / elementSize)
            throw std::runtime_error("FBX array is too large");
        const size_t rawSize = static_cast<size_t>(count) * elementSize;
        std::vector<uint8_t> raw;
        if (encoding == 0) {
            if (compressedLength != rawSize)
                throw std::runtime_error("FBX uncompressed array length mismatch");
            raw = r->Bytes(compressedLength);
        }
        else if (encoding == 1) {
            const std::vector<uint8_t> compressed = r->Bytes(compressedLength);
            raw.resize(rawSize);
            if (!InflateZlib(compressed.data(), compressed.size(), raw.data(), raw.size()))
                throw std::runtime_error("FBX zlib array decode failed");
        }
        else {
            throw std::runtime_error("Unsupported FBX array encoding");
        }
        if (type == 'i') {
            p.kind = Prop::Kind::Int32Array;
            p.i32.resize(count);
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t v = static_cast<uint32_t>(raw[i * 4]) |
                             (static_cast<uint32_t>(raw[i * 4 + 1]) << 8) |
                             (static_cast<uint32_t>(raw[i * 4 + 2]) << 16) |
                             (static_cast<uint32_t>(raw[i * 4 + 3]) << 24);
                p.i32[i] = static_cast<int32_t>(v);
            }
        }
        else if (type == 'l') {
            p.kind = Prop::Kind::Int64Array;
            p.i64.resize(count);
            for (uint32_t i = 0; i < count; ++i) {
                uint64_t v = 0;
                for (int byte = 0; byte < 8; ++byte)
                    v |= static_cast<uint64_t>(raw[i * 8 + static_cast<size_t>(byte)]) << (byte * 8);
                p.i64[i] = static_cast<int64_t>(v);
            }
        }
        else if (type == 'f' || type == 'd') {
            p.kind = Prop::Kind::DoubleArray;
            p.doubles.resize(count);
            for (uint32_t i = 0; i < count; ++i) {
                if (type == 'f') {
                    uint32_t bits = static_cast<uint32_t>(raw[i * 4]) |
                                    (static_cast<uint32_t>(raw[i * 4 + 1]) << 8) |
                                    (static_cast<uint32_t>(raw[i * 4 + 2]) << 16) |
                                    (static_cast<uint32_t>(raw[i * 4 + 3]) << 24);
                    float f;
                    std::memcpy(&f, &bits, sizeof(f));
                    p.doubles[i] = f;
                }
                else {
                    uint64_t bits = 0;
                    for (int byte = 0; byte < 8; ++byte)
                        bits |= static_cast<uint64_t>(raw[i * 8 + static_cast<size_t>(byte)]) << (byte * 8);
                    double d;
                    std::memcpy(&d, &bits, sizeof(d));
                    p.doubles[i] = d;
                }
            }
        }
        else {
            p.kind = Prop::Kind::Raw;
            p.raw = std::move(raw);
        }
        break;
    }
    default:
        throw std::runtime_error(std::string("Unsupported FBX property type '") + type + "'");
    }
    return p;
}

std::unique_ptr<Node> ReadNode(Reader *r)
{
    const bool wide = r->version >= 7500;
    const size_t nullSize = wide ? 25u : 13u;
    if (!r->Need(nullSize))
        return nullptr;
    const uint64_t endOffset = wide ? r->U64() : r->U32();
    const uint64_t propertyCount = wide ? r->U64() : r->U32();
    const uint64_t propertyLength = wide ? r->U64() : r->U32();
    const uint8_t nameLength = r->U8();
    if (endOffset == 0 && propertyCount == 0 && propertyLength == 0 && nameLength == 0)
        return nullptr;
    if (endOffset > r->data.size() || endOffset < r->pos)
        throw std::runtime_error("FBX node end offset is invalid");
    auto node = std::make_unique<Node>();
    node->name = r->String(nameLength);
    const size_t propertyStart = r->pos;
    if (propertyCount > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
        throw std::runtime_error("FBX property count is too large");
    node->props.reserve(static_cast<size_t>(propertyCount));
    for (uint64_t i = 0; i < propertyCount; ++i)
        node->props.push_back(ReadProp(r));
    if (r->pos - propertyStart != propertyLength)
        throw std::runtime_error("FBX property list length mismatch in node " + node->name);

    while (r->pos + nullSize <= endOffset) {
        bool allZero = true;
        for (size_t i = 0; i < nullSize; ++i) {
            if (r->data[r->pos + i] != 0) {
                allZero = false;
                break;
            }
        }
        if (allZero) {
            r->pos += nullSize;
            break;
        }
        std::unique_ptr<Node> child = ReadNode(r);
        if (!child)
            break;
        node->children.push_back(std::move(child));
    }
    r->pos = static_cast<size_t>(endOffset);
    return node;
}

bool ParseBinaryFbx(const std::vector<uint8_t> &bytes, std::vector<std::unique_ptr<Node>> *roots,
                    uint32_t *version, std::string *error)
{
    static const uint8_t signature[] = {'K','a','y','d','a','r','a',' ','F','B','X',' ','B','i','n','a','r','y',' ',' ',0,0x1a,0};
    if (bytes.size() < 27 || std::memcmp(bytes.data(), signature, sizeof(signature)) != 0) {
        if (error)
            *error = "Only binary FBX files are supported by this milestone";
        return false;
    }
    try {
        Reader r{bytes};
        r.pos = 23;
        r.version = r.U32();
        if (r.version < 6100 || r.version > 7700)
            throw std::runtime_error("Unsupported FBX version " + std::to_string(r.version));
        while (r.pos < bytes.size()) {
            std::unique_ptr<Node> node = ReadNode(&r);
            if (!node)
                break;
            roots->push_back(std::move(node));
        }
        *version = r.version;
        return true;
    }
    catch (const std::exception &ex) {
        if (error)
            *error = ex.what();
        return false;
    }
}

const Node *Child(const Node *node, const char *name)
{
    if (node == nullptr)
        return nullptr;
    for (const auto &child : node->children) {
        if (child->name == name)
            return child.get();
    }
    return nullptr;
}

std::vector<const Node *> Children(const Node *node, const char *name)
{
    std::vector<const Node *> out;
    if (node == nullptr)
        return out;
    for (const auto &child : node->children) {
        if (child->name == name)
            out.push_back(child.get());
    }
    return out;
}

const Prop *P(const Node *node, size_t index)
{
    return node != nullptr && index < node->props.size() ? &node->props[index] : nullptr;
}

int64_t IntProp(const Node *node, size_t index, int64_t fallback = 0)
{
    const Prop *p = P(node, index);
    if (p == nullptr)
        return fallback;
    if (p->kind == Prop::Kind::Int)
        return p->i;
    if (p->kind == Prop::Kind::Double)
        return static_cast<int64_t>(p->d);
    return fallback;
}

std::string StringProp(const Node *node, size_t index)
{
    const Prop *p = P(node, index);
    return p != nullptr && p->kind == Prop::Kind::String ? p->s : std::string();
}

const std::vector<double> *DoubleArray(const Node *node)
{
    const Prop *p = P(node, 0);
    return p != nullptr && p->kind == Prop::Kind::DoubleArray ? &p->doubles : nullptr;
}

const std::vector<int32_t> *Int32Array(const Node *node)
{
    const Prop *p = P(node, 0);
    return p != nullptr && p->kind == Prop::Kind::Int32Array ? &p->i32 : nullptr;
}

const Node *Property70(const Node *object, const std::string &name)
{
    const Node *p70 = Child(object, "Properties70");
    if (p70 == nullptr)
        return nullptr;
    for (const auto &p : p70->children) {
        if (p->name == "P" && StringProp(p.get(), 0) == name)
            return p.get();
    }
    return nullptr;
}

double NumericValue(const Prop *p, double fallback)
{
    if (p == nullptr)
        return fallback;
    if (p->kind == Prop::Kind::Double)
        return p->d;
    if (p->kind == Prop::Kind::Int)
        return static_cast<double>(p->i);
    return fallback;
}

std::array<double, 3> VecProperty(const Node *object, const std::string &name,
                                  std::array<double, 3> fallback)
{
    const Node *p = Property70(object, name);
    if (p == nullptr || p->props.size() < 7)
        return fallback;
    return {NumericValue(P(p, 4), fallback[0]), NumericValue(P(p, 5), fallback[1]), NumericValue(P(p, 6), fallback[2])};
}

double ScalarProperty(const Node *object, const std::string &name, double fallback)
{
    const Node *p = Property70(object, name);
    return p != nullptr && p->props.size() >= 5 ? NumericValue(P(p, 4), fallback) : fallback;
}

struct Mat4 {
    double m[4][4]{};
};

Mat4 Identity()
{
    Mat4 r{};
    for (int i = 0; i < 4; ++i)
        r.m[i][i] = 1.0;
    return r;
}

Mat4 Mul(const Mat4 &a, const Mat4 &b)
{
    Mat4 r{};
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            for (int k = 0; k < 4; ++k)
                r.m[row][col] += a.m[row][k] * b.m[k][col];
    return r;
}

Mat4 Translation(const std::array<double, 3> &v)
{
    Mat4 r = Identity();
    r.m[0][3] = v[0]; r.m[1][3] = v[1]; r.m[2][3] = v[2];
    return r;
}

Mat4 Scale(const std::array<double, 3> &v)
{
    Mat4 r = Identity();
    r.m[0][0] = v[0]; r.m[1][1] = v[1]; r.m[2][2] = v[2];
    return r;
}

Mat4 RotateX(double deg)
{
    const double a = deg * 3.14159265358979323846 / 180.0;
    const double c = std::cos(a), s = std::sin(a);
    Mat4 r = Identity();
    r.m[1][1] = c; r.m[1][2] = -s; r.m[2][1] = s; r.m[2][2] = c;
    return r;
}

Mat4 RotateY(double deg)
{
    const double a = deg * 3.14159265358979323846 / 180.0;
    const double c = std::cos(a), s = std::sin(a);
    Mat4 r = Identity();
    r.m[0][0] = c; r.m[0][2] = s; r.m[2][0] = -s; r.m[2][2] = c;
    return r;
}

Mat4 RotateZ(double deg)
{
    const double a = deg * 3.14159265358979323846 / 180.0;
    const double c = std::cos(a), s = std::sin(a);
    Mat4 r = Identity();
    r.m[0][0] = c; r.m[0][1] = -s; r.m[1][0] = s; r.m[1][1] = c;
    return r;
}

Mat4 Euler(const std::array<double, 3> &r, int order)
{
    const Mat4 rx = RotateX(r[0]), ry = RotateY(r[1]), rz = RotateZ(r[2]);
    switch (order) {
    case 1: return Mul(Mul(rx, rz), ry); // XZY
    case 2: return Mul(Mul(ry, rz), rx); // YZX
    case 3: return Mul(Mul(ry, rx), rz); // YXZ
    case 4: return Mul(Mul(rz, rx), ry); // ZXY
    case 5: return Mul(Mul(rz, ry), rx); // ZYX
    default: return Mul(Mul(rx, ry), rz); // XYZ
    }
}

Mat4 LocalTransform(const Node *model)
{
    const auto t = VecProperty(model, "Lcl Translation", {0.0, 0.0, 0.0});
    const auto r = VecProperty(model, "Lcl Rotation", {0.0, 0.0, 0.0});
    const auto s = VecProperty(model, "Lcl Scaling", {1.0, 1.0, 1.0});
    const int order = static_cast<int>(ScalarProperty(model, "RotationOrder", 0.0));
    return Mul(Mul(Translation(t), Euler(r, order)), Scale(s));
}

Mat4 GeometricTransform(const Node *model)
{
    const auto t = VecProperty(model, "GeometricTranslation", {0.0, 0.0, 0.0});
    const auto r = VecProperty(model, "GeometricRotation", {0.0, 0.0, 0.0});
    const auto s = VecProperty(model, "GeometricScaling", {1.0, 1.0, 1.0});
    return Mul(Mul(Translation(t), Euler(r, 0)), Scale(s));
}

std::array<double, 3> TransformPoint(const Mat4 &m, const std::array<double, 3> &p)
{
    return {
        m.m[0][0] * p[0] + m.m[0][1] * p[1] + m.m[0][2] * p[2] + m.m[0][3],
        m.m[1][0] * p[0] + m.m[1][1] * p[1] + m.m[1][2] * p[2] + m.m[1][3],
        m.m[2][0] * p[0] + m.m[2][1] * p[1] + m.m[2][2] * p[2] + m.m[2][3],
    };
}

double Det3(const Mat4 &m)
{
    return m.m[0][0] * (m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1]) -
           m.m[0][1] * (m.m[1][0] * m.m[2][2] - m.m[1][2] * m.m[2][0]) +
           m.m[0][2] * (m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0]);
}

struct Connection {
    std::string type;
    int64_t child = 0;
    int64_t parent = 0;
    std::string property;
};

std::vector<Connection> Connections(const Node *connections)
{
    std::vector<Connection> out;
    if (connections == nullptr)
        return out;
    for (const auto &c : connections->children) {
        if (c->name != "C" || c->props.size() < 3)
            continue;
        Connection connection;
        connection.type = StringProp(c.get(), 0);
        connection.child = IntProp(c.get(), 1);
        connection.parent = IntProp(c.get(), 2);
        if (c->props.size() >= 4)
            connection.property = StringProp(c.get(), 3);
        out.push_back(std::move(connection));
    }
    return out;
}

std::string ChildString(const Node *node, const char *childName)
{
    const Node *child = Child(node, childName);
    return child != nullptr ? StringProp(child, 0) : std::string();
}

const std::vector<uint8_t> *ChildRaw(const Node *node, const char *childName)
{
    const Node *child = Child(node, childName);
    const Prop *p = P(child, 0);
    return p != nullptr && p->kind == Prop::Kind::Raw ? &p->raw : nullptr;
}

const std::vector<uint8_t> *FindArchiveByBaseName(const std::map<std::string, std::vector<uint8_t>> &archive,
                                                  const std::string &name, std::string *matchedName)
{
    const std::string target = LowerAscii(BaseName(name));
    for (const auto &entry : archive) {
        if (LowerAscii(BaseName(entry.first)) == target) {
            if (matchedName)
                *matchedName = BaseName(entry.first);
            return &entry.second;
        }
    }
    return nullptr;
}

const std::vector<uint8_t> *FindArchiveByMaterial(const std::map<std::string, std::vector<uint8_t>> &archive,
                                                  const std::string &materialName, std::string *matchedName)
{
    const std::string target = NormalizedKey(materialName);
    if (target.empty())
        return nullptr;
    for (const auto &entry : archive) {
        const std::string base = BaseName(entry.first);
        const size_t dot = base.find_last_of('.');
        const std::string stem = dot == std::string::npos ? base : base.substr(0, dot);
        if (NormalizedKey(stem) == target) {
            if (matchedName)
                *matchedName = base;
            return &entry.second;
        }
    }
    return nullptr;
}

int LayerIndex(const Node *layer, size_t polygonIndex, size_t polygonVertexIndex, size_t controlPointIndex,
               size_t directCount, const std::vector<int32_t> *indices)
{
    if (layer == nullptr)
        return -1;
    const std::string mapping = ChildString(layer, "MappingInformationType");
    const std::string reference = ChildString(layer, "ReferenceInformationType");
    size_t mapped = 0;
    if (mapping == "ByPolygonVertex") mapped = polygonVertexIndex;
    else if (mapping == "ByVertice" || mapping == "ByVertex") mapped = controlPointIndex;
    else if (mapping == "ByPolygon") mapped = polygonIndex;
    else if (mapping == "AllSame") mapped = 0;
    else return -1;
    if (reference == "IndexToDirect" || reference == "Index") {
        if (indices == nullptr || mapped >= indices->size())
            return -1;
        const int32_t index = (*indices)[mapped];
        return index >= 0 && static_cast<size_t>(index) < directCount ? index : -1;
    }
    return mapped < directCount ? static_cast<int>(mapped) : -1;
}

int MaterialSlot(const Node *layer, size_t polygonIndex, size_t polygonVertexIndex, size_t controlPointIndex)
{
    if (layer == nullptr)
        return 0;
    const Node *materialsNode = Child(layer, "Materials");
    const std::vector<int32_t> *materials = Int32Array(materialsNode);
    if (materials == nullptr || materials->empty())
        return 0;
    const int index = LayerIndex(layer, polygonIndex, polygonVertexIndex, controlPointIndex,
                                 materials->size(), nullptr);
    if (index >= 0 && static_cast<size_t>(index) < materials->size())
        return (*materials)[static_cast<size_t>(index)];
    const std::string mapping = ChildString(layer, "MappingInformationType");
    size_t mapped = mapping == "ByPolygon" ? polygonIndex : 0;
    if (mapped < materials->size())
        return (*materials)[mapped];
    return 0;
}

std::array<int, 3> DiffuseColor(const Node *material)
{
    const Node *p = Property70(material, "DiffuseColor");
    if (p == nullptr || p->props.size() < 7)
        return {255, 255, 255};
    std::array<int, 3> out{};
    for (int i = 0; i < 3; ++i) {
        const double v = std::max(0.0, std::min(1.0, NumericValue(P(p, static_cast<size_t>(4 + i)), 1.0)));
        out[static_cast<size_t>(i)] = static_cast<int>(std::lround(v * 255.0));
    }
    return out;
}

} // namespace

bool LoadFbxScene(const std::filesystem::path &path, ParsedScene *scene, std::string *error)
{
    if (scene == nullptr)
        return false;
    *scene = ParsedScene{};
    try {
        std::vector<uint8_t> fbxBytes;
        std::map<std::string, std::vector<uint8_t>> archive;
        std::string fbxEntry;
        const std::string ext = LowerAscii(path.extension().string());
        if (ext == ".zip") {
            if (!ReadZipEntries(path, &archive, error))
                return false;
            for (const auto &entry : archive) {
                const std::string entryExt = LowerAscii(std::filesystem::path(entry.first).extension().string());
                if (entryExt == ".fbx") {
                    fbxEntry = entry.first;
                    fbxBytes = entry.second;
                    break;
                }
            }
            if (fbxBytes.empty()) {
                if (error)
                    *error = "FBX ZIP does not contain a .fbx file";
                return false;
            }
        }
        else if (!ReadAll(path, &fbxBytes)) {
            if (error)
                *error = "Could not read FBX: " + path.string();
            return false;
        }

        std::vector<std::unique_ptr<Node>> roots;
        uint32_t version = 0;
        if (!ParseBinaryFbx(fbxBytes, &roots, &version, error))
            return false;
        (void)version;

        const Node *objects = nullptr;
        const Node *connectionsNode = nullptr;
        const Node *global = nullptr;
        for (const auto &root : roots) {
            if (root->name == "Objects") objects = root.get();
            else if (root->name == "Connections") connectionsNode = root.get();
            else if (root->name == "GlobalSettings") global = root.get();
        }
        if (objects == nullptr || connectionsNode == nullptr) {
            if (error)
                *error = "FBX is missing Objects or Connections";
            return false;
        }

        std::unordered_map<int64_t, const Node *> objectById;
        for (const auto &object : objects->children) {
            if (!object->props.empty())
                objectById[IntProp(object.get(), 0)] = object.get();
        }
        const std::vector<Connection> connections = Connections(connectionsNode);

        std::unordered_map<int64_t, int64_t> modelParent;
        std::unordered_map<int64_t, std::vector<int64_t>> modelMaterials;
        std::unordered_map<int64_t, std::vector<int64_t>> modelGeometry;
        std::unordered_map<int64_t, std::vector<std::pair<int64_t, std::string>>> materialTextures;
        std::unordered_map<int64_t, std::vector<int64_t>> textureVideos;
        for (const Connection &c : connections) {
            auto childIt = objectById.find(c.child);
            auto parentIt = objectById.find(c.parent);
            const Node *childObject = childIt == objectById.end() ? nullptr : childIt->second;
            const Node *parentObject = parentIt == objectById.end() ? nullptr : parentIt->second;
            if (c.type == "OO") {
                if (childObject != nullptr && parentObject != nullptr && childObject->name == "Model" && parentObject->name == "Model")
                    modelParent[c.child] = c.parent;
                else if (childObject != nullptr && parentObject != nullptr && childObject->name == "Geometry" && parentObject->name == "Model")
                    modelGeometry[c.parent].push_back(c.child);
                else if (childObject != nullptr && parentObject != nullptr && childObject->name == "Material" && parentObject->name == "Model")
                    modelMaterials[c.parent].push_back(c.child);
                else if (childObject != nullptr && parentObject != nullptr && childObject->name == "Video" && parentObject->name == "Texture")
                    textureVideos[c.parent].push_back(c.child);
            }
            else if (c.type == "OP" && childObject != nullptr && parentObject != nullptr &&
                     childObject->name == "Texture" && parentObject->name == "Material") {
                materialTextures[c.parent].push_back({c.child, c.property});
            }
        }

        std::unordered_map<int64_t, Mat4> worldCache;
        std::set<int64_t> worldStack;
        std::function<Mat4(int64_t)> worldFor = [&](int64_t modelId) -> Mat4 {
            auto cached = worldCache.find(modelId);
            if (cached != worldCache.end())
                return cached->second;
            if (!worldStack.insert(modelId).second)
                throw std::runtime_error("FBX model hierarchy contains a cycle");
            const Node *model = objectById[modelId];
            Mat4 world = LocalTransform(model);
            auto parent = modelParent.find(modelId);
            if (parent != modelParent.end())
                world = Mul(worldFor(parent->second), world);
            worldStack.erase(modelId);
            worldCache[modelId] = world;
            return world;
        };

        const int upAxis = static_cast<int>(ScalarProperty(global, "UpAxis", 1.0));
        const int upSign = static_cast<int>(ScalarProperty(global, "UpAxisSign", 1.0));
        const int coordAxis = static_cast<int>(ScalarProperty(global, "CoordAxis", 0.0));
        const int coordSign = static_cast<int>(ScalarProperty(global, "CoordAxisSign", 1.0));
        const int frontAxis = static_cast<int>(ScalarProperty(global, "FrontAxis", 2.0));
        const int frontSign = static_cast<int>(ScalarProperty(global, "FrontAxisSign", 1.0));
        const double unitScaleMeters = ScalarProperty(global, "UnitScaleFactor", 1.0) / 100.0;
        auto canonical = [&](const std::array<double, 3> &p) {
            const double source[3] = {p[0], p[1], p[2]};
            const int xAxis = coordAxis >= 0 && coordAxis <= 2 ? coordAxis : 0;
            const int yAxis = upAxis >= 0 && upAxis <= 2 ? upAxis : 1;
            const int zAxis = frontAxis >= 0 && frontAxis <= 2 ? frontAxis : 2;
            return std::array<double, 3>{source[xAxis] * coordSign * unitScaleMeters,
                                         source[yAxis] * upSign * unitScaleMeters,
                                         source[zAxis] * frontSign * unitScaleMeters};
        };

        std::map<int64_t, std::string> materialNames;
        std::map<std::string, int> materialNameCounts;
        for (const auto &object : objects->children) {
            if (object->name != "Material")
                continue;
            const int64_t id = IntProp(object.get(), 0);
            std::string base = CleanName(StringProp(object.get(), 1));
            if (base.empty())
                base = "material_" + std::to_string(id);
            int &count = materialNameCounts[base];
            const std::string name = count == 0 ? base : base + "_" + std::to_string(count + 1);
            ++count;
            materialNames[id] = name;
            Material material{name};
            material.kd = DiffuseColor(object.get());
            material.twoSided = ScalarProperty(object.get(), "DoubleSided", 0.0) != 0.0 ||
                                ScalarProperty(object.get(), "TwoSided", 0.0) != 0.0;

            auto texList = materialTextures.find(id);
            if (texList != materialTextures.end()) {
                for (const auto &link : texList->second) {
                    const std::string propLower = LowerAscii(link.second);
                    if (!propLower.empty() && propLower.find("diffuse") == std::string::npos &&
                        propLower.find("basecolor") == std::string::npos && propLower.find("base_color") == std::string::npos)
                        continue;
                    const Node *texture = objectById[link.first];
                    if (texture == nullptr)
                        continue;
                    std::string textureFile = ChildString(texture, "RelativeFilename");
                    if (textureFile.empty()) textureFile = ChildString(texture, "FileName");
                    auto videos = textureVideos.find(link.first);
                    if (videos != textureVideos.end()) {
                        for (int64_t videoId : videos->second) {
                            const Node *video = objectById[videoId];
                            if (video == nullptr) continue;
                            const std::vector<uint8_t> *content = ChildRaw(video, "Content");
                            if (content != nullptr && !content->empty()) {
                                material.imageBytes = *content;
                                material.imageName = BaseName(ChildString(video, "RelativeFilename"));
                                if (material.imageName.empty()) material.imageName = BaseName(ChildString(video, "Filename"));
                                if (material.imageName.empty()) material.imageName = BaseName(textureFile);
                                material.hasImageBytes = true;
                                break;
                            }
                        }
                    }
                    if (!material.hasImageBytes && !archive.empty() && !textureFile.empty()) {
                        std::string matched;
                        if (const auto *bytes = FindArchiveByBaseName(archive, textureFile, &matched)) {
                            material.imageBytes = *bytes;
                            material.imageName = matched;
                            material.hasImageBytes = true;
                        }
                    }
                    if (!material.hasImageBytes && archive.empty() && !textureFile.empty()) {
                        const std::string baseFile = BaseName(textureFile);
                        std::vector<std::filesystem::path> candidates = {
                            path.parent_path() / std::filesystem::u8path(textureFile),
                            path.parent_path() / std::filesystem::u8path(baseFile),
                            path.parent_path().parent_path() / "textures" / std::filesystem::u8path(baseFile),
                        };
                        for (const auto &candidate : candidates) {
                            std::error_code ec;
                            if (std::filesystem::exists(candidate, ec) && !ec) {
                                material.mapKd = candidate;
                                material.hasMapKd = true;
                                break;
                            }
                        }
                    }
                    if (material.hasImageBytes || material.hasMapKd)
                        break;
                }
            }
            if (!material.hasImageBytes && !material.hasMapKd && !archive.empty()) {
                std::string matched;
                if (const auto *bytes = FindArchiveByMaterial(archive, name, &matched)) {
                    material.imageBytes = *bytes;
                    material.imageName = matched;
                    material.hasImageBytes = true;
                }
            }
            scene->materials[name] = std::move(material);
        }

        for (const auto &modelEntry : modelGeometry) {
            const int64_t modelId = modelEntry.first;
            const Node *model = objectById[modelId];
            if (model == nullptr)
                continue;
            const Mat4 transform = Mul(worldFor(modelId), GeometricTransform(model));
            const bool mirrored = Det3(transform) * static_cast<double>(coordSign * upSign * frontSign) < 0.0;
            const auto materialListIt = modelMaterials.find(modelId);
            const std::vector<int64_t> emptyMaterials;
            const std::vector<int64_t> &modelMats = materialListIt == modelMaterials.end() ? emptyMaterials : materialListIt->second;

            for (int64_t geometryId : modelEntry.second) {
                const Node *geometry = objectById[geometryId];
                if (geometry == nullptr)
                    continue;
                const std::vector<double> *vertices = DoubleArray(Child(geometry, "Vertices"));
                const std::vector<int32_t> *polygonIndices = Int32Array(Child(geometry, "PolygonVertexIndex"));
                if (vertices == nullptr || polygonIndices == nullptr || vertices->size() % 3 != 0)
                    continue;
                const Node *uvLayer = nullptr;
                const auto uvLayers = Children(geometry, "LayerElementUV");
                if (!uvLayers.empty()) uvLayer = uvLayers.front();
                const std::vector<double> *uvs = uvLayer != nullptr ? DoubleArray(Child(uvLayer, "UV")) : nullptr;
                const std::vector<int32_t> *uvIndices = uvLayer != nullptr ? Int32Array(Child(uvLayer, "UVIndex")) : nullptr;
                const Node *materialLayer = nullptr;
                const auto materialLayers = Children(geometry, "LayerElementMaterial");
                if (!materialLayers.empty()) materialLayer = materialLayers.front();

                const size_t vertexBase = scene->positions.size();
                const size_t controlPointCount = vertices->size() / 3;
                scene->positions.reserve(scene->positions.size() + controlPointCount);
                for (size_t i = 0; i < controlPointCount; ++i) {
                    const std::array<double, 3> p = {(*vertices)[i * 3], (*vertices)[i * 3 + 1], (*vertices)[i * 3 + 2]};
                    scene->positions.push_back(canonical(TransformPoint(transform, p)));
                }

                size_t polygonStart = 0;
                size_t polygonIndex = 0;
                for (size_t i = 0; i < polygonIndices->size(); ++i) {
                    const int32_t encoded = (*polygonIndices)[i];
                    if (encoded >= 0)
                        continue;
                    const size_t polygonEnd = i;
                    if (polygonEnd >= polygonStart) {
                        const size_t firstCp = static_cast<size_t>((*polygonIndices)[polygonStart] >= 0 ? (*polygonIndices)[polygonStart] : -(*polygonIndices)[polygonStart] - 1);
                        int slot = MaterialSlot(materialLayer, polygonIndex, polygonStart, firstCp);
                        std::string materialName = "default";
                        if (slot >= 0 && static_cast<size_t>(slot) < modelMats.size()) {
                            auto mn = materialNames.find(modelMats[static_cast<size_t>(slot)]);
                            if (mn != materialNames.end()) materialName = mn->second;
                        }
                        else if (!modelMats.empty()) {
                            auto mn = materialNames.find(modelMats.front());
                            if (mn != materialNames.end()) materialName = mn->second;
                        }
                        if (scene->materials.find(materialName) == scene->materials.end())
                            scene->materials.emplace(materialName, Material{materialName});
                        SceneFace face;
                        face.material = materialName;
                        face.twoSided = scene->materials[materialName].twoSided;
                        for (size_t pv = polygonStart; pv <= polygonEnd; ++pv) {
                            const int32_t value = (*polygonIndices)[pv];
                            const int64_t cp64 = value < 0 ? -static_cast<int64_t>(value) - 1 : value;
                            if (cp64 < 0 || static_cast<size_t>(cp64) >= controlPointCount)
                                throw std::runtime_error("FBX polygon vertex index is out of range");
                            const size_t sceneVertex = vertexBase + static_cast<size_t>(cp64);
                            int texcoord = -1;
                            if (uvLayer != nullptr && uvs != nullptr && uvs->size() % 2 == 0) {
                                const int uvIndex = LayerIndex(uvLayer, polygonIndex, pv, static_cast<size_t>(cp64), uvs->size() / 2, uvIndices);
                                if (uvIndex >= 0) {
                                    scene->texcoords.push_back({(*uvs)[static_cast<size_t>(uvIndex) * 2],
                                                                (*uvs)[static_cast<size_t>(uvIndex) * 2 + 1]});
                                    texcoord = static_cast<int>(scene->texcoords.size() - 1);
                                }
                            }
                            if (sceneVertex > static_cast<size_t>(std::numeric_limits<int>::max()))
                                throw std::runtime_error("FBX scene exceeds converter vertex index range");
                            face.corners.push_back({static_cast<int>(sceneVertex), texcoord});
                        }
                        if (mirrored)
                            std::reverse(face.corners.begin(), face.corners.end());
                        if (face.corners.size() >= 3)
                            scene->faces.push_back(std::move(face));
                    }
                    polygonStart = i + 1;
                    ++polygonIndex;
                }
            }
        }

        if (scene->faces.empty()) {
            if (error)
                *error = "FBX contains no mesh polygons";
            return false;
        }
        scene->format = path.extension() == ".zip" ? "fbx-zip" : "fbx";
        return true;
    }
    catch (const std::exception &ex) {
        if (error)
            *error = ex.what();
        return false;
    }
}

} // namespace obj2vxp2
