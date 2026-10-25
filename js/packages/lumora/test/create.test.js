import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { create } from "../src/create.js";
import { resolveHost } from "../src/host.js";

const here = path.dirname(fileURLToPath(import.meta.url));
const bin = path.join(here, "..", "bin", "lumora.js");
const tmp = () => fs.mkdtempSync(path.join(os.tmpdir(), "lumora-create-"));
const json = (...p) => JSON.parse(fs.readFileSync(path.join(...p), "utf8"));
const files = (dir) =>
  fs.readdirSync(dir, { withFileTypes: true }).flatMap((e) => (e.isDirectory() ? files(path.join(dir, e.name)) : [path.join(dir, e.name)]));

function cli(args, { cwd, input = "" } = {}) {
  const env = { ...process.env };
  delete env.LUMORA_HOST;
  return spawnSync(process.execPath, [bin, ...args], { cwd, input, env, encoding: "utf8" });
}

test("create scaffolds a project", async () => {
  const cwd = tmp();
  const dir = await create({ name: "demo", cwd, install: false });
  assert.equal(dir, path.join(cwd, "demo"));
  for (const f of ["package.json", "tsconfig.json", ".gitignore", "src/main.tsx", "src/App.tsx", "src/app.css", "README.md"]) {
    assert.ok(fs.existsSync(path.join(dir, f)), f);
  }
  const pkg = json(dir, "package.json");
  const version = json(here, "..", "..", "runtime", "package.json").version;
  assert.equal(pkg.name, "demo");
  assert.equal(pkg.scripts.dev, "lumora dev");
  assert.equal(pkg.scripts.android, "lumora android");
  assert.equal(pkg.dependencies["@lumora/runtime"], `^${version}`);
  assert.equal(pkg.devDependencies.lumora, `^${version}`);
  for (const f of files(dir)) assert.ok(!fs.readFileSync(f, "utf8").includes("{{"), f);
});

test("create rejects bad names and non-empty dirs", async () => {
  const cwd = tmp();
  for (const name of ["", "Demo", "-x", "a b", "../x", "a/b"]) {
    await assert.rejects(create({ name, cwd, install: false }), /name/, JSON.stringify(name));
  }
  assert.deepEqual(fs.readdirSync(cwd), []);
  fs.mkdirSync(path.join(cwd, "full"));
  fs.writeFileSync(path.join(cwd, "full", "a.txt"), "x");
  await assert.rejects(create({ name: "full", cwd, install: false }), /not empty/);
  assert.deepEqual(fs.readdirSync(path.join(cwd, "full")), ["a.txt"]);
  fs.mkdirSync(path.join(cwd, "empty"));
  await create({ name: "empty", cwd, install: false });
  assert.ok(fs.existsSync(path.join(cwd, "empty", "package.json")));
});

test("lumora and @lumora/runtime are released in lockstep", () => {
  assert.equal(json(here, "..", "package.json").version, json(here, "..", "..", "runtime", "package.json").version);
});

test("resolveHost order", () => {
  assert.equal(resolveHost({ flag: "a.exe", env: { LUMORA_HOST: "b.exe" } }), "a.exe");
  assert.equal(resolveHost({ env: { LUMORA_HOST: "b.exe" } }), "b.exe");
  assert.throws(() => resolveHost({ env: {} }), /LumoraJsHost.*LUMORA_HOST/s);
});

test("cli: first positional that is not a command is the project name", () => {
  const cwd = tmp();
  const r = cli(["my-app", "--no-install"], { cwd });
  assert.equal(r.status, 0, r.stderr);
  assert.equal(json(cwd, "my-app", "package.json").name, "my-app");
  const r2 = cli(["create", "other", "--no-install"], { cwd });
  assert.equal(r2.status, 0, r2.stderr);
  assert.ok(fs.existsSync(path.join(cwd, "other", "src", "main.tsx")));
});

test("cli: missing name prompts, empty answer or no input uses the default", () => {
  const cwd = tmp();
  assert.equal(cli(["--no-install"], { cwd, input: "\n" }).status, 0);
  assert.ok(fs.existsSync(path.join(cwd, "my-lumora-app", "package.json")));
  assert.equal(cli(["create", "--no-install"], { cwd, input: "typed\n" }).status, 0);
  assert.ok(fs.existsSync(path.join(cwd, "typed", "package.json")));
  const eof = tmp();
  assert.equal(cli(["--no-install"], { cwd: eof }).status, 0);
  assert.ok(fs.existsSync(path.join(eof, "my-lumora-app", "package.json")));
});

test("cli: bare lumora inside a lumora project prints help", () => {
  const cwd = tmp();
  fs.writeFileSync(path.join(cwd, "package.json"), JSON.stringify({ devDependencies: { lumora: "^0.1.0" } }));
  const r = cli([], { cwd });
  assert.equal(r.status, 0, r.stderr);
  assert.match(r.stdout, /lumora dev/);
  assert.deepEqual(fs.readdirSync(cwd), ["package.json"]);
});

test("cli: errors exit 1 with a message", () => {
  const cwd = tmp();
  const r = cli(["Bad Name", "--no-install"], { cwd });
  assert.equal(r.status, 1);
  assert.match(r.stderr, /name/);
  const r2 = cli(["--bogus"], { cwd });
  assert.equal(r2.status, 1);
  assert.match(r2.stderr, /bogus/);
});
