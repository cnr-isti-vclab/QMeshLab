#pragma once

#include "document.h"

#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

class DocumentUndoManager
{
public:
    explicit DocumentUndoManager(Document &doc);

    void beginStep(const QString &label, const Document::ScriptAction &scriptAction = {});
    void endStep(bool commit = true, bool restoreOnCancel = false);

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
    std::vector<Document::UndoTreeNodeInfo> undoTreeInfo() const;
    std::optional<Document::ScriptAction> nodeScriptAction(int nodeId) const;

    bool jumpToNode(int nodeId, bool restoreCamera = true);
    bool updateNodeCamera(int nodeId, const ViewState &viewState);
    bool makeRoot(int nodeId);
    bool purgeBranch(int nodeId);
    bool linearizeHistory();
    bool undo();
    bool redo();
    void clear();

    Document::UndoMemoryStats memoryStats() const;

    bool restoreCamera() const { return m_restoreCamera; }
    bool suppressSignals() const { return m_suppressUndoRedoSignals; }
    void setRestoring(bool restoring) { m_restoringUndoRedo = restoring; }
    void emitStateChanged();

    std::map<std::pair<std::uint64_t, std::uint64_t>, std::weak_ptr<const VCGMesh>> &geometryCache()
    {
        return m_undoGeometryCache;
    }

    const std::vector<Document::UndoNode> &nodes() const { return m_undoNodes; }
    int currentNode() const { return m_undoCurrentNode; }

private:
    void pushStep(
        const QString &label,
        Document::UndoState &&before,
        Document::UndoState &&after,
        std::optional<Document::ScriptAction> scriptAction = {});
    void pruneTreeToLimit();

    Document &m_doc;
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::weak_ptr<const VCGMesh>> m_undoGeometryCache;
    std::vector<Document::UndoNode> m_undoNodes;
    int m_undoCurrentNode = -1;
    int m_undoLimit = 20;
    bool m_undoStepActive = false;
    QString m_undoStepLabel;
    std::optional<Document::ScriptAction> m_pendingScriptAction;
    std::optional<Document::UndoState> m_pendingUndoBefore;
    bool m_restoringUndoRedo = false;
    bool m_suppressUndo = false;
    bool m_suppressUndoRedoSignals = false;
    bool m_restoreCamera = true;
};
