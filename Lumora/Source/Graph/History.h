#pragma once

#include <lumora/Graph/Model.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lumora::nodegraph
{
    /// One reversible edit; carries enough to replay both ways without consulting the model.
    struct Edit
    {
        enum class Kind : std::uint8_t
        { AddNode, RemoveNode, Connect, Disconnect, Mutate, AddFrame, RemoveFrame, MutateFrame };

        Kind kind = Kind::Mutate;

        /// AddNode / RemoveNode: the node's full contents, so redo re-adds it identically.
        Node   node{};
        /// The handle the edit produced (AddNode) or consumed (RemoveNode, Mutate).
        GraphNodeId nodeId{};

        PinRef from{};
        PinRef to{};
        LinkId linkId{};

        /// Mutate: the node before and after (whole-node copies, not a field tag).
        Node before{};
        Node after{};

        /// AddFrame / RemoveFrame carry the frame in `frameAfter` / `frameBefore`; MutateFrame both.
        /// Frame ids are stable across undo, so unlike nodes they never need remapping.
        Frame frameBefore{};
        Frame frameAfter{};
    };

    /// Transactional undo/redo over a Model. A drag is one transaction, so one undo step.
    /// Redoing an AddNode yields a NEW handle (bumped generation), so Undo/Redo remap handles in remaining transactions.
    class History
    {
    public:
        void Begin(std::string_view label);
        /// Append to the open transaction. Ignored when none is open.
        void Record(Edit edit);
        /// Close the open transaction. An empty one is discarded rather than pushed.
        void Commit();
        /// Roll the open transaction back and discard it, leaving the undo stack untouched.
        void Abort(Model& model);
        [[nodiscard]] bool Open() const { return m_open; }

        bool Undo(Model& model);
        bool Redo(Model& model);
        [[nodiscard]] bool CanUndo() const { return !m_undo.empty(); }
        [[nodiscard]] bool CanRedo() const { return !m_redo.empty(); }
        /// Label of the transaction Undo would apply, or empty.
        [[nodiscard]] std::string_view UndoLabel() const;

        void Clear();

    private:
        struct Transaction
        {
            std::string       label;
            std::vector<Edit> edits;
        };

        /// Apply one edit forwards or backwards, and remap handles when a node handle changes.
        void ApplyForward(Model& model, Edit& edit);
        void ApplyBackward(Model& model, Edit& edit);
        /// Substitute `to` for `from` in every stored edit on both stacks, in the open transaction and
        /// in the model's frame memberships.
        void Remap(Model& model, GraphNodeId from, GraphNodeId to);

        std::vector<Transaction> m_undo;
        std::vector<Transaction> m_redo;
        Transaction              m_current;
        bool                     m_open = false;
    };
} // namespace lumora::nodegraph
