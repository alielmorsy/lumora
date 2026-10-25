import { buildHost } from "./build-host.js";
import { discoverModules } from "./link.js";

// --host, then LUMORA_HOST, then a custom host built from LUMORA_SOURCE (the only way C++ modules get linked).
export async function resolveHost({ flag, env = process.env, root = process.cwd(), build = buildHost }) {
  if (flag) return flag;
  if (env.LUMORA_HOST) {
    const cpp = (await discoverModules(root)).filter((m) => m.cpp).map((m) => m.name);
    if (cpp.length) console.warn(`[lumora] LUMORA_HOST is a prebuilt host; C++ modules ${cpp.join(", ")} are not in it. Unset it and set LUMORA_SOURCE to build one that has them.`);
    return env.LUMORA_HOST;
  }
  if (env.LUMORA_SOURCE) return build({ root, lumoraSource: env.LUMORA_SOURCE });
  throw new Error("No host found. Set LUMORA_HOST to a built LumoraJsHost, set LUMORA_SOURCE to a Lumora checkout to build one, or pass --host <exe>.");
}
