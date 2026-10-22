#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace lumora::svg
{
    struct XmlAttr
    {
        std::string name;  ///< local name; namespace prefix stripped except `xlink:` is kept on href
        std::string value; ///< entity-decoded
    };

    /// A parsed XML element. Text content and processing instructions are dropped (SVG <text> is out of scope).
    struct XmlNode
    {
        std::string tag; ///< element local name (prefix stripped), e.g. "svg", "g", "path"
        std::vector<XmlAttr> attrs;
        std::vector<XmlNode> children;

        [[nodiscard]] const std::string* Attr(std::string_view name) const noexcept;
    };

    /// Parse an XML/SVG document into its root element. Tolerant of prolog, comments, DOCTYPE and CDATA.
    /// Returns false when no root element is found.
    bool ParseXml(std::string_view text, XmlNode& out);
} // namespace lumora::svg
