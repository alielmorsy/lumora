#include "Gallery.h"

namespace gallery {

Elem Card(Str title, Str caption, Children children)
{
    return Column({.className = "card"},
        Column({.className = "card-head"},
            Text(std::move(title), {.className = "card-title"}),
            Text(std::move(caption), {.className = "card-caption"})),
        Column({.className = "card-body"}, std::move(children)));
}

Elem SectionHeader(Str title, Str description)
{
    return Column({.className = "section-head"},
        Text(std::move(title), {.className = "section-title"}),
        Text(std::move(description), {.className = "section-desc"}));
}

Elem Label(Str text, std::string className)
{
    return Text(std::move(text), {.className = std::move(className)});
}

}
