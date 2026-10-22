#include "Style/Resolve.h"

#include <vector>

namespace lumora::style
{
    namespace
    {
        // The inherited properties (CSS inheritance flows these when a node does not declare them).
        const StringId kInherited[] = {
            "color"_sid,     "font-family"_sid,  "font-size"_sid, "font-weight"_sid,
            "font-style"_sid, "line-height"_sid, "text-align"_sid,
        };

        [[nodiscard]] CssValue ResolveValue(const CssValue& v, const VarMap& vars, std::vector<StringId>& stack)
        {
            if (v.kind == ValueKind::VarRef)
            {
                for (StringId s : stack)
                    if (s == v.keyword) return {}; // cycle → invalid
                if (auto it = vars.find(v.keyword); it != vars.end())
                {
                    stack.push_back(v.keyword);
                    CssValue r = ResolveValue(it->second, vars, stack);
                    stack.pop_back();
                    if (r.Valid()) return r;
                }
                if (!v.list.empty()) return ResolveValue(v.list[0], vars, stack); // fallback
                return {};
            }
            if (v.kind == ValueKind::List || v.kind == ValueKind::Function)
            {
                CssValue out = v;
                for (CssValue& c : out.list) c = ResolveValue(c, vars, stack);
                return out;
            }
            return v;
        }

        void ComputeUnit(CssValue& v, const ComputeContext& ctx)
        {
            if (v.kind == ValueKind::Length)
            {
                switch (v.unit)
                {
                case Unit::Em: v = CssValue::Px(v.num * ctx.fontSize); break;
                case Unit::Rem: v = CssValue::Px(v.num * ctx.rootFontSize); break;
                case Unit::Vw: v = CssValue::Px(v.num / 100.f * ctx.viewport.x); break;
                case Unit::Vh: v = CssValue::Px(v.num / 100.f * ctx.viewport.y); break;
                default: break; // Px and Percent stay symbolic
                }
            }
            else if (v.kind == ValueKind::List || v.kind == ValueKind::Function)
            {
                for (CssValue& c : v.list) ComputeUnit(c, ctx);
            }
        }

        [[nodiscard]] bool HasViewportUnit(const CssValue& v)
        {
            if (v.kind == ValueKind::Length) return v.unit == Unit::Vw || v.unit == Unit::Vh;
            if (v.kind == ValueKind::List || v.kind == ValueKind::Function)
                for (const CssValue& c : v.list)
                    if (HasViewportUnit(c)) return true;
            return false;
        }
    } // namespace

    void ResolveVars(ComputedStyle& cs, const VarMap& vars)
    {
        for (auto& [prop, val] : cs.props)
        {
            std::vector<StringId> stack;
            val = ResolveValue(val, vars, stack);
        }
    }

    float ResolveFontSize(ComputedStyle& cs, const InheritedStyle* parent, float rootFontSize)
    {
        float parentFs = rootFontSize;
        if (parent)
            if (auto it = parent->props.find("font-size"_sid);
                it != parent->props.end() && it->second.kind == ValueKind::Length && it->second.unit == Unit::Px)
                parentFs = it->second.num;

        auto it = cs.props.find("font-size"_sid);
        if (it == cs.props.end()) return parentFs; // inherited

        float px = parentFs;
        const CssValue& v = it->second;
        if (v.kind == ValueKind::Length)
        {
            switch (v.unit)
            {
            case Unit::Px: px = v.num; break;
            case Unit::Em: px = v.num * parentFs; break;
            case Unit::Rem: px = v.num * rootFontSize; break;
            case Unit::Percent: px = v.num / 100.f * parentFs; break;
            default: px = v.num; break;
            }
        }
        else if (v.kind == ValueKind::Number) px = v.num;

        it->second = CssValue::Px(px); // pin to px so ComputeUnits does not re-scale it
        return px;
    }

    void ComputeUnits(ComputedStyle& cs, const ComputeContext& ctx)
    {
        for (auto& [prop, val] : cs.props) ComputeUnit(val, ctx);
    }

    bool UsesViewportUnits(const ComputedStyle& cs)
    {
        for (const auto& [prop, val] : cs.props)
            if (HasViewportUnit(val)) return true;
        return false;
    }

    InheritedStyle ApplyInheritance(ComputedStyle& cs, const InheritedStyle* parent, const VarMap& vars)
    {
        InheritedStyle out;
        out.vars = vars;
        for (StringId p : kInherited)
        {
            if (auto it = cs.props.find(p); it != cs.props.end())
            {
                if (it->second.kind == ValueKind::Keyword && it->second.keyword == "inherit"_sid)
                {
                    const CssValue* pv = parent ? [&]() -> const CssValue* {
                        auto pit = parent->props.find(p);
                        return pit != parent->props.end() ? &pit->second : nullptr;
                    }() : nullptr;
                    if (pv) it->second = *pv;
                    else cs.props.erase(p);
                }
                else if (it->second.kind == ValueKind::Keyword && it->second.keyword == "initial"_sid)
                {
                    cs.props.erase(p); // fall back to the widget/inline default
                }
            }

            if (!cs.props.contains(p) && parent)
                if (auto pit = parent->props.find(p); pit != parent->props.end())
                    cs.props[p] = pit->second; // inject the inherited value

            if (auto kt = cs.props.find(p); kt != cs.props.end()) out.props[p] = kt->second;
        }
        return out;
    }
} // namespace lumora::style
