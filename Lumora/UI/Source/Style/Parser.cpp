#include "Style/Properties.h"
#include "Style/Tokenizer.h"
#include "Style/ValueParse.h"

#include <lumora/Style/Css.h>
#include <lumora/log/Log.h>

#include <algorithm>
#include <bit>
#include <cctype>
#include <span>
#include <string>
#include <vector>

LUMORA_LOG_CATEGORY("Style")

namespace lumora::style
{
    namespace
    {
        [[nodiscard]] std::string ToLower(std::string_view s)
        {
            std::string out(s);
            for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return out;
        }

        [[nodiscard]] bool IEquals(std::string_view a, std::string_view b) noexcept
        {
            if (a.size() != b.size()) return false;
            for (size_t k = 0; k < a.size(); ++k)
                if (std::tolower(static_cast<unsigned char>(a[k])) != std::tolower(static_cast<unsigned char>(b[k])))
                    return false;
            return true;
        }

        // Parse-local selector structures (before `&`/nesting desugaring and finalisation).
        struct PCompound
        {
            std::vector<StringId> classes;
            PseudoMask pseudo = 0;
            bool universal = false;
            bool parentRef = false;
            bool root = false;
            bool valid = true;
        };

        struct PSelector
        {
            std::vector<PCompound> compounds;
            std::vector<Combinator> combinators; // size == compounds.size() - 1
            bool valid = true;
        };

        [[nodiscard]] bool IsSelectorStart(const Token& t)
        {
            if (t.type == TokType::Ident || t.type == TokType::Colon) return true;
            if (t.type == TokType::Delim) return t.text == "." || t.text == "*" || t.text == "&";
            return false;
        }

        struct Parser
        {
            std::span<const Token> toks;
            size_t pos = 0;
            Stylesheet& sheet;
            /// Properties declared but never read by the resolver, reported once per parse (deduped: one line per rule is noise).
            std::vector<std::string> unconsulted;

            [[nodiscard]] const Token& Cur() const { return toks[pos]; }
            [[nodiscard]] const Token& At(size_t k) const { return toks[k]; }
            [[nodiscard]] bool Is(TokType t) const { return toks[pos].type == t; }
            [[nodiscard]] bool Eof() const { return toks[pos].type == TokType::Eof; }


            PCompound ParseCompound(size_t& i, size_t end)
            {
                PCompound c;
                bool consumedAny = false;
                while (i < end)
                {
                    const Token& t = At(i);
                    // A leading-whitespace token (other than the first simple) ends the compound.
                    if (consumedAny && t.wsBefore) break;

                    if (t.type == TokType::Delim && t.text == "*")
                    {
                        c.universal = true;
                        ++i;
                        consumedAny = true;
                        continue;
                    }
                    if (t.type == TokType::Delim && t.text == "&")
                    {
                        c.parentRef = true;
                        ++i;
                        consumedAny = true;
                        continue;
                    }
                    if (t.type == TokType::Delim && t.text == ".")
                    {
                        ++i;
                        if (i < end && At(i).type == TokType::Ident)
                        {
                            c.classes.push_back(StringId(At(i).text));
                            ++i;
                        }
                        else c.valid = false;
                        consumedAny = true;
                        continue;
                    }
                    if (t.type == TokType::Colon)
                    {
                        ++i;
                        // `::` pseudo-element → unsupported, drop the selector.
                        if (i < end && At(i).type == TokType::Colon)
                        {
                            c.valid = false;
                            ++i;
                            if (i < end && At(i).type == TokType::Ident) ++i;
                            consumedAny = true;
                            continue;
                        }
                        if (i < end && At(i).type == TokType::Ident)
                        {
                            const std::string name = ToLower(At(i).text);
                            ++i;
                            if (name == "hover") c.pseudo = c.pseudo | static_cast<uint8_t>(Pseudo::Hover);
                            else if (name == "active") c.pseudo = c.pseudo | static_cast<uint8_t>(Pseudo::Active);
                            else if (name == "focus") c.pseudo = c.pseudo | static_cast<uint8_t>(Pseudo::Focus);
                            else if (name == "disabled") c.pseudo = c.pseudo | static_cast<uint8_t>(Pseudo::Disabled);
                            else if (name == "checked") c.pseudo = c.pseudo | static_cast<uint8_t>(Pseudo::Checked);
                            else if (name == "drop-active") c.pseudo = c.pseudo | static_cast<uint8_t>(Pseudo::DropActive);
                            else if (name == "root") c.root = true;
                            else c.valid = false; // unsupported pseudo-class → drop
                        }
                        else c.valid = false;
                        consumedAny = true;
                        continue;
                    }
                    if (t.type == TokType::Ident)
                    {
                        // A bare type selector (`div`) - unsupported (classes only). Drop.
                        c.valid = false;
                        ++i;
                        consumedAny = true;
                        continue;
                    }
                    break;
                }
                return c;
            }

            PSelector ParseComplex(size_t begin, size_t end)
            {
                PSelector sel;
                size_t i = begin;

                // A leading combinator (nesting form `> .x`) implies a parent reference.
                if (i < end && At(i).type == TokType::Delim && (At(i).text == ">" || At(i).text == "+" || At(i).text ==
                    "~"))
                {
                    PCompound ref;
                    ref.parentRef = true;
                    sel.compounds.push_back(ref);
                    if (At(i).text == ">") sel.combinators.push_back(Combinator::Child);
                    else
                    {
                        sel.valid = false;
                        sel.combinators.push_back(Combinator::Descendant);
                    }
                    ++i;
                }

                while (i < end)
                {
                    while (i < end && (At(i).type == TokType::Delim && (At(i).text == ">")))
                    {
                        // Shouldn't normally hit here (handled below), but guard.
                        break;
                    }
                    PCompound c = ParseCompound(i, end);
                    if (!c.valid) sel.valid = false;
                    sel.compounds.push_back(std::move(c));

                    if (i >= end) break;
                    const Token& t = At(i);
                    if (t.type == TokType::Delim && t.text == ">")
                    {
                        sel.combinators.push_back(Combinator::Child);
                        ++i;
                    }
                    else if (t.type == TokType::Delim && (t.text == "+" || t.text == "~"))
                    {
                        sel.valid = false; // sibling combinators out of scope
                        sel.combinators.push_back(Combinator::Descendant);
                        ++i;
                    }
                    else if (IsSelectorStart(t) && t.wsBefore)
                    {
                        sel.combinators.push_back(Combinator::Descendant);
                    }
                    else
                    {
                        break;
                    }
                }
                return sel;
            }

            std::vector<PSelector> ParseSelectorList(size_t begin, size_t end)
            {
                std::vector<PSelector> list;
                size_t start = begin;
                for (size_t k = begin; k <= end; ++k)
                {
                    if (k == end || At(k).type == TokType::Comma)
                    {
                        if (k > start) list.push_back(ParseComplex(start, k));
                        start = k + 1;
                    }
                }
                return list;
            }

            // Substitute parent references / apply implicit descendant nesting.
            static std::vector<PSelector> Combine(const std::vector<PSelector>& parents, const PSelector& child)
            {
                const bool hasRef = [&]
                {
                    for (const auto& c : child.compounds)
                        if (c.parentRef) return true;
                    return false;
                }();

                std::vector<PSelector> out;

                if (parents.empty())
                {
                    // Top level: a stray parentRef is invalid, otherwise use as-is.
                    PSelector s = child;
                    if (hasRef) s.valid = false;
                    out.push_back(std::move(s));
                    return out;
                }

                for (const PSelector& p : parents)
                {
                    PSelector s;
                    s.valid = p.valid && child.valid;
                    if (hasRef)
                    {
                        for (size_t k = 0; k < child.compounds.size(); ++k)
                        {
                            if (k > 0) s.combinators.push_back(child.combinators[k - 1]);
                            const PCompound& ci = child.compounds[k];
                            if (ci.parentRef)
                            {
                                for (size_t j = 0; j < p.compounds.size(); ++j)
                                {
                                    if (j > 0) s.combinators.push_back(p.combinators[j - 1]);
                                    s.compounds.push_back(p.compounds[j]);
                                }
                                PCompound& last = s.compounds.back();
                                for (StringId cl : ci.classes) last.classes.push_back(cl);
                                last.pseudo = last.pseudo | ci.pseudo;
                                last.root = last.root || ci.root;
                                last.universal = last.universal || ci.universal;
                            }
                            else
                            {
                                s.compounds.push_back(ci);
                            }
                        }
                    }
                    else
                    {
                        // Implicit descendant: parent  child
                        s.compounds = p.compounds;
                        s.combinators = p.combinators;
                        s.combinators.push_back(Combinator::Descendant);
                        for (const auto& c : child.compounds) s.compounds.push_back(c);
                        for (auto cb : child.combinators) s.combinators.push_back(cb);
                    }
                    out.push_back(std::move(s));
                }
                return out;
            }

            static bool Finalize(const PSelector& p, Selector& out)
            {
                if (!p.valid) return false;
                uint32_t spec = 0;
                bool root = false;
                for (const PCompound& c : p.compounds)
                {
                    if (c.parentRef) return false; // unresolved
                    if (!c.valid) return false;
                    CompoundSelector cs;
                    cs.classes = c.classes;
                    cs.pseudo = c.pseudo;
                    cs.universal = c.universal;
                    cs.root = c.root;
                    spec += static_cast<uint32_t>(c.classes.size());
                    spec += static_cast<uint32_t>(std::popcount(c.pseudo));
                    if (c.root)
                    {
                        root = true;
                        ++spec;
                    }
                    out.compounds.push_back(std::move(cs));
                }
                if (out.compounds.empty()) return false;
                out.combinators = p.combinators;
                out.specificity = spec;
                out.matchesRoot = root;
                return true;
            }

            [[nodiscard]] static bool IsPureRoot(const Selector& s)
            {
                return s.matchesRoot && s.compounds.size() == 1 && s.compounds[0].classes.empty() &&
                    s.compounds[0].pseudo == 0;
            }


            void ParseDeclaration(size_t begin, size_t end, std::vector<Declaration>& out)
            {
                size_t i = begin;
                if (i >= end || At(i).type != TokType::Ident) return;
                Declaration decl;
                const std::string& rawName = At(i).text;
                decl.custom = rawName.rfind("--", 0) == 0;
                decl.prop = StringId(decl.custom ? rawName : ToLower(rawName));
                ++i;
                if (i >= end || At(i).type != TokType::Colon) return;
                ++i;

                // Detect trailing `! important`.
                size_t valueEnd = end;
                for (size_t k = i; k + 1 < end; ++k)
                {
                    if (At(k).type == TokType::Delim && At(k).text == "!" && At(k + 1).type == TokType::Ident &&
                        IEquals(At(k + 1).text, "important"))
                    {
                        decl.important = true;
                        valueEnd = k;
                        break;
                    }
                }

                if (valueEnd <= i) return;
                decl.value = ParseValue(toks.subspan(i, valueEnd - i));
                if (!decl.value.Valid()) return;

                // Audited here, not at apply time: StringId is lossy in release, so this is the last place the property's spelling
                // exists.
                if (!decl.custom)
                {
                    const std::string lowered = ToLower(rawName);
                    if (!IsConsultedProperty(lowered) &&
                        std::find(unconsulted.begin(), unconsulted.end(), lowered) == unconsulted.end())
                    {
                        unconsulted.push_back(lowered);
                    }
                }
                out.push_back(std::move(decl));
            }


            // Parse a `{ ... }` block. `parents` are the combined parent selectors (empty at top level); `ownDecls` collects
            // declarations for them; nested rules and @media go to `ruleSink` / the sheet.
            void ParseBlock(const std::vector<PSelector>& parents, std::vector<Declaration>& ownDecls,
                            std::vector<Rule>& ruleSink)
            {
                // assumes Cur() is LBrace
                ++pos;
                while (!Eof() && !Is(TokType::RBrace))
                {
                    if (Is(TokType::Semicolon))
                    {
                        ++pos;
                        continue;
                    }
                    if (Is(TokType::AtKeyword))
                    {
                        ParseAtRule(parents, ruleSink);
                        continue;
                    }

                    // Look ahead for the first top-level `{`, `;`, or `}`.
                    size_t scan = pos;
                    int depth = 0;
                    enum { Rule, Decl } kind = Decl;
                    for (; scan < toks.size(); ++scan)
                    {
                        const TokType tt = toks[scan].type;
                        if (tt == TokType::Function || tt == TokType::LParen) ++depth;
                        else if (tt == TokType::RParen) { if (depth > 0) --depth; }
                        else if (tt == TokType::Eof) { break; }
                        else if (depth == 0)
                        {
                            if (tt == TokType::LBrace)
                            {
                                kind = Rule;
                                break;
                            }
                            if (tt == TokType::Semicolon)
                            {
                                kind = Decl;
                                break;
                            }
                            if (tt == TokType::RBrace)
                            {
                                kind = Decl;
                                break;
                            }
                        }
                    }

                    if (kind == Rule)
                    {
                        ParseQualifiedRule(pos, scan, parents, ruleSink);
                        pos = scan; // now at LBrace
                        SkipBlock();
                    }
                    else
                    {
                        ParseDeclaration(pos, scan, ownDecls);
                        pos = scan;
                        if (Is(TokType::Semicolon)) ++pos;
                    }
                }
                if (Is(TokType::RBrace)) ++pos;
            }

            // Parse the selector prelude [begin,end) and its following block (at `end`, an LBrace),
            // combining with `parents` and emitting the produced rules into `ruleSink`.
            void ParseQualifiedRule(size_t begin, size_t end, const std::vector<PSelector>& parents,
                                    std::vector<Rule>& ruleSink)
            {
                std::vector<PSelector> raw = ParseSelectorList(begin, end);
                std::vector<PSelector> combined;
                for (const PSelector& r : raw)
                    for (PSelector& c : Combine(parents, r)) combined.push_back(std::move(c));

                // Reserve source order before the block so this rule's own declarations rank ahead
                // of any rules nested inside it.
                const uint32_t myOrder = sheet.ruleCount++;

                const size_t savedPos = pos;
                pos = end;

                std::vector<Declaration> decls;
                ParseBlock(combined, decls, ruleSink);

                pos = savedPos; // the caller re-skips the block to advance past it

                if (decls.empty()) return;

                std::vector<Selector> finals;
                for (const PSelector& p : combined)
                {
                    Selector s;
                    if (Finalize(p, s)) finals.push_back(std::move(s));
                }
                if (finals.empty()) return;

                Rule rule;
                rule.selectors = std::move(finals);
                rule.declarations = std::move(decls);
                rule.order = myOrder;
                ruleSink.push_back(std::move(rule));
            }

            void SkipBlock()
            {
                // assumes Cur() is LBrace - skip to matching RBrace.
                if (!Is(TokType::LBrace)) return;
                int depth = 0;
                for (; !Eof(); ++pos)
                {
                    if (Is(TokType::LBrace)) ++depth;
                    else if (Is(TokType::RBrace))
                    {
                        --depth;
                        if (depth == 0)
                        {
                            ++pos;
                            return;
                        }
                    }
                }
            }


            void ParseAtRule(const std::vector<PSelector>& parents, std::vector<Rule>& ruleSink)
            {
                const std::string name = ToLower(Cur().text);
                if (name == "media")
                {
                    ParseMedia(parents);
                    return;
                }
                if (name == "keyframes" || name == "-webkit-keyframes")
                {
                    ParseKeyframes();
                    return;
                }
                // Unknown at-rule: skip its prelude and optional block.
                ++pos;
                while (!Eof() && !Is(TokType::LBrace) && !Is(TokType::Semicolon)) ++pos;
                if (Is(TokType::Semicolon))
                {
                    ++pos;
                    return;
                }
                if (Is(TokType::LBrace)) SkipBlock();
            }

            void ParseMedia(const std::vector<PSelector>& parents)
            {
                ++pos;
                MediaCondition cond = ParseMediaCondition();
                if (!Is(TokType::LBrace))
                {
                    // malformed - recover
                    while (!Eof() && !Is(TokType::LBrace) && !Is(TokType::Semicolon)) ++pos;
                    if (Is(TokType::Semicolon))
                    {
                        ++pos;
                        return;
                    }
                    if (!Is(TokType::LBrace)) return;
                }

                MediaGroup group;
                group.condition = std::move(cond);
                std::vector<Declaration> decls; // bare declarations under nested @media apply to parents
                ParseBlock(parents, decls, group.rules);
                if (!decls.empty() && !parents.empty())
                {
                    std::vector<Selector> finals;
                    for (const PSelector& p : parents)
                    {
                        Selector s;
                        if (Finalize(p, s)) finals.push_back(std::move(s));
                    }
                    if (!finals.empty())
                    {
                        Rule rule;
                        rule.selectors = std::move(finals);
                        rule.declarations = std::move(decls);
                        rule.order = sheet.ruleCount++;
                        group.rules.push_back(std::move(rule));
                    }
                }
                if (!group.rules.empty()) sheet.media.push_back(std::move(group));
            }

            MediaCondition ParseMediaCondition()
            {
                MediaCondition cond;
                // Read up to '{': tokens are like `screen and (min-width: 600px) and (max-width: 900px)`.
                while (!Eof() && !Is(TokType::LBrace))
                {
                    if (Is(TokType::LParen))
                    {
                        ++pos;
                        std::string feat;
                        if (Is(TokType::Ident))
                        {
                            feat = ToLower(Cur().text);
                            ++pos;
                        }
                        float value = 0.f;
                        if (Is(TokType::Colon)) ++pos;
                        if (Is(TokType::Dimension))
                        {
                            const std::string u = ToLower(Cur().text);
                            float v = Cur().num;
                            if (u == "em" || u == "rem") v *= 16.f; // root font size default; refined in the resolver
                            else if (u == "pt") v *= 96.f / 72.f;
                            else if (u == "pc") v *= 16.f;
                            else if (u == "in") v *= 96.f;
                            else if (u == "cm") v *= 96.f / 2.54f;
                            else if (u == "mm") v *= 96.f / 25.4f;
                            value = v;
                            ++pos;
                        }
                        else if (Is(TokType::Number))
                        {
                            value = Cur().num;
                            ++pos;
                        }
                        while (!Eof() && !Is(TokType::RParen) && !Is(TokType::LBrace)) ++pos;
                        if (Is(TokType::RParen)) ++pos;

                        MediaFeature f;
                        if (feat == "min-width") f.type = MediaFeature::Type::MinWidth;
                        else if (feat == "max-width") f.type = MediaFeature::Type::MaxWidth;
                        else if (feat == "min-height") f.type = MediaFeature::Type::MinHeight;
                        else if (feat == "max-height") f.type = MediaFeature::Type::MaxHeight;
                        else continue; // unknown feature: ignore
                        f.value = value;
                        cond.features.push_back(f);
                    }
                    else
                    {
                        ++pos; // skip `screen`, `and`, `,`, etc. (media types / operators)
                    }
                }
                return cond;
            }

            void ParseKeyframes()
            {
                ++pos;
                KeyframesDef def;
                if (Is(TokType::Ident) || Is(TokType::String))
                {
                    def.name = StringId(Cur().text);
                    ++pos;
                }
                if (!Is(TokType::LBrace)) return;
                ++pos;
                while (!Eof() && !Is(TokType::RBrace))
                {
                    // stop selectors: percentages / from / to, comma-separated, until '{'
                    std::vector<float> offsets;
                    while (!Eof() && !Is(TokType::LBrace) && !Is(TokType::RBrace))
                    {
                        if (Is(TokType::Percentage))
                        {
                            offsets.push_back(Cur().num / 100.f);
                            ++pos;
                        }
                        else if (Is(TokType::Number))
                        {
                            offsets.push_back(Cur().num / 100.f);
                            ++pos;
                        }
                        else if (Is(TokType::Ident))
                        {
                            const std::string kw = ToLower(Cur().text);
                            if (kw == "from") offsets.push_back(0.f);
                            else if (kw == "to") offsets.push_back(1.f);
                            ++pos;
                        }
                        else ++pos;
                    }
                    if (!Is(TokType::LBrace)) break;

                    std::vector<Declaration> decls;
                    std::vector<Rule> discard; // keyframe blocks don't nest rules
                    std::vector<PSelector> none;
                    ParseBlock(none, decls, discard);

                    for (float off : offsets)
                    {
                        KeyframeStop stop;
                        stop.offset = off;
                        stop.declarations = decls;
                        def.stops.push_back(std::move(stop));
                    }
                }
                if (Is(TokType::RBrace)) ++pos;
                if (def.name != StringId{}) sheet.keyframes[def.name] = std::move(def);
            }


            void ParseTopLevel()
            {
                std::vector<PSelector> noParents;
                while (!Eof())
                {
                    if (Is(TokType::Semicolon))
                    {
                        ++pos;
                        continue;
                    }
                    if (Is(TokType::AtKeyword))
                    {
                        ParseAtRule(noParents, sheet.rules);
                        continue;
                    }

                    size_t scan = pos;
                    int depth = 0;
                    bool foundBrace = false;
                    for (; scan < toks.size(); ++scan)
                    {
                        const TokType tt = toks[scan].type;
                        if (tt == TokType::Function || tt == TokType::LParen) ++depth;
                        else if (tt == TokType::RParen) { if (depth > 0) --depth; }
                        else if (tt == TokType::Eof) break;
                        else if (depth == 0 && tt == TokType::LBrace)
                        {
                            foundBrace = true;
                            break;
                        }
                    }
                    if (!foundBrace) break; // trailing garbage

                    ParseTopRule(pos, scan);
                    pos = scan;
                    SkipBlock();
                }
            }

            // Like ParseQualifiedRule but routes a pure `:root { }` block's declarations into rootVars.
            void ParseTopRule(size_t begin, size_t end)
            {
                std::vector<PSelector> raw = ParseSelectorList(begin, end);
                std::vector<PSelector> empty;
                std::vector<PSelector> combined;
                for (const PSelector& r : raw)
                    for (PSelector& c : Combine(empty, r)) combined.push_back(std::move(c));

                std::vector<Selector> finals;
                for (const PSelector& p : combined)
                {
                    Selector s;
                    if (Finalize(p, s)) finals.push_back(std::move(s));
                }

                const uint32_t myOrder = sheet.ruleCount++;

                const size_t savedPos = pos;
                pos = end;

                std::vector<Declaration> decls;
                std::vector<Rule> nested;
                ParseBlock(combined, decls, nested);

                pos = savedPos;

                for (Rule& r : nested) sheet.rules.push_back(std::move(r));

                if (decls.empty() || finals.empty()) return;

                if (finals.size() == 1 && IsPureRoot(finals[0]))
                {
                    for (Declaration& d : decls) sheet.rootVars.push_back(std::move(d));
                    return;
                }

                Rule rule;
                rule.selectors = std::move(finals);
                rule.declarations = std::move(decls);
                rule.order = myOrder;
                sheet.rules.push_back(std::move(rule));
            }
        };
    } // namespace

    Stylesheet ParseStylesheet(std::string_view source)
    {
        Stylesheet sheet;
        const std::vector<Token> tokens = Tokenize(source);
        Parser parser{std::span<const Token>(tokens), 0, sheet};
        parser.ParseTopLevel();
        for (const std::string& prop : parser.unconsulted)
        {
            LOG_WARN("stylesheet declares '{}', which this resolver never reads -- it will have no effect "
                     "and no error. Either it is a typo, or the property is not implemented (see "
                     "Style/Properties.h for the list that is)", prop);
        }
        return sheet;
    }

    void MergeStylesheet(Stylesheet& into, Stylesheet&& addend)
    {
        const uint32_t base = into.ruleCount;
        for (Rule& r : addend.rules)
        {
            r.order += base;
            into.rules.push_back(std::move(r));
        }
        for (MediaGroup& g : addend.media)
        {
            for (Rule& r : g.rules) r.order += base;
            into.media.push_back(std::move(g));
        }
        for (auto& [name, def] : addend.keyframes) into.keyframes[name] = std::move(def);
        for (Declaration& d : addend.rootVars) into.rootVars.push_back(std::move(d));
        into.ruleCount += addend.ruleCount;
    }
} // namespace lumora::style
