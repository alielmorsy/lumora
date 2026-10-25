import fs from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { bundle } from "./bundle.js";
import { discoverModules, writeAndroidLinkFiles, writeDesktopLinkFiles, writeIfChanged } from "./link.js";
import { defaultRun, makeAdb, startApp, pushBundle, logcat } from "./adb.js";

const templates = fileURLToPath(new URL("../templates/android", import.meta.url));
const slash = (p) => p.replaceAll("\\", "/");
const subst = (text, vars) => text.replace(/\{\{(\w+)\}\}/g, (m, k) => vars[k] ?? m);
const exists = (p) => fs.access(p).then(() => true, () => false);
const template = async (name, vars) => subst(await fs.readFile(path.join(templates, name), "utf8"), vars);

function appConfig(pkg, root) {
  const name = pkg.name ?? path.basename(root);
  const sanitized = name.split("/").pop().toLowerCase().replace(/[^a-z0-9]/g, "") || "app";
  const app = pkg.lumora?.app ?? {};
  const json = {
    id: app.id ?? `com.lumora.app.${/^\d/.test(sanitized) ? "a" : ""}${sanitized}`,
    label: app.label ?? name,
    orientation: app.orientation,
    permissions: app.permissions,
    entry: { js: "bundle.js" },
  };
  return JSON.stringify(json, null, 2) + "\n";
}

async function writeProject({ root, lumoraSource, pkg, modules, dir }) {
  const src = path.join(lumoraSource, "Android");
  const { cmakeFile } = await writeDesktopLinkFiles(root, modules);
  const vars = { lumoraSource: slash(lumoraSource), root: slash(root), jsModulesCmake: slash(cmakeFile) };
  await writeAndroidLinkFiles(root, modules, dir);
  await writeIfChanged(path.join(dir, "settings.gradle.kts"), await template("settings.gradle.kts", vars));
  await writeIfChanged(path.join(dir, "gradle.properties"), await template("gradle.properties", vars));
  await writeIfChanged(path.join(dir, "app", "android", "build.gradle.kts"), await template("app/android/build.gradle.kts", vars));
  await writeIfChanged(path.join(dir, "app", "lumora.json"), appConfig(pkg, root));
  await fs.mkdir(path.join(dir, "gradle"), { recursive: true });
  await fs.copyFile(path.join(src, "gradle", "libs.versions.toml"), path.join(dir, "gradle", "libs.versions.toml"));
  await fs.cp(path.join(src, "gradle", "wrapper"), path.join(dir, "gradle", "wrapper"), { recursive: true });
  for (const f of ["gradlew", "gradlew.bat"]) await fs.copyFile(path.join(src, f), path.join(dir, f));
}

export async function android({ root, lumoraSource, device, run = defaultRun, signal, env = process.env }) {
  if (!lumoraSource) throw new Error("LUMORA_SOURCE is not set. Set it to the path of a Lumora checkout.");
  for (const v of ["ANDROID_HOME", "JAVA_HOME"]) {
    if (!env[v]) throw new Error(`${v} is not set. Install the Android SDK and a JDK, and set ${v}.`);
  }
  if (!(await exists(path.join(lumoraSource, "Android", "settings.gradle.kts")))) {
    throw new Error(`LUMORA_SOURCE (${lumoraSource}) is not a Lumora checkout: Android/settings.gradle.kts not found.`);
  }
  const pkg = JSON.parse(await fs.readFile(path.join(root, "package.json"), "utf8"));
  const dir = path.join(root, ".lumora", "android");
  await writeProject({ root, lumoraSource, pkg, modules: await discoverModules(root), dir });

  const outfile = path.join(dir, "app", "bundle.js");
  const adb = makeAdb({ run, exe: path.join(env.ANDROID_HOME, "platform-tools", "adb"), device, signal });
  const id = JSON.parse(await fs.readFile(path.join(dir, "app", "lumora.json"), "utf8")).id;
  // pushes run one at a time, in save order
  let pushing = Promise.resolve();
  const build = await bundle({
    entry: path.join(root, "src", "main.tsx"),
    outfile,
    watch: true,
    onRebuild: ({ ok, errors, ms }) => {
      if (!ok) return console.error(`[lumora] build failed\n${errors.join("\n")}`);
      console.log(`[lumora] rebuilt in ${Math.round(ms)} ms`);
      pushing = pushing.then(async () => {
        if ((await pushBundle(adb, id, outfile)) !== 0) console.error("[lumora] could not push the bundle to the device");
      });
    },
  });
  try {
    // ponytail: a failing first build ends the command since the APK needs a bundle; same as dev
    if (!build.ok) {
      console.error(`[lumora] build failed\n${build.errors.join("\n")}`);
      return 1;
    }
    const gradlew = path.join(dir, process.platform === "win32" ? "gradlew.bat" : "gradlew");
    let code = await run(gradlew, [":app:installDebug"], { cwd: dir, env, signal });
    if (code !== 0) return code;
    code = await startApp(adb, id);
    if (code !== 0) return code;
    const logCode = await logcat(adb, id);
    await pushing;
    return signal?.aborted ? 0 : logCode;
  } finally {
    await build.stop();
  }
}
