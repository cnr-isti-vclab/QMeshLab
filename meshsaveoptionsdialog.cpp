#include "meshsaveoptionsdialog.h"

#include <wrap/io_trimesh/io_mask.h>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QLabel>
#include <QVBoxLayout>

namespace {
struct OptionalMaskItem {
    int bit = 0;
    const char *label = nullptr;
};

const OptionalMaskItem kOptionalMaskItems[] = {
    { vcg::tri::io::Mask::IOM_VERTFLAGS, QT_TR_NOOP("Vertex Flags") },
    { vcg::tri::io::Mask::IOM_VERTCOLOR, QT_TR_NOOP("Vertex Color") },
    { vcg::tri::io::Mask::IOM_VERTQUALITY, QT_TR_NOOP("Vertex Quality") },
    { vcg::tri::io::Mask::IOM_VERTNORMAL, QT_TR_NOOP("Vertex Normal") },
    { vcg::tri::io::Mask::IOM_VERTTEXCOORD, QT_TR_NOOP("Vertex Texcoord") },
    { vcg::tri::io::Mask::IOM_VERTRADIUS, QT_TR_NOOP("Vertex Radius") },
    { vcg::tri::io::Mask::IOM_FACEFLAGS, QT_TR_NOOP("Face Flags") },
    { vcg::tri::io::Mask::IOM_FACECOLOR, QT_TR_NOOP("Face Color") },
    { vcg::tri::io::Mask::IOM_FACEQUALITY, QT_TR_NOOP("Face Quality") },
    { vcg::tri::io::Mask::IOM_FACENORMAL, QT_TR_NOOP("Face Normal") },
    { vcg::tri::io::Mask::IOM_WEDGCOLOR, QT_TR_NOOP("Wedge Color") },
    { vcg::tri::io::Mask::IOM_WEDGTEXCOORD, QT_TR_NOOP("Wedge Texcoord") },
    { vcg::tri::io::Mask::IOM_WEDGTEXMULTI, QT_TR_NOOP("Wedge Multi Texcoord") },
    { vcg::tri::io::Mask::IOM_WEDGNORMAL, QT_TR_NOOP("Wedge Normal") },
    { vcg::tri::io::Mask::IOM_CAMERA, QT_TR_NOOP("Camera") },
    { vcg::tri::io::Mask::IOM_BITPOLYGONAL, QT_TR_NOOP("Polygonal Faces") },
};
}

MeshSaveOptionsDialog::MeshSaveOptionsDialog(
    const QString &targetFileName,
    int capabilityMask,
    int availableMask,
    int requiredMask,
    const MeshIOSaveOptions &initialOptions,
    bool binarySupported,
    bool supportsEmbeddedTextures,
    QWidget *parent)
    : QDialog(parent)
    , m_requiredMask(requiredMask)
    , m_binarySupported(binarySupported)
    , m_supportsEmbeddedTextures(supportsEmbeddedTextures)
{
    setWindowTitle(tr("Save Mesh Options"));

    auto *layout = new QVBoxLayout(this);

    const QString fileName = QFileInfo(targetFileName).fileName();
    auto *caption = new QLabel(
        tr("Choose which attributes to save for <b>%1</b>.").arg(fileName.isEmpty() ? targetFileName : fileName),
        this);
    caption->setWordWrap(true);
    layout->addWidget(caption);

    if ((requiredMask & vcg::tri::io::Mask::IOM_VERTCOORD) != 0) {
        auto *geometryLabel = new QLabel(
            tr("Geometry is always saved."),
            this);
        geometryLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
        layout->addWidget(geometryLabel);
    }

    bool hasOptionalItems = false;
    const int initialMask = initialOptions.mask;
    for (const OptionalMaskItem &item : kOptionalMaskItems) {
        if ((capabilityMask & item.bit) == 0)
            continue;
        if ((requiredMask & item.bit) != 0)
            continue;

        hasOptionalItems = true;

        auto *check = new QCheckBox(tr(item.label), this);
        const bool available = (availableMask & item.bit) != 0;
        const bool enabled = available;
        check->setEnabled(enabled);

        bool checked = (initialMask & item.bit) != 0;
        if (!available)
            checked = false;
        check->setChecked(checked);

        if (!available)
            check->setToolTip(tr("Not available in the current mesh data."));

        m_maskCheckBoxes.append({ item.bit, check });
        layout->addWidget(check);
    }

    if (!hasOptionalItems) {
        auto *noneLabel = new QLabel(tr("No optional attributes for this format."), this);
        noneLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
        layout->addWidget(noneLabel);
    }

    m_binaryCheckBox = new QCheckBox(tr("Binary format"), this);
    m_binaryCheckBox->setEnabled(m_binarySupported);
    m_binaryCheckBox->setChecked(m_binarySupported ? initialOptions.binary : false);
    if (!m_binarySupported)
        m_binaryCheckBox->setToolTip(tr("This format is always saved as text."));
    layout->addWidget(m_binaryCheckBox);

    m_embedTexturesCheckBox = new QCheckBox(tr("Embed textures"), this);
    m_embedTexturesCheckBox->setVisible(m_supportsEmbeddedTextures);
    m_embedTexturesCheckBox->setChecked(m_supportsEmbeddedTextures ? initialOptions.embedTextures : false);
    if (m_supportsEmbeddedTextures) {
        m_embedTexturesCheckBox->setToolTip(
            tr("When enabled, texture image data is embedded in the output file."));
        layout->addWidget(m_embedTexturesCheckBox);
    }

    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

MeshIOSaveOptions MeshSaveOptionsDialog::selectedOptions() const
{
    MeshIOSaveOptions options;
    int mask = m_requiredMask;
    for (const auto &entry : m_maskCheckBoxes) {
        const int bit = entry.first;
        const QCheckBox *check = entry.second;
        if (check && check->isChecked())
            mask |= bit;
    }
    options.mask = mask;
    options.binary = m_binarySupported && m_binaryCheckBox && m_binaryCheckBox->isChecked();
    options.embedTextures =
        m_supportsEmbeddedTextures && m_embedTexturesCheckBox && m_embedTexturesCheckBox->isChecked();
    return options;
}
