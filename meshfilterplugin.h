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
    Mesh,
    String,
    FileOpen,
    FileSave,
    TextureRef,
    TextureOutputRef,
    Enum,
    Color,
    Point3f    // 3D point or direction — editor shows X/Y/Z spinboxes with fill-from-context buttons
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
    // Only used when type == FileOpen / FileSave.
    QString fileDialogTitle;
    QStringList fileNameFilters;
    QString fileDefaultSuffix;
    // Only used when type == TextureRef / TextureOutputRef.
    QString textureSourceMeshParameter;
    bool textureAllowAutomatic = true;
    // Only used when type == Point3f: "point" (position) or "direction" (unit vector).
    QString point3fRole = QStringLiteral("point");

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
    // When true the framework automatically appends an "incremental_selection" bool
    // parameter and, when enabled, ORs the pre-run selection back after the filter.
    bool incrementalSelection = false;
    std::vector<MeshFilterParameterDescriptor> parameters;

    // Two-letter codes describing which mesh attributes this filter modifies.
    // Recognised codes: VG VN VC VQ VT VA VS FV FN FC FQ FA FS FP WT TX TM
    // Used only when outputDomain == ModifyCurrentMesh.
    // VG=vertex geometry  VN=vertex normals  VC=vertex color  VQ=vertex quality
    // VT=vertex texcoords VA=vertex attributes VS=vertex selection
    // FV=face-vertex connectivity  FN=face normals  FC=face color  FQ=face quality
    // FA=face attributes  FS=face selection  FP=face polygon (faux-edge) bits
    // WT=wedge texcoords  TX=texture images  TM=per-mesh transform matrix
    QStringList outputModifies;

    // Codes declaring volatile mesh data the framework must prepare before
    // calling runFilter().  The framework executes the corresponding VCG
    // algorithms automatically, so filter code need not repeat them.
    //
    // Recognised codes (applied in dependency order):
    //   FF        — UpdateTopology::FaceFace
    //   VF        — UpdateTopology::VertexFace
    //   BorderFF  — FF + FaceBorderFromFF + VertexBorderFromFaceBorder
    //   BorderVF  — VF + FaceBorderFromVF + VertexBorderFromFaceBorder
    //   FNorm     — UpdateNormal::PerFaceNormalized
    //   VNorm     — UpdateNormal::PerVertexNormalizedPerFaceNormalized
    //   BBox      — UpdateBounding::Box
    QStringList inputPrepare;
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
