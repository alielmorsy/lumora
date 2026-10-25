import { test, mock } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { create } from "../src/create.js";
import { dev } from "../src/dev.js";

const here = path.dirname(fileURLToPath(import.meta.url));
const workspace = path.resolve(here, "..", "..", "..");
const fakeHost = [process.execPath, path.join(here, "fake-host.js")];
const read = (...p) => fs.readFileSync(path.join(...p), "utf8");
const until = async (cond, ms = 15000) => {
  const end = Date.now() + ms;
  while (!cond()) {
    if (Date.now() > end) throw new Error("timed out");
    await new Promise((r) => setTimeout(r, 20));
  }
};

// A scaffolded project whose dependencies resolve to the workspace instead of an npm install.
async function project() {
  const cwd = fs.mkdtempSync(path.join(os.tmpdir(), "lumora-dev-"));
  const root = await create({ name: "demo", cwd, install: false });
  fs.mkdirSync(path.join(root, "node_modules", "@lumora"), { recursive: true });
  fs.symlinkSync(path.join(workspace, "packages", "runtime"), path.join(root, "node_modules", "@lumora", "runtime"), "junction");
  fs.symlinkSync(path.join(workspace, "node_modules", "solid-js"), path.join(root, "node_modules", "solid-js"), "junction");
  return { cwd, root };
}

// `npm run dev` runs with the project as cwd, where only the project's own node_modules are on hand.
async function inProject(fn) {
  const { cwd, root } = await project();
  const before = process.cwd();
  process.chdir(root);
  try {
    await fn(root);
  } finally {
    process.chdir(before);
    fs.rmSync(cwd, { recursive: true, force: true });
  }
}

test("dev bundles and launches host", { timeout: 30000 }, () =>
  inProject(async (root) => {
    const out = path.join(root, "host.json");
    const code = await dev({ root, host: fakeHost, hostArgs: ["--out", out] });
    assert.equal(code, 0);
    const bundle = path.join(root, ".lumora", "dev", "bundle.js");
    const seen = JSON.parse(read(out));
    const i = seen.argv.indexOf("--bundle");
    assert.equal(seen.argv[i + 1], bundle);
    assert.ok(path.isAbsolute(seen.argv[i + 1]));
    assert.equal(seen.argv[seen.argv.indexOf("--title") + 1], "demo");
    assert.deepEqual(seen.argv.slice(-2), ["--out", out]);
    assert.equal(seen.bundle, read(bundle));
    assert.match(seen.bundle, /Count: /);
  }),
);

test("dev rebuild rewrites bundle while host runs", { timeout: 30000 }, () =>
  inProject(async (root) => {
    const out = path.join(root, "host.json");
    const log = mock.method(console, "log", () => {});
    try {
      const running = dev({ root, host: fakeHost, hostArgs: ["--out", out, "--wait"] });
      await until(() => fs.existsSync(out));
      const app = path.join(root, "src", "App.tsx");
      fs.writeFileSync(app, read(app).replace("Count: ", "Clicked: "));
      assert.equal(await running, 0);
      assert.match(JSON.parse(read(out)).bundle, /Clicked: /);
      assert.ok(
        log.mock.calls.some((c) => /^\[lumora\] rebuilt in \d+ ms$/.test(c.arguments[0])),
        JSON.stringify(log.mock.calls.map((c) => c.arguments)),
      );
    } finally {
      log.mock.restore();
    }
  }),
);

test("dev reports build errors on rebuild and keeps the host", { timeout: 30000 }, () =>
  inProject(async (root) => {
    const out = path.join(root, "host.json");
    const err = mock.method(console, "error", () => {});
    try {
      const running = dev({ root, host: fakeHost, hostArgs: ["--out", out, "--wait"] });
      await until(() => fs.existsSync(out));
      const app = path.join(root, "src", "App.tsx");
      const good = read(app);
      fs.writeFileSync(app, "export default function App() { return <view>; }\n");
      await until(() => err.mock.calls.length > 0);
      assert.match(err.mock.calls[0].arguments.join(" "), /App\.tsx:1/);
      fs.writeFileSync(app, good.replace("Count: ", "Fixed: "));
      assert.equal(await running, 0);
      assert.match(JSON.parse(read(out)).bundle, /Fixed: /);
    } finally {
      err.mock.restore();
    }
  }),
);

test("dev exits 1 without launching the host when the first build fails", { timeout: 30000 }, () =>
  inProject(async (root) => {
    const out = path.join(root, "host.json");
    fs.writeFileSync(path.join(root, "src", "main.tsx"), "const = ;\n");
    const err = mock.method(console, "error", () => {});
    try {
      assert.equal(await dev({ root, host: fakeHost, hostArgs: ["--out", out] }), 1);
      assert.match(err.mock.calls.flatMap((c) => c.arguments).join("\n"), /main\.tsx:1/);
    } finally {
      err.mock.restore();
    }
    assert.ok(!fs.existsSync(out));
  }),
);

test("dev kills the host when the signal aborts", { timeout: 30000 }, () =>
  inProject(async (root) => {
    const out = path.join(root, "host.json");
    const ac = new AbortController();
    const running = dev({ root, host: fakeHost, hostArgs: ["--out", out, "--hang"], signal: ac.signal });
    await until(() => fs.existsSync(out));
    ac.abort();
    assert.notEqual(await running, 0);
  }),
);

test("dev reports a host that cannot be started", { timeout: 30000 }, () =>
  inProject(async (root) => {
    const err = mock.method(console, "error", () => {});
    try {
      assert.equal(await dev({ root, host: path.join(root, "no-such-host") }), 1);
      assert.match(err.mock.calls.flatMap((c) => c.arguments).join("\n"), /no-such-host/);
    } finally {
      err.mock.restore();
    }
  }),
);

test("cli: dev without a host explains how to get one", () => {
  const env = { ...process.env };
  delete env.LUMORA_HOST;
  const r = spawnSync(process.execPath, [path.join(here, "..", "bin", "lumora.js"), "dev"], { cwd: os.tmpdir(), env, encoding: "utf8" });
  assert.equal(r.status, 1);
  assert.match(r.stderr, /LUMORA_HOST/);
});
