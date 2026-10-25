import { native } from "./native.js";

const EVENTS = {
  onClick: "click",
  onDoubleClick: "dblclick",
  onPress: "press",
  onRelease: "release",
  onPointerMove: "move",
  onPointerEnter: "enter",
  onPointerLeave: "leave",
  onScroll: "scroll",
  onFocus: "focus",
  onBlur: "blur",
  onKeyDown: "keydown",
  onKeyUp: "keyup",
  onInput: "input",
  onSubmit: "submit",
};
const UNITLESS = new Set(["opacity", "flex", "flexGrow", "flexShrink", "zIndex", "order", "fontWeight", "lineHeight", "aspectRatio"]);
const warned = new Set();

export function styleString(style) {
  if (style == null || typeof style !== "object") return style;
  return Object.entries(style)
    .filter(([, v]) => v != null && v !== "")
    .map(([k, v]) => {
      const camel = k.replace(/-([a-z])/g, (_, c) => c.toUpperCase());
      const kebab = camel.replace(/[A-Z]/g, (c) => "-" + c.toLowerCase());
      return `${kebab}: ${typeof v === "number" && !UNITLESS.has(camel) ? v + "px" : v}`;
    })
    .join("; ");
}

export function setProperty(node, name, value) {
  if (/^on[A-Z]/.test(name)) {
    const event = EVENTS[name];
    if (event) native.setHandler(node.native, event, typeof value === "function" ? value : null);
    else if (!warned.has(name)) {
      warned.add(name);
      console.warn(`@lumora/runtime: unknown event prop "${name}"`);
    }
    return;
  }
  if (name === "className") name = "class";
  else if (name === "style") value = styleString(value);
  native.setProp(node.native, name, value ?? null);
}
