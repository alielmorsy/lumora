#pragma once

// Pure text-editing model behind Input: UTF-8 string with byte-offset caret + selection and coalescing undo.
// Free of fonts and rendering; owns no pointers, so it relocates safely in a dense payload pool.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace lumora
{
    class TextEditor
    {
    public:
        /// Adopt an externally-owned value: clamps the caret to a code-point boundary, collapses selection, clears undo.
        void Reset(std::string text, std::size_t caret);

        [[nodiscard]] const std::string& Text() const noexcept { return m_text; }
        [[nodiscard]] std::size_t Caret() const noexcept { return m_caret; }
        [[nodiscard]] std::size_t Anchor() const noexcept { return m_anchor; }
        [[nodiscard]] std::size_t SelMin() const noexcept { return m_caret < m_anchor ? m_caret : m_anchor; }
        [[nodiscard]] std::size_t SelMax() const noexcept { return m_caret < m_anchor ? m_anchor : m_caret; }
        [[nodiscard]] bool HasSelection() const noexcept { return m_caret != m_anchor; }

        bool Insert(std::string_view utf8);
        bool Backspace();
        bool DeleteForward();
        bool DeleteWordBack();
        bool DeleteWordForward();
        bool ReplaceRange(std::size_t a, std::size_t b, std::string_view s);
        bool DeleteSelection();

        void MoveLeft(bool extend);
        void MoveRight(bool extend);
        void MoveWordLeft(bool extend);
        void MoveWordRight(bool extend);
        void MoveHome(bool extend);
        void MoveEnd(bool extend);
        void MoveToOffset(std::size_t byteOffset, bool extend);
        void SelectAll();
        void SelectWordAt(std::size_t byteOffset);
        void SelectLineAt(std::size_t byteOffset);
        void ClearSelection() noexcept { m_anchor = m_caret; }

        [[nodiscard]] std::string SelectedText() const { return m_text.substr(SelMin(), SelMax() - SelMin()); }

        bool Undo();
        bool Redo();

    private:
        enum class EditKind { None, Insert, DeleteBack, DeleteFwd, Other };

        struct Edit
        {
            std::size_t Pos = 0;
            std::string Removed;
            std::string Inserted;
            std::size_t CaretBefore = 0, AnchorBefore = 0;
            std::size_t CaretAfter = 0;
        };

        bool Splice(std::size_t pos, std::size_t removeLen, std::string_view ins, EditKind kind);
        void PushEdit(Edit&& e, EditKind kind);

        std::string m_text;
        std::size_t m_caret = 0;
        std::size_t m_anchor = 0;
        std::vector<Edit> m_undo, m_redo;
        EditKind m_lastKind = EditKind::None;

        static constexpr std::size_t kMaxUndo = 256;
    };
} // namespace lumora
