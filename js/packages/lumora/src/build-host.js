import fs from "node:fs";
import path from "node:path";
import { spawn } from "node:child_process";
import { discoverModules, writeDesktopLinkFiles } from "./link.js";
import { msvcEnv } from "./msvc.js";

const slash = (p) => p.replaceAll("\\", "/");

function defaultRun(cmd, args, opts) {
  return new Promise((resolve, reject) => {
    const child = spawn(cmd, args, { ...opts, stdio: "inherit" });
    child.on("error", reject);
    child.on("exit", (code) => resolve(code ?? 1));
  });
}

// Builds LumoraJsHost from the Lumora source tree with the app's C++ modules linked in; returns the exe path.
export async function buildHost({ root, lumoraSource, config = "RelWithDebInfo", run = defaultRun, env }) {
  if (!env) {
    env = process.platform === "win32" ? await msvcEnv() : process.env;
    if (!env) throw new Error("Visual Studio with the C++ x64 tools was not found; install it or run from a VS developer prompt.");
  }
  const { cmakeFile, changed } = await writeDesktopLinkFiles(root, await discoverModules(root));
  const dir = path.join(root, ".lumora", "host", `${process.platform}-${process.arch}`);

  const step = async (label, args) => {
    const code = await run("cmake", args, { cwd: root, env });
    if (code !== 0) throw new Error(`cmake ${label} failed with exit code ${code}`);
  };
  if (changed || !fs.existsSync(path.join(dir, "CMakeCache.txt"))) {
    await step("configure", ["-S", lumoraSource, "-B", dir, "-G", "Ninja", `-DCMAKE_BUILD_TYPE=${config}`,
      "-DLUMORA_BUILD_TESTS=OFF", "-DLUMORA_BUILD_BENCHMARKS=OFF", `-DLUMORA_JS_MODULES_CMAKE=${slash(cmakeFile)}`]);
  }
  await step("build", ["--build", dir, "--target", "LumoraJsHost"]);
  return path.join(dir, "bin", process.platform === "win32" ? "LumoraJsHost.exe" : "LumoraJsHost");
}
