#include "plugins/io_obj_rapidobj/rapidobjimportplugin.h"

#include "meshioplugin.h"
#include "meshiopluginmanager.h"

#include <vcg/complex/allocate.h>
#include <wrap/io_trimesh/io_mask.h>

#include <QFileInfo>
#include <QObject>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_set>

#include <rapidobj/rapidobj.hpp>

namespace {
constexpr int kErrParse = -1;
constexpr int kErrTriangulate = -2;
constexpr int kErrInvalidData = -3;

struct VertexKey {
    int positionIndex = -1;
    int normalIndex = -1;

    bool operator==(const VertexKey &other) const
    {
        return positionIndex == other.positionIndex && normalIndex == other.normalIndex;
    }
};

struct VertexKeyHash {
    size_t operator()(const VertexKey &key) const noexcept
    {
        const size_t a = std::hash<int>{}(key.positionIndex);
        const size_t b = std::hash<int>{}(key.normalIndex);
        return a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2));
    }
};

bool hasTriplet(const rapidobj::Array<float> &data, int index)
{
    if (index < 0)
        return false;
    const size_t base = size_t(index) * 3;
    return base + 2 < data.size();
}

bool hasPair(const rapidobj::Array<float> &data, int index)
{
    if (index < 0)
        return false;
    const size_t base = size_t(index) * 2;
    return base + 1 < data.size();
}

void appendTexture(std::unordered_set<std::string> &seen, VCGMesh &mesh, const std::string &textureName)
{
    if (textureName.empty())
        return;
    if (seen.insert(textureName).second)
        mesh.textures.push_back(textureName);
}
}

class RapidObjImportPlugin final : public MeshIOPlugin
{
public:
    QString name() const override
    {
        return QObject::tr("rapidobj OBJ Importer");
    }

    bool canLoad(const QString &filename) const override
    {
        return QFileInfo(filename).suffix().compare(QStringLiteral("obj"), Qt::CaseInsensitive) == 0;
    }

    int load(const QString &filename, VCGMesh &mesh, vcg::CallBackPos *cb, int *outLoadMask) const override
    {
        if (outLoadMask)
            *outLoadMask = 0;

        mesh.Clear();

        rapidobj::Result result = rapidobj::ParseFile(
            filename.toStdString(),
            rapidobj::MaterialLibrary::Default(rapidobj::Load::Optional));
        if (result.error)
            return kErrParse;

        if (!rapidobj::Triangulate(result))
            return kErrTriangulate;

        const auto &positions = result.attributes.positions;
        const auto &normals = result.attributes.normals;
        const auto &texcoords = result.attributes.texcoords;
        const auto &colors = result.attributes.colors;
        if (positions.empty())
            return kErrInvalidData;

        bool importedNormals = false;
        bool importedTexcoords = false;
        bool importedColors = false;

        const auto toColorByte = [](float v) -> uint8_t {
            const float clamped = std::clamp(v, 0.0f, 1.0f);
            return uint8_t(std::lround(clamped * 255.0f));
        };

        std::unordered_map<VertexKey, int, VertexKeyHash> vertexCache;
        auto getOrCreateVertex = [&](const rapidobj::Index &idx) -> int {
            if (!hasTriplet(positions, idx.position_index))
                return -1;

            const int normalIndex = hasTriplet(normals, idx.normal_index) ? idx.normal_index : -1;
            const VertexKey key { idx.position_index, normalIndex };
            const auto existing = vertexCache.find(key);
            if (existing != vertexCache.end())
                return existing->second;

            const size_t pBase = size_t(idx.position_index) * 3;
            auto vi = vcg::tri::Allocator<VCGMesh>::AddVertex(
                mesh,
                VCGMesh::CoordType(
                    positions[pBase + 0],
                    positions[pBase + 1],
                    positions[pBase + 2]));
            VCGVertex *v = &(*vi);
            const int vertexIndex = mesh.VN() - 1;

            if (normalIndex >= 0) {
                const size_t nBase = size_t(normalIndex) * 3;
                v->N() = VCGMesh::CoordType(
                    normals[nBase + 0],
                    normals[nBase + 1],
                    normals[nBase + 2]);
                importedNormals = true;
            }

            if (hasTriplet(colors, idx.position_index)) {
                const size_t cBase = size_t(idx.position_index) * 3;
                v->C() = vcg::Color4b(
                    toColorByte(colors[cBase + 0]),
                    toColorByte(colors[cBase + 1]),
                    toColorByte(colors[cBase + 2]),
                    255);
                importedColors = true;
            }

            vertexCache.emplace(key, vertexIndex);
            return vertexIndex;
        };

        for (size_t shapeIndex = 0; shapeIndex < result.shapes.size(); ++shapeIndex) {
            const rapidobj::Shape &shape = result.shapes[shapeIndex];
            size_t indexOffset = 0;

            for (size_t faceIndex = 0; faceIndex < shape.mesh.num_face_vertices.size(); ++faceIndex) {
                const uint8_t vertexCount = shape.mesh.num_face_vertices[faceIndex];
                if (indexOffset + size_t(vertexCount) > shape.mesh.indices.size())
                    break;

                if (vertexCount != 3) {
                    indexOffset += size_t(vertexCount);
                    continue;
                }

                rapidobj::Index cornerIndices[3];
                bool faceValid = true;
                for (int c = 0; c < 3; ++c) {
                    cornerIndices[c] = shape.mesh.indices[indexOffset + size_t(c)];
                    if (!hasTriplet(positions, cornerIndices[c].position_index)) {
                        faceValid = false;
                        break;
                    }
                }
                indexOffset += 3;

                if (!faceValid)
                    continue;

                int vertexIndices[3] = { -1, -1, -1 };
                for (int c = 0; c < 3; ++c) {
                    vertexIndices[c] = getOrCreateVertex(cornerIndices[c]);
                }

                if (vertexIndices[0] < 0 || vertexIndices[1] < 0 || vertexIndices[2] < 0)
                    continue;

                auto fi = vcg::tri::Allocator<VCGMesh>::AddFace(
                    mesh,
                    size_t(vertexIndices[0]),
                    size_t(vertexIndices[1]),
                    size_t(vertexIndices[2]));
                for (int c = 0; c < 3; ++c) {
                    const rapidobj::Index idx = cornerIndices[c];
                    if (!hasPair(texcoords, idx.texcoord_index))
                        continue;

                    const size_t tBase = size_t(idx.texcoord_index) * 2;
                    fi->WT(c).U() = texcoords[tBase + 0];
                    fi->WT(c).V() = texcoords[tBase + 1];
                    fi->WT(c).N() = 0;
                    importedTexcoords = true;
                }
            }

            for (size_t i = 0; i < shape.points.indices.size(); ++i) {
                const rapidobj::Index idx = shape.points.indices[i];
                getOrCreateVertex(idx);
            }

            if (cb) {
                const int progress = result.shapes.empty()
                    ? 100
                    : int((shapeIndex + 1) * 100 / result.shapes.size());
                cb(progress, "Loading OBJ (rapidobj)\r");
            }
        }

        std::unordered_set<std::string> seenTextures;
        for (const auto &material : result.materials) {
            appendTexture(seenTextures, mesh, material.diffuse_texname);
            appendTexture(seenTextures, mesh, material.normal_texname);
            appendTexture(seenTextures, mesh, material.bump_texname);
            appendTexture(seenTextures, mesh, material.specular_texname);
            appendTexture(seenTextures, mesh, material.emissive_texname);
            appendTexture(seenTextures, mesh, material.ambient_texname);
        }

        if (mesh.VN() == 0)
            return kErrInvalidData;

        int loadMask = 0;
        if (importedNormals)
            loadMask |= vcg::tri::io::Mask::IOM_VERTNORMAL;
        if (importedTexcoords)
            loadMask |= vcg::tri::io::Mask::IOM_WEDGTEXCOORD;
        if (importedColors)
            loadMask |= vcg::tri::io::Mask::IOM_VERTCOLOR;

        if (outLoadMask)
            *outLoadMask = loadMask;

        return 0;
    }

    QString filterString() const override
    {
        return QObject::tr("OBJ Files (*.obj)");
    }

    QString errorString(int errCode) const override
    {
        switch (errCode) {
        case kErrParse:
            return QObject::tr("Cannot parse OBJ file with rapidobj.");
        case kErrTriangulate:
            return QObject::tr("Failed to triangulate OBJ faces with rapidobj.");
        case kErrInvalidData:
            return QObject::tr("OBJ file has no valid vertex data.");
        default:
            return QObject::tr("Unknown rapidobj import error.");
        }
    }
};

void registerRapidObjImportPlugin(MeshIOPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<RapidObjImportPlugin>());
}
