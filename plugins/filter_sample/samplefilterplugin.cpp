#include "samplefilterplugin.h"

#include "document.h"
#include "meshfilterpluginmanager.h"
#include <wrap/io_trimesh/io_mask.h>
#include <vcg/complex/algorithms/update/bounding.h>
#include <vcg/complex/algorithms/update/normal.h>
#include <QRandomGenerator>
#include <random>

namespace {
constexpr QLatin1StringView kFilterRandomDisplacement("apply_coord_random_displacement");
using Mask = vcg::tri::io::Mask;

MeshFilterRunResult fail(const QString &message)
{
    return { false, false, message };
}

MeshFilterRunResult success(const QStringList &info)
{
    MeshFilterRunResult result;
    result.success = true;
    result.documentModified = true;
    result.infoMessages = info;
    return result;
}

template<typename Rng>
float symmetricRandom(Rng &rng, float maxDisplacement)
{
    std::uniform_real_distribution<float> dist(-maxDisplacement, maxDisplacement);
    return dist(rng);
}

} // namespace

QString SampleFilterPlugin::pluginId() const
{
    return QStringLiteral("qmeshlab.filter.sample");
}

QString SampleFilterPlugin::name() const
{
    return QObject::tr("QMeshLab Sample Filters");
}

MeshFilterRunResult SampleFilterPlugin::runFilter(
    const QString &filterId,
    const FilterParams &params,
    Document &doc) const
{
    if (filterId != QString::fromLatin1(kFilterRandomDisplacement))
        return fail(QObject::tr("Unknown filter id: %1").arg(filterId));

    const int meshIndex = doc.currentMeshIndex();
    if (meshIndex < 0 || meshIndex >= doc.meshCount())
        return fail(QObject::tr("No current mesh selected."));

    Document::MeshEntry &entry = doc.mesh(meshIndex);
    VCGMesh &mesh = entry.mesh;
    if (mesh.VN() <= 0)
        return fail(QObject::tr("Current mesh has no vertices."));

    const float maxDisplacement = float(params.getDouble(QStringLiteral("Displacement"), 0.0));
    if (!(std::isfinite(maxDisplacement)) || maxDisplacement < 0.0f)
        return fail(QObject::tr("Max displacement must be a finite non-negative value."));

    const bool updateNormals = params.getBool(QStringLiteral("UpdateNormals"), true);
    const int randomSeed = params.getInt(QStringLiteral("RandomSeed"), 0);

    std::mt19937 rng;
    if (randomSeed == 0)
        rng.seed(QRandomGenerator::global()->generate());
    else
        rng.seed(static_cast<std::mt19937::result_type>(randomSeed));

    vcg::CallBackPos *cb = doc.progressCallback();
    const int totalVerts = std::max(1, mesh.VN());
    int processed = 0;
    for (VCGVertex &v : mesh.vert) {
        if (cb)
            cb((100 * processed) / totalVerts, "Randomly Displacing...");
        v.P() += vcg::Point3f(
            symmetricRandom(rng, maxDisplacement),
            symmetricRandom(rng, maxDisplacement),
            symmetricRandom(rng, maxDisplacement));
        ++processed;
    }

    if (updateNormals) {
        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFace(mesh);
        entry.ioMask |= Mask::IOM_VERTNORMAL | Mask::IOM_FACENORMAL;
    }
    entry.ioMask |= Mask::IOM_VERTCOORD;
    vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
    doc.markMeshGeometryChanged(meshIndex, QObject::tr("Randomly displaced vertices of '%1'").arg(entry.name));

    return success({
        QObject::tr("Successfully displaced %1 vertices.").arg(mesh.VN()),
        QObject::tr("Maximum displacement: %1").arg(QString::number(maxDisplacement, 'f', 6)),
        randomSeed == 0
            ? QObject::tr("Random seed: automatic")
            : QObject::tr("Random seed: %1").arg(randomSeed)
    });
}

void registerSampleFilterPlugin(MeshFilterPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<SampleFilterPlugin>());
}
