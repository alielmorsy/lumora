// Stands in for LumoraJsHost. Writes its argv and the bundle it was given to --out, then exits 0.
// --wait: stay up until the bundle changes, write again, exit 0. --hang: stay up until killed.
// Run by `node --test test/` without arguments, so it does nothing then.
import fs from "node:fs";

const argv = process.argv.slice(2);
const arg = (name) => argv[argv.indexOf(name) + 1];
const bundle = arg("--bundle");
if (bundle) {
  const out = arg("--out");
  const report = () => fs.writeFileSync(out, JSON.stringify({ argv, bundle: fs.readFileSync(bundle, "utf8") }));
  report();
  if (argv.includes("--hang")) setInterval(() => {}, 1000);
  else if (argv.includes("--wait")) {
    const first = fs.statSync(bundle).mtimeMs;
    const started = Date.now();
    const poll = setInterval(() => {
      if (fs.statSync(bundle).mtimeMs !== first) {
        clearInterval(poll);
        report();
      } else if (Date.now() - started > 20000) process.exit(3);
    }, 20);
  }
}
