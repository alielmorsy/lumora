import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { bundle } from "../src/bundle.js";

const host = process.env.LUMORA_HOST;
const here = path.dirname(fileURLToPath(import.meta.url));
const hello = path.resolve(here, "..", "..", "..", "examples", "hello");

// Bundles entry, runs 30 headless frames (about 500 ms) on the real host and returns its exit code and text dump.
async function run(entry) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "lumora-e2e-"));
  try {
    const outfile = path.join(dir, "bundle.js");
    const dump = path.join(dir, "dump.txt");
    const built = await bundle({ entry, outfile, dev: true });
    assert.deepEqual(built.errors, []);
    const r = spawnSync(host, ["--bundle", outfile, "--headless-frames", "30", "--dump-text", dump], { encoding: "utf8", timeout: 30000 });
    return { code: r.status, text: fs.existsSync(dump) ? fs.readFileSync(dump, "utf8") : "" };
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
}

test("hello example updates after a timer", { skip: !host && "LUMORA_HOST not set", timeout: 60000 }, async () => {
  const { code, text } = await run(path.join(hello, "src", "main.tsx"));
  assert.equal(code, 0);
  assert.match(text, /Count: 1/);
});

test("an app that throws during render shows the error and exits 0", { skip: !host && "LUMORA_HOST not set", timeout: 60000 }, async () => {
  const entry = path.join(hello, "src", "e2e-throw.tsx");
  fs.writeFileSync(
    entry,
    `import { render } from "@lumora/runtime";\nrender(() => { throw new Error("boom from render"); });\n`,
  );
  try {
    const { code, text } = await run(entry);
    assert.equal(code, 0);
    assert.match(text, /boom from render/);
  } finally {
    fs.rmSync(entry, { force: true });
  }
});
