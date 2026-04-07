#include "plugins/io_gltf/gltfimportplugin.h"

#include "meshioplugin.h"
#include "meshiopluginmanager.h"

#include <vcg/complex/allocate.h>
#include <wrap/io_trimesh/io_mask.h>

#include <QFileInfo>
#include <QDir>
#include <QImage>
#include <QMatrix3x3>
#include <QMatrix4x4>
#include <QObject>
#include <QQuaternion>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <functional>
#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include <tiny_gltf.h>

namespace {
constexpr int kErrOpen = -1;
constexpr int kErrParse = -2;
constexpr int kErrNoMesh = -3;
constexpr int kErrInvalidData = -4;

struct AccessorView {
    const unsigned char *data = nullptr;
    size_t stride = 0;
    size_t count = 0;
    int componentType = 0;
    int componentCount = 0;
    bool normalized = false;
    bool valid = false;
};

size_t componentSize(int componentType)
{
    switch (componentType) {
    case TINYGLTF_COMPONENT_TYPE_BYTE:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        return 1;
    case TINYGLTF_COMPONENT_TYPE_SHORT:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        return 2;
    case TINYGLTF_COMPONENT_TYPE_INT:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
    case TINYGLTF_COMPONENT_TYPE_FLOAT:
        return 4;
    case TINYGLTF_COMPONENT_TYPE_DOUBLE:
        return 8;
    default:
        return 0;
    }
}

double readComponentAsDouble(const unsigned char *ptr, int componentType, bool normalized)
{
    switch (componentType) {
    case TINYGLTF_COMPONENT_TYPE_BYTE: {
        const auto v = *reinterpret_cast<const int8_t *>(ptr);
        return normalized ? std::max(-1.0, double(v) / 127.0) : double(v);
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
        const auto v = *reinterpret_cast<const uint8_t *>(ptr);
        return normalized ? double(v) / 255.0 : double(v);
    }
    case TINYGLTF_COMPONENT_TYPE_SHORT: {
        const auto v = *reinterpret_cast<const int16_t *>(ptr);
        return normalized ? std::max(-1.0, double(v) / 32767.0) : double(v);
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
        const auto v = *reinterpret_cast<const uint16_t *>(ptr);
        return normalized ? double(v) / 65535.0 : double(v);
    }
    case TINYGLTF_COMPONENT_TYPE_INT: {
        const auto v = *reinterpret_cast<const int32_t *>(ptr);
        return normalized ? std::max(-1.0, double(v) / 2147483647.0) : double(v);
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
        const auto v = *reinterpret_cast<const uint32_t *>(ptr);
        return normalized ? double(v) / 4294967295.0 : double(v);
    }
    case TINYGLTF_COMPONENT_TYPE_FLOAT:
        return double(*reinterpret_cast<const float *>(ptr));
    case TINYGLTF_COMPONENT_TYPE_DOUBLE:
        return *reinterpret_cast<const double *>(ptr);
    default:
        return 0.0;
    }
}

bool readIndex(const AccessorView &view, size_t index, uint32_t &outIndex)
{
    if (!view.valid || index >= view.count)
        return false;
    if (view.componentCount != 1)
        return false;

    const unsigned char *ptr = view.data + index * view.stride;
    switch (view.componentType) {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        outIndex = *reinterpret_cast<const uint8_t *>(ptr);
        return true;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        outIndex = *reinterpret_cast<const uint16_t *>(ptr);
        return true;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        outIndex = *reinterpret_cast<const uint32_t *>(ptr);
        return true;
    default:
        return false;
    }
}

bool makeAccessorView(const tinygltf::Model &model, int accessorIndex, AccessorView &view)
{
    view = {};
    if (accessorIndex < 0 || accessorIndex >= int(model.accessors.size()))
        return false;

    const tinygltf::Accessor &accessor = model.accessors[accessorIndex];
    if (accessor.bufferView < 0 || accessor.bufferView >= int(model.bufferViews.size()))
        return false;

    const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
    if (bufferView.buffer < 0 || bufferView.buffer >= int(model.buffers.size()))
        return false;

    const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];
    const size_t compSize = componentSize(accessor.componentType);
    const int compCount = tinygltf::GetNumComponentsInType(accessor.type);
    if (compSize == 0 || compCount <= 0)
        return false;

    const size_t minStride = compSize * size_t(compCount);
    const size_t stride = accessor.ByteStride(bufferView) > 0
        ? size_t(accessor.ByteStride(bufferView))
        : minStride;
    if (stride < minStride)
        return false;

    const size_t baseOffset = size_t(bufferView.byteOffset) + size_t(accessor.byteOffset);
    if (baseOffset > buffer.data.size())
        return false;

    const size_t count = size_t(accessor.count);
    if (count > 0) {
        const size_t lastElementOffset = baseOffset + (count - 1) * stride;
        if (lastElementOffset + minStride > buffer.data.size())
            return false;
    }

    view.data = buffer.data.data() + baseOffset;
    view.stride = stride;
    view.count = count;
    view.componentType = accessor.componentType;
    view.componentCount = compCount;
    view.normalized = accessor.normalized;
    view.valid = true;
    return true;
}

QVector3D readVec3(const AccessorView &view, size_t index)
{
    if (!view.valid || index >= view.count || view.componentCount < 3)
        return {};
    const unsigned char *ptr = view.data + index * view.stride;
    const size_t step = componentSize(view.componentType);
    const float x = float(readComponentAsDouble(ptr + 0 * step, view.componentType, view.normalized));
    const float y = float(readComponentAsDouble(ptr + 1 * step, view.componentType, view.normalized));
    const float z = float(readComponentAsDouble(ptr + 2 * step, view.componentType, view.normalized));
    return { x, y, z };
}

QVector2D readVec2(const AccessorView &view, size_t index)
{
    if (!view.valid || index >= view.count || view.componentCount < 2)
        return {};
    const unsigned char *ptr = view.data + index * view.stride;
    const size_t step = componentSize(view.componentType);
    const float x = float(readComponentAsDouble(ptr + 0 * step, view.componentType, view.normalized));
    const float y = float(readComponentAsDouble(ptr + 1 * step, view.componentType, view.normalized));
    return { x, y };
}

QVector3D mulMat3Vec3(const QMatrix3x3 &m, const QVector3D &v)
{
    return QVector3D(
        m(0, 0) * v.x() + m(0, 1) * v.y() + m(0, 2) * v.z(),
        m(1, 0) * v.x() + m(1, 1) * v.y() + m(1, 2) * v.z(),
        m(2, 0) * v.x() + m(2, 1) * v.y() + m(2, 2) * v.z());
}

vcg::Color4b readColor4b(const AccessorView &view, size_t index, const vcg::Color4b &fallback)
{
    if (!view.valid || index >= view.count || view.componentCount < 3)
        return fallback;

    const unsigned char *ptr = view.data + index * view.stride;
    const size_t step = componentSize(view.componentType);

    const auto comp = [&](int i) -> float {
        const float v = float(readComponentAsDouble(ptr + size_t(i) * step, view.componentType, view.normalized));
        return view.normalized ? v : (v / 255.0f);
    };

    const float r = std::clamp(comp(0), 0.0f, 1.0f);
    const float g = std::clamp(comp(1), 0.0f, 1.0f);
    const float b = std::clamp(comp(2), 0.0f, 1.0f);
    const float a = (view.componentCount >= 4) ? std::clamp(comp(3), 0.0f, 1.0f) : 1.0f;

    return vcg::Color4b(
        uint8_t(std::round(r * 255.0f)),
        uint8_t(std::round(g * 255.0f)),
        uint8_t(std::round(b * 255.0f)),
        uint8_t(std::round(a * 255.0f)));
}

QMatrix4x4 nodeLocalMatrix(const tinygltf::Node &node)
{
    if (node.matrix.size() == 16) {
        float m[16];
        for (int i = 0; i < 16; ++i)
            m[i] = float(node.matrix[size_t(i)]);
        return QMatrix4x4(m);
    }

    QMatrix4x4 m;
    m.setToIdentity();
    if (node.translation.size() == 3) {
        m.translate(
            float(node.translation[0]),
            float(node.translation[1]),
            float(node.translation[2]));
    }
    if (node.rotation.size() == 4) {
        const QQuaternion q(
            float(node.rotation[3]),
            float(node.rotation[0]),
            float(node.rotation[1]),
            float(node.rotation[2]));
        m.rotate(q.normalized());
    }
    if (node.scale.size() == 3) {
        m.scale(
            float(node.scale[0]),
            float(node.scale[1]),
            float(node.scale[2]));
    }
    return m;
}

class GLTFImportPlugin final : public MeshIOPlugin
{
public:
    QString name() const override
    {
        return QObject::tr("glTF Importer (tinygltf)");
    }

    bool canLoad(const QString &filename) const override
    {
        const QString ext = QFileInfo(filename).suffix().toLower();
        return ext == QLatin1String("gltf") || ext == QLatin1String("glb");
    }

    int load(const QString &filename, VCGMesh &mesh, vcg::CallBackPos *cb, int *outLoadMask) const override
    {
        if (outLoadMask)
            *outLoadMask = 0;

        tinygltf::TinyGLTF loader;
        tinygltf::Model model;
        std::string warn;
        std::string err;

        const QString ext = QFileInfo(filename).suffix().toLower();
        const bool ok = (ext == QLatin1String("glb"))
            ? loader.LoadBinaryFromFile(&model, &warn, &err, filename.toStdString())
            : loader.LoadASCIIFromFile(&model, &warn, &err, filename.toStdString());
        if (!ok)
            return kErrOpen;
        if (model.meshes.empty())
            return kErrNoMesh;

        mesh.Clear();
        mesh.textures.clear();

        int loadMask = 0;
        bool hasVertexNormals = false;
        bool hasVertexColors = false;
        bool hasWedgeTexCoords = false;

        std::unordered_map<int, int> imageToTextureSlot;
        std::vector<std::string> textureFiles;

        auto textureSlotForMaterial = [&](int materialIndex) -> int {
            if (materialIndex < 0 || materialIndex >= int(model.materials.size()))
                return -1;
            const auto &mat = model.materials[size_t(materialIndex)];
            const int texIndex = mat.pbrMetallicRoughness.baseColorTexture.index;
            if (texIndex < 0 || texIndex >= int(model.textures.size()))
                return -1;
            const int imageIndex = model.textures[size_t(texIndex)].source;
            if (imageIndex < 0 || imageIndex >= int(model.images.size()))
                return -1;
            const auto existing = imageToTextureSlot.find(imageIndex);
            if (existing != imageToTextureSlot.end())
                return existing->second;

            const tinygltf::Image &img = model.images[size_t(imageIndex)];
            QString uri = QString::fromStdString(img.uri);
            uri = QUrl::fromPercentEncoding(uri.toUtf8());

            const bool isDataUri = uri.startsWith(QStringLiteral("data:"), Qt::CaseInsensitive);
            if (uri.isEmpty() || isDataUri) {
                if (img.image.empty() || img.width <= 0 || img.height <= 0)
                    return -1;

                QImage qimg;
                if (img.bits == 8 && img.pixel_type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                    if (img.component == 4) {
                        qimg = QImage(img.image.data(), img.width, img.height, QImage::Format_RGBA8888).copy();
                    } else if (img.component == 3) {
                        qimg = QImage(img.image.data(), img.width, img.height, QImage::Format_RGB888).copy();
                    } else if (img.component == 1) {
                        qimg = QImage(img.image.data(), img.width, img.height, QImage::Format_Grayscale8).copy();
                    }
                }
                if (qimg.isNull())
                    return -1;

                QDir tmpDir(QDir::tempPath() + QStringLiteral("/qmeshlab_gltf_textures"));
                if (!tmpDir.exists())
                    tmpDir.mkpath(QStringLiteral("."));
                const QString baseName = QFileInfo(filename).completeBaseName();
                const QString fileName = QStringLiteral("%1_img_%2.png").arg(baseName).arg(imageIndex);
                const QString absPath = tmpDir.filePath(fileName);
                if (!qimg.save(absPath))
                    return -1;
                uri = absPath;
            }

            const int slot = int(textureFiles.size());
            textureFiles.push_back(uri.toStdString());
            imageToTextureSlot[imageIndex] = slot;
            return slot;
        };

        auto primitiveBaseColor = [&](int materialIndex) -> vcg::Color4b {
            if (materialIndex < 0 || materialIndex >= int(model.materials.size()))
                return vcg::Color4b::White;
            const auto &factor = model.materials[size_t(materialIndex)].pbrMetallicRoughness.baseColorFactor;
            if (factor.size() < 3)
                return vcg::Color4b::White;
            const auto toU8 = [](double v) {
                return uint8_t(std::round(std::clamp(v, 0.0, 1.0) * 255.0));
            };
            const uint8_t r = toU8(factor[0]);
            const uint8_t g = toU8(factor[1]);
            const uint8_t b = toU8(factor[2]);
            const uint8_t a = (factor.size() >= 4) ? toU8(factor[3]) : 255;
            return vcg::Color4b(r, g, b, a);
        };

        std::function<void(int, const QMatrix4x4 &)> processNode;
        processNode = [&](int nodeIndex, const QMatrix4x4 &parentWorld) {
            if (nodeIndex < 0 || nodeIndex >= int(model.nodes.size()))
                return;
            const tinygltf::Node &node = model.nodes[size_t(nodeIndex)];
            const QMatrix4x4 world = parentWorld * nodeLocalMatrix(node);

            if (node.mesh >= 0 && node.mesh < int(model.meshes.size())) {
                const tinygltf::Mesh &srcMesh = model.meshes[size_t(node.mesh)];
                const int primitiveCount = int(srcMesh.primitives.size());
                for (int pi = 0; pi < primitiveCount; ++pi) {
                    const tinygltf::Primitive &prim = srcMesh.primitives[size_t(pi)];
                    const int mode = prim.mode;
                    if (mode != TINYGLTF_MODE_TRIANGLES && mode != TINYGLTF_MODE_POINTS)
                        continue;

                    const auto posIt = prim.attributes.find("POSITION");
                    if (posIt == prim.attributes.end())
                        continue;

                    AccessorView posView, nrmView, uvView, colView, idxView;
                    if (!makeAccessorView(model, posIt->second, posView) || posView.componentCount < 3)
                        continue;
                    const auto nIt = prim.attributes.find("NORMAL");
                    if (nIt != prim.attributes.end() && makeAccessorView(model, nIt->second, nrmView) && nrmView.componentCount >= 3)
                        hasVertexNormals = true;
                    const auto uvIt = prim.attributes.find("TEXCOORD_0");
                    if (uvIt != prim.attributes.end() && makeAccessorView(model, uvIt->second, uvView) && uvView.componentCount >= 2)
                        hasWedgeTexCoords = true;
                    const auto cIt = prim.attributes.find("COLOR_0");
                    if (cIt != prim.attributes.end() && makeAccessorView(model, cIt->second, colView) && colView.componentCount >= 3)
                        hasVertexColors = true;
                    const bool hasIndices = (prim.indices >= 0) && makeAccessorView(model, prim.indices, idxView);

                    const int texSlot = textureSlotForMaterial(prim.material);
                    const vcg::Color4b baseColor = primitiveBaseColor(prim.material);
                    const QMatrix3x3 normalMat = world.normalMatrix();

                    auto addVertexFromSource = [&](uint32_t srcIndex) -> int {
                        if (srcIndex >= posView.count)
                            return -1;
                        const QVector3D p = readVec3(posView, srcIndex);
                        const QVector4D wp = world * QVector4D(p, 1.0f);
                        auto vi = vcg::tri::Allocator<VCGMesh>::AddVertex(
                            mesh, VCGMesh::CoordType(wp.x(), wp.y(), wp.z()));
                        VCGVertex *v = &(*vi);

                        if (nrmView.valid && srcIndex < nrmView.count) {
                            QVector3D n = mulMat3Vec3(normalMat, readVec3(nrmView, srcIndex));
                            if (!qFuzzyIsNull(n.lengthSquared()))
                                n.normalize();
                            v->N() = VCGMesh::CoordType(n.x(), n.y(), n.z());
                        }

                        if (colView.valid && srcIndex < colView.count) {
                            v->C() = readColor4b(colView, srcIndex, baseColor);
                        } else {
                            v->C() = baseColor;
                        }
                        return mesh.VN() - 1;
                    };

                    auto indexAt = [&](size_t i, uint32_t &dst) -> bool {
                        if (hasIndices)
                            return readIndex(idxView, i, dst);
                        if (i >= posView.count)
                            return false;
                        dst = uint32_t(i);
                        return true;
                    };

                    if (mode == TINYGLTF_MODE_POINTS) {
                        const size_t pointCount = hasIndices ? idxView.count : posView.count;
                        for (size_t i = 0; i < pointCount; ++i) {
                            uint32_t src = 0;
                            if (!indexAt(i, src))
                                continue;
                            addVertexFromSource(src);
                        }
                    } else {
                        const size_t indexCount = hasIndices ? idxView.count : posView.count;
                        const size_t triCount = indexCount / 3;
                        for (size_t ti = 0; ti < triCount; ++ti) {
                            uint32_t idx[3] = { 0, 0, 0 };
                            if (!indexAt(ti * 3 + 0, idx[0]) || !indexAt(ti * 3 + 1, idx[1]) || !indexAt(ti * 3 + 2, idx[2]))
                                continue;

                            const int vi[3] = {
                                addVertexFromSource(idx[0]),
                                addVertexFromSource(idx[1]),
                                addVertexFromSource(idx[2])
                            };
                            if (vi[0] < 0 || vi[1] < 0 || vi[2] < 0)
                                continue;

                            auto fi = vcg::tri::Allocator<VCGMesh>::AddFace(
                                mesh, size_t(vi[0]), size_t(vi[1]), size_t(vi[2]));
                            if (texSlot >= 0 && uvView.valid) {
                                hasWedgeTexCoords = true;
                                for (int c = 0; c < 3; ++c) {
                                    const uint32_t src = idx[c];
                                    const QVector2D uv = (src < uvView.count) ? readVec2(uvView, src) : QVector2D();
                                    fi->WT(c).U() = uv.x();
                                    fi->WT(c).V() = 1.0f - uv.y();
                                    fi->WT(c).N() = texSlot;
                                }
                            }
                        }
                    }

                    if (cb != nullptr && primitiveCount > 0) {
                        const int primitiveProgress = int((pi + 1) * 100 / primitiveCount);
                        cb(primitiveProgress, "Loading glTF primitives\r");
                    }
                }
            }

            for (const int child : node.children)
                processNode(child, world);
        };

        bool processedAnyRoot = false;
        QMatrix4x4 identity;
        identity.setToIdentity();

        if (model.defaultScene >= 0 && model.defaultScene < int(model.scenes.size())) {
            const auto &scene = model.scenes[size_t(model.defaultScene)];
            for (const int nodeIndex : scene.nodes) {
                processedAnyRoot = true;
                processNode(nodeIndex, identity);
            }
        } else if (!model.scenes.empty()) {
            for (const auto &scene : model.scenes) {
                for (const int nodeIndex : scene.nodes) {
                    processedAnyRoot = true;
                    processNode(nodeIndex, identity);
                }
            }
        }

        if (!processedAnyRoot) {
            for (size_t ni = 0; ni < model.nodes.size(); ++ni)
                processNode(int(ni), identity);
        }

        if (mesh.VN() == 0)
            return kErrInvalidData;

        mesh.textures = textureFiles;

        if (hasVertexNormals)
            loadMask |= vcg::tri::io::Mask::IOM_VERTNORMAL;
        if (hasVertexColors)
            loadMask |= vcg::tri::io::Mask::IOM_VERTCOLOR;
        if (hasWedgeTexCoords)
            loadMask |= vcg::tri::io::Mask::IOM_WEDGTEXCOORD;
        if (hasWedgeTexCoords && textureFiles.size() > 1)
            loadMask |= vcg::tri::io::Mask::IOM_WEDGTEXMULTI;

        if (outLoadMask)
            *outLoadMask = loadMask;

        if (cb != nullptr)
            cb(100, "Loading glTF done\r");

        return 0;
    }

    QString filterString() const override
    {
        return QObject::tr("glTF Files (*.gltf *.glb)");
    }

    QString errorString(int errCode) const override
    {
        switch (errCode) {
        case kErrOpen:
            return QObject::tr("Cannot open or parse glTF/glb file.");
        case kErrParse:
            return QObject::tr("Failed to parse glTF document.");
        case kErrNoMesh:
            return QObject::tr("No mesh data found in glTF document.");
        case kErrInvalidData:
            return QObject::tr("glTF geometry data is empty or invalid.");
        default:
            return QObject::tr("Unknown glTF import error.");
        }
    }
};
}

void registerGltfImportPlugin(MeshIOPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<GLTFImportPlugin>());
}
