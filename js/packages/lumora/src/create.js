import fs from "node:fs/promises";
import path from "node:path";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("..", import.meta.url));
const NAME = /^[a-z0-9][a-z0-9-_]*$/;
const NPM_NAME = /^(@[a-z0-9][a-z0-9-_.]*\/)?[a-z0-9][a-z0-9-_.]*$/;
const MODULE_NAME = /^[A-Z][A-Za-z0-9]*$/;
const ANDROID_PACKAGE = /^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+$/;

const subst = (text, vars) => text.replace(/\{\{(\w+)\}\}/g, (m, k) => vars[k] ?? m);

async function copy(from, to, vars) {
  await fs.mkdir(to, { recursive: true });
  for (const e of await fs.readdir(from, { withFileTypes: true })) {
    const src = path.join(from, e.name);
    const dst = path.join(to, subst(e.name, vars));
    if (e.isDirectory()) await copy(src, dst, vars);
    else await fs.writeFile(dst, subst(await fs.readFile(src, "utf8"), vars));
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

async function emptyDir(cwd, name) {
  const dir = path.resolve(cwd, name);
  if ((await fs.readdir(dir).catch(() => [])).length) throw new Error(`${dir} is not empty.`);
  return dir;
}

async function runtimeVersion() {
  // lumora and @lumora/runtime are released in lockstep, so templates pin both to this package's version
  return JSON.parse(await fs.readFile(path.join(root, "package.json"), "utf8")).version;
}

export async function create({ name, cwd, install = true }) {
  if (!NAME.test(name ?? "")) throw new Error(`Invalid project name "${name}". The name must match ${NAME}.`);
  const dir = await emptyDir(cwd, name);
  await copy(path.join(root, "templates", "app"), dir, { name, runtimeVersion: await runtimeVersion() });
  if (install) await npmInstall(dir);
  return dir;
}

export async function createModule({ name, cwd, module, androidPackage }) {
  if (!NPM_NAME.test(name ?? "")) throw new Error(`Invalid package name "${name}". The name must match ${NPM_NAME}.`);
  const base = name.split("/").pop();
  const Name = module ?? base.split(/[-_.]/).map((w) => w.charAt(0).toUpperCase() + w.slice(1)).join("");
  if (!MODULE_NAME.test(Name)) throw new Error(`Invalid module name "${Name}". Pass --module <PascalName>; it must match ${MODULE_NAME}.`);
  const lower = Name.toLowerCase();
  const pkg = androidPackage ?? `com.lumora.modules.${lower}`;
  if (!ANDROID_PACKAGE.test(pkg)) throw new Error(`Invalid Android package "${pkg}". It must match ${ANDROID_PACKAGE}.`);
  const dir = await emptyDir(cwd, base);
  const vars = { npmName: name, Name, lower, androidPackage: pkg, pkgPath: pkg.replaceAll(".", "/"), runtimeVersion: await runtimeVersion() };
  await copy(path.join(root, "templates", "module"), dir, vars);
  return dir;
}
