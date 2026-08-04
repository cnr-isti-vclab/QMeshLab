#pragma once

#include "interactivetool.h"

// Selects (makes current) the layer whose surface is clicked. This is pure view
// state — like clicking a row in the layer list — so it commits nothing to the
// document and produces no undo/filter entry. Use Tab to suspend the tool when
// camera navigation is needed.
class SelectLayerTool final : public InteractiveTool
{
public:
    QString id() const override;
    QString name() const override;
    QString statusHint() const override;
    QString iconPath() const override;
    QCursor cursor() const override;
    bool mousePress(QMouseEvent *e) override;
    void onSurfacePicked(const SurfacePick &result) override;
};
