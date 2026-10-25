import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { discoverModules, writeDesktopLinkFiles } from "../src/link.js";

const tmp = () => fs.mkdtemp(path.join(os.tmpdir(), "lumora-link-"));
const write = async (file, text) => {
  await fs.mkdir(path.dirname(file), { recursive: true });
  await fs.writeFile(file, text);
};
const pkg = (dir, name, lumora, withCmake = true) =>
  Promise.all([
    write(path.join(dir, "package.json"), JSON.stringify({ name, lumora })),
    withCmake && lumora?.cpp ? write(path.join(dir, lumora.cpp.dir, "CMakeLists.txt"), "") : null,
  ]);
const cpp = (module, target = module.toLowerCase()) => ({ module, cpp: { dir: "native", target } });

async function fixture() {
  const root = await tmp();
  await pkg(root, "app", { module: "AppMod", cpp: { dir: "native", target: "appmod" } });
  await pkg(path.join(root, "node_modules/a"), "a", { module: "Alpha", android: { dir: "android", class: "x.Alpha" }, ...cpp("Alpha") });
  await write(path.join(root, "node_modules/a/android/build.gradle"), "");
  await pkg(path.join(root, "node_modules/@x/b"), "@x/b", cpp("Beta"));
  await pkg(path.join(root, "node_modules/plain"), "plain", undefined);
  return root;
}

test("finds app-local, plain and scoped packages", async () => {
  const root = await fixture();
  const mods = await discoverModules(root);
  assert.deepEqual(mods.map((m) => [m.name, m.packageName]), [["Alpha", "a"], ["AppMod", "app"], ["Beta", "@x/b"]]);
  const alpha = mods[0];
  assert.equal(alpha.cpp.target, "alpha");
  assert.equal(alpha.cpp.dir, path.join(await fs.realpath(root), "node_modules", "a", "native"));
  assert.equal(alpha.android.className, "x.Alpha");
  assert.equal(mods[2].android, undefined);
});

test("follows symlinks and dedupes", async () => {
  const root = await tmp();
  await pkg(path.join(root, "packages/c"), "c", cpp("Gamma"));
  await fs.mkdir(path.join(root, "node_modules/@s"), { recursive: true });
  await fs.symlink(path.join(root, "packages/c"), path.join(root, "node_modules/c"), "junction");
  await fs.symlink(path.join(root, "packages/c"), path.join(root, "node_modules/@s/c"), "junction");
  const mods = await discoverModules(root);
  assert.equal(mods.length, 1);
  assert.equal(mods[0].packageDir, await fs.realpath(path.join(root, "packages/c")));
});

test("duplicate module names fail naming both packages", async () => {
  const root = await tmp();
  await pkg(path.join(root, "node_modules/p1"), "p1", cpp("Dup"));
  await pkg(path.join(root, "node_modules/p2"), "p2", cpp("Dup"));
  await assert.rejects(discoverModules(root), /Dup.*p1.*p2|Dup.*p2.*p1/);
});

test("invalid name, missing dir, missing manifest parts fail", async () => {
  const bad = async (lumora, re, cmake) => {
    const root = await tmp();
    await pkg(path.join(root, "node_modules/p"), "p", lumora, cmake);
    await assert.rejects(discoverModules(root), re);
  };
  await bad(cpp("lower"), /lower/);
  await bad({ module: "NoImpl" }, /cpp or android/);
  await bad({ module: "Gone", cpp: { dir: "nope", target: "t" } }, /nope/, false);
});

test("cpp dir without CMakeLists fails", async () => {
  const root = await tmp();
  await pkg(path.join(root, "node_modules/p"), "p", cpp("NoCmake"), false);
  await fs.mkdir(path.join(root, "node_modules/p/native"));
  await assert.rejects(discoverModules(root), /CMakeLists\.txt/);
});

test("writes cmake and registry, unchanged second time", async () => {
  const root = await tmp();
  await pkg(path.join(root, "node_modules/a"), "a", cpp("A"));
  const mods = await discoverModules(root);
  const first = await writeDesktopLinkFiles(root, mods);
  assert.equal(first.changed, true);
  assert.equal(first.cmakeFile, path.join(root, ".lumora", "native", "modules.cmake"));
  const cmake = await fs.readFile(first.cmakeFile, "utf8");
  const dir = mods[0].cpp.dir.replaceAll("\\", "/");
  assert.ok(cmake.includes(`add_subdirectory("${dir}" "\${CMAKE_BINARY_DIR}/lumora_modules/A")`));
  assert.ok(cmake.includes("set(LUMORA_JS_MODULE_TARGETS a)"));
  assert.ok(cmake.includes("LUMORA_JS_MODULES_SOURCE"));
  assert.ok(!cmake.includes("\\"));
  const gen = await fs.readFile(path.join(root, ".lumora/native/LinkedModules.gen.cpp"), "utf8");
  assert.ok(gen.includes("#include <lumora/script/NativeModule.h>"));
  assert.ok(gen.includes("LUMORA_NATIVE_MODULE(A);"));
  assert.ok(gen.includes('registry.Add("A", &LumoraModule_A_Install)'));
  assert.ok(gen.includes("lumora::script::RegisterLinkedModules(") && gen.includes("ModuleRegistry& registry"));
  assert.equal((await writeDesktopLinkFiles(root, mods)).changed, false);
});

test("no modules writes an empty target list", async () => {
  const root = await tmp();
  const r = await writeDesktopLinkFiles(root, await discoverModules(root));
  const cmake = await fs.readFile(r.cmakeFile, "utf8");
  assert.match(cmake, /set\(LUMORA_JS_MODULE_TARGETS\s*\)/);
  assert.ok(!cmake.includes("add_subdirectory"));
});
