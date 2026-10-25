import fs from "node:fs/promises";
import path from "node:path";
import { spawn } from "node:child_process";
import { bundle } from "./bundle.js";

const failed = (errors) => console.error(`[lumora] build failed\n${errors.join("\n")}`);

// host is the executable, or [executable, ...leading args]
export async function dev({ root, host, hostArgs = [], signal }) {
  const outfile = path.join(root, ".lumora", "dev", "bundle.js");
  const { name = path.basename(root) } = JSON.parse(await fs.readFile(path.join(root, "package.json"), "utf8"));
  const build = await bundle({
    entry: path.join(root, "src", "main.tsx"),
    outfile,
    watch: true,
    onRebuild: ({ ok, errors, ms }) => (ok ? console.log(`[lumora] rebuilt in ${Math.round(ms)} ms`) : failed(errors)),
  });
  // ponytail: a failing first build ends dev since the host needs a bundle to load; wait for the first good build if restarting gets annoying
  if (!build.ok) {
    failed(build.errors);
    await build.stop();
    return 1;
  }
  const [exe, ...lead] = [host].flat();
  try {
    return await new Promise((resolve) => {
      const child = spawn(exe, [...lead, "--bundle", outfile, "--title", name, ...hostArgs], { stdio: "inherit" });
      const stop = () => child.kill();
      if (signal?.aborted) stop();
      signal?.addEventListener("abort", stop, { once: true });
      child.on("error", (e) => {
        console.error(`[lumora] cannot start host ${exe}: ${e.message}`);
        resolve(1);
      });
      child.on("close", (code) => resolve(code ?? 1));
    });
  } finally {
    await build.stop();
  }
}
