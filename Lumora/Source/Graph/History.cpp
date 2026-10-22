#include "Graph/History.h"

#include <algorithm>
#include <utility>

namespace lumora::nodegraph
{
    void History::Begin(std::string_view label)
    {
        m_current = Transaction{std::string(label), {}};
        m_open = true;
    }

    void History::Record(Edit edit)
    {
        if (!m_open)
        {
            return;
        }
        m_current.edits.push_back(std::move(edit));
    }

    void History::Commit()
    {
        if (!m_open)
        {
            return;
        }
        m_open = false;
        if (m_current.edits.empty())
        {
            m_current = Transaction{};
            return;
        }
        m_undo.push_back(std::move(m_current));
        m_current = Transaction{};
        m_redo.clear();
    }

    void History::Abort(Model& model)
    {
        if (!m_open)
        {
            return;
        }
        m_open = false;
        for (std::size_t i = m_current.edits.size(); i-- > 0;)
        {
            ApplyBackward(model, m_current.edits[i]);
        }
        m_current = Transaction{};
    }

    bool History::Undo(Model& model)
    {
        if (m_open || m_undo.empty())
        {
            return false;
        }
        // Apply in place, still inside m_undo, so Remap's sweep reaches this transaction's unapplied edits;
        // only move it to m_redo once every edit is applied.
        Transaction& txn = m_undo.back();
        for (std::size_t i = txn.edits.size(); i-- > 0;)
        {
            ApplyBackward(model, txn.edits[i]);
        }
        m_redo.push_back(std::move(txn));
        m_undo.pop_back();
        return true;
    }

    bool History::Redo(Model& model)
    {
        if (m_open || m_redo.empty())
        {
            return false;
        }
        // As in Undo: stay inside m_redo while applying so Remap still reaches the remaining edits.
        Transaction& txn = m_redo.back();
        for (Edit& edit : txn.edits)
        {
            ApplyForward(model, edit);
        }
        m_undo.push_back(std::move(txn));
        m_redo.pop_back();
        return true;
    }

    std::string_view History::UndoLabel() const
    {
        return m_undo.empty() ? std::string_view{} : std::string_view{m_undo.back().label};
    }

    void History::Clear()
    {
        m_undo.clear();
        m_redo.clear();
        m_current = Transaction{};
        m_open = false;
    }

    void History::ApplyForward(Model& model, Edit& edit)
    {
        switch (edit.kind)
        {
        case Edit::Kind::AddNode:
        {
            const GraphNodeId fresh = model.AddRaw(edit.node);
            const GraphNodeId stale = edit.nodeId;
            edit.nodeId = fresh;
            if (stale.Valid() && !(stale == fresh))
            {
                Remap(model, stale, fresh);
            }
            break;
        }
        case Edit::Kind::RemoveNode:
            model.Remove(edit.nodeId);
            break;
        case Edit::Kind::Connect:
            edit.linkId = model.Connect(edit.from, edit.to);
            break;
        case Edit::Kind::Disconnect:
            model.Disconnect(edit.linkId);
            break;
        case Edit::Kind::Mutate:
            if (Node* node = model.Get(edit.nodeId))
            {
                *node = edit.after;
            }
            break;
        case Edit::Kind::AddFrame:
            model.AddFrame(edit.frameAfter);
            break;
        case Edit::Kind::RemoveFrame:
            model.RemoveFrame(edit.frameBefore.id);
            break;
        case Edit::Kind::MutateFrame:
            if (Frame* frame = model.GetFrame(edit.frameAfter.id))
            {
                *frame = edit.frameAfter;
            }
            break;
        }
    }

    void History::ApplyBackward(Model& model, Edit& edit)
    {
        switch (edit.kind)
        {
        case Edit::Kind::AddNode:
            model.Remove(edit.nodeId);
            break;
        case Edit::Kind::RemoveNode:
        {
            const GraphNodeId fresh = model.AddRaw(edit.node);
            const GraphNodeId stale = edit.nodeId;
            edit.nodeId = fresh;
            if (stale.Valid() && !(stale == fresh))
            {
                Remap(model, stale, fresh);
            }
            break;
        }
        case Edit::Kind::Connect:
            model.Disconnect(edit.linkId);
            break;
        case Edit::Kind::Disconnect:
            edit.linkId = model.Connect(edit.from, edit.to);
            break;
        case Edit::Kind::Mutate:
            if (Node* node = model.Get(edit.nodeId))
            {
                *node = edit.before;
            }
            break;
        case Edit::Kind::AddFrame:
            model.RemoveFrame(edit.frameAfter.id);
            break;
        case Edit::Kind::RemoveFrame:
            model.AddFrame(edit.frameBefore);
            break;
        case Edit::Kind::MutateFrame:
            if (Frame* frame = model.GetFrame(edit.frameBefore.id))
            {
                *frame = edit.frameBefore;
            }
            break;
        }
    }

    void History::Remap(Model& model, GraphNodeId from, GraphNodeId to)
    {
        model.RemapNode(from, to);
        const auto fix = [from, to](Edit& edit)
        {
            std::ranges::replace(edit.frameBefore.members, from, to);
            std::ranges::replace(edit.frameAfter.members, from, to);
            if (edit.nodeId == from)
            {
                edit.nodeId = to;
            }
            if (edit.from.node == from)
            {
                edit.from.node = to;
            }
            if (edit.to.node == from)
            {
                edit.to.node = to;
            }
        };
        for (Transaction& txn : m_undo)
        {
            for (Edit& edit : txn.edits)
            {
                fix(edit);
            }
        }
        for (Transaction& txn : m_redo)
        {
            for (Edit& edit : txn.edits)
            {
                fix(edit);
            }
        }
        for (Edit& edit : m_current.edits)
        {
            fix(edit);
        }
    }
} // namespace lumora::nodegraph
