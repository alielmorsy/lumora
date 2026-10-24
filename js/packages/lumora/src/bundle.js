import fs from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import * as esbuild from "esbuild";
import { transformAsync } from "@babel/core";

// Babel resolves preset names from cwd, which is the user's project when run through `npm run dev`.
const babelCwd = path.dirname(fileURLToPath(import.meta.url));
const solidPreset = ["babel-preset-solid", { generate: "universal", moduleName: "@lumora/runtime" }];
const tsPreset = ["@babel/preset-typescript", { isTSX: true, allExtensions: true }];

const solidPlugin = {
  name: "lumora-solid",
  setup(build) {
    build.onLoad({ filter: /\.[jt]sx$/ }, async (args) => {
      const source = await fs.readFile(args.path, "utf8");
      try {
        const out = await transformAsync(source, {
          filename: args.path,
          presets: [solidPreset, tsPreset],
          sourceMaps: "inline",
          cwd: babelCwd,
          configFile: false,
          babelrc: false,
        });
        return { contents: out.code, loader: "js" };
      } catch (e) {
        const text = String(e.message).split("\n")[0].replace(/^.*?: /, "").replace(/ \(\d+:\d+\)$/, "");
        return { errors: [{ text, location: { file: args.path, line: e.loc?.line ?? 1, column: e.loc?.column ?? 0 } }] };
      }
    });
  },
};

// Each imported css file pushes its text onto globalThis.__lumoraCss when the module runs.
const cssPlugin = {
  name: "lumora-css",
  setup(build) {
    build.onLoad({ filter: /\.css$/ }, async (args) => ({
      contents: `(globalThis.__lumoraCss ??= []).push(${JSON.stringify(await fs.readFile(args.path, "utf8"))});`,
      loader: "js",
    }));
  },
};

const fmt = (msgs) =>
  msgs.map((m) => (m.location ? `${m.location.file}:${m.location.line}:${m.location.column}: ${m.text}` : m.text));

export async function bundle({ entry, outfile, dev = true, watch = false, external = [], onRebuild }) {
  let started = false;
  let t0 = 0;
  let last;
  const ctx = await esbuild.context({
    entryPoints: [entry],
    outfile,
    write: false,
    bundle: true,
    format: "iife",
    platform: "neutral",
    mainFields: ["module", "main"],
    conditions: ["browser"],
    target: "es2020",
    sourcemap: dev ? "inline" : false,
    minify: !dev,
    define: { "process.env.NODE_ENV": dev ? '"development"' : '"production"' },
    external,
    logLevel: "silent",
    plugins: [
      solidPlugin,
      cssPlugin,
      {
        name: "lumora-rebuild",
        setup(b) {
          b.onStart(() => void (t0 = performance.now()));
          // esbuild deletes the outfile when a rebuild fails, so write it ourselves only on success
          b.onEnd(async (r) => {
            const ok = r.errors.length === 0;
            // esbuild rebuilds once right after watch() starts, and editors save unchanged files: skip repeats
            const key = ok ? r.outputFiles[0].hash : fmt(r.errors).join("\n");
            if (key === last) return;
            last = key;
            if (ok) {
              await fs.mkdir(path.dirname(outfile), { recursive: true });
              await fs.writeFile(outfile, r.outputFiles[0].contents);
            }
            if (started) onRebuild?.({ ok, errors: fmt(r.errors), ms: performance.now() - t0 });
          });
        },
      },
    ],
  });
  let res;
  try {
    await ctx.rebuild();
    res = { ok: true, errors: [] };
  } catch (e) {
    res = { ok: false, errors: fmt(e.errors ?? [{ text: String(e.message ?? e) }]) };
  }
  started = true;
  if (watch) await ctx.watch();
  else await ctx.dispose();
  return { ...res, stop: () => ctx.dispose() };
}
