#include "documentundomanager.h"
#include "document.h"
#include "document_internal.h"

#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QObject>
#include <QVector>

#include <algorithm>
#include <set>

using namespace DocumentInternal;

DocumentUndoManager::DocumentUndoManager(Document &doc)
    : m_doc(doc)
{
}

void DocumentUndoManager::beginStep(const QString &label, const ScriptAction &scriptAction)
{
    if (m_restoringUndoRedo || m_undoStepActive || m_suppressUndo)
        return;

    m_undoStepActive = true;
    m_undoStepLabel = label.trimmed();
    if (m_undoStepLabel.isEmpty())
        m_undoStepLabel = QObject::tr("Edit");
    m_pendingScriptAction = scriptAction;
    m_pendingUndoBefore = m_doc.captureUndoState();
}

void DocumentUndoManager::endStep(bool commit, bool restoreOnCancel)
{
    if (!m_undoStepActive)
        return;

    // Delta mode: defer to endDeltaStep for selection-only operations.
    if (m_pendingDeltaMeshIndex.has_value()) {
        if (!commit) {
            if (restoreOnCancel && m_pendingDeltaBefore.has_value()) {
                m_restoringUndoRedo = true;
                m_doc.applySelectionDelta(*m_pendingDeltaBefore);
                m_restoringUndoRedo = false;
            }
            m_pendingDeltaBefore.reset();
            m_pendingDeltaMeshIndex.reset();
            m_pendingScriptAction.reset();
            m_undoStepActive = false;
            m_undoStepLabel.clear();
            return;
        }
        endDeltaStep();
        return;
    }

    const QString label = m_undoStepLabel;
    std::optional<UndoState> before = std::move(m_pendingUndoBefore);
    std::optional<ScriptAction> scriptAction = std::move(m_pendingScriptAction);
    m_pendingUndoBefore.reset();
    m_pendingScriptAction.reset();
    m_undoStepActive = false;
    m_undoStepLabel.clear();

    if (!before.has_value())
        return;

    if (!commit) {
        if (restoreOnCancel) {
            m_restoringUndoRedo = true;
            m_doc.restoreUndoState(*before);
            m_restoringUndoRedo = false;
        }
        return;
    }

    QElapsedTimer timer;
    timer.start();
    UndoState after = m_doc.captureUndoState();
    const qint64 captureNs = timer.nsecsElapsed();

    timer.restart();
    pushStep(label, std::move(*before), std::move(after), std::move(scriptAction));
    const qint64 commitNs = timer.nsecsElapsed();

    m_doc.writeLog(QObject::tr("Undo checkpoint '%1': snapshot %2 ms, commit %3 ms")
                       .arg(label)
                       .arg(captureNs / 1.0e6, 0, 'f', 2)
                       .arg(commitNs / 1.0e6, 0, 'f', 2),
                   Document::LogSource::Application);
}

void DocumentUndoManager::beginDeltaStep(const QString &label, int meshIndex,
                                        std::optional<ScriptAction> scriptAction)
{
    if (m_restoringUndoRedo || m_undoStepActive || m_suppressUndo)
        return;

    m_undoStepActive = true;
    m_undoStepLabel = label.trimmed();
    if (m_undoStepLabel.isEmpty())
        m_undoStepLabel = QObject::tr("Edit");
    m_pendingDeltaBefore = m_doc.captureSelectionDelta(meshIndex);
    m_pendingDeltaMeshIndex = meshIndex;
    m_pendingScriptAction = std::move(scriptAction);
    m_pendingUndoBefore.reset();
}

void DocumentUndoManager::endDeltaStep()
{
    if (!m_undoStepActive || !m_pendingDeltaMeshIndex.has_value())
        return;

    const QString label = m_undoStepLabel;
    SelectionDelta before = std::move(*m_pendingDeltaBefore);
    const int meshIndex = *m_pendingDeltaMeshIndex;
    std::optional<ScriptAction> scriptAction = std::move(m_pendingScriptAction);
    m_pendingDeltaBefore.reset();
    m_pendingDeltaMeshIndex.reset();
    m_pendingScriptAction.reset();
    m_undoStepActive = false;
    m_undoStepLabel.clear();

    SelectionDelta after = m_doc.captureSelectionDelta(meshIndex);
    pushDeltaStep(label, std::move(before), std::move(after), std::move(scriptAction));
}

bool DocumentUndoManager::canUndo() const
{
    if (m_undoCurrentNode < 0 || m_undoCurrentNode >= static_cast<int>(m_undoNodes.size()))
        return false;
    return m_undoNodes[static_cast<size_t>(m_undoCurrentNode)].parentId >= 0;
}

bool DocumentUndoManager::canRedo() const
{
    if (m_undoCurrentNode < 0 || m_undoCurrentNode >= static_cast<int>(m_undoNodes.size()))
        return false;
    return !m_undoNodes[static_cast<size_t>(m_undoCurrentNode)].children.empty();
}

QString DocumentUndoManager::undoText() const
{
    if (!canUndo())
        return {};
    return m_undoNodes[static_cast<size_t>(m_undoCurrentNode)].label;
}

QString DocumentUndoManager::redoText() const
{
    if (!canRedo())
        return {};
    const auto &node = m_undoNodes[static_cast<size_t>(m_undoCurrentNode)];
    const int preferred = node.preferredChild >= 0 ? node.preferredChild
                                                   : node.children.front();
    return m_undoNodes[static_cast<size_t>(preferred)].label;
}

QStringList DocumentUndoManager::undoHistoryLabels() const
{
    QStringList labels;
    if (m_undoCurrentNode < 0)
        return labels;
    int id = m_undoCurrentNode;
    while (id >= 0) {
        const auto &node = m_undoNodes[static_cast<size_t>(id)];
        if (!node.label.isEmpty())
            labels.prepend(node.label);
        id = node.parentId;
    }
    return labels;
}

QStringList DocumentUndoManager::undoStackLabels() const
{
    return undoHistoryLabels();
}

int DocumentUndoManager::undoCursorPosition() const
{
    if (m_undoCurrentNode < 0 || m_undoCurrentNode >= static_cast<int>(m_undoNodes.size()))
        return 0;
    int depth = 0;
    int id = m_undoCurrentNode;
    while (m_undoNodes[static_cast<size_t>(id)].parentId >= 0) {
        ++depth;
        id = m_undoNodes[static_cast<size_t>(id)].parentId;
    }
    return depth;
}

std::vector<UndoTreeNodeInfo> DocumentUndoManager::undoTreeInfo() const
{
    if (m_undoNodes.empty())
        return {};

    std::set<int> onPath;
    {
        int id = m_undoCurrentNode;
        while (id >= 0) {
            onPath.insert(id);
            id = m_undoNodes[static_cast<size_t>(id)].parentId;
        }
    }

    std::vector<UndoTreeNodeInfo> result;
    result.reserve(m_undoNodes.size());
    struct Frame { int id; int depth; };
    std::vector<Frame> stack = {{ 0, 0 }};
    while (!stack.empty()) {
        const auto [id, depth] = stack.back();
        stack.pop_back();
        const auto &node = m_undoNodes[static_cast<size_t>(id)];
        UndoTreeNodeInfo info;
        info.nodeId = id;
        info.parentId = node.parentId;
        info.depth = depth;
        info.isCurrent = (id == m_undoCurrentNode);
        info.isOnCurrentPath = onPath.count(id) > 0;
        info.lane = node.lane;
        info.label = node.label;
        result.push_back(std::move(info));
        for (int i = static_cast<int>(node.children.size()) - 1; i >= 0; --i)
            stack.push_back({ node.children[static_cast<size_t>(i)], depth + 1 });
    }
    return result;
}

bool DocumentUndoManager::updateNodeCamera(int nodeId, const ViewState &viewState)
{
    if (nodeId < 0 || nodeId >= static_cast<int>(m_undoNodes.size()))
        return false;
    m_undoNodes[static_cast<size_t>(nodeId)].state.viewState = viewState;
    return true;
}

bool DocumentUndoManager::jumpToNode(int nodeId, bool restoreCamera)
{
    if (nodeId < 0 || nodeId >= static_cast<int>(m_undoNodes.size()))
        return false;
    if (nodeId == m_undoCurrentNode) {
        m_doc.restoreViewState(m_undoNodes[static_cast<size_t>(nodeId)].state.viewState, restoreCamera);
        return true;
    }
    if (m_undoStepActive)
        return false;

    // Walk up from both nodes to find the lowest common ancestor (LCA).
    // Build the path from target to root.
    std::vector<int> targetPath;
    {
        int id = nodeId;
        while (id >= 0) {
            targetPath.push_back(id);
            id = m_undoNodes[static_cast<size_t>(id)].parentId;
        }
    }
    // Build ancestor set for current node.
    std::set<int> currentAncestors;
    {
        int id = m_undoCurrentNode;
        while (id >= 0) {
            currentAncestors.insert(id);
            id = m_undoNodes[static_cast<size_t>(id)].parentId;
        }
    }
    // The LCA is the first node in targetPath that is also an ancestor of current.
    int lca = -1;
    for (int id : targetPath) {
        if (currentAncestors.count(id)) { lca = id; break; }
    }
    if (lca < 0)
        return false;

    {
        std::vector<int> redoPreview;
        for (int id : targetPath) {
            if (id == lca) break;
            redoPreview.push_back(id);
        }
        std::reverse(redoPreview.begin(), redoPreview.end());
        qDebug() << "[JUMP] target=" << nodeId
                 << "current=" << m_undoCurrentNode
                 << "lca=" << lca
                 << "undos=" << (m_undoCurrentNode != lca ? "yes" : "no")
                 << "redoPath=" << QVector<int>(redoPreview.begin(), redoPreview.end());
    }

    // Suppress per-step signals during the multi-step navigation so that
    // intermediate states don't trigger thumbnail capture (grabFramebuffer)
    // or other GUI refreshes that could interfere with the GPU/render state.
    // Camera is never restored for intermediate steps; only the final redo
    // restores it (controlled by the restoreCamera parameter).
    m_suppressUndoRedoSignals = true;
    m_restoreCamera = false;

    // Undo until we reach the LCA.
    while (m_undoCurrentNode != lca) {
        if (!undo()) {
            m_restoreCamera = true;
            m_suppressUndoRedoSignals = false;
            emitStateChanged();
            return false;
        }
    }
    // Now redo along the path from LCA to target.
    // Collect the path from LCA to target (excluding LCA).
    std::vector<int> redoPath;
    for (int id : targetPath) {
        if (id == lca) break;
        redoPath.push_back(id);
    }
    std::reverse(redoPath.begin(), redoPath.end());
    for (int i = 0; i < static_cast<int>(redoPath.size()); ++i) {
        const int id = redoPath[static_cast<size_t>(i)];
        // Restore camera only on the very last redo step.
        m_restoreCamera = (i == static_cast<int>(redoPath.size()) - 1) ? restoreCamera : false;
        // Set the preferred child so redo() follows the correct branch.
        m_undoNodes[static_cast<size_t>(m_undoCurrentNode)].preferredChild = id;
        if (!redo()) {
            m_restoreCamera = true;
            m_suppressUndoRedoSignals = false;
            emitStateChanged();
            return false;
        }
    }
    // If there were no redo steps (target == lca), the undo steps ran with
    // m_restoreCamera=false so render modes were restored but camera was not.
    // Now explicitly re-apply the view with the correct restoreCamera value.
    if (redoPath.empty()) {
        m_doc.restoreViewState(m_undoNodes[static_cast<size_t>(m_undoCurrentNode)].state.viewState, restoreCamera);
    }

    m_restoreCamera = true;
    m_suppressUndoRedoSignals = false;
    emitStateChanged();
    return m_undoCurrentNode == nodeId;
}

bool DocumentUndoManager::undo()
{
    if (!canUndo() || m_undoStepActive)
        return false;

    const auto &node = m_undoNodes[static_cast<size_t>(m_undoCurrentNode)];
    const int parentId = node.parentId;
    const auto &target = m_undoNodes[static_cast<size_t>(parentId)];
    qDebug() << "[UNDO]  from=" << m_undoCurrentNode << '(' << node.label << ')'
             << "to=" << parentId << '(' << target.label << ')';

    if (node.storageKind == UndoStorageKind::Delta && node.beforeSelection.has_value()) {
        // Delta undo: revert selection on live mesh, no full state restore.
        m_restoringUndoRedo = true;
        m_doc.applySelectionDelta(*node.beforeSelection);
        m_undoNodes[static_cast<size_t>(parentId)].preferredChild = m_undoCurrentNode;
        m_undoCurrentNode = parentId;
        emitStateChanged();
        m_restoringUndoRedo = false;
        m_doc.writeLog(QObject::tr("Undo '%1' (selection delta)").arg(node.label),
            Document::LogSource::Application);
        return true;
    }

    QElapsedTimer timer;
    timer.start();
    m_restoringUndoRedo = true;
    m_doc.restoreUndoState(target.state);
    // Remember which child to return to on redo.
    m_undoNodes[static_cast<size_t>(parentId)].preferredChild = m_undoCurrentNode;
    m_undoCurrentNode = parentId;
    emitStateChanged();
    m_restoringUndoRedo = false;
    m_doc.writeLog(QObject::tr("Undo '%1': %2 ms (%3 meshes, %4 rasters)")
        .arg(node.label)
        .arg(timer.elapsed())
        .arg(target.state.meshes.size())
        .arg(target.state.rasters.size()),
        Document::LogSource::Application);
    return true;
}

bool DocumentUndoManager::redo()
{
    if (!canRedo() || m_undoStepActive)
        return false;

    auto &node = m_undoNodes[static_cast<size_t>(m_undoCurrentNode)];
    const int childId = node.preferredChild >= 0 ? node.preferredChild
                                                  : node.children.front();
    const auto &target = m_undoNodes[static_cast<size_t>(childId)];
    qDebug() << "[REDO]  from=" << m_undoCurrentNode << '(' << node.label << ')'
             << "to=" << childId << '(' << target.label << ')'
             << "prefChild=" << node.preferredChild
             << "children=" << QVector<int>(node.children.begin(), node.children.end());
    if (target.storageKind == UndoStorageKind::Delta && target.afterSelection.has_value()) {
        // Delta redo: apply after-selection on live mesh, no full state restore.
        m_restoringUndoRedo = true;
        m_doc.applySelectionDelta(*target.afterSelection);
        m_undoCurrentNode = childId;
        emitStateChanged();
        m_restoringUndoRedo = false;
        m_doc.writeLog(QObject::tr("Redo '%1' (selection delta)").arg(target.label),
            Document::LogSource::Application);
        return true;
    }

    QElapsedTimer timer;
    timer.start();
    m_restoringUndoRedo = true;
    m_doc.restoreUndoState(target.state);
    m_undoCurrentNode = childId;
    emitStateChanged();
    m_restoringUndoRedo = false;
    m_doc.writeLog(QObject::tr("Redo '%1': %2 ms (%3 meshes, %4 rasters)")
        .arg(target.label)
        .arg(timer.elapsed())
        .arg(target.state.meshes.size())
        .arg(target.state.rasters.size()),
        Document::LogSource::Application);
    return true;
}

void DocumentUndoManager::clear()
{
    if (m_undoNodes.empty() && m_undoCurrentNode < 0 && !m_undoStepActive)
        return;

    m_undoNodes.clear();
    m_undoGeometryCache.clear();
    m_undoCurrentNode = -1;
    m_undoStepActive = false;
    m_undoStepLabel.clear();
    m_pendingUndoBefore.reset();
    m_pendingDeltaBefore.reset();
    m_pendingDeltaMeshIndex.reset();
    emitStateChanged();
}

bool DocumentUndoManager::makeRoot(int nodeId)
{
    if (nodeId < 0 || nodeId >= static_cast<int>(m_undoNodes.size()))
        return false;
    if (m_undoStepActive)
        return false;
    // Already the only node (trivial root) – nothing to do.
    if (m_undoNodes[static_cast<size_t>(nodeId)].parentId < 0 &&
        m_undoNodes.size() == 1)
        return false;

    // Collect all nodes reachable from nodeId (the node itself + all descendants).
    std::vector<int> reachable;
    {
        std::vector<int> stack = { nodeId };
        while (!stack.empty()) {
            const int id = stack.back(); stack.pop_back();
            reachable.push_back(id);
            for (int c : m_undoNodes[static_cast<size_t>(id)].children)
                stack.push_back(c);
        }
    }
    std::sort(reachable.begin(), reachable.end());

    // If the current node is not in the surviving subtree, reset it to nodeId.
    const bool currentSurvives =
        std::binary_search(reachable.begin(), reachable.end(), m_undoCurrentNode);
    const int effectiveCurrent = currentSurvives ? m_undoCurrentNode : nodeId;

    // Compact: remap old ids → new compact ids.
    std::map<int, int> remap;
    for (int ni = 0; ni < static_cast<int>(reachable.size()); ++ni)
        remap[reachable[ni]] = ni;

    std::vector<UndoNode> compacted;
    compacted.reserve(reachable.size());
    for (int oldId : reachable) {
        UndoNode n = std::move(m_undoNodes[static_cast<size_t>(oldId)]);
        n.parentId = (n.parentId >= 0 && remap.count(n.parentId)) ? remap[n.parentId] : -1;
        std::vector<int> newChildren;
        for (int c : n.children)
            if (remap.count(c)) newChildren.push_back(remap[c]);
        n.children = std::move(newChildren);
        if (n.preferredChild >= 0 && remap.count(n.preferredChild))
            n.preferredChild = remap[n.preferredChild];
        else
            n.preferredChild = n.children.empty() ? -1 : n.children.back();
        compacted.push_back(std::move(n));
    }
    // The new root (nodeId) has no parent and no label (it is a root state).
    compacted[remap[nodeId]].parentId = -1;
    compacted[remap[nodeId]].label.clear();

    m_undoNodes = std::move(compacted);
    m_undoCurrentNode = remap.count(effectiveCurrent) ? remap[effectiveCurrent] : remap[nodeId];
    emitStateChanged();
    return true;
}

bool DocumentUndoManager::purgeBranch(int nodeId)
{
    if (nodeId < 0 || nodeId >= static_cast<int>(m_undoNodes.size()))
        return false;
    if (m_undoStepActive)
        return false;
    // Nothing to purge if the node is already a leaf.
    if (m_undoNodes[static_cast<size_t>(nodeId)].children.empty())
        return false;

    // Collect the set of strictly descendant ids (not including nodeId itself).
    std::set<int> descendants;
    {
        std::vector<int> stack;
        for (int c : m_undoNodes[static_cast<size_t>(nodeId)].children)
            stack.push_back(c);
        while (!stack.empty()) {
            const int id = stack.back(); stack.pop_back();
            descendants.insert(id);
            for (int c : m_undoNodes[static_cast<size_t>(id)].children)
                stack.push_back(c);
        }
    }

    // Collect surviving nodes: everything that is not a descendant of nodeId.
    std::vector<int> reachable;
    for (int i = 0; i < static_cast<int>(m_undoNodes.size()); ++i)
        if (!descendants.count(i))
            reachable.push_back(i);
    // reachable is already sorted (sequential iteration).

    // The root is always index 0 and is never a descendant, so it always survives.
    const int rootId = reachable.front(); // always 0

    // If the current node was a descendant, reset it to nodeId.
    const bool currentSurvives = !descendants.count(m_undoCurrentNode);
    const int effectiveCurrent = currentSurvives ? m_undoCurrentNode : nodeId;

    // Compact: remap old ids → new compact ids.
    std::map<int, int> remap;
    for (int ni = 0; ni < static_cast<int>(reachable.size()); ++ni)
        remap[reachable[ni]] = ni;

    std::vector<UndoNode> compacted;
    compacted.reserve(reachable.size());
    for (int oldId : reachable) {
        UndoNode n = std::move(m_undoNodes[static_cast<size_t>(oldId)]);
        n.parentId = (n.parentId >= 0 && remap.count(n.parentId)) ? remap[n.parentId] : -1;
        std::vector<int> newChildren;
        for (int c : n.children)
            if (remap.count(c)) newChildren.push_back(remap[c]);
        n.children = std::move(newChildren);
        if (n.preferredChild >= 0 && remap.count(n.preferredChild))
            n.preferredChild = remap[n.preferredChild];
        else
            n.preferredChild = n.children.empty() ? -1 : n.children.back();
        compacted.push_back(std::move(n));
    }
    compacted[remap[rootId]].parentId = -1;

    m_undoNodes = std::move(compacted);
    m_undoCurrentNode = remap.count(effectiveCurrent) ? remap[effectiveCurrent] : remap[nodeId];
    emitStateChanged();
    return true;
}

bool DocumentUndoManager::linearizeHistory()
{
    if (m_undoNodes.empty() || m_undoStepActive)
        return false;

    // Build the set of node ids on the current path: root → m_undoCurrentNode.
    std::vector<int> path;
    {
        int id = m_undoCurrentNode;
        while (id >= 0) {
            path.push_back(id);
            id = m_undoNodes[static_cast<size_t>(id)].parentId;
        }
    }
    // path is currently ordered current→root; reverse to get root→current.
    std::reverse(path.begin(), path.end());

    // If the history is already linear (no side branches), nothing to do.
    if (static_cast<int>(path.size()) == static_cast<int>(m_undoNodes.size()))
        return false;

    // Keep only path nodes, sorted ascending (they already are after the reverse).
    std::sort(path.begin(), path.end());

    // Remap old ids → new compact ids (0 = root, 1 = next, …, n-1 = current).
    std::map<int, int> remap;
    for (int ni = 0; ni < static_cast<int>(path.size()); ++ni)
        remap[path[ni]] = ni;

    std::vector<UndoNode> compacted;
    compacted.reserve(path.size());
    for (int oldId : path) {
        UndoNode n = std::move(m_undoNodes[static_cast<size_t>(oldId)]);
        n.parentId = (n.parentId >= 0 && remap.count(n.parentId)) ? remap[n.parentId] : -1;
        // Keep only the single child that is also on the path.
        std::vector<int> newChildren;
        for (int c : n.children)
            if (remap.count(c)) newChildren.push_back(remap[c]);
        n.children = std::move(newChildren);
        n.preferredChild = n.children.empty() ? -1 : n.children.front();
        compacted.push_back(std::move(n));
    }
    compacted[0].parentId = -1; // root has no parent

    // After linearization there is only one chain, so all nodes belong to lane 0.
    for (UndoNode &n : compacted)
        n.lane = 0;

    m_undoNodes = std::move(compacted);
    m_undoCurrentNode = remap[m_undoCurrentNode];
    emitStateChanged();
    return true;
}

void DocumentUndoManager::setUndoLimit(int limit)
{
    m_undoLimit = std::max(1, limit);
    pruneTreeToLimit();
}


void DocumentUndoManager::pushStep(const QString &label, UndoState &&before, UndoState &&after,
                            std::optional<ScriptAction> scriptAction)
{
    // If there is no tree yet, create the root node from the "before" state.
    if (m_undoCurrentNode < 0) {
        UndoNode root;
        root.state   = std::move(before);
        root.label   = {};
        root.parentId = -1;
        m_undoNodes.push_back(std::move(root));
        m_undoCurrentNode = 0;
    } else {
        // Update the current node's state to be the "before" snapshot.
        // (It should already match, but this keeps things consistent if the
        //  caller uses an older capture.)
        m_undoNodes[static_cast<size_t>(m_undoCurrentNode)].state = std::move(before);
    }

    // Append a new child node carrying the "after" state.
    const int newId = static_cast<int>(m_undoNodes.size());
    UndoNode child;
    child.state    = std::move(after);
    child.storageKind = UndoStorageKind::FullSnapshot;
    child.label    = label;
    child.parentId = m_undoCurrentNode;
    if (scriptAction.has_value())
        child.actionRecord = UndoActionRecord::fromScriptAction(*scriptAction);

    // Lane: inherit parent's lane if this is the first child; otherwise open a
    // new lane (max lane currently in tree + 1).
    {
        const auto &parentNode = m_undoNodes[static_cast<size_t>(m_undoCurrentNode)];
        if (parentNode.children.empty()) {
            child.lane = parentNode.lane;
        } else {
            int maxLane = 0;
            for (const auto &n : m_undoNodes)
                maxLane = std::max(maxLane, n.lane);
            child.lane = maxLane + 1;
        }
    }

    m_undoNodes.push_back(std::move(child));

    // Link parent → new child and make it the preferred redo target.
    auto &parent = m_undoNodes[static_cast<size_t>(m_undoCurrentNode)];
    parent.children.push_back(newId);
    parent.preferredChild = newId;

    m_undoCurrentNode = newId;

    {
        const auto &n = m_undoNodes[static_cast<size_t>(newId)];
        qDebug() << "[STATE SAVED]" << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
                 << "idx=" << newId
                 << "lane=" << n.lane
                 << "parent=" << n.parentId
                 << "label=" << n.label;
    }

    pruneTreeToLimit();
    emitStateChanged();
}

void DocumentUndoManager::pushDeltaStep(
    const QString &label,
    SelectionDelta &&before,
    SelectionDelta &&after,
    std::optional<ScriptAction> scriptAction)
{
    if (m_undoCurrentNode < 0) {
        UndoNode root;
        root.state = m_doc.captureUndoState();
        root.label = {};
        root.parentId = -1;
        m_undoNodes.push_back(std::move(root));
        m_undoCurrentNode = 0;
    }
    // Don't overwrite current node's state — for delta steps the parent
    // already carries the correct full snapshot from a prior action.

    const int newId = static_cast<int>(m_undoNodes.size());
    UndoNode child;
    child.storageKind = UndoStorageKind::Delta;
    child.label = label;
    child.parentId = m_undoCurrentNode;
    child.beforeSelection = std::move(before);
    child.afterSelection = std::move(after);
    if (scriptAction.has_value())
        child.actionRecord = UndoActionRecord::fromScriptAction(*scriptAction);

    {
        const auto &parentNode = m_undoNodes[static_cast<size_t>(m_undoCurrentNode)];
        if (parentNode.children.empty()) {
            child.lane = parentNode.lane;
        } else {
            int maxLane = 0;
            for (const auto &n : m_undoNodes)
                maxLane = std::max(maxLane, n.lane);
            child.lane = maxLane + 1;
        }
    }

    m_undoNodes.push_back(std::move(child));
    auto &parent = m_undoNodes[static_cast<size_t>(m_undoCurrentNode)];
    parent.children.push_back(newId);
    parent.preferredChild = newId;
    m_undoCurrentNode = newId;

    {
        const auto &n = m_undoNodes[static_cast<size_t>(newId)];
        qDebug() << "[STATE SAVED]" << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
                 << "idx=" << newId
                 << "lane=" << n.lane
                 << "parent=" << n.parentId
                 << "label=" << n.label
                 << "(selection delta)";
    }

    pruneTreeToLimit();
    emitStateChanged();
}

std::optional<ScriptAction> DocumentUndoManager::nodeScriptAction(int nodeId) const
{
    if (nodeId < 0 || nodeId >= static_cast<int>(m_undoNodes.size()))
        return {};
    const std::optional<UndoActionRecord> &record =
        m_undoNodes[static_cast<size_t>(nodeId)].actionRecord;
    if (!record.has_value())
        return {};
    return record->toScriptAction();
}

// Prune the oldest ancestor nodes until the tree has at most m_undoLimit nodes
// (not counting the root which serves as the "no history" sentinel).
// The current path to m_undoCurrentNode is always preserved.
void DocumentUndoManager::pruneTreeToLimit()
{
    // Count nodes along the current path (root to current).
    int depth = 0;
    {
        int id = m_undoCurrentNode;
        while (id >= 0) {
            ++depth;
            id = m_undoNodes[static_cast<size_t>(id)].parentId;
        }
    }
    if (depth - 1 <= m_undoLimit)  // depth-1 = number of transitions
        return;

    // Find the ancestor of m_undoCurrentNode that sits at depth m_undoLimit from
    // the current tip, i.e. the new root after pruning.
    const int stepsToRemove = depth - 1 - m_undoLimit;
    int newRoot = m_undoCurrentNode;
    for (int i = 0; i < m_undoLimit; ++i)
        newRoot = m_undoNodes[static_cast<size_t>(newRoot)].parentId;
    // newRoot is the node to become the new root; everything above it is discarded.
    // Use a BFS/DFS to mark all reachable node ids from newRoot; drop the rest.
    // We do this by building a remapping of old ids → new compact ids.
    std::vector<int> reachable;
    {
        std::vector<int> stack = { newRoot };
        while (!stack.empty()) {
            const int id = stack.back(); stack.pop_back();
            reachable.push_back(id);
            for (int c : m_undoNodes[static_cast<size_t>(id)].children)
                stack.push_back(c);
        }
    }
    std::sort(reachable.begin(), reachable.end());
    // Build old→new id map.
    std::map<int,int> remap;
    for (int ni = 0; ni < static_cast<int>(reachable.size()); ++ni)
        remap[reachable[ni]] = ni;

    std::vector<UndoNode> pruned;
    pruned.reserve(reachable.size());
    for (int oldId : reachable) {
        UndoNode n = std::move(m_undoNodes[static_cast<size_t>(oldId)]);
        n.parentId = (n.parentId >= 0 && remap.count(n.parentId)) ? remap[n.parentId] : -1;
        std::vector<int> newChildren;
        for (int c : n.children)
            if (remap.count(c))
                newChildren.push_back(remap[c]);
        n.children = std::move(newChildren);
        if (n.preferredChild >= 0 && remap.count(n.preferredChild))
            n.preferredChild = remap[n.preferredChild];
        else
            n.preferredChild = n.children.empty() ? -1 : n.children.back();
        pruned.push_back(std::move(n));
    }
    pruned[0].parentId = -1;  // new root has no parent
    m_undoCurrentNode = remap[m_undoCurrentNode];
    m_undoNodes = std::move(pruned);
    (void)stepsToRemove;
}

void DocumentUndoManager::emitStateChanged()
{
    if (m_suppressUndoRedoSignals)
        return;
    m_doc.undoRedoStateChanged(canUndo(), canRedo(), undoText(), redoText());
}

UndoMemoryStats DocumentUndoManager::memoryStats() const
{
    UndoMemoryStats stats;
    std::vector<int> path;
    {
        int id = m_undoCurrentNode;
        while (id >= 0) {
            path.push_back(id);
            id = m_undoNodes[static_cast<size_t>(id)].parentId;
        }
        std::reverse(path.begin(), path.end());
    }

    for (int pi = 1; pi < static_cast<int>(path.size()); ++pi) {
        UndoStepMemoryInfo info;
        info.label = m_undoNodes[static_cast<size_t>(path[pi])].label;
        for (const auto &snap : m_undoNodes[static_cast<size_t>(path[pi - 1])].state.meshes)
            info.beforeBytes += vcgMeshCpuBytes(*snap.geometry);
        for (const auto &snap : m_undoNodes[static_cast<size_t>(path[pi])].state.meshes)
            info.afterBytes += vcgMeshCpuBytes(*snap.geometry);
        stats.steps.push_back(info);
    }

    std::set<const VCGMesh *> seen;
    for (const auto &node : m_undoNodes) {
        for (const auto &snap : node.state.meshes) {
            if (seen.insert(snap.geometry.get()).second)
                stats.totalBytes += vcgMeshCpuBytes(*snap.geometry);
        }
    }
    return stats;
}
