#include "mlgui.h"

#include "../render/renderwidget.h"

#include <nanobind/nanobind.h>

#include <QImage>

namespace nb = nanobind;

MlGui::MlGui(RenderWidget *view)
    : m_view(view)
{
}

void MlGui::setRenderWidget(RenderWidget *view)
{
    m_view = view;
}

// -----------------------------------------------------------------------
// JSON state access
// -----------------------------------------------------------------------

std::string MlGui::cameraStateJson() const
{
    if (!m_view)
        return {};
    return m_view->cameraStateJson().toStdString();
}

std::string MlGui::renderStateJson() const
{
    if (!m_view)
        return {};
    return m_view->renderStateJson().toStdString();
}

bool MlGui::applyCameraStateJson(const std::string &json, std::string *error)
{
    if (!m_view) {
        if (error) *error = "No active view";
        return false;
    }
    QString err;
    const bool ok = m_view->applyCameraStateJson(QString::fromStdString(json), &err);
    if (!ok && error)
        *error = err.toStdString();
    return ok;
}

bool MlGui::applyRenderStateJson(const std::string &json, std::string *error)
{
    if (!m_view) {
        if (error) *error = "No active view";
        return false;
    }
    QString err;
    const bool ok = m_view->applyRenderStateJson(QString::fromStdString(json), &err);
    if (!ok && error)
        *error = err.toStdString();
    return ok;
}

// -----------------------------------------------------------------------
// Offscreen render using the live RenderWidget
// -----------------------------------------------------------------------

nb::bytes MlGui::renderSnapshot(const std::string &renderStateJson,
                                int width, int height)
{
    if (!m_view)
        throw std::runtime_error("No active view");
    if (width <= 0 || height <= 0)
        throw std::runtime_error("Invalid output size");

    // Save current state
    const QString previousRenderState = m_view->renderStateJson();

    // Apply requested state
    const QString stateJson = QString::fromStdString(renderStateJson);
    QString err;
    if (!m_view->applyRenderStateJson(stateJson, &err)) {
        throw std::runtime_error("applyRenderStateJson failed: " + err.toStdString());
    }

    // Render
    QString captureError;
    const QSize outSize(width, height);
    QImage image = m_view->renderOffscreenToImage(outSize, false, &captureError);

    // Restore
    QString restoreError;
    if (!m_view->applyRenderStateJson(previousRenderState, &restoreError)) {
        // Non-fatal: log but continue
    }

    if (image.isNull())
        throw std::runtime_error("Render failed: " +
            (captureError.isEmpty() ? std::string("grabFramebuffer returned null")
                                     : captureError.toStdString()));

    if (image.format() != QImage::Format_RGBA8888)
        image = image.convertToFormat(QImage::Format_RGBA8888);

    const size_t byteCount = size_t(image.sizeInBytes());
    return nb::bytes(
        reinterpret_cast<const char *>(image.constBits()),
        byteCount);
}

void MlGui::saveSnapshot(const std::string &path,
                          int width, int height,
                          const std::string &renderStateJson)
{
    if (!m_view)
        throw std::runtime_error("No active view");
    if (width <= 0 || height <= 0)
        throw std::runtime_error("Invalid output size");

    QImage image;
    if (!renderStateJson.empty()) {
        // Save current state, apply JSON, render, restore
        const QString previous = m_view->renderStateJson();
        QString err;
        if (!m_view->applyRenderStateJson(QString::fromStdString(renderStateJson), &err))
            throw std::runtime_error("applyRenderStateJson failed: " + err.toStdString());
        QString capErr;
        image = m_view->renderOffscreenToImage(QSize(width, height), false, &capErr);
        m_view->applyRenderStateJson(previous);  // best-effort restore
    } else {
        // Use current view state
        QString capErr;
        image = m_view->renderOffscreenToImage(QSize(width, height), false, &capErr);
    }

    if (image.isNull())
        throw std::runtime_error("Render failed");

    const bool saved = image.save(QString::fromStdString(path), "PNG");
    if (!saved)
        throw std::runtime_error("Failed to save PNG: " + path);
}
