#pragma once

#include <QMetaType>
#include <QVariant>
#include <QString>
#include <QStringList>
#include <QVector>
#include <vector>

class Document;

enum class MeshFilterInputDomain
{
    None,
    SingleMesh,
    WholeDocument
};
Q_DECLARE_METATYPE(MeshFilterInputDomain)

enum class MeshFilterOutputDomain
{
    Information,
    ModifyCurrentMesh,
    NewMeshes
};
Q_DECLARE_METATYPE(MeshFilterOutputDomain)

enum class MeshFilterParameterType
{
    Bool,
    Int,
    Double,
    String,
    Enum,
    Color
};
Q_DECLARE_METATYPE(MeshFilterParameterType)

struct MeshFilterMeshRequirements
{
    bool requireVertices = false;
    bool requireEdges = false;
    bool requireFaces = false;
    bool requireVertexColor = false;
    bool requireFaceColor = false;
    bool requireTextureCoordinates = false;
    bool requireTextures = false;
    bool requireVertexQuality = false;
    bool requireFaceQuality = false;
};

struct MeshFilterEnumOption
{
    QString id;
    QString label;
    QString helpMarkdown;
};

struct MeshFilterParameterDescriptor
{
    QString id;
    QString label;
    QString helpMarkdown;
    QString group;
    MeshFilterParameterType type = MeshFilterParameterType::String;
    QVariant defaultValue;
    QVariant minValue;
    QVariant maxValue;
    int decimals = 3;
    std::vector<MeshFilterEnumOption> enumOptions;

    bool isAdvancedGroup() const
    {
        return group.startsWith(QStringLiteral("advanced"), Qt::CaseInsensitive);
    }
};

struct MeshFilterDescriptor
{
    QString id;
    QString menuPath;
    QString name;
    QString shortDescription;
    QString longDescriptionMarkdown;
    QStringList tags;
    MeshFilterInputDomain inputDomain = MeshFilterInputDomain::SingleMesh;
    MeshFilterMeshRequirements inputRequirements;
    MeshFilterOutputDomain outputDomain = MeshFilterOutputDomain::Information;
    std::vector<MeshFilterParameterDescriptor> parameters;
};

using MeshFilterParameterValues = QVariantMap;

struct MeshFilterRunResult
{
    bool success = false;
    bool documentModified = false;
    QString errorMessage;
    QStringList infoMessages;
    QVector<int> newMeshIndices;
};

class MeshFilterPlugin
{
public:
    virtual ~MeshFilterPlugin() = default;

    virtual QString pluginId() const = 0;
    virtual QString name() const = 0;

    // Returns all filter descriptors exposed by this plugin.
    // Default values can depend on document state.
    virtual std::vector<MeshFilterDescriptor> filters(const Document &doc) const = 0;

    // Runs one filter declared by filters().
    // filterId is the plugin-local id (MeshFilterDescriptor::id).
    virtual MeshFilterRunResult runFilter(
        const QString &filterId,
        const MeshFilterParameterValues &parameters,
        Document &doc) const = 0;
};
