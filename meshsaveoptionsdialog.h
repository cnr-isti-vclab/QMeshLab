#pragma once

#include "meshioplugin.h"

#include <QDialog>
#include <QList>
#include <utility>

class QCheckBox;
class QSpinBox;

class MeshSaveOptionsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MeshSaveOptionsDialog(
        const QString &targetFileName,
        int capabilityMask,
        int availableMask,
        int requiredMask,
        const MeshIOSaveOptions &initialOptions,
        bool binarySupported,
        bool supportsEmbeddedTextures = false,
        bool supportsCopyAssociatedTextures = false,
        bool supportsDracoCompression = false,
        QWidget *parent = nullptr);

    MeshIOSaveOptions selectedOptions() const;

private:
    QList<std::pair<int, QCheckBox *>> m_maskCheckBoxes;
    QCheckBox *m_binaryCheckBox = nullptr;
    QCheckBox *m_embedTexturesCheckBox = nullptr;
    QCheckBox *m_copyAssociatedTexturesCheckBox = nullptr;
    QCheckBox *m_dracoCompressionCheckBox = nullptr;
    QSpinBox *m_dracoCompressionLevelSpinBox = nullptr;
    int m_requiredMask = 0;
    bool m_binarySupported = false;
    bool m_supportsEmbeddedTextures = false;
    bool m_supportsCopyAssociatedTextures = false;
    bool m_supportsDracoCompression = false;
};
