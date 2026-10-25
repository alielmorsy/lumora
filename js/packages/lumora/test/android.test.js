import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { create } from "../src/create.js";
import { android } from "../src/android.js";
import { discoverModules, writeAndroidLinkFiles } from "../src/link.js";

const here = path.dirname(fileURLToPath(import.meta.url));
const workspace = path.resolve(here, "..", "..", "..");
const read = (...p) => fs.readFileSync(path.join(...p), "utf8");
const write = (file, text) => {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, text);
};
const until = async (cond, ms = 15000) => {
  const end = Date.now() + ms;
  while (!cond()) {
    if (Date.now() > end) throw new Error("timed out");
    await new Promise((r) => setTimeout(r, 20));
  }
};

const env = { ANDROID_HOME: path.join(os.tmpdir(), "sdk"), JAVA_HOME: path.join(os.tmpdir(), "jdk") };
const isAdb = (c) => path.basename(c.cmd).startsWith("adb");
const text = (c) => `${path.basename(c.cmd)} ${c.args.join(" ")}`;

// Records every command. Gradle and adb succeed at once, logcat runs until the signal aborts.
function recorder() {
  const calls = [];
  const run = (cmd, args, opts = {}) => {
    calls.push({ cmd, args, opts });
    if (!args.some((a) => a.includes("logcat"))) return Promise.resolve(0);
    return new Promise((resolve) => opts.signal?.addEventListener("abort", () => resolve(0), { once: true }));
  };
  return { calls, run };
}

function fakeSource() {
  const src = fs.mkdtempSync(path.join(os.tmpdir(), "lumora-src-"));
  write(path.join(src, "Android", "settings.gradle.kts"), "");
  write(path.join(src, "Android", "gradlew"), "#!/bin/sh\n");
  write(path.join(src, "Android", "gradlew.bat"), "@echo off\n");
  write(path.join(src, "Android", "gradle", "libs.versions.toml"), "[versions]\nagp = \"9.2.1\"\n");
  write(path.join(src, "Android", "gradle", "wrapper", "gradle-wrapper.properties"), "distributionUrl=x\n");
  fs.writeFileSync(path.join(src, "Android", "gradle", "wrapper", "gradle-wrapper.jar"), Buffer.from([1, 2, 3]));
  return src;
}

// A scaffolded app with one Kotlin module (Device) and one C++-only module (Cpp).
async function project() {
  const cwd = fs.mkdtempSync(path.join(os.tmpdir(), "lumora-android-"));
  const root = await create({ name: "demo", cwd, install: false });
  fs.mkdirSync(path.join(root, "node_modules", "@lumora"), { recursive: true });
  fs.symlinkSync(path.join(workspace, "packages", "runtime"), path.join(root, "node_modules", "@lumora", "runtime"), "junction");
  fs.symlinkSync(path.join(workspace, "node_modules", "solid-js"), path.join(root, "node_modules", "solid-js"), "junction");
  const mod = (name, lumora, files) => {
    const dir = path.join(root, "node_modules", name);
    write(path.join(dir, "package.json"), JSON.stringify({ name, lumora }));
    for (const f of files) write(path.join(dir, f), "");
  };
  mod("device", { module: "Device", android: { dir: "native/android", class: "com.acme.DeviceModule" } }, ["native/android/build.gradle.kts"]);
  mod("cppmod", { module: "Cpp", cpp: { dir: "native/cpp", target: "cppmod" } }, ["native/cpp/CMakeLists.txt"]);
  return { cwd, root, source: fakeSource() };
}

async function inProject(fn) {
  const p = await project();
  const before = process.cwd();
  process.chdir(p.root);
  try {
    await fn(p);
  } finally {
    process.chdir(before);
    fs.rmSync(p.cwd, { recursive: true, force: true });
    fs.rmSync(p.source, { recursive: true, force: true });
  }
}

// Runs android() until logcat starts, then stops it.
async function runOnce(p, opts = {}) {
  const { calls, run } = recorder();
  const ac = new AbortController();
  const done = android({ root: p.root, lumoraSource: p.source, env, run, signal: ac.signal, ...opts });
  await until(() => calls.some((c) => c.args.some((a) => a.includes("logcat"))));
  ac.abort();
  assert.equal(await done, 0);
  return calls;
}

const slash = (s) => s.replaceAll("\\", "/");

test("generates gradle project with modules and properties", () =>
  inProject(async (p) => {
    await runOnce(p);
    const dir = path.join(p.root, ".lumora", "android");
    const settings = read(dir, "settings.gradle.kts");
    assert.ok(settings.includes(`includeBuild("${slash(p.source)}/Android/build-logic")`));
    assert.ok(settings.includes(`project(":lumora-android").projectDir = file("${slash(p.source)}/Android/lumora-android")`));
    assert.ok(settings.includes('project(":app").projectDir = file("app")'));
    assert.ok(settings.includes('buildFileName = "android/build.gradle.kts"'));
    const modules = read(dir, "settings-modules.gradle.kts");
    assert.ok(modules.includes('include(":lumora-module-device")'));
    const deviceDir = slash(fs.realpathSync.native(path.join(p.root, "node_modules", "device", "native", "android")));
    assert.ok(modules.includes(`project(":lumora-module-device").projectDir = file("${deviceDir}")`));
    assert.ok(!modules.includes("cpp"));
    const props = read(dir, "gradle.properties");
    assert.ok(props.includes(`lumora.root=${slash(p.root)}`));
    assert.ok(props.includes(`lumora.jsModulesCmake=${slash(path.join(p.root, ".lumora", "native", "modules.cmake"))}`));
    assert.deepEqual(JSON.parse(read(dir, "app", "lumora.json")), { id: "com.lumora.app.demo", label: "demo", entry: { js: "bundle.js" } });
    assert.match(read(dir, "app", "android", "build.gradle.kts"), /lumora\.android\.app/);
    assert.match(read(dir, "app", "bundle.js"), /Count: /);
    assert.equal(read(dir, "gradle", "libs.versions.toml"), read(p.source, "Android", "gradle", "libs.versions.toml"));
    assert.ok(fs.existsSync(path.join(dir, "gradle", "wrapper", "gradle-wrapper.jar")));
    assert.ok(fs.existsSync(path.join(dir, "gradlew")) && fs.existsSync(path.join(dir, "gradlew.bat")));
  }));

test("app lumora.json takes id, label, orientation and permissions from package.json", () =>
  inProject(async (p) => {
    const file = path.join(p.root, "package.json");
    const pkg = JSON.parse(read(file));
    pkg.lumora = { app: { id: "org.acme.demo", label: "Demo!", orientation: "portrait", permissions: ["CAMERA"] } };
    write(file, JSON.stringify(pkg));
    await runOnce(p);
    const json = JSON.parse(read(p.root, ".lumora", "android", "app", "lumora.json"));
    assert.deepEqual(json, { id: "org.acme.demo", label: "Demo!", orientation: "portrait", permissions: ["CAMERA"], entry: { js: "bundle.js" } });
  }));

test("LumoraGeneratedModules lists android classes only", async () => {
  const out = fs.mkdtempSync(path.join(os.tmpdir(), "lumora-gen-"));
  const mods = [
    { name: "Device", android: { dir: "/d", className: "com.acme.DeviceModule" } },
    { name: "Cpp", cpp: { dir: "/c", target: "c" } },
    { name: "Zed", android: { dir: "/z", className: "x.Zed" } },
  ];
  const file = path.join(out, "app", "gen", "kotlin", "com", "lumora", "generated", "LumoraGeneratedModules.kt");
  assert.deepEqual(await writeAndroidLinkFiles("/root", mods, out), { changed: true });
  const kt = read(file);
  assert.ok(kt.includes("package com.lumora.generated"));
  assert.ok(kt.includes("object LumoraGeneratedModules"));
  assert.ok(kt.includes("@JvmField"));
  assert.ok(kt.includes("val all: List<LumoraModule> = listOf("));
  assert.ok(kt.includes("com.acme.DeviceModule(),") && kt.includes("x.Zed(),"));
  assert.ok(!/Cpp/.test(kt));
  const before = fs.statSync(file).mtimeMs;
  assert.deepEqual(await writeAndroidLinkFiles("/root", mods, out), { changed: false });
  assert.equal(fs.statSync(file).mtimeMs, before);
  fs.rmSync(out, { recursive: true, force: true });
});

test("an app package.json carrying only lumora.app is not a module", async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "lumora-app-"));
  write(path.join(dir, "package.json"), JSON.stringify({ name: "a", lumora: { app: { id: "x.y" } } }));
  assert.deepEqual(await discoverModules(dir), []);
  fs.rmSync(dir, { recursive: true, force: true });
});

test("runs install, start, then pushes on rebuild", { timeout: 30000 }, () =>
  inProject(async (p) => {
    const { calls, run } = recorder();
    const ac = new AbortController();
    const done = android({ root: p.root, lumoraSource: p.source, env, run, signal: ac.signal });
    await until(() => calls.some((c) => c.args.some((a) => a.includes("logcat"))));
    const id = "com.lumora.app.demo";
    const dir = path.join(p.root, ".lumora", "android");
    assert.equal(calls.length, 3);
    assert.match(path.basename(calls[0].cmd), /^gradlew(\.bat)?$/);
    assert.deepEqual(calls[0].args, [":app:installDebug"]);
    assert.equal(calls[0].opts.cwd, dir);
    assert.equal(calls[0].opts.env.ANDROID_HOME, env.ANDROID_HOME);
    assert.ok(isAdb(calls[1]));
    assert.deepEqual(calls[1].args, ["shell", "am", "start", "-n", `${id}/com.lumora.android.LumoraActivity`]);
    assert.ok(calls[2].args[1].includes(`pidof -s ${id}`));

    const app = path.join(p.root, "src", "App.tsx");
    write(app, read(app) + "\n// edited\n");
    await until(() => calls.some((c) => c.args.includes("run-as")));
    const push = calls[3];
    const tmp = `/data/local/tmp/lumora-${id}.js`;
    assert.ok(isAdb(push));
    assert.deepEqual(push.args, ["push", path.join(dir, "app", "bundle.js"), tmp]);
    assert.deepEqual(calls[4].args, ["shell", "run-as", id, "sh", "-c", `'mkdir -p files/lumora-dev && cp ${tmp} files/lumora-dev/bundle.js'`]);
    ac.abort();
    assert.equal(await done, 0);
  }));

test("a failing gradle install stops before launching", () =>
  inProject(async (p) => {
    const calls = [];
    const run = async (cmd, args) => (calls.push(text({ cmd, args })), 3);
    assert.equal(await android({ root: p.root, lumoraSource: p.source, env, run }), 3);
    assert.equal(calls.length, 1);
  }));

test("missing ANDROID_HOME fails with a clear message", () =>
  inProject(async (p) => {
    const { calls, run } = recorder();
    await assert.rejects(android({ root: p.root, lumoraSource: p.source, env: { JAVA_HOME: "/jdk" }, run }), /ANDROID_HOME is not set/);
    await assert.rejects(android({ root: p.root, lumoraSource: p.source, env: { ANDROID_HOME: "/sdk" }, run }), /JAVA_HOME is not set/);
    await assert.rejects(android({ root: p.root, lumoraSource: undefined, env, run }), /LUMORA_SOURCE/);
    assert.equal(calls.length, 0);
  }));

test("--device adds -s to every adb call", { timeout: 30000 }, () =>
  inProject(async (p) => {
    const { calls, run } = recorder();
    const ac = new AbortController();
    const done = android({ root: p.root, lumoraSource: p.source, device: "emu-5554", env, run, signal: ac.signal });
    await until(() => calls.length >= 3);
    const app = path.join(p.root, "src", "App.tsx");
    write(app, read(app) + "\n// edited\n");
    await until(() => calls.some((c) => c.args.includes("run-as")));
    ac.abort();
    await done;
    const adbCalls = calls.filter(isAdb);
    assert.equal(adbCalls.length, 4);
    for (const c of adbCalls) assert.deepEqual(c.args.slice(0, 2), ["-s", "emu-5554"], text(c));
    assert.ok(!isAdb(calls[0]) && !calls[0].args.includes("-s"));
  }));
