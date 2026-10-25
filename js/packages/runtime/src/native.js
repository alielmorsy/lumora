// The host installs globalThis.__lumora before the bundle runs.
export const native = globalThis.__lumora;
if (!native) throw new Error("@lumora/runtime: globalThis.__lumora is missing, run the bundle inside a Lumora host");
