#pragma once

#include "document_undo_types.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

class Document;

class DocumentUndoManager
{
public:
    explicit DocumentUndoManager(Document &doc);

    void beginStep(const QString &label, const ScriptAction &scriptAction = {});
    void endStep(bool commit = true, bool restoreOnCancel = false);

    // Selection delta steps — skip full snapshot capture for selection-only changes.
    // An optional scriptAction is recorded on the node so selection filters stay
    // reproducible/scriptable despite using the lightweight delta storage.
    void beginDeltaStep(const QString &label, int meshIndex,
                        std::optional<ScriptAction> scriptAction = std::nullopt);
    void endDeltaStep();

    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;
    QString redoText() const;
    bool isRestoring() const { return m_restoringUndoRedo; }
    bool isStepActive() const { return m_undoStepActive; }
    void setSuppressUndo(bool suppress) { m_suppressUndo = suppress; }
    int undoLimit() const { return m_undoLimit; }
    void setUndoLimit(int limit);

    QStringList undoHistoryLabels() const;
    QStringList undoStackLabels() const;
    int undoCursorPosition() const;
    int currentNodeId() const { return m_undoCurrentNode; }
    std::vector<UndoTreeNodeInfo> undoTreeInfo() const;
    std::optional<ScriptAction> nodeScriptAction(int nodeId) const;

    bool jumpToNode(int nodeId, bool restoreCamera = true);
    bool updateNodeCamera(int nodeId, const ViewState &viewState);
    bool makeRoot(int nodeId);
    bool purgeBranch(int nodeId);
    bool linearizeHistory();
    bool undo();
    bool redo();
    void clear();

    UndoMemoryStats memoryStats() const;

    bool restoreCamera() const { return m_restoreCamera; }
    bool suppressSignals() const { return m_suppressUndoRedoSignals; }
    void setRestoring(bool restoring) { m_restoringUndoRedo = restoring; }
    void emitStateChanged();

    // Key: (meshId, geometryRevision, selectionRevision) — the full content
    // identity of a mesh, so selection-only changes still get their own interned
    // copy even though they no longer bump geometryRevision.
    std::map<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>, std::weak_ptr<const VCGMesh>> &geometryCache()
    {
        return m_undoGeometryCache;
    }

    const std::vector<UndoNode> &nodes() const { return m_undoNodes; }
    int currentNode() const { return m_undoCurrentNode; }

private:
    void pushStep(
        const QString &label,
        UndoState &&before,
        UndoState &&after,
        std::optional<ScriptAction> scriptAction = {});
    void pushDeltaStep(
        const QString &label,
        SelectionDelta &&before,
        SelectionDelta &&after,
        std::optional<ScriptAction> scriptAction = {});
    void pruneTreeToLimit();

    Document &m_doc;
    std::map<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>, std::weak_ptr<const VCGMesh>> m_undoGeometryCache;
    std::vector<UndoNode> m_undoNodes;
    int m_undoCurrentNode = -1;
    int m_undoLimit = 20;
    bool m_undoStepActive = false;
    QString m_undoStepLabel;
    std::optional<ScriptAction> m_pendingScriptAction;
    std::optional<UndoState> m_pendingUndoBefore;
    std::optional<SelectionDelta> m_pendingDeltaBefore;
    std::optional<int> m_pendingDeltaMeshIndex;
    bool m_restoringUndoRedo = false;
    bool m_suppressUndo = false;
    bool m_suppressUndoRedoSignals = false;
    bool m_restoreCamera = true;
};
