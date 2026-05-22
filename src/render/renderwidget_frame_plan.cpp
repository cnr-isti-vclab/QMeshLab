#include "renderwidget.h"
#include "document.h"

using namespace RenderWidgetInternal;

namespace {

struct RenderFramePassRequests {
    bool fill = false;
    bool wire = false;
    bool edges = false;
    bool boundingBox = false;
    bool points = false;
    bool selection = false;
    bool decorators = false;

    bool hasSimpleBufferRequests() const
    {
        return wire || edges || boundingBox || points;
    }
};

bool requestsSelectionPass(const PerMeshRenderSettings &settings)
{
    return settings.showSelection
        && (settings.showSelectionVertices || settings.showSelectionFaces);
}

bool requestsDecoratorPass(const PerMeshRenderSettings &settings)
{
    return settings.decoratorVertexNormals
        || settings.decoratorFaceNormals
        || settings.decoratorCurvatureDir
        || settings.decoratorBoundaryEdges
        || settings.decoratorTextureSeams
        || settings.decoratorNonManifoldEdges
        || settings.decoratorNonManifoldVertices;
}

} // namespace

RenderWidget::RenderFramePlan RenderWidget::buildRenderFramePlan(
    const QSize &pixelSize,
    const QMatrix4x4 &proj,
    const QMatrix4x4 &view,
    const QVector3D &lightDir)
{
    RenderFramePlan plan;
    plan.viewMode = m_viewMode;
    plan.pixelSize = pixelSize;
    plan.proj = proj;
    plan.view = view;
    plan.lightDir = lightDir;

    RenderFramePassRequests requests;
    for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
        if (!meshVisible(mi))
            continue;
        const PerMeshRenderSettings meshSettings = renderModeForMesh(mi);
        requests.fill = requests.fill || meshSettings.showFill;
        requests.wire = requests.wire || meshSettings.showWire;
        requests.edges = requests.edges || meshSettings.showEdges;
        requests.boundingBox = requests.boundingBox || meshSettings.showBoundingBox;
        requests.points = requests.points || meshSettings.showPoints;
        requests.selection = requests.selection || requestsSelectionPass(meshSettings);
        requests.decorators = requests.decorators || requestsDecoratorPass(meshSettings);
    }

    if (requests.fill)
        plan.sceneFill = buildSceneFillFramePlan(pixelSize, proj, view, lightDir);

    auto appendBufferDrawItem =
        [](std::vector<SceneBufferDrawItem> &items,
           int meshIndex,
           QRhiGraphicsPipeline *pipeline,
           const PerMeshRenderSettings &meshSettings,
           QRhiBuffer *vertexBuffer,
           int vertexCount) {
            if (!pipeline || !vertexBuffer || vertexCount <= 0)
                return;
            items.push_back(SceneBufferDrawItem {
                meshIndex,
                pipeline,
                meshSettings,
                vertexBuffer,
                vertexCount
            });
        };
    auto appendDecoratorDrawItem =
        [](std::vector<SceneDecoratorDrawItem> &items,
           int meshIndex,
           int slot,
           SceneDecoratorDrawKind kind,
           const QColor &color,
           float width,
           QRhiBuffer *vertexBuffer,
           int vertexCount) {
            if (!vertexBuffer || vertexCount <= 0)
                return;
            items.push_back(SceneDecoratorDrawItem {
                meshIndex,
                slot,
                kind,
                color,
                width,
                vertexBuffer,
                vertexCount
            });
        };

    const bool buildSimpleBufferItems =
        requests.hasSimpleBufferRequests()
        && (requests.wire || requests.edges || (requests.boundingBox && m_bboxPipeline)
            || (requests.points && m_pointsPipeline));
    if (buildSimpleBufferItems) {
        for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
            if (!meshVisible(mi))
                continue;

            const PerMeshRenderSettings meshSettings = renderModeForMesh(mi);

            if (requests.wire && meshSettings.showWire) {
                const MeshGpuResourceCache::WirePassView wireView =
                    m_doc->wirePassGpuView(m_rhi, mi);
                if (wireView.valid) {
                    appendBufferDrawItem(
                        plan.wireItems,
                        mi,
                        wirePipelineForSettings(meshSettings),
                        meshSettings,
                        wireView.vertexBuffer,
                        wireView.vertexCount);
                }
            }

            if (requests.edges && meshSettings.showEdges) {
                bool edgeItemAppended = false;
                const MeshGpuResourceCache::EdgeFatPassView fatView =
                    m_doc->edgeFatPassGpuView(m_rhi, mi);
                if (fatView.valid) {
                    const size_t before = plan.edgeItems.size();
                    appendBufferDrawItem(
                        plan.edgeItems,
                        mi,
                        fatEdgesPipelineForSettings(meshSettings),
                        meshSettings,
                        fatView.vertexBuffer,
                        fatView.vertexCount);
                    edgeItemAppended = (plan.edgeItems.size() != before);
                }

                if (!edgeItemAppended) {
                    const MeshGpuResourceCache::EdgePassView lineView =
                        m_doc->edgePassGpuView(m_rhi, mi);
                    if (lineView.valid) {
                        appendBufferDrawItem(
                            plan.edgeItems,
                            mi,
                            edgesPipelineForSettings(meshSettings),
                            meshSettings,
                            lineView.vertexBuffer,
                            lineView.vertexCount);
                    }
                }
            }

            if (requests.boundingBox && m_bboxPipeline && meshSettings.showBoundingBox) {
                const MeshGpuResourceCache::BBoxPassView bboxView =
                    m_doc->bboxPassGpuView(m_rhi, mi);
                if (bboxView.valid) {
                    appendBufferDrawItem(
                        plan.boundingBoxItems,
                        mi,
                        m_bboxPipeline.get(),
                        meshSettings,
                        bboxView.vertexBuffer,
                        bboxView.vertexCount);
                }
            }

            if (requests.points && m_pointsPipeline && meshSettings.showPoints) {
                const auto pointVariant = static_cast<Document::PointGpuVariant>(
                    pointGpuVariantIndexForSettings(meshSettings));
                const MeshGpuResourceCache::PointsPassView pointsView =
                    m_doc->pointsPassGpuView(m_rhi, mi, pointVariant);
                if (pointsView.valid) {
                    appendBufferDrawItem(
                        plan.pointItems,
                        mi,
                        m_pointsPipeline.get(),
                        meshSettings,
                        pointsView.vertexBuffer,
                        pointsView.vertexCount);
                }
            }
        }
    }

    if (requests.decorators) {
        auto canDrawLineDecoratorSlot = [&](int slot) {
            return m_decoratorPipeline
                && slot >= 0
                && slot < kDecoratorSlotCount
                && m_decoratorUbufs[slot]
                && m_decoratorSrbs[slot];
        };

        auto appendLineDecoratorItems =
            [&](int slot, auto shouldDraw, auto colorGetter, auto bufferGetter, auto countGetter) {
                if (!canDrawLineDecoratorSlot(slot))
                    return;
                for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
                    if (!meshVisible(mi))
                        continue;
                    const PerMeshRenderSettings meshSettings = renderModeForMesh(mi);
                    if (!shouldDraw(meshSettings))
                        continue;

                    const MeshGpuResourceCache::DecoratorPassView decoratorView =
                        m_doc->decoratorPassGpuView(m_rhi, mi);
                    if (!decoratorView.valid)
                        continue;

                    appendDecoratorDrawItem(
                        plan.decoratorItems,
                        mi,
                        slot,
                        SceneDecoratorDrawKind::Line,
                        colorGetter(meshSettings),
                        1.0f,
                        bufferGetter(decoratorView),
                        countGetter(decoratorView));
                }
            };

        auto appendFatOrLineDecoratorItems =
            [&](int slot,
                auto shouldDraw,
                auto colorGetter,
                auto fatBufferGetter,
                auto fatCountGetter,
                auto lineBufferGetter,
                auto lineCountGetter) {
                const bool canDrawFatDecorator =
                    m_decoratorFatPipeline && m_decoratorFatUbuf && m_decoratorFatSrb;
                for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
                    if (!meshVisible(mi))
                        continue;
                    const PerMeshRenderSettings meshSettings = renderModeForMesh(mi);
                    if (!shouldDraw(meshSettings))
                        continue;

                    const MeshGpuResourceCache::DecoratorPassView decoratorView =
                        m_doc->decoratorPassGpuView(m_rhi, mi);
                    if (!decoratorView.valid)
                        continue;

                    const QColor color = colorGetter(meshSettings);
                    const float width = qMax(0.5f, meshSettings.decoratorBoundaryWidth);
                    if (canDrawFatDecorator) {
                        const size_t before = plan.decoratorItems.size();
                        appendDecoratorDrawItem(
                            plan.decoratorItems,
                            mi,
                            slot,
                            SceneDecoratorDrawKind::FatLine,
                            color,
                            width,
                            fatBufferGetter(decoratorView),
                            fatCountGetter(decoratorView));
                        if (plan.decoratorItems.size() != before)
                            continue;
                    }

                    if (!canDrawLineDecoratorSlot(slot))
                        continue;
                    appendDecoratorDrawItem(
                        plan.decoratorItems,
                        mi,
                        slot,
                        SceneDecoratorDrawKind::Line,
                        color,
                        width,
                        lineBufferGetter(decoratorView),
                        lineCountGetter(decoratorView));
                }
            };

        appendLineDecoratorItems(
            kDecoratorSlotVertexNormals,
            [](const PerMeshRenderSettings &settings) {
                return settings.decoratorVertexNormals;
            },
            [](const PerMeshRenderSettings &settings) {
                return settings.decoratorVertexNormalColor;
            },
            [](const MeshGpuResourceCache::DecoratorPassView &view) {
                return view.vertexNormalsBuffer;
            },
            [](const MeshGpuResourceCache::DecoratorPassView &view) {
                return view.vertexNormalsVertexCount;
            });
        appendLineDecoratorItems(
            kDecoratorSlotFaceNormals,
            [](const PerMeshRenderSettings &settings) {
                return settings.decoratorFaceNormals;
            },
            [](const PerMeshRenderSettings &settings) {
                return settings.decoratorFaceNormalColor;
            },
            [](const MeshGpuResourceCache::DecoratorPassView &view) {
                return view.faceNormalsBuffer;
            },
            [](const MeshGpuResourceCache::DecoratorPassView &view) {
                return view.faceNormalsVertexCount;
            });
        appendLineDecoratorItems(
            kDecoratorSlotCurvaturePD1,
            [](const PerMeshRenderSettings &settings) {
                return settings.decoratorCurvatureDir;
            },
            [](const PerMeshRenderSettings &settings) {
                return settings.decoratorCurvatureDirPD1Color;
            },
            [](const MeshGpuResourceCache::DecoratorPassView &view) {
                return view.curvatureDirPD1Buffer;
            },
            [](const MeshGpuResourceCache::DecoratorPassView &view) {
                return view.curvatureDirPD1VertexCount;
            });
        appendLineDecoratorItems(
            kDecoratorSlotCurvaturePD2,
            [](const PerMeshRenderSettings &settings) {
                return settings.decoratorCurvatureDir;
            },
            [](const PerMeshRenderSettings &settings) {
                return settings.decoratorCurvatureDirPD2Color;
            },
            [](const MeshGpuResourceCache::DecoratorPassView &view) {
                return view.curvatureDirPD2Buffer;
            },
            [](const MeshGpuResourceCache::DecoratorPassView &view) {
                return view.curvatureDirPD2VertexCount;
            });
        appendFatOrLineDecoratorItems(
            kDecoratorSlotBoundaryEdges,
            [](const PerMeshRenderSettings &settings) {
                return settings.decoratorBoundaryEdges;
            },
            [](const PerMeshRenderSettings &settings) {
                return settings.decoratorBoundaryEdgeColor;
            },
            [](const MeshGpuResourceCache::DecoratorPassView &view) {
                return view.boundaryEdgesFatBuffer;
            },
            [](const MeshGpuResourceCache::DecoratorPassView &view) {
                return view.boundaryEdgesFatVertexCount;
            },
            [](const MeshGpuResourceCache::DecoratorPassView &view) {
                return view.boundaryEdgesBuffer;
            },
            [](const MeshGpuResourceCache::DecoratorPassView &view) {
                return view.boundaryEdgesVertexCount;
            });
        appendFatOrLineDecoratorItems(
            kDecoratorSlotTextureSeams,
            [](const PerMeshRenderSettings &settings) {
                return settings.decoratorTextureSeams;
            },
            [](const PerMeshRenderSettings &settings) {
                return settings.decoratorTextureSeamColor;
            },
            [](const MeshGpuResourceCache::DecoratorPassView &view) {
                return view.textureSeamsFatBuffer;
            },
            [](const MeshGpuResourceCache::DecoratorPassView &view) {
                return view.textureSeamsFatVertexCount;
            },
            [](const MeshGpuResourceCache::DecoratorPassView &view) {
                return view.textureSeamsBuffer;
            },
            [](const MeshGpuResourceCache::DecoratorPassView &view) {
                return view.textureSeamsVertexCount;
            });
        appendFatOrLineDecoratorItems(
            kDecoratorSlotNonManifoldEdges,
            [](const PerMeshRenderSettings &settings) {
                return settings.decoratorNonManifoldEdges;
            },
            [](const PerMeshRenderSettings &settings) {
                return settings.decoratorNonManifoldEdgeColor;
            },
            [](const MeshGpuResourceCache::DecoratorPassView &view) {
                return view.nonManifoldEdgesFatBuffer;
            },
            [](const MeshGpuResourceCache::DecoratorPassView &view) {
                return view.nonManifoldEdgesFatVertexCount;
            },
            [](const MeshGpuResourceCache::DecoratorPassView &view) {
                return view.nonManifoldEdgesBuffer;
            },
            [](const MeshGpuResourceCache::DecoratorPassView &view) {
                return view.nonManifoldEdgesVertexCount;
            });

        const int nonManifoldVertexSlot = kDecoratorSlotNonManifoldVertices;
        if (canDrawLineDecoratorSlot(nonManifoldVertexSlot) && m_decoratorPointPipeline) {
            for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
                if (!meshVisible(mi))
                    continue;
                const PerMeshRenderSettings meshSettings = renderModeForMesh(mi);
                if (!meshSettings.decoratorNonManifoldVertices)
                    continue;

                const MeshGpuResourceCache::DecoratorPassView decoratorView =
                    m_doc->decoratorPassGpuView(m_rhi, mi);
                if (!decoratorView.valid)
                    continue;

                appendDecoratorDrawItem(
                    plan.decoratorItems,
                    mi,
                    nonManifoldVertexSlot,
                    SceneDecoratorDrawKind::Point,
                    meshSettings.decoratorNonManifoldVertexColor,
                    1.0f,
                    decoratorView.nonManifoldVerticesBuffer,
                    decoratorView.nonManifoldVerticesVertexCount);
            }
        }
    }

    if (requests.selection
        && m_selectionUbuf
        && m_selectionSrb
        && (m_selectionFacesPipeline || m_selectionVerticesPipeline)) {
        for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
            if (!meshVisible(mi))
                continue;

            const PerMeshRenderSettings meshSettings = renderModeForMesh(mi);
            if (!meshSettings.showSelection
                || (!meshSettings.showSelectionVertices && !meshSettings.showSelectionFaces)) {
                continue;
            }

            const MeshGpuResourceCache::SelectionPassView selectionView =
                m_doc->selectionPassGpuView(m_rhi, mi);
            if (!selectionView.valid)
                continue;

            const bool drawFaces =
                meshSettings.showSelectionFaces
                && m_selectionFacesPipeline
                && selectionView.selectedFacesBuffer
                && selectionView.selectedFacesVertexCount > 0;
            const bool drawVertices =
                meshSettings.showSelectionVertices
                && m_selectionVerticesPipeline
                && selectionView.selectedVerticesBuffer
                && selectionView.selectedVerticesVertexCount > 0;
            if (!drawFaces && !drawVertices)
                continue;

            plan.selectionItems.push_back(SceneSelectionDrawItem {
                mi,
                drawFaces,
                drawVertices,
                selectionView
            });
        }
    }

    return plan;
}
