#include "plugins/io_e57/e57importplugin.h"

#include "meshioplugin.h"
#include "meshiopluginmanager.h"

#include <QFileInfo>
#include <QObject>
#include <memory>

#include <E57SimpleReader.h>
#include <vcg/complex/allocate.h>

namespace {
class E57ImportPlugin final : public MeshIOPlugin
{
public:
    QString name() const override
    {
        return QObject::tr("E57 Importer");
    }

    bool canLoad(const QString &filename) const override
    {
        return QFileInfo(filename).suffix().compare(QStringLiteral("e57"), Qt::CaseInsensitive) == 0;
    }

    int load(const QString &filename, VCGMesh &mesh, vcg::CallBackPos *cb) const override
    {
        e57::Reader reader(filename.toStdString(), {});
        if (!reader.IsOpen())
            return -1;

        const int64_t scanCount = reader.GetData3DCount();
        if (scanCount <= 0) {
            reader.Close();
            return -2;
        }

        mesh.Clear();

        for (int64_t scanIndex = 0; scanIndex < scanCount; ++scanIndex) {
            e57::Data3D scanHeader{};
            if (!reader.ReadData3D(scanIndex, scanHeader)) {
                reader.Close();
                return -3;
            }

            int64_t rowCount = 0, colCount = 0, pointCount = 0, groupCount = 0, countSize = 0;
            bool columnIndex = false;
            if (!reader.GetData3DSizes(scanIndex, rowCount, colCount, pointCount, groupCount, countSize, columnIndex)) {
                reader.Close();
                return -4;
            }

            if (pointCount <= 0)
                continue;

            std::vector<double> cartesianX(static_cast<size_t>(pointCount));
            std::vector<double> cartesianY(static_cast<size_t>(pointCount));
            std::vector<double> cartesianZ(static_cast<size_t>(pointCount));
            std::vector<int8_t> cartesianInvalid(scanHeader.pointFields.cartesianInvalidStateField
                ? static_cast<size_t>(pointCount)
                : 0);

            e57::Data3DPointsData_t<double> pointsData{};
            pointsData.cartesianX = cartesianX.data();
            pointsData.cartesianY = cartesianY.data();
            pointsData.cartesianZ = cartesianZ.data();
            if (scanHeader.pointFields.cartesianInvalidStateField)
                pointsData.cartesianInvalidState = cartesianInvalid.data();

            e57::CompressedVectorReader dataReader = reader.SetUpData3DPointsData(scanIndex, pointCount, pointsData);

            size_t readCount = 0;
            while ((readCount = dataReader.read()) > 0) {
                for (size_t i = 0; i < readCount; ++i) {
                    if (!cartesianInvalid.empty() && cartesianInvalid[i] != 0)
                        continue;

                    vcg::tri::Allocator<VCGMesh>::AddVertex(mesh, VCGMesh::CoordType(
                        static_cast<float>(cartesianX[i]),
                        static_cast<float>(cartesianY[i]),
                        static_cast<float>(cartesianZ[i])));
                }
            }
            dataReader.close();

            if (cb != nullptr) {
                const int progress = int((scanIndex + 1) * 100 / scanCount);
                cb(progress, "Loading E57 points\r");
            }
        }

        reader.Close();
        return 0;
    }

    QString filterString() const override
    {
        return QObject::tr("E57 Files (*.e57)");
    }

    QString errorString(int errCode) const override
    {
        switch (errCode) {
        case -1: return QObject::tr("Cannot open E57 file.");
        case -2: return QObject::tr("No point clouds found in E57 file.");
        case -3: return QObject::tr("Failed to read E57 scan metadata.");
        case -4: return QObject::tr("Failed to read E57 scan sizes.");
        default: return QObject::tr("Unknown E57 import error.");
        }
    }
};
}

void registerE57ImportPlugin(MeshIOPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<E57ImportPlugin>());
}
