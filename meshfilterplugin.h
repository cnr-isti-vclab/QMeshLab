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
    AbsPerc,
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

// FilterParams is a typed wrapper around MeshFilterParameterValues.
// Declared in filterparam.h; forward-declared here to avoid circular inclusion.
class FilterParams;

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
    // The default implementation loads from the Qt resource path
    //   :/filters/<pluginId>/filters.json
    // and resolves symbolic bound tokens against doc.
    // Override for plugins that need completely custom descriptor logic.
    virtual std::vector<MeshFilterDescriptor> filters(const Document &doc) const;

    // Runs one filter declared by filters().
    // filterId is the plugin-local id (MeshFilterDescriptor::id).
    // params contains pre-normalized, validated values for all declared parameters.
    virtual MeshFilterRunResult runFilter(
        const QString &filterId,
        const FilterParams &params,
        Document &doc) const = 0;
};
