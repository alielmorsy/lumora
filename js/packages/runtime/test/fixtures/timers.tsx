import "@lumora/runtime";

const log: string[] = [];
globalThis.log = log;
setTimeout(() => log.push("t"), 100);
const id = setInterval(() => log.push("i"), 100);
globalThis.stop = () => clearInterval(id);
globalThis.frame = () => requestAnimationFrame((now) => log.push("raf " + now));
globalThis.boom = () => setTimeout(() => { throw new Error("boom"); }, 0);
globalThis.fast = () => setInterval(() => log.push("f"), 1);
