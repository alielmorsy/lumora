import fs from "node:fs/promises";
import path from "node:path";
import * as esbuild from "esbuild";
import { transformAsync } from "@babel/core";

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
          // esbuild deletes the outfile when a rebuild fails, so write it ourselves only on success
          b.onEnd(async (r) => {
            const ok = r.errors.length === 0;
            if (ok) {
              await fs.mkdir(path.dirname(outfile), { recursive: true });
              await fs.writeFile(outfile, r.outputFiles[0].contents);
            }
            if (started) onRebuild?.({ ok, errors: fmt(r.errors) });
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
