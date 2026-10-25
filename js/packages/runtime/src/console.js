import { native } from "./native.js";

const LEVELS = { debug: 0, log: 1, info: 1, warn: 2, error: 3 };

// ponytail: the WeakSet guard also prints repeated (non-cyclic) references as [Circular]
function json(v) {
  const seen = new WeakSet();
  try {
    return JSON.stringify(v, (_, x) => {
      if (x && typeof x === "object") {
        if (seen.has(x)) return "[Circular]";
        seen.add(x);
      }
      return x;
    });
  } catch {
    return String(v);
  }
}

const fmt = (a) =>
  typeof a === "string" ? a : a instanceof Error ? a.stack ?? String(a) : a && typeof a === "object" ? json(a) : String(a);

const methods = {};
for (const [name, level] of Object.entries(LEVELS)) methods[name] = (...args) => native.log(level, args.map(fmt).join(" "));
globalThis.console = Object.assign(globalThis.console ?? {}, methods);
