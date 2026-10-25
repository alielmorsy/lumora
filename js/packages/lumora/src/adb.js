import { spawn } from "node:child_process";

// Runs a command with inherited stdio and resolves to its exit code. Stops it when signal aborts.
export function defaultRun(cmd, args, { cwd, env, signal } = {}) {
  return new Promise((resolve) => {
    // .bat/.cmd files only run through a shell on Windows
    const shell = /\.(bat|cmd)$/i.test(cmd);
    const child = spawn(shell ? `"${cmd}"` : cmd, args, { cwd, env, stdio: "inherit", shell });
    const stop = () => child.kill();
    if (signal?.aborted) stop();
    signal?.addEventListener("abort", stop, { once: true });
    child.on("error", (e) => {
      console.error(`[lumora] cannot run ${cmd}: ${e.message}`);
      resolve(127);
    });
    child.on("close", (code) => {
      signal?.removeEventListener("abort", stop);
      resolve(code ?? 1);
    });
  });
}

// Returns adb(...args), which prefixes `-s <device>` when a serial is given.
export function makeAdb({ run, exe, device, signal }) {
  return (...args) => run(exe, [...(device ? ["-s", device] : []), ...args], { signal });
}

export const startApp = (adb, id) => adb("shell", "am", "start", "-n", `${id}/com.lumora.android.LumoraActivity`);

// The debug host reads <filesDir>/lumora-dev/bundle.js. run-as can only copy from a world-readable spot.
export async function pushBundle(adb, id, file) {
  const tmp = `/data/local/tmp/lumora-${id}.js`;
  const code = await adb("push", file, tmp);
  if (code !== 0) return code;
  // adb joins its arguments and the device shell splits them again, so the sh -c script needs its own quotes
  return adb("shell", "run-as", id, "sh", "-c", `'mkdir -p files/lumora-dev && cp ${tmp} files/lumora-dev/bundle.js'`);
}

// ponytail: pidof runs once when logcat starts, so a later app restart is not followed; rerun `lumora android` if so
export const logcat = (adb, id) => adb("shell", `logcat --pid=$(pidof -s ${id})`);
