#include "Widgets/TextEditor.h"

#include <algorithm>

namespace lumora
{
    namespace
    {
        [[nodiscard]] bool IsContinuation(unsigned char c) noexcept { return (c & 0xC0) == 0x80; }

        [[nodiscard]] std::size_t SnapToBoundary(std::string_view s, std::size_t pos) noexcept
        {
            if (pos >= s.size()) return s.size();
            while (pos > 0 && IsContinuation(static_cast<unsigned char>(s[pos]))) --pos;
            return pos;
        }

        [[nodiscard]] std::size_t PrevCp(std::string_view s, std::size_t pos) noexcept
        {
            if (pos == 0) return 0;
            --pos;
            while (pos > 0 && IsContinuation(static_cast<unsigned char>(s[pos]))) --pos;
            return pos;
        }

        [[nodiscard]] std::size_t NextCp(std::string_view s, std::size_t pos) noexcept
        {
            if (pos >= s.size()) return s.size();
            ++pos;
            while (pos < s.size() && IsContinuation(static_cast<unsigned char>(s[pos]))) ++pos;
            return pos;
        }

        enum class CharClass { Space, Word, Punct };

        [[nodiscard]] CharClass ClassAt(std::string_view s, std::size_t pos) noexcept
        {
            if (pos >= s.size()) return CharClass::Space;
            const unsigned char c = static_cast<unsigned char>(s[pos]);
            if (c >= 0x80) return CharClass::Word;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') return CharClass::Space;
            const bool word = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
            return word ? CharClass::Word : CharClass::Punct;
        }

        [[nodiscard]] std::size_t WordLeft(std::string_view s, std::size_t pos) noexcept
        {
            if (pos == 0) return 0;
            std::size_t p = pos;
            std::size_t prev = PrevCp(s, p);
            while (prev < p && ClassAt(s, prev) == CharClass::Space)
            {
                p = prev;
                prev = PrevCp(s, p);
            }
            if (p == 0) return 0;
            const CharClass cls = ClassAt(s, PrevCp(s, p));
            while (p > 0)
            {
                const std::size_t q = PrevCp(s, p);
                if (ClassAt(s, q) != cls) break;
                p = q;
            }
            return p;
        }

        [[nodiscard]] std::size_t WordRight(std::string_view s, std::size_t pos) noexcept
        {
            const std::size_t n = s.size();
            if (pos >= n) return n;
            std::size_t p = pos;
            if (ClassAt(s, p) != CharClass::Space)
            {
                const CharClass cls = ClassAt(s, p);
                while (p < n && ClassAt(s, p) == cls) p = NextCp(s, p);
            }
            while (p < n && ClassAt(s, p) == CharClass::Space) p = NextCp(s, p);
            return p;
        }

        [[nodiscard]] bool IsSpaceByte(char c) noexcept
        {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
        }

        [[nodiscard]] bool IsWhitespaceOnly(std::string_view s) noexcept
        {
            return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return IsSpaceByte(c); });
        }

        [[nodiscard]] bool EndsWithWhitespace(std::string_view s) noexcept
        {
            return !s.empty() && IsSpaceByte(s.back());
        }
    } // namespace

    void TextEditor::Reset(std::string text, std::size_t caret)
    {
        m_text = std::move(text);
        m_caret = m_anchor = SnapToBoundary(m_text, std::min(caret, m_text.size()));
        m_undo.clear();
        m_redo.clear();
        m_lastKind = EditKind::None;
    }

    void TextEditor::PushEdit(Edit&& e, EditKind kind)
    {
        bool merged = false;
        if (kind != EditKind::Other && kind == m_lastKind && !m_undo.empty())
        {
            Edit& top = m_undo.back();
            if (kind == EditKind::Insert &&
                top.Removed.empty() && e.Removed.empty() &&
                top.Pos + top.Inserted.size() == e.Pos &&
                !IsWhitespaceOnly(e.Inserted) && !EndsWithWhitespace(top.Inserted))
            {
                top.Inserted += e.Inserted;
                top.CaretAfter = e.CaretAfter;
                merged = true;
            }
            else if (kind == EditKind::DeleteBack &&
                top.Inserted.empty() && e.Inserted.empty() &&
                e.Pos + e.Removed.size() == top.Pos)
            {
                top.Removed = e.Removed + top.Removed;
                top.Pos = e.Pos;
                top.CaretAfter = e.CaretAfter;
                merged = true;
            }
            else if (kind == EditKind::DeleteFwd &&
                top.Inserted.empty() && e.Inserted.empty() &&
                e.Pos == top.Pos)
            {
                top.Removed += e.Removed;
                top.CaretAfter = e.CaretAfter;
                merged = true;
            }
        }
        if (!merged) m_undo.push_back(std::move(e));
        m_redo.clear();
        m_lastKind = kind;
        if (m_undo.size() > kMaxUndo)
            m_undo.erase(m_undo.begin(), m_undo.begin() + static_cast<std::ptrdiff_t>(m_undo.size() - kMaxUndo));
    }

    bool TextEditor::Splice(std::size_t pos, std::size_t removeLen, std::string_view ins, EditKind kind)
    {
        pos = std::min(pos, m_text.size());
        removeLen = std::min(removeLen, m_text.size() - pos);
        if (removeLen == 0 && ins.empty()) return false;

        Edit e;
        e.Pos = pos;
        e.Removed = m_text.substr(pos, removeLen);
        e.Inserted.assign(ins);
        e.CaretBefore = m_caret;
        e.AnchorBefore = m_anchor;
        e.CaretAfter = pos + ins.size();

        m_text.replace(pos, removeLen, ins);
        m_caret = m_anchor = e.CaretAfter;
        PushEdit(std::move(e), kind);
        return true;
    }

    bool TextEditor::Insert(std::string_view utf8)
    {
        if (utf8.empty()) return false;
        const bool sel = HasSelection();
        return Splice(SelMin(), SelMax() - SelMin(), utf8, sel ? EditKind::Other : EditKind::Insert);
    }

    bool TextEditor::Backspace()
    {
        if (HasSelection()) return Splice(SelMin(), SelMax() - SelMin(), {}, EditKind::Other);
        if (m_caret == 0) return false;
        const std::size_t prev = PrevCp(m_text, m_caret);
        return Splice(prev, m_caret - prev, {}, EditKind::DeleteBack);
    }

    bool TextEditor::DeleteForward()
    {
        if (HasSelection()) return Splice(SelMin(), SelMax() - SelMin(), {}, EditKind::Other);
        if (m_caret >= m_text.size()) return false;
        const std::size_t next = NextCp(m_text, m_caret);
        return Splice(m_caret, next - m_caret, {}, EditKind::DeleteFwd);
    }

    bool TextEditor::DeleteWordBack()
    {
        if (HasSelection()) return Splice(SelMin(), SelMax() - SelMin(), {}, EditKind::Other);
        if (m_caret == 0) return false;
        const std::size_t w = WordLeft(m_text, m_caret);
        return Splice(w, m_caret - w, {}, EditKind::Other);
    }

    bool TextEditor::DeleteWordForward()
    {
        if (HasSelection()) return Splice(SelMin(), SelMax() - SelMin(), {}, EditKind::Other);
        if (m_caret >= m_text.size()) return false;
        const std::size_t w = WordRight(m_text, m_caret);
        return Splice(m_caret, w - m_caret, {}, EditKind::Other);
    }

    bool TextEditor::ReplaceRange(std::size_t a, std::size_t b, std::string_view s)
    {
        const std::size_t lo = SnapToBoundary(m_text, std::min({a, b, m_text.size()}));
        const std::size_t hi = SnapToBoundary(m_text, std::min(std::max(a, b), m_text.size()));
        return Splice(lo, hi - lo, s, EditKind::Other);
    }

    bool TextEditor::DeleteSelection()
    {
        if (!HasSelection()) return false;
        return Splice(SelMin(), SelMax() - SelMin(), {}, EditKind::Other);
    }

    void TextEditor::MoveLeft(bool extend)
    {
        m_lastKind = EditKind::None;
        if (!extend && HasSelection())
        {
            m_caret = m_anchor = SelMin();
            return;
        }
        m_caret = PrevCp(m_text, m_caret);
        if (!extend) m_anchor = m_caret;
    }

    void TextEditor::MoveRight(bool extend)
    {
        m_lastKind = EditKind::None;
        if (!extend && HasSelection())
        {
            m_caret = m_anchor = SelMax();
            return;
        }
        m_caret = NextCp(m_text, m_caret);
        if (!extend) m_anchor = m_caret;
    }

    void TextEditor::MoveWordLeft(bool extend)
    {
        m_lastKind = EditKind::None;
        m_caret = WordLeft(m_text, m_caret);
        if (!extend) m_anchor = m_caret;
    }

    void TextEditor::MoveWordRight(bool extend)
    {
        m_lastKind = EditKind::None;
        m_caret = WordRight(m_text, m_caret);
        if (!extend) m_anchor = m_caret;
    }

    void TextEditor::MoveHome(bool extend)
    {
        m_lastKind = EditKind::None;
        std::size_t p = m_caret;
        while (p > 0 && m_text[p - 1] != '\n') --p;
        m_caret = p;
        if (!extend) m_anchor = m_caret;
    }

    void TextEditor::MoveEnd(bool extend)
    {
        m_lastKind = EditKind::None;
        std::size_t p = m_caret;
        while (p < m_text.size() && m_text[p] != '\n') ++p;
        m_caret = p;
        if (!extend) m_anchor = m_caret;
    }

    void TextEditor::MoveToOffset(std::size_t byteOffset, bool extend)
    {
        m_lastKind = EditKind::None;
        m_caret = SnapToBoundary(m_text, std::min(byteOffset, m_text.size()));
        if (!extend) m_anchor = m_caret;
    }

    void TextEditor::SelectAll()
    {
        m_lastKind = EditKind::None;
        m_anchor = 0;
        m_caret = m_text.size();
    }

    void TextEditor::SelectWordAt(std::size_t byteOffset)
    {
        m_lastKind = EditKind::None;
        const std::size_t pos = SnapToBoundary(m_text, std::min(byteOffset, m_text.size()));
        if (m_text.empty())
        {
            m_anchor = m_caret = 0;
            return;
        }
        const std::size_t probe = (pos >= m_text.size()) ? PrevCp(m_text, pos) : pos;
        const CharClass cls = ClassAt(m_text, probe);
        std::size_t start = probe, end = NextCp(m_text, probe);
        while (start > 0 && ClassAt(m_text, PrevCp(m_text, start)) == cls) start = PrevCp(m_text, start);
        while (end < m_text.size() && ClassAt(m_text, end) == cls) end = NextCp(m_text, end);
        m_anchor = start;
        m_caret = end;
    }

    void TextEditor::SelectLineAt(std::size_t byteOffset)
    {
        m_lastKind = EditKind::None;
        const std::size_t pos = SnapToBoundary(m_text, std::min(byteOffset, m_text.size()));
        std::size_t start = pos;
        while (start > 0 && m_text[start - 1] != '\n') --start;
        std::size_t end = pos;
        while (end < m_text.size() && m_text[end] != '\n') ++end;
        m_anchor = start;
        m_caret = end;
    }

    bool TextEditor::Undo()
    {
        if (m_undo.empty()) return false;
        Edit e = std::move(m_undo.back());
        m_undo.pop_back();
        m_text.replace(e.Pos, e.Inserted.size(), e.Removed);
        m_caret = e.CaretBefore;
        m_anchor = e.AnchorBefore;
        m_redo.push_back(std::move(e));
        m_lastKind = EditKind::None;
        return true;
    }

    bool TextEditor::Redo()
    {
        if (m_redo.empty()) return false;
        Edit e = std::move(m_redo.back());
        m_redo.pop_back();
        m_text.replace(e.Pos, e.Removed.size(), e.Inserted);
        m_caret = m_anchor = e.CaretAfter;
        m_undo.push_back(std::move(e));
        m_lastKind = EditKind::None;
        return true;
    }
} // namespace lumora
