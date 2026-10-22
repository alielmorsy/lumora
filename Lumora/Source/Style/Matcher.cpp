#include "Style/Matcher.h"

namespace lumora::style
{
    bool MatchesCompound(const CompoundSelector& c, NodeHandle n, const MatchOps& ops)
    {
        if (n == 0) return false;
        if (c.root && !ops.isRoot(n)) return false;

        if (!c.classes.empty())
        {
            const std::vector<StringId>& have = ops.classes(n);
            for (StringId need : c.classes)
            {
                bool found = false;
                for (StringId h : have)
                {
                    if (h == need)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found) return false;
            }
        }

        if (c.pseudo != 0)
        {
            const PseudoMask active = ops.pseudo(n);
            if ((active & c.pseudo) != c.pseudo) return false;
        }
        return true;
    }

    namespace
    {
        // Match compounds[0..idx] with compounds[idx] anchored at `node`, walking ancestors leftward.
        bool MatchFrom(const Selector& sel, size_t idx, NodeHandle node, const MatchOps& ops)
        {
            if (!MatchesCompound(sel.compounds[idx], node, ops)) return false;
            if (idx == 0) return true;

            const Combinator comb = sel.combinators[idx - 1];
            const NodeHandle p = ops.parent(node);
            if (comb == Combinator::Child)
            {
                return p != 0 && MatchFrom(sel, idx - 1, p, ops);
            }
            // Descendant: try every ancestor (backtracking keeps it complete).
            NodeHandle anc = p;
            while (anc != 0)
            {
                if (MatchFrom(sel, idx - 1, anc, ops)) return true;
                anc = ops.parent(anc);
            }
            return false;
        }
    } // namespace

    bool MatchesSelector(const Selector& sel, NodeHandle n, const MatchOps& ops)
    {
        if (sel.compounds.empty()) return false;
        return MatchFrom(sel, sel.compounds.size() - 1, n, ops);
    }
} // namespace lumora::style
