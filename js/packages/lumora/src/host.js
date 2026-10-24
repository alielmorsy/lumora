export function resolveHost({ flag, env = process.env }) {
  const host = flag || env.LUMORA_HOST;
  if (!host) throw new Error("No host found. Build LumoraJsHost and set LUMORA_HOST to its path, or pass --host <exe>.");
  return host;
}
