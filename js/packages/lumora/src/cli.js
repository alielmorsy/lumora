import fs from "node:fs";
import path from "node:path";
import readline from "node:readline";
import { parseArgs } from "node:util";
import { create, createModule } from "./create.js";
import { dev } from "./dev.js";
import { resolveHost } from "./host.js";

const HELP = `Usage:
  lumora [name]                      Create a project (same as "lumora create")
  lumora create [name] [--no-install]
  lumora create-module <npm-name> [--module <PascalName>] [--android-package <pkg>]
  lumora dev [--host <exe>] [-- <host args>]

The host is --host, then the LUMORA_HOST environment variable.
`;

const DEFAULT_NAME = "my-lumora-app";

function inLumoraProject(cwd) {
  try {
    const pkg = JSON.parse(fs.readFileSync(path.join(cwd, "package.json"), "utf8"));
    return Boolean(pkg.dependencies?.lumora || pkg.devDependencies?.lumora);
  } catch {
    return false;
  }
}

function ask() {
  return new Promise((resolve) => {
    const rl = readline.createInterface({ input: process.stdin, output: process.stdout });
    rl.once("close", () => resolve(DEFAULT_NAME));
    rl.question(`Project name (${DEFAULT_NAME}): `, (answer) => {
      resolve(answer.trim() || DEFAULT_NAME);
      rl.close();
    });
  });
}

export async function main(argv, cwd = process.cwd()) {
  try {
    const split = argv.indexOf("--");
    const hostArgs = split < 0 ? [] : argv.slice(split + 1);
    const { values, positionals } = parseArgs({
      args: split < 0 ? argv : argv.slice(0, split),
      allowPositionals: true,
      options: { host: { type: "string" }, "no-install": { type: "boolean" }, module: { type: "string" }, "android-package": { type: "string" }, help: { type: "boolean", short: "h" } },
    });
    const [first, ...rest] = positionals;
    if (values.help || first === "help" || (!first && inLumoraProject(cwd))) {
      console.log(HELP);
      return 0;
    }
    if (first === "dev") {
      const host = resolveHost({ flag: values.host });
      const ac = new AbortController();
      for (const s of ["SIGINT", "SIGTERM"]) process.on(s, () => ac.abort());
      return await dev({ root: cwd, host, hostArgs, signal: ac.signal });
    }
    if (first === "create-module") {
      const dir = await createModule({ name: rest[0], cwd, module: values.module, androidPackage: values["android-package"] });
      console.log(`Created ${dir}`);
      return 0;
    }
    const name = first === "create" ? rest[0] : first;
    const dir = await create({ name: name ?? (await ask()), cwd, install: !values["no-install"] });
    console.log(`Created ${dir}\nNext: cd ${path.relative(cwd, dir)} && npm run dev`);
    return 0;
  } catch (e) {
    console.error(`lumora: ${e.message}`);
    return 1;
  }
}
