import { test, mock } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { buildHost } from "../src/build-host.js";
import { msvcEnv } from "../src/msvc.js";
import { resolveHost } from "../src/host.js";

const slash = (p) => p.replaceAll("\\", "/");
const write = (file, text) => {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, text);
};
const addModule = (root, name) => {
  const dir = path.join(root, "node_modules", name.toLowerCase());
  write(path.join(dir, "package.json"), JSON.stringify({ name: name.toLowerCase(), lumora: { module: name, cpp: { dir: "cpp", target: `lumora_${name.toLowerCase()}` } } }));
  write(path.join(dir, "cpp", "CMakeLists.txt"), "");
};
function app() {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "lumora-build-host-"));
  write(path.join(root, "package.json"), JSON.stringify({ name: "app" }));
  addModule(root, "Echo");
  return root;
}
// Records calls and fakes the CMakeCache.txt a real configure leaves behind.
function fakeRun() {
  const calls = [];
  const run = async (cmd, args, opts) => {
    calls.push({ cmd, args, opts });
    if (args[0] === "-S") write(path.join(args[args.indexOf("-B") + 1], "CMakeCache.txt"), "");
    return 0;
  };
  return { calls, run };
}
const kinds = (calls) => calls.map((c) => (c.args[0] === "-S" ? "configure" : "build"));

test("buildHost configures once and rebuilds on link change", async () => {
  const root = app();
  const { calls, run } = fakeRun();
  const opts = { root, lumoraSource: "/lumora", run, env: {} };
  const exe = await buildHost(opts);
  const dir = path.join(root, ".lumora", "host", `${process.platform}-${process.arch}`);
  assert.equal(exe, path.join(dir, "bin", process.platform === "win32" ? "LumoraJsHost.exe" : "LumoraJsHost"));
  assert.deepEqual(kinds(calls), ["configure", "build"]);
  assert.deepEqual(calls[1].args, ["--build", dir, "--target", "LumoraJsHost"]);
  assert.deepEqual(calls[0].args.slice(0, 6), ["-S", "/lumora", "-B", dir, "-G", "Ninja"]);
  assert.ok(calls[0].args.includes("-DCMAKE_BUILD_TYPE=RelWithDebInfo"));
  assert.ok(calls[0].args.includes("-DLUMORA_BUILD_TESTS=OFF"));
  assert.ok(calls[0].args.includes("-DLUMORA_BUILD_BENCHMARKS=OFF"));
  assert.ok(calls.every((c) => c.cmd === "cmake"));

  calls.length = 0;
  await buildHost(opts);
  assert.deepEqual(kinds(calls), ["build"]);

  calls.length = 0;
  addModule(root, "Other");
  await buildHost(opts);
  assert.deepEqual(kinds(calls), ["configure", "build"]);
});

test("buildHost passes the modules cmake path", async () => {
  const root = app();
  const { calls, run } = fakeRun();
  await buildHost({ root, lumoraSource: "/lumora", config: "Debug", run, env: {} });
  const want = `-DLUMORA_JS_MODULES_CMAKE=${slash(path.join(root, ".lumora", "native", "modules.cmake"))}`;
  assert.ok(calls[0].args.includes(want), calls[0].args.join(" "));
  assert.ok(calls[0].args.includes("-DCMAKE_BUILD_TYPE=Debug"));
  assert.match(fs.readFileSync(path.join(root, ".lumora", "native", "modules.cmake"), "utf8"), /lumora_echo/);
});

test("buildHost fails when a step fails", async () => {
  const root = app();
  await assert.rejects(buildHost({ root, lumoraSource: "/lumora", run: async () => 2, env: {} }), /cmake.*exit code 2/);
});

test("msvcEnv uses PATH cl.exe, then vswhere, else null", async () => {
  const pf = "C:\\PF86";
  const vswhere = `${pf}\\Microsoft Visual Studio\\Installer\\vswhere.exe`;
  const vcvars = "C:\\VS\\VC\\Auxiliary\\Build\\vcvars64.bat";

  // cl.exe already on PATH
  const env = { Path: "C:\\a;C:\\tools", "ProgramFiles(x86)": pf };
  const none = async () => assert.fail("run must not be called");
  assert.equal(await msvcEnv({ run: none, env, exists: (p) => p === "C:\\tools\\cl.exe" }), env);

  // vswhere then vcvars
  const calls = [];
  const run = async (cmd, args) => {
    calls.push([cmd, ...args]);
    return cmd === vswhere ? "C:\\VS\r\n" : "Path=C:\\msvc\\bin\r\nINCLUDE=C:\\inc\r\n";
  };
  const got = await msvcEnv({ run, env, exists: (p) => p === vswhere || p === vcvars });
  assert.deepEqual(got, { Path: "C:\\msvc\\bin", INCLUDE: "C:\\inc" });
  assert.equal(calls[0][0], vswhere);
  assert.ok(calls[0].includes("installationPath"));
  assert.equal(calls[1][0], "cmd");
  assert.equal(calls[1].at(-1), `"call "${vcvars}" >nul && set"`);

  // no Visual Studio
  assert.equal(await msvcEnv({ run: none, env, exists: () => false }), null);
  assert.equal(await msvcEnv({ run: async () => "", env, exists: (p) => p === vswhere }), null);
});

test("resolveHost prefers flag, then LUMORA_HOST, then source build", async () => {
  const root = app();
  const builds = [];
  const build = async (o) => {
    builds.push(o);
    return "built.exe";
  };
  const env = { LUMORA_HOST: "env.exe", LUMORA_SOURCE: "/lumora" };
  assert.equal(await resolveHost({ flag: "flag.exe", env, root, build }), "flag.exe");

  const warn = mock.method(console, "warn", () => {});
  try {
    assert.equal(await resolveHost({ env, root, build }), "env.exe");
    assert.match(warn.mock.calls.flatMap((c) => c.arguments).join(" "), /Echo/);
  } finally {
    warn.mock.restore();
  }

  assert.equal(await resolveHost({ env: { LUMORA_SOURCE: "/lumora" }, root, build }), "built.exe");
  assert.deepEqual(builds, [{ root, lumoraSource: "/lumora" }]);
  await assert.rejects(resolveHost({ env: {}, root, build }), /LUMORA_HOST.*LUMORA_SOURCE|LUMORA_SOURCE.*LUMORA_HOST/s);
});

test("resolveHost with LUMORA_HOST and no C++ modules does not warn", async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "lumora-build-host-"));
  write(path.join(root, "package.json"), JSON.stringify({ name: "app" }));
  const warn = mock.method(console, "warn", () => {});
  try {
    assert.equal(await resolveHost({ env: { LUMORA_HOST: "env.exe" }, root }), "env.exe");
    assert.equal(warn.mock.callCount(), 0);
  } finally {
    warn.mock.restore();
  }
});
