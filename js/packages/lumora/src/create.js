import fs from "node:fs/promises";
import path from "node:path";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("..", import.meta.url));
const NAME = /^[a-z0-9][a-z0-9-_]*$/;

async function copy(from, to, vars) {
  await fs.mkdir(to, { recursive: true });
  for (const e of await fs.readdir(from, { withFileTypes: true })) {
    const src = path.join(from, e.name);
    const dst = path.join(to, e.name);
    if (e.isDirectory()) await copy(src, dst, vars);
    else await fs.writeFile(dst, (await fs.readFile(src, "utf8")).replace(/\{\{(\w+)\}\}/g, (m, k) => vars[k] ?? m));
  }
}

function npmInstall(cwd) {
  return new Promise((resolve, reject) => {
    // npm is npm.cmd on Windows, which spawn only runs through a shell
    const child = spawn("npm install", { cwd, stdio: "inherit", shell: true });
    child.on("error", reject);
    child.on("close", (code) => (code === 0 ? resolve() : reject(new Error(`npm install failed (exit ${code}). Run it in ${cwd} once fixed.`))));
  });
}

export async function create({ name, cwd, install = true }) {
  if (!NAME.test(name ?? "")) throw new Error(`Invalid project name "${name}". The name must match ${NAME}.`);
  const dir = path.resolve(cwd, name);
  const existing = await fs.readdir(dir).catch(() => []);
  if (existing.length) throw new Error(`${dir} is not empty.`);
  // lumora and @lumora/runtime are released in lockstep, so the template pins both to this package's version
  const { version } = JSON.parse(await fs.readFile(path.join(root, "package.json"), "utf8"));
  await copy(path.join(root, "templates", "app"), dir, { name, runtimeVersion: version });
  if (install) await npmInstall(dir);
  return dir;
}
