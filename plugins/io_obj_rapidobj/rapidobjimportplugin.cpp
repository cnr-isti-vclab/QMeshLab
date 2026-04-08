#include "plugins/io_obj_rapidobj/rapidobjimportplugin.h"

#include "meshioplugin.h"
#include "meshiopluginmanager.h"

#include <vcg/complex/allocate.h>
#include <wrap/io_trimesh/io_mask.h>

#include <QFileInfo>
#include <QObject>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
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

QString compactSourceLine(const std::string &line)
{
    QString s = QString::fromStdString(line).trimmed();
    if (s.size() > 160)
        s = s.left(157) + QStringLiteral("...");
    return s;
}

QString formatRapidObjErrorDetails(const rapidobj::Error &error)
{
    if (!error)
        return QObject::tr("No additional rapidobj diagnostics are available.");

    QStringList detail;
    if (error.code)
        detail << QObject::tr("code: %1").arg(QString::fromStdString(error.code.message()));
    if (error.line_num > 0)
        detail << QObject::tr("line: %1").arg(error.line_num);

    const QString sourceLine = compactSourceLine(error.line);
    if (!sourceLine.isEmpty())
        detail << QObject::tr("source: %1").arg(sourceLine);

    return detail.join(QStringLiteral("; "));
}
}

class RapidObjImportPlugin final : public MeshIOPlugin
{
public:
    QString pluginId() const override
    {
        return QStringLiteral("io_obj_rapidobj");
    }

    QString name() const override
    {
        return QObject::tr("rapidobj OBJ Importer");
    }

    QStringList supportedExtensions() const override
    {
        return { QStringLiteral("obj") };
    }

    bool canLoad(const QString &filename) const override
    {
        const QString ext = QFileInfo(filename).suffix().toLower();
        return supportedExtensions().contains(ext);
    }

    int load(const QString &filename, VCGMesh &mesh, vcg::CallBackPos *cb, int *outLoadMask) const override
    {
        m_lastDetailedError.clear();

        if (outLoadMask)
            *outLoadMask = 0;

        mesh.Clear();

        rapidobj::Result result = rapidobj::ParseFile(
            filename.toStdString(),
            rapidobj::MaterialLibrary::Default(rapidobj::Load::Optional));
        if (result.error) {
            m_lastDetailedError = formatRapidObjErrorDetails(result.error);
            return kErrParse;
        }

        if (!rapidobj::Triangulate(result)) {
            m_lastDetailedError = formatRapidObjErrorDetails(result.error);
            return kErrTriangulate;
        }

        const auto &positions = result.attributes.positions;
        const auto &normals = result.attributes.normals;
        const auto &texcoords = result.attributes.texcoords;
        const auto &colors = result.attributes.colors;
        if (positions.empty()) {
            m_lastDetailedError = QObject::tr(
                "positions=%1, normals=%2, texcoords=%3, colors=%4, shapes=%5")
                                      .arg(positions.size() / 3)
                                      .arg(normals.size() / 3)
                                      .arg(texcoords.size() / 2)
                                      .arg(colors.size() / 3)
                                      .arg(result.shapes.size());
            return kErrInvalidData;
        }

        bool importedNormals = false;
        bool importedTexcoords = false;
        bool importedColors = false;
        size_t skippedMalformedFaces = 0;
        size_t skippedInvalidPositionFaces = 0;
        size_t skippedNonTriangleFaces = 0;
        size_t skippedInvalidPointIndices = 0;
        size_t invalidTexcoordCorners = 0;

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
                if (indexOffset + size_t(vertexCount) > shape.mesh.indices.size()) {
                    skippedMalformedFaces += (shape.mesh.num_face_vertices.size() - faceIndex);
                    break;
                }

                if (vertexCount != 3) {
                    indexOffset += size_t(vertexCount);
                    ++skippedNonTriangleFaces;
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

                if (!faceValid) {
                    ++skippedInvalidPositionFaces;
                    continue;
                }

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
                    if (!hasPair(texcoords, idx.texcoord_index)) {
                        if (idx.texcoord_index >= 0)
                            ++invalidTexcoordCorners;
                        continue;
                    }

                    const size_t tBase = size_t(idx.texcoord_index) * 2;
                    fi->WT(c).U() = texcoords[tBase + 0];
                    fi->WT(c).V() = texcoords[tBase + 1];
                    fi->WT(c).N() = 0;
                    importedTexcoords = true;
                }
            }

            for (size_t i = 0; i < shape.points.indices.size(); ++i) {
                const rapidobj::Index idx = shape.points.indices[i];
                if (getOrCreateVertex(idx) < 0)
                    ++skippedInvalidPointIndices;
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

        if (mesh.VN() == 0) {
            size_t totalShapeIndices = 0;
            for (const auto &shape : result.shapes)
                totalShapeIndices += shape.mesh.indices.size();
            m_lastDetailedError = QObject::tr(
                "No vertices created from parsed data (positions=%1, shapeIndices=%2, shapes=%3).")
                                      .arg(positions.size() / 3)
                                      .arg(totalShapeIndices)
                                      .arg(result.shapes.size());
            return kErrInvalidData;
        }

        if (cb) {
            QStringList warnings;
            if (skippedMalformedFaces > 0) {
                warnings << QObject::tr("skipped %1 malformed face record(s)")
                                .arg(skippedMalformedFaces);
            }
            if (skippedNonTriangleFaces > 0) {
                warnings << QObject::tr("skipped %1 non-triangle face(s) after triangulation")
                                .arg(skippedNonTriangleFaces);
            }
            if (skippedInvalidPositionFaces > 0) {
                warnings << QObject::tr("skipped %1 face(s) with invalid position index")
                                .arg(skippedInvalidPositionFaces);
            }
            if (skippedInvalidPointIndices > 0) {
                warnings << QObject::tr("skipped %1 point(s) with invalid position index")
                                .arg(skippedInvalidPointIndices);
            }
            if (invalidTexcoordCorners > 0) {
                warnings << QObject::tr("%1 corner texcoord index(es) were invalid and ignored")
                                .arg(invalidTexcoordCorners);
            }

            if (!warnings.isEmpty()) {
                const QString warningMsg =
                    QObject::tr("rapidobj warnings: %1").arg(warnings.join(QStringLiteral("; ")));
                const QByteArray rawWarning = warningMsg.toLocal8Bit();
                cb(99, rawWarning.constData());
            }
        }

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
            return QObject::tr("Cannot parse OBJ file with rapidobj. %1")
                .arg(m_lastDetailedError);
        case kErrTriangulate:
            return QObject::tr("Failed to triangulate OBJ faces with rapidobj. %1")
                .arg(m_lastDetailedError);
        case kErrInvalidData:
            return QObject::tr("OBJ file has no valid vertex data. %1")
                .arg(m_lastDetailedError);
        default:
            return QObject::tr("Unknown rapidobj import error.");
        }
    }

private:
    mutable QString m_lastDetailedError;
};

void registerRapidObjImportPlugin(MeshIOPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<RapidObjImportPlugin>());
}
