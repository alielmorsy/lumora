import { test } from "node:test";
import assert from "node:assert/strict";
import vm from "node:vm";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { bundle } from "lumora/src/bundle.js";
import { createFakeNative } from "./fake-native.js";

const fx = path.join(path.dirname(fileURLToPath(import.meta.url)), "fixtures");

test("requireNativeModule returns the host module and throws for unknown names", async () => {
  const outfile = path.join(fs.mkdtempSync(path.join(os.tmpdir(), "lumora-modules-")), "modules.js");
  const r = await bundle({ entry: path.join(fx, "modules.tsx"), outfile });
  assert.equal(r.ok, true, r.errors.join("\n"));
  const echo = { echo: (s) => s };
  const fake = createFakeNative({ modules: { Echo: echo } });
  const ctx = { __lumora: fake.api };
  vm.runInNewContext(fs.readFileSync(outfile, "utf8"), ctx);
  assert.equal(ctx.echo, echo);
  assert.equal(
    ctx.err,
    'Native module "Nope" is not linked into this host. Install the package that provides it and restart the dev command.',
  );
});
