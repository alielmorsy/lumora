#include "Svg/SvgDocument.h"

#include "Svg/SvgBuilder.h"
#include "Svg/XmlReader.h"

namespace lumora::svg
{
    namespace
    {
        [[nodiscard]] const XmlNode* FindSvg(const XmlNode& n)
        {
            if (n.tag == "svg")
            {
                return &n;
            }
            for (const XmlNode& c : n.children)
            {
                if (const XmlNode* found = FindSvg(c))
                {
                    return found;
                }
            }
            return nullptr;
        }
    } // namespace

    SvgScene ParseSvg(std::string_view svgText)
    {
        XmlNode root;
        if (!ParseXml(svgText, root))
        {
            return {};
        }
        const XmlNode* svg = FindSvg(root);
        if (!svg)
        {
            return {};
        }
        return BuildScene(*svg);
    }
} // namespace lumora::svg
