import { test } from "node:test";
import assert from "node:assert/strict";
import vm from "node:vm";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { bundle } from "lumora/src/bundle.js";
import { createFakeNative } from "./fake-native.js";

const fx = path.join(path.dirname(fileURLToPath(import.meta.url)), "fixtures");
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "lumora-runtime-"));

async function run(name) {
  const outfile = path.join(tmp, `${name}.js`);
  const r = await bundle({ entry: path.join(fx, `${name}.tsx`), outfile });
  assert.equal(r.ok, true, r.errors.join("\n"));
  const fake = createFakeNative();
  const ctx = { __lumora: fake.api };
  vm.runInNewContext(fs.readFileSync(outfile, "utf8"), ctx);
  return { ...fake, ctx, root: fake.api.root() };
}

const el = (tag, props = {}, children = [], handlers = {}) => ({ tag, props, handlers, children });

test("renders intrinsic tree", async () => {
  const { tree } = await run("intrinsic");
  assert.deepEqual(tree(), el("root", {}, [el("view", { class: "a" }, [el("text", { text: "Hi" })])]));
});

test("signal update patches one prop", async () => {
  const { calls, root } = await run("counter");
  const [text, button] = root.children[0].children;
  assert.equal(text.props.text, "Count: 0");
  const n = calls.length;
  button.handlers.click();
  assert.deepEqual(calls.slice(n), [["setProp", text, "text", "Count: 1"]]);
});

test("Show swaps nodes", async () => {
  const { ctx, calls, root } = await run("show");
  const view = root.children[0];
  assert.equal(view.children.length, 0);
  ctx.setOn(true);
  assert.equal(view.children.length, 1);
  const text = view.children[0];
  assert.deepEqual(text.props, { text: "X" });
  assert.ok(calls.some((c) => c[0] === "insert" && c[1] === view && c[2] === text));
  ctx.setOn(false);
  assert.equal(view.children.length, 0);
  assert.deepEqual(calls.at(-1), ["remove", view, text]);
});

test("For reorders by moving", async () => {
  const { ctx, calls, root } = await run("for");
  const view = root.children[0];
  const texts = () => view.children.map((c) => c.props.text);
  assert.deepEqual(texts(), ["a", "b", "c"]);
  const n = calls.length;
  ctx.setItems(["c", "a", "b"]);
  assert.deepEqual(texts(), ["c", "a", "b"]);
  const after = calls.slice(n);
  assert.ok(after.length > 0);
  assert.ok(!after.some((c) => c[0] === "createNode" || c[0] === "remove"), JSON.stringify(after.map((c) => c[0])));
});

test("style object serializes", async () => {
  const { root } = await run("style");
  assert.equal(root.children[0].props.style, "font-size: 12px; opacity: 0.5; padding: 4px 8px");
});

test("timers run from __lumoraTick", async () => {
  const { ctx } = await run("timers");
  const { log } = ctx;
  ctx.__lumoraTick(50);
  assert.deepEqual([...log], []);
  ctx.__lumoraTick(100);
  assert.deepEqual([...log], ["t", "i"]);
  ctx.__lumoraTick(150);
  assert.deepEqual([...log], ["t", "i"]);
  ctx.frame();
  ctx.__lumoraTick(200);
  assert.deepEqual([...log], ["t", "i", "i", "raf 200"]);
  ctx.__lumoraTick(300);
  assert.deepEqual([...log], ["t", "i", "i", "raf 200", "i"]);
  ctx.stop();
  ctx.__lumoraTick(400);
  assert.equal(log.length, 5);
  ctx.boom();
  assert.throws(() => ctx.__lumoraTick(500), /boom/);
});

test("bare text outside <text> becomes a native text node", async () => {
  const { ctx, root } = await run("baretext");
  const view = root.children[0];
  assert.deepEqual(
    view.children.map((c) => [c.tag, c.props.text]),
    [["text", "n="], ["text", "0"]],
  );
  ctx.setN(1);
  assert.deepEqual(
    view.children.map((c) => c.props.text),
    ["n=", "1"],
  );
});

test("console routes to __lumora.log", async () => {
  const { calls } = await run("console");
  const logs = calls.filter((c) => c[0] === "log");
  assert.deepEqual(logs[0], ["log", 2, 'x {"a":1}']);
  assert.equal(logs[1][1], 1);
  assert.match(logs[1][2], /"b":2/);
});

test("render applies collected css first", async () => {
  const { calls } = await run("css");
  assert.deepEqual(calls[0], ["setStylesheet", ".a { color: red; }\n\n.b { color: blue; }\n"]);
  assert.equal(calls[1][0], "createNode");
});
