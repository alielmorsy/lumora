import { test } from "node:test";
import assert from "node:assert/strict";
import vm from "node:vm";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { bundle } from "../src/bundle.js";

const fx = path.join(path.dirname(fileURLToPath(import.meta.url)), "fixtures", "basic");
const tmp = () => fs.mkdtempSync(path.join(os.tmpdir(), "lumora-bundle-"));
// the watch fixture copy must sit under the package so solid-js resolves from node_modules
const pkgTmp = () => fs.mkdtempSync(path.join(fx, "..", "..", ".tmp-"));
const read = (f) => fs.readFileSync(f, "utf8");

test("bundles tsx through solid universal", async () => {
  const outfile = path.join(tmp(), "out.js");
  const r = await bundle({ entry: path.join(fx, "main.tsx"), outfile, external: ["@lumora/runtime"] });
  assert.equal(r.ok, true, r.errors.join("\n"));
  const out = read(outfile);
  // external imports are emitted as `(0, import_x.createElement)(`
  assert.match(out, /createElement\)?\(/);
  assert.match(out, /insertNode\)?\(/);
  assert.ok(!out.includes("document."));
  assert.ok(!/template\)?\(/.test(out));
  await r.stop();
});

test("css import is collected before importing module code", async () => {
  const outfile = path.join(tmp(), "out.js");
  const r = await bundle({ entry: path.join(fx, "main.tsx"), outfile, external: ["@lumora/runtime"] });
  assert.equal(r.ok, true, r.errors.join("\n"));
  const out = read(outfile);
  const css = out.indexOf("__lumoraCss");
  assert.ok(css >= 0 && out.includes(".card"));
  assert.ok(css < out.indexOf("function App"));
  await r.stop();
});

test("solid resolves to the reactive build", async () => {
  const outfile = path.join(tmp(), "out.js");
  const r = await bundle({ entry: path.join(fx, "reactive.ts"), outfile });
  assert.equal(r.ok, true, r.errors.join("\n"));
  const out = read(outfile);
  assert.ok(!out.includes("dist/server"));
  const ctx = { console };
  ctx.globalThis = ctx;
  vm.runInNewContext(out, ctx);
  assert.deepEqual([...ctx.runs], [1, 2]);
  await r.stop();
});

test("type errors do not fail the bundle but syntax errors do", async () => {
  const dir = tmp();
  const ok = await bundle({ entry: path.join(fx, "typeerr.ts"), outfile: path.join(dir, "a.js") });
  assert.equal(ok.ok, true, ok.errors.join("\n"));
  const bad = await bundle({ entry: path.join(fx, "syntaxerr.ts"), outfile: path.join(dir, "b.js") });
  assert.equal(bad.ok, false);
  assert.match(bad.errors[0], /syntaxerr\.ts:2/);
  await ok.stop();
  await bad.stop();
});

test("watch rebuild keeps last good output on error", async () => {
  const dir = pkgTmp();
  fs.cpSync(fx, dir, { recursive: true });
  let r;
  try {
    const outfile = path.join(dir, "out.js");
    let done;
    const rebuilt = new Promise((res) => (done = res));
    r = await bundle({
      entry: path.join(dir, "main.tsx"),
      outfile,
      watch: true,
      external: ["@lumora/runtime"],
      onRebuild: (x) => done(x),
    });
    assert.equal(r.ok, true, r.errors.join("\n"));
    const good = read(outfile);
    fs.writeFileSync(path.join(dir, "util.ts"), "export const = ;\n");
    const x = await rebuilt;
    assert.equal(x.ok, false);
    assert.equal(read(outfile), good);
  } finally {
    await r?.stop();
    fs.rmSync(dir, { recursive: true, force: true });
  }
});

test("watch reports real changes only", async () => {
  const dir = pkgTmp();
  fs.cpSync(fx, dir, { recursive: true });
  let r;
  try {
    const seen = [];
    const wait = async (cond) => {
      for (let i = 0; i < 500 && !cond(); i++) await new Promise((res) => setTimeout(res, 20));
      assert.ok(cond(), JSON.stringify(seen));
    };
    r = await bundle({
      entry: path.join(dir, "main.tsx"),
      outfile: path.join(dir, "out.js"),
      watch: true,
      external: ["@lumora/runtime"],
      onRebuild: (x) => seen.push(x.ok),
    });
    // esbuild runs one rebuild right after watch() starts, which must not be reported
    await new Promise((res) => setTimeout(res, 300));
    assert.deepEqual(seen, []);
    const util = path.join(dir, "util.ts");
    const good = read(util);
    fs.writeFileSync(util, good);
    await new Promise((res) => setTimeout(res, 300));
    assert.deepEqual(seen, []);
    fs.writeFileSync(util, "export const = ;\n");
    await wait(() => seen.length === 1);
    fs.writeFileSync(util, good);
    await wait(() => seen.length === 2);
    assert.deepEqual(seen, [false, true]);
  } finally {
    await r?.stop();
    fs.rmSync(dir, { recursive: true, force: true });
  }
});
