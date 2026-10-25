import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { createModule } from "../src/create.js";
import { discoverModules } from "../src/link.js";

const here = path.dirname(fileURLToPath(import.meta.url));
const bin = path.join(here, "..", "bin", "lumora.js");
const tmp = () => fs.mkdtempSync(path.join(os.tmpdir(), "lumora-create-module-"));
const read = (...p) => fs.readFileSync(path.join(...p), "utf8");
const files = (dir) =>
  fs.readdirSync(dir, { withFileTypes: true }).flatMap((e) => (e.isDirectory() ? files(path.join(dir, e.name)) : [path.join(dir, e.name)]));

test("createModule scaffolds a scoped package with defaults", async () => {
  const cwd = tmp();
  const dir = await createModule({ name: "@acme/haptics", cwd });
  assert.equal(dir, path.join(cwd, "haptics"));
  const kt = "native/android/src/main/kotlin/com/lumora/modules/haptics/HapticsModule.kt";
  for (const f of ["package.json", "index.js", "index.d.ts", "README.md", "native/cpp/CMakeLists.txt", "native/cpp/Haptics.cpp", "native/android/build.gradle.kts", kt]) {
    assert.ok(fs.existsSync(path.join(dir, f)), f);
  }
  const pkg = JSON.parse(read(dir, "package.json"));
  assert.equal(pkg.name, "@acme/haptics");
  assert.deepEqual(pkg.lumora, {
    module: "Haptics",
    cpp: { dir: "native/cpp", target: "lumora_haptics" },
    android: { dir: "native/android", class: "com.lumora.modules.haptics.HapticsModule" },
  });
  assert.match(read(dir, "index.js"), /requireNativeModule\("Haptics"\)/);
  assert.match(read(dir, "index.d.ts"), /hello\(\): string/);
  assert.match(read(dir, "native/cpp/Haptics.cpp"), /LUMORA_NATIVE_MODULE\(Haptics\)[\s\S]*Hello from C\+\+/);
  assert.match(read(dir, "native/cpp/CMakeLists.txt"), /add_library\(lumora_haptics STATIC Haptics\.cpp\)[\s\S]*lumora::script/);
  const kotlin = read(dir, kt);
  assert.match(kotlin, /^package com\.lumora\.modules\.haptics$/m);
  assert.match(kotlin, /class HapticsModule : LumoraModule\(\)/);
  assert.match(kotlin, /Name\("Haptics"\)/);
  assert.match(kotlin, /Hello from Kotlin/);
  const gradle = read(dir, "native/android/build.gradle.kts");
  assert.match(gradle, /namespace = "com\.lumora\.modules\.haptics"/);
  assert.match(gradle, /project\(":lumora-android"\)/);
  for (const f of files(dir)) assert.ok(!read(f).includes("{{"), f);
});

test("createModule honours --module and --android-package", async () => {
  const cwd = tmp();
  const dir = await createModule({ name: "plain-pkg", cwd, module: "Buzz", androidPackage: "org.x.buzz" });
  const pkg = JSON.parse(read(dir, "package.json"));
  assert.equal(pkg.lumora.module, "Buzz");
  assert.equal(pkg.lumora.cpp.target, "lumora_buzz");
  assert.equal(pkg.lumora.android.class, "org.x.buzz.BuzzModule");
  assert.ok(fs.existsSync(path.join(dir, "native/android/src/main/kotlin/org/x/buzz/BuzzModule.kt")));
});

test("createModule derives PascalCase from dashed names", async () => {
  const dir = await createModule({ name: "my-cool_thing", cwd: tmp() });
  assert.equal(JSON.parse(read(dir, "package.json")).lumora.module, "MyCoolThing");
});

test("generated module is discovered with cpp and android", async () => {
  const cwd = tmp();
  const dir = await createModule({ name: "@acme/haptics", cwd });
  const app = path.join(cwd, "app");
  const dest = path.join(app, "node_modules", "@acme", "haptics");
  fs.mkdirSync(path.dirname(dest), { recursive: true });
  fs.cpSync(dir, dest, { recursive: true });
  fs.writeFileSync(path.join(app, "package.json"), JSON.stringify({ name: "app" }));
  const [m, ...rest] = await discoverModules(app);
  assert.equal(rest.length, 0);
  assert.equal(m.name, "Haptics");
  assert.equal(m.cpp.target, "lumora_haptics");
  assert.equal(m.android.className, "com.lumora.modules.haptics.HapticsModule");
});

test("createModule rejects bad input and non-empty dirs", async () => {
  const cwd = tmp();
  await assert.rejects(createModule({ name: "Bad Name", cwd }), /Invalid package name/);
  await assert.rejects(createModule({ name: "ok", cwd, module: "lower" }), /Invalid module name/);
  await assert.rejects(createModule({ name: "ok", cwd, androidPackage: "Nope" }), /Invalid Android package/);
  await assert.rejects(createModule({ name: "123", cwd }), /Invalid module name/);
  await createModule({ name: "ok", cwd });
  await assert.rejects(createModule({ name: "ok", cwd }), /not empty/);
});

test("lumora create-module CLI", () => {
  const cwd = tmp();
  const r = spawnSync(process.execPath, [bin, "create-module", "@acme/haptics", "--module", "Tap", "--android-package", "org.x.tap"], { cwd, encoding: "utf8" });
  assert.equal(r.status, 0, r.stderr);
  const pkg = JSON.parse(read(cwd, "haptics", "package.json"));
  assert.equal(pkg.lumora.module, "Tap");
  assert.equal(pkg.lumora.android.class, "org.x.tap.TapModule");
  const bad = spawnSync(process.execPath, [bin, "create-module"], { cwd, encoding: "utf8" });
  assert.equal(bad.status, 1);
  assert.match(bad.stderr, /package name/);
});
