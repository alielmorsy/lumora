#pragma once

#include <string>
#include <string_view>

namespace lumora {

/// Clipboard seam; the host swaps in a platform backend via LumoraDesc. Text-only.
class Clipboard {
public:
    virtual ~Clipboard() = default;

    virtual void                            SetText(std::string_view text) = 0;
    [[nodiscard]] virtual const std::string& GetText() const               = 0;
};

/// Default: process-local string, no OS integration.
class DefaultClipboard final : public Clipboard {
public:
    void SetText(std::string_view text) override { m_text.assign(text); }

    [[nodiscard]] const std::string& GetText() const override { return m_text; }

private:
    std::string m_text;
};

}
