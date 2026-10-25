// In-memory stand-in for the host's globalThis.__lumora. Every call is recorded in `calls`.
const TAGS = new Set(["view", "text", "button", "input", "image", "scroll"]);

export function createFakeNative({ modules = {} } = {}) {
  const calls = [];
  const node = (tag) => ({ tag, props: {}, handlers: {}, children: [], parent: null });
  const root = node("root");
  const unlink = (child) => {
    if (child.parent) child.parent.children.splice(child.parent.children.indexOf(child), 1);
    child.parent = null;
  };
  const api = {
    root: () => root,
    createNode(tag) {
      calls.push(["createNode", tag]);
      if (!TAGS.has(tag)) throw new TypeError(`unknown tag "${tag}"`);
      return node(tag);
    },
    // Like the host, a node goes live at its first insert, so the parent must already be reachable from the root.
    // `firstInsert` is the index in `calls` of that first insert.
    insert(parent, child, before) {
      calls.push(["insert", parent, child, before]);
      if (before && before.parent !== parent) throw new Error("insert: before is not a child of parent");
      let p = parent;
      while (p && p !== root) p = p.parent;
      if (!p) throw new Error("insert: parent is not attached to the root");
      child.firstInsert ??= calls.length - 1;
      unlink(child);
      const i = before ? parent.children.indexOf(before) : parent.children.length;
      parent.children.splice(i, 0, child);
      child.parent = parent;
    },
    remove(parent, child) {
      calls.push(["remove", parent, child]);
      if (child.parent !== parent) throw new Error("remove: not a child of parent");
      unlink(child);
    },
    setProp(n, name, value) {
      calls.push(["setProp", n, name, value]);
      if (value === null) delete n.props[name];
      else n.props[name] = value;
    },
    setHandler(n, event, fn) {
      calls.push(["setHandler", n, event, fn]);
      if (fn) n.handlers[event] = fn;
      else delete n.handlers[event];
    },
    setStylesheet(css) {
      calls.push(["setStylesheet", css]);
    },
    log(level, msg) {
      calls.push(["log", level, msg]);
    },
    requireModule: (name) => modules[name],
  };
  const snap = (n) => ({ tag: n.tag, props: { ...n.props }, handlers: { ...n.handlers }, children: n.children.map(snap) });
  return { api, calls, tree: () => snap(root) };
}
