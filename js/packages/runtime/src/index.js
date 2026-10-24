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

// JS mirror of the native tree, only for Solid's parent/sibling walks. Elements are { tag, native, parent, children }.
// Text nodes are { text, parent, native? }. Inside a `text` element they stay JS-only and the element's `text` prop is
// their concatenation. Anywhere else they get a native `text` node.
const isText = (n) => n.tag === undefined;
const placed = (parent, n) => !isText(n) || parent.tag !== "text";

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

function removeNode(parent, n) {
  parent.children.splice(parent.children.indexOf(n), 1);
  n.parent = null;
  if (placed(parent, n)) native.remove(parent.native, n.native);
  if (parent.tag === "text") syncText(parent);
}

// Solid moves nodes by inserting them again without removing them first.
export function insertNode(parent, n, anchor) {
  if (anchor === n) anchor = nextSibling(n);
  const old = n.parent;
  if (old) {
    old.children.splice(old.children.indexOf(n), 1);
    if (placed(old, n) && !placed(parent, n)) native.remove(old.native, n.native);
    if (old !== parent && old.tag === "text") syncText(old);
  }
  n.parent = parent;
  const kids = parent.children;
  const i = anchor ? kids.indexOf(anchor) : -1;
  if (i < 0) kids.push(n);
  else kids.splice(i, 0, n);
  if (placed(parent, n)) {
    if (!n.native) {
      n.native = native.createNode("text");
      native.setProp(n.native, "text", n.text);
    }
    let before = null;
    for (let j = i < 0 ? kids.length : i + 1; j < kids.length && !before; j++)
      if (placed(parent, kids[j])) before = kids[j].native;
    native.insert(parent.native, n.native, before);
  }
  if (parent.tag === "text") syncText(parent);
}

const renderer = createRenderer({
  createElement: (tag) => ({ tag, native: native.createNode(tag), parent: null, children: [] }),
  createTextNode: (text) => ({ text: String(text), parent: null }),
  isTextNode: isText,
  replaceText(n, text) {
    n.text = text;
    if (n.native) native.setProp(n.native, "text", text);
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
  const root = { tag: "root", native: native.root(), parent: null, children: [] };
  const dispose = renderer.render(code, root);
  return () => {
    dispose();
    while (root.children.length) removeNode(root, root.children[0]);
  };
}
