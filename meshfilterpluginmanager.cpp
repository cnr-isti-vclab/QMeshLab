#include "meshfilterpluginmanager.h"

#include "document.h"
#include "filterparam.h"
#include "vcgmesh.h"
#include <wrap/io_trimesh/io_mask.h>
#include <QColor>
#include <QFileInfo>
#include <QObject>
#include <QSet>
#include <QVector3D>
#include <algorithm>
#include <cmath>

namespace {
constexpr QLatin1StringView kKeySeparator("::");

const MeshFilterDescriptor *findDescriptorById(
    const std::vector<MeshFilterDescriptor> &descriptors,
    const QString &filterId)
{
    for (const MeshFilterDescriptor &descriptor : descriptors) {
        if (descriptor.id == filterId)
            return &descriptor;
    }
    return nullptr;
}

QVariant defaultValueForParameter(const MeshFilterParameterDescriptor &parameter)
{
    if (parameter.defaultValue.isValid())
        return parameter.defaultValue;

    switch (parameter.type) {
    case MeshFilterParameterType::Bool:
        return false;
    case MeshFilterParameterType::Int:
    case MeshFilterParameterType::Mesh:
    case MeshFilterParameterType::TextureRef:
        return 0;
    case MeshFilterParameterType::TextureOutputRef:
        return QVariantMap{
            { QStringLiteral("mode"), QStringLiteral("new") },
            { QStringLiteral("path"), parameter.defaultValue.toString() }
        };
    case MeshFilterParameterType::Double:
    case MeshFilterParameterType::AbsPerc:
        return 0.0;
    case MeshFilterParameterType::String:
    case MeshFilterParameterType::FileOpen:
    case MeshFilterParameterType::FileSave:
        return QString();
    case MeshFilterParameterType::Enum:
        if (!parameter.enumOptions.empty())
            return parameter.enumOptions.front().id;
        return QString();
    case MeshFilterParameterType::Color:
        return QColor(Qt::white);
    case MeshFilterParameterType::Point3f:
        return QVariant::fromValue(QVector3D(0.0f, 0.0f, 0.0f));
    }
    return {};
}

bool parseBoolFromVariant(const QVariant &value, bool &out)
{
    if (value.userType() == QMetaType::Bool) {
        out = value.toBool();
        return true;
    }

    bool intOk = false;
    const int intValue = value.toInt(&intOk);
    if (intOk) {
        out = (intValue != 0);
        return true;
    }

    const QString text = value.toString().trimmed().toLower();
    if (text == QStringLiteral("true")
        || text == QStringLiteral("1")
        || text == QStringLiteral("yes")
        || text == QStringLiteral("on")) {
        out = true;
        return true;
    }
    if (text == QStringLiteral("false")
        || text == QStringLiteral("0")
        || text == QStringLiteral("no")
        || text == QStringLiteral("off")) {
        out = false;
        return true;
    }
    return false;
}

QString parameterErrorPrefix(const MeshFilterParameterDescriptor &parameter)
{
    return QObject::tr("Parameter '%1'").arg(parameter.id);
}

QString appendSuffixIfMissing(const QString &path, const QString &suffix)
{
    if (path.trimmed().isEmpty() || suffix.trimmed().isEmpty())
        return path;
    if (!QFileInfo(path).suffix().isEmpty())
        return path;
    return QStringLiteral("%1.%2").arg(path, suffix);
}
}

void MeshFilterPluginManager::registerPlugin(std::unique_ptr<MeshFilterPlugin> plugin)
{
    if (!plugin)
        return;
    m_plugins.push_back(std::move(plugin));
}

std::vector<MeshFilterPluginManager::FilterInfo> MeshFilterPluginManager::filterInfos(
    const Document &doc) const
{
    std::vector<FilterInfo> infos;
    for (const auto &plugin : m_plugins) {
        const std::vector<MeshFilterDescriptor> descriptors = plugin->filters(doc);
        infos.reserve(infos.size() + descriptors.size());
        for (const MeshFilterDescriptor &descriptor : descriptors) {
            FilterInfo info;
            info.key = buildFilterKey(plugin->pluginId(), descriptor.id);
            info.pluginId = plugin->pluginId();
            info.pluginName = plugin->name();
            info.descriptor = descriptor;
            QString applicabilityError;
            info.applicable = validateDomain(descriptor, doc, applicabilityError);
            info.applicabilityError = applicabilityError;
            infos.push_back(std::move(info));
        }
    }
    return infos;
}

std::optional<MeshFilterPluginManager::FilterInfo> MeshFilterPluginManager::filterInfo(
    const QString &filterKey,
    const Document &doc) const
{
    QString pluginId;
    QString filterId;
    if (!splitFilterKey(filterKey, pluginId, filterId))
        return std::nullopt;

    for (const auto &plugin : m_plugins) {
        if (plugin->pluginId() != pluginId)
            continue;
        const std::vector<MeshFilterDescriptor> descriptors = plugin->filters(doc);
        const MeshFilterDescriptor *descriptor = findDescriptorById(descriptors, filterId);
        if (!descriptor)
            return std::nullopt;

        FilterInfo info;
        info.key = buildFilterKey(pluginId, filterId);
        info.pluginId = pluginId;
        info.pluginName = plugin->name();
        info.descriptor = *descriptor;
        QString applicabilityError;
        info.applicable = validateDomain(*descriptor, doc, applicabilityError);
        info.applicabilityError = applicabilityError;
        return info;
    }
    return std::nullopt;
}

QStringList MeshFilterPluginManager::loadedPluginSummaries() const
{
    QStringList summaries;
    summaries.reserve(static_cast<int>(m_plugins.size()));
    for (const auto &plugin : m_plugins) {
        summaries.push_back(QObject::tr("%1 (%2)").arg(plugin->name(), plugin->pluginId()));
    }
    return summaries;
}

MeshFilterRunResult MeshFilterPluginManager::runFilter(
    const QString &filterKey,
    const MeshFilterParameterValues &parameters,
    Document &doc) const
{
    QString pluginId;
    QString filterId;
    if (!splitFilterKey(filterKey, pluginId, filterId)) {
        return {
            false,
            false,
            QObject::tr("Invalid filter key: %1").arg(filterKey)
        };
    }

    const MeshFilterPlugin *targetPlugin = nullptr;
    const MeshFilterDescriptor *targetDescriptor = nullptr;
    std::vector<MeshFilterDescriptor> descriptors;
    for (const auto &plugin : m_plugins) {
        if (plugin->pluginId() != pluginId)
            continue;
        targetPlugin = plugin.get();
        descriptors = plugin->filters(doc);
        targetDescriptor = findDescriptorById(descriptors, filterId);
        break;
    }

    if (!targetPlugin || !targetDescriptor) {
        return {
            false,
            false,
            QObject::tr("Filter not found: %1").arg(filterKey)
        };
    }

    QString domainError;
    if (!validateDomain(*targetDescriptor, doc, domainError)) {
        return {
            false,
            false,
            domainError
        };
    }

    MeshFilterParameterValues normalizedParameters;
    QString parameterError;
    if (!normalizeAndValidateParameters(
            *targetDescriptor,
            parameters,
            normalizedParameters,
            parameterError)) {
        return {
            false,
            false,
            parameterError
        };
    }

    const bool wrapUndo = (targetDescriptor->outputDomain != MeshFilterOutputDomain::Information);
    if (wrapUndo)
        doc.beginUndoStep(targetDescriptor->name);

    // Framework-level incremental selection: save the current face/vertex selection
    // bits before running the filter, then OR them back afterwards.
    const bool saveSelection =
        targetDescriptor->incrementalSelection
        && FilterParams(normalizedParameters).getBool(
               QStringLiteral("incremental_selection"));

    std::vector<bool> savedFaceSel;
    std::vector<bool> savedVertSel;
    if (saveSelection) {
        const int meshIdx = doc.currentMeshIndex();
        if (meshIdx >= 0 && meshIdx < doc.meshCount()) {
            const VCGMesh &m = doc.mesh(meshIdx).mesh;
            savedFaceSel.reserve(m.face.size());
            for (const VCGFace &f : m.face)
                savedFaceSel.push_back(!f.IsD() && f.IsS());
            savedVertSel.reserve(m.vert.size());
            for (const VCGVertex &v : m.vert)
                savedVertSel.push_back(!v.IsD() && v.IsS());
        }
    }

    const FilterParams typedParams(normalizedParameters);
    MeshFilterRunResult result = targetPlugin->runFilter(filterId, typedParams, doc);
    if (!result.success) {
        if (wrapUndo)
            doc.endUndoStep(false, true);
        return result;
    }

    // OR back the previously saved selection (if incremental was requested).
    if (saveSelection) {
        const int meshIdx = doc.currentMeshIndex();
        if (meshIdx >= 0 && meshIdx < doc.meshCount()) {
            VCGMesh &m = doc.mesh(meshIdx).mesh;
            for (size_t i = 0; i < savedFaceSel.size() && i < m.face.size(); ++i) {
                if (savedFaceSel[i] && !m.face[i].IsD())
                    m.face[i].SetS();
            }
            for (size_t i = 0; i < savedVertSel.size() && i < m.vert.size(); ++i) {
                if (savedVertSel[i] && !m.vert[i].IsD())
                    m.vert[i].SetS();
            }
        }
    }

    if (wrapUndo)
        doc.endUndoStep(result.documentModified);

    for (const QString &line : result.infoMessages) {
        if (!line.isEmpty()) {
            doc.writeLog(
                QObject::tr("Filter '%1': %2").arg(targetDescriptor->name, line),
                Document::LogSource::Application);
        }
    }

    return result;
}

QString MeshFilterPluginManager::buildFilterKey(const QString &pluginId, const QString &filterId)
{
    return pluginId + kKeySeparator + filterId;
}

bool MeshFilterPluginManager::splitFilterKey(
    const QString &filterKey,
    QString &pluginId,
    QString &filterId)
{
    const int sep = filterKey.indexOf(kKeySeparator);
    if (sep <= 0)
        return false;
    const int sepLen = int(kKeySeparator.size());
    if (sep + sepLen >= filterKey.size())
        return false;

    pluginId = filterKey.left(sep).trimmed();
    filterId = filterKey.mid(sep + sepLen).trimmed();
    return !pluginId.isEmpty() && !filterId.isEmpty();
}

bool MeshFilterPluginManager::validateDomain(
    const MeshFilterDescriptor &descriptor,
    const Document &doc,
    QString &errorMessage) const
{
    if (descriptor.inputDomain == MeshFilterInputDomain::None)
        return true;

    if (descriptor.inputDomain == MeshFilterInputDomain::WholeDocument) {
        if (doc.meshCount() <= 0) {
            errorMessage = QObject::tr("Filter '%1' requires a non-empty document.")
                               .arg(descriptor.name);
            return false;
        }
        return true;
    }

    const int meshIndex = doc.currentMeshIndex();
    if (meshIndex < 0 || meshIndex >= doc.meshCount()) {
        errorMessage = QObject::tr("Filter '%1' requires a current mesh.")
                           .arg(descriptor.name);
        return false;
    }

    const auto &meshEntry = doc.mesh(meshIndex);
    const auto &req = descriptor.inputRequirements;
    using Mask = vcg::tri::io::Mask;

    auto fail = [&](const QString &msg) {
        errorMessage = msg;
        return false;
    };

    if (req.requireVertices && meshEntry.mesh.VN() <= 0)
        return fail(QObject::tr("Filter '%1' requires vertices.").arg(descriptor.name));
    if (req.requireEdges && meshEntry.mesh.EN() <= 0)
        return fail(QObject::tr("Filter '%1' requires edges.").arg(descriptor.name));
    if (req.requireFaces && meshEntry.mesh.FN() <= 0)
        return fail(QObject::tr("Filter '%1' requires faces.").arg(descriptor.name));
    if (req.requireVertexColor && (meshEntry.ioMask & Mask::IOM_VERTCOLOR) == 0)
        return fail(QObject::tr("Filter '%1' requires vertex color.").arg(descriptor.name));
    if (req.requireFaceColor && (meshEntry.ioMask & Mask::IOM_FACECOLOR) == 0)
        return fail(QObject::tr("Filter '%1' requires face color.").arg(descriptor.name));
    if (req.requireTextureCoordinates) {
        const bool hasTex = (meshEntry.ioMask & (Mask::IOM_WEDGTEXCOORD | Mask::IOM_VERTTEXCOORD)) != 0;
        if (!hasTex) {
            return fail(
                QObject::tr("Filter '%1' requires texture coordinates.")
                    .arg(descriptor.name));
        }
    }
    if (req.requireTextures && meshEntry.textureFilePaths.isEmpty())
        return fail(QObject::tr("Filter '%1' requires textures.").arg(descriptor.name));
    if (req.requireVertexQuality && (meshEntry.ioMask & Mask::IOM_VERTQUALITY) == 0)
        return fail(QObject::tr("Filter '%1' requires vertex quality.").arg(descriptor.name));
    if (req.requireFaceQuality && (meshEntry.ioMask & Mask::IOM_FACEQUALITY) == 0)
        return fail(QObject::tr("Filter '%1' requires face quality.").arg(descriptor.name));

    return true;
}

bool MeshFilterPluginManager::normalizeAndValidateParameters(
    const MeshFilterDescriptor &descriptor,
    const MeshFilterParameterValues &inputParameters,
    MeshFilterParameterValues &normalizedParameters,
    QString &errorMessage) const
{
    normalizedParameters.clear();
    QSet<QString> knownKeys;
    knownKeys.reserve(static_cast<qsizetype>(descriptor.parameters.size()));

    for (const MeshFilterParameterDescriptor &parameter : descriptor.parameters) {
        const QString parameterId = parameter.id.trimmed();
        if (parameterId.isEmpty())
            continue;
        knownKeys.insert(parameterId);

        const QVariant rawValue = inputParameters.contains(parameterId)
            ? inputParameters.value(parameterId)
            : defaultValueForParameter(parameter);

        QVariant convertedValue;
        if (!convertParameterValue(parameter, rawValue, convertedValue, errorMessage))
            return false;

        normalizedParameters.insert(parameterId, convertedValue);
    }

    for (auto it = inputParameters.constBegin(); it != inputParameters.constEnd(); ++it) {
        if (!knownKeys.contains(it.key())) {
            errorMessage = QObject::tr("Unknown parameter '%1' for filter '%2'.")
                               .arg(it.key(), descriptor.name);
            return false;
        }
    }

    return true;
}

bool MeshFilterPluginManager::convertParameterValue(
    const MeshFilterParameterDescriptor &parameter,
    const QVariant &inputValue,
    QVariant &outputValue,
    QString &errorMessage)
{
    const QString prefix = parameterErrorPrefix(parameter);

    switch (parameter.type) {
    case MeshFilterParameterType::Bool: {
        bool value = false;
        if (!parseBoolFromVariant(inputValue, value)) {
            errorMessage = QObject::tr("%1 must be a boolean value.").arg(prefix);
            return false;
        }
        outputValue = value;
        return true;
    }
    case MeshFilterParameterType::Int:
    case MeshFilterParameterType::Mesh:
    case MeshFilterParameterType::TextureRef: {
        bool ok = false;
        const qlonglong value = inputValue.toLongLong(&ok);
        if (!ok) {
            errorMessage = QObject::tr("%1 must be an integer value.").arg(prefix);
            return false;
        }
        if (parameter.minValue.isValid() && value < parameter.minValue.toLongLong()) {
            errorMessage = QObject::tr("%1 is below minimum (%2).")
                               .arg(prefix)
                               .arg(parameter.minValue.toLongLong());
            return false;
        }
        if (parameter.maxValue.isValid() && value > parameter.maxValue.toLongLong()) {
            errorMessage = QObject::tr("%1 is above maximum (%2).")
                               .arg(prefix)
                               .arg(parameter.maxValue.toLongLong());
            return false;
        }
        outputValue = static_cast<int>(value);
        return true;
    }
    case MeshFilterParameterType::TextureOutputRef: {
        QVariantMap map = inputValue.toMap();
        if (map.isEmpty()) {
            const QString path = appendSuffixIfMissing(
                inputValue.toString().trimmed(),
                parameter.fileDefaultSuffix.trimmed());
            if (path.isEmpty()) {
                errorMessage = QObject::tr("%1 requires either an existing texture choice or a new output file path.")
                                   .arg(prefix);
                return false;
            }
            map.insert(QStringLiteral("mode"), QStringLiteral("new"));
            map.insert(QStringLiteral("path"), path);
        }

        const QString mode = map.value(QStringLiteral("mode")).toString().trimmed().toLower();
        if (mode == QStringLiteral("existing")) {
            bool ok = false;
            const int oneBasedSlot = map.value(QStringLiteral("slot")).toInt(&ok);
            if (!ok || oneBasedSlot <= 0) {
                errorMessage = QObject::tr("%1 has an invalid existing texture selection.").arg(prefix);
                return false;
            }
            outputValue = QVariantMap{
                { QStringLiteral("mode"), QStringLiteral("existing") },
                { QStringLiteral("slot"), oneBasedSlot }
            };
            return true;
        }

        const QString path = appendSuffixIfMissing(
            map.value(QStringLiteral("path")).toString().trimmed(),
            parameter.fileDefaultSuffix.trimmed());
        if (path.isEmpty()) {
            errorMessage = QObject::tr("%1 requires a destination file path when creating a new texture.")
                               .arg(prefix);
            return false;
        }
        outputValue = QVariantMap{
            { QStringLiteral("mode"), QStringLiteral("new") },
            { QStringLiteral("path"), path }
        };
        return true;
    }
    case MeshFilterParameterType::Double:
    case MeshFilterParameterType::AbsPerc: {
        bool ok = false;
        const double value = inputValue.toDouble(&ok);
        if (!ok || !std::isfinite(value)) {
            errorMessage = QObject::tr("%1 must be a finite numeric value.").arg(prefix);
            return false;
        }
        if (parameter.minValue.isValid() && value < parameter.minValue.toDouble()) {
            errorMessage = QObject::tr("%1 is below minimum (%2).")
                               .arg(prefix)
                               .arg(parameter.minValue.toDouble());
            return false;
        }
        if (parameter.maxValue.isValid() && value > parameter.maxValue.toDouble()) {
            errorMessage = QObject::tr("%1 is above maximum (%2).")
                               .arg(prefix)
                               .arg(parameter.maxValue.toDouble());
            return false;
        }
        outputValue = value;
        return true;
    }
    case MeshFilterParameterType::String:
    case MeshFilterParameterType::FileOpen: {
        const QString value = inputValue.toString().trimmed();
        if (parameter.type == MeshFilterParameterType::FileOpen && !value.isEmpty()) {
            const QFileInfo info(value);
            if (!info.exists()) {
                errorMessage = QObject::tr("%1 does not exist.").arg(prefix);
                return false;
            }
            if (!info.isFile()) {
                errorMessage = QObject::tr("%1 must point to a file.").arg(prefix);
                return false;
            }
        }
        outputValue = value;
        return true;
    }
    case MeshFilterParameterType::FileSave:
        outputValue = appendSuffixIfMissing(
            inputValue.toString().trimmed(),
            parameter.fileDefaultSuffix.trimmed());
        return true;
    case MeshFilterParameterType::Enum: {
        const QString value = inputValue.toString();
        const bool valid =
            std::any_of(parameter.enumOptions.begin(),
                        parameter.enumOptions.end(),
                        [&value](const MeshFilterEnumOption &option) { return option.id == value; });
        if (!valid) {
            errorMessage = QObject::tr("%1 has invalid option '%2'.").arg(prefix, value);
            return false;
        }
        outputValue = value;
        return true;
    }
    case MeshFilterParameterType::Color: {
        QColor color;
        if (inputValue.userType() == QMetaType::QColor) {
            color = inputValue.value<QColor>();
        } else {
            color = QColor(inputValue.toString().trimmed());
        }
        if (!color.isValid()) {
            errorMessage = QObject::tr("%1 must be a valid color (e.g. #RRGGBB).").arg(prefix);
            return false;
        }
        outputValue = color;
        return true;
    }
    case MeshFilterParameterType::Point3f: {
        if (inputValue.userType() == QMetaType::QVector3D) {
            outputValue = inputValue;
            return true;
        }
        // Accept "x,y,z" string
        const QStringList parts = inputValue.toString().trimmed().split(QLatin1Char(','));
        if (parts.size() == 3) {
            bool okX = false, okY = false, okZ = false;
            const float x = parts[0].trimmed().toFloat(&okX);
            const float y = parts[1].trimmed().toFloat(&okY);
            const float z = parts[2].trimmed().toFloat(&okZ);
            if (okX && okY && okZ) {
                outputValue = QVariant::fromValue(QVector3D(x, y, z));
                return true;
            }
        }
        errorMessage = QObject::tr("%1 must be a 3D point (QVector3D or \"x,y,z\" string).").arg(prefix);
        return false;
    }
    }

    errorMessage = QObject::tr("%1 has unsupported parameter type.").arg(prefix);
    return false;
}
