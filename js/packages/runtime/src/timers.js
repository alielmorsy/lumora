// Timers are driven by the host calling __lumoraTick(nowMs) once per frame, so resolution is one frame.
// The clock is the nowMs the host passes; timers created before the first tick count from 0.
const g = globalThis;
const timers = new Map();
let frames = new Map();
let now = 0;
let nextId = 1;

const add = (fn, ms, args, repeat) => {
  ms = Math.max(repeat ? 1 : 0, Number(ms) || 0);
  timers.set(nextId, { fn, args, ms, repeat, due: now + ms });
  return nextId++;
};
const clear = (id) => void timers.delete(id);

g.setTimeout ??= (fn, ms, ...args) => add(fn, ms, args, false);
g.setInterval ??= (fn, ms, ...args) => add(fn, ms, args, true);
g.clearTimeout ??= clear;
g.clearInterval ??= clear;
g.requestAnimationFrame ??= (fn) => (frames.set(nextId, fn), nextId++);
g.cancelAnimationFrame ??= (id) => void frames.delete(id);
g.queueMicrotask ??= (fn) => void Promise.resolve().then(fn);

// Exceptions propagate to the host. Timers created during a tick wait for the next one.
g.__lumoraTick = (t) => {
  now = t;
  const due = [...timers].filter(([, x]) => x.due <= t).sort((a, b) => a[1].due - b[1].due);
  for (const [id, x] of due) {
    if (timers.get(id) !== x) continue;
    if (x.repeat) x.due += Math.ceil((t - x.due + 1) / x.ms) * x.ms;
    else timers.delete(id);
    x.fn(...x.args);
  }
  const cbs = frames;
  frames = new Map();
  for (const fn of cbs.values()) fn(t);
};
