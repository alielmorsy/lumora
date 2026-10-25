import fs from "node:fs";
import path from "node:path";
import { execFile } from "node:child_process";

// Captures stdout; cmd needs verbatim args or Node re-quotes the `call ...` string.
function capture(cmd, args) {
  return new Promise((resolve) => {
    execFile(cmd, args, { windowsVerbatimArguments: cmd === "cmd", maxBuffer: 16 * 1024 * 1024 }, (err, stdout) => resolve(err ? "" : stdout));
  });
}

// Environment with the MSVC x64 toolchain: the current one if cl.exe is on PATH, else vcvars64.bat from the newest
// Visual Studio that vswhere finds. Null when there is no Visual Studio.
export async function msvcEnv({ run = capture, env = process.env, exists = fs.existsSync } = {}) {
  const pathVar = env.Path ?? env.PATH ?? "";
  if (pathVar.split(";").some((dir) => dir && exists(path.win32.join(dir, "cl.exe")))) return env;

  const vswhere = path.win32.join(env["ProgramFiles(x86)"] ?? "C:\\Program Files (x86)", "Microsoft Visual Studio", "Installer", "vswhere.exe");
  if (!exists(vswhere)) return null;
  const install = (await run(vswhere, ["-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"]))
    .split(/\r?\n/)[0]
    .trim();
  if (!install) return null;
  const vcvars = path.win32.join(install, "VC", "Auxiliary", "Build", "vcvars64.bat");
  if (!exists(vcvars)) return null;

  const out = await run("cmd", ["/d", "/s", "/c", `"call "${vcvars}" >nul && set"`]);
  const vars = {};
  for (const line of out.split(/\r?\n/)) {
    const eq = line.indexOf("=");
    if (eq > 0) vars[line.slice(0, eq)] = line.slice(eq + 1);
  }
  return Object.keys(vars).length ? vars : null;
}
