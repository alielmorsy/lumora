import "./console.js";
import "./timers.js";
import { createRenderer } from "solid-js/universal";
import { native } from "./native.js";
import { setProperty } from "./props.js";

export {
  createSignal,
  createEffect,
  createMemo,
  createResource,
  createContext,
  useContext,
  batch,
  untrack,
  on,
  onMount,
  onCleanup,
  Show,
  For,
  Index,
  Switch,
  Match,
  ErrorBoundary,
  Suspense,
} from "solid-js";

// JS mirror of the native tree, only for Solid's parent/sibling walks. Elements are { tag, native, parent, children },
// text nodes are { text, parent, native? }. `linked` means the native node is inserted in the parent's native node.
// A node is only linked once its parent is attached to the root, so a template's static props (some of them
// creation-only in the host) are all set before its first native insert. Detaching keeps descendants linked under
// the detached node, so re-attaching inserts the same native nodes again.
// Inside a `text` element, text nodes stay JS-only and the element's `text` prop is their concatenation. Elsewhere a
// text node gets a native `text` node once it has text, and keeps it after that.
const isText = (n) => n.tag === undefined;
const placed = (parent, n) => !isText(n) || parent.tag !== "text";

function attached(n) {
  for (; n; n = n.parent) {
    if (n.root) return true;
    if (!n.linked) return false;
  }
  return false;
}

function syncText(el) {
  const text = el.children.filter(isText).map((c) => c.text).join("");
  if (text === el.text) return;
  el.text = text;
  native.setProp(el.native, "text", text);
}

// ponytail: indexOf makes sibling walks O(n), fine for UI-sized child lists
function nextSibling(n) {
  const s = n.parent?.children;
  return s ? s[s.indexOf(n) + 1] : undefined;
}

function nativeBefore(parent, n) {
  const kids = parent.children;
  for (let j = kids.indexOf(n) + 1; j < kids.length; j++) if (kids[j].linked) return kids[j].native;
  return null;
}

// parent is attached. Links n (if not already) and then its unlinked descendants in child order.
function link(parent, n) {
  if (!placed(parent, n)) return;
  if (!n.linked) {
    if (!n.native) {
      if (!n.text) return;
      n.native = native.createNode("text");
      native.setProp(n.native, "text", n.text);
    }
    native.insert(parent.native, n.native, nativeBefore(parent, n));
    n.linked = true;
  }
  if (!isText(n)) for (const c of n.children) link(n, c);
}

function removeNode(parent, n) {
  const i = parent.children.indexOf(n);
  if (i < 0) return;
  parent.children.splice(i, 1);
  n.parent = null;
  if (n.linked) {
    native.remove(parent.native, n.native);
    n.linked = false;
  }
  if (parent.tag === "text") syncText(parent);
}

// Solid moves nodes by inserting them again without removing them first.
export function insertNode(parent, n, anchor) {
  if (anchor === n) anchor = nextSibling(n);
  const old = n.parent;
  if (old) {
    old.children.splice(old.children.indexOf(n), 1);
    if (old !== parent && old.tag === "text") syncText(old);
  }
  n.parent = parent;
  const kids = parent.children;
  const i = anchor ? kids.indexOf(anchor) : -1;
  if (i < 0) kids.push(n);
  else kids.splice(i, 0, n);
  const live = placed(parent, n) && attached(parent);
  if (n.linked) {
    if (live) native.insert(parent.native, n.native, nativeBefore(parent, n));
    else {
      native.remove(old.native, n.native);
      n.linked = false;
    }
  }
  if (live) link(parent, n);
  if (parent.tag === "text") syncText(parent);
}

const renderer = createRenderer({
  createElement: (tag) => ({ tag, native: native.createNode(tag), parent: null, children: [] }),
  createTextNode: (text) => ({ text: String(text), parent: null }),
  isTextNode: isText,
  replaceText(n, text) {
    n.text = text;
    if (n.native) native.setProp(n.native, "text", text);
    else if (n.parent && attached(n.parent)) link(n.parent, n);
    if (n.parent?.tag === "text") syncText(n.parent);
  },
  setProperty,
  insertNode,
  removeNode,
  getParentNode: (n) => n.parent ?? undefined,
  getFirstChild: (n) => n.children?.[0],
  getNextSibling: nextSibling,
});

export const { effect, memo, createComponent, createElement, createTextNode, insert, spread, setProp, mergeProps, use } =
  renderer;

export function render(code) {
  const css = globalThis.__lumoraCss ?? [];
  if (css.length) native.setStylesheet(css.join("\n"));
  const root = { tag: "root", root: true, native: native.root(), parent: null, children: [] };
  const dispose = renderer.render(code, root);
  return () => {
    dispose();
    while (root.children.length) removeNode(root, root.children[0]);
  };
}
