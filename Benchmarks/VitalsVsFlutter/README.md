# Vitals: Lumora vs Flutter

Same app built twice and measured on the same phone: Lumora's `Samples/Vitals` and a Flutter port of it.

- **Device:** Realme RMX3636, MediaTek Helio G99 (MT6789, 8 cores), 8 GB RAM, Android 15. The panel ran in 60 Hz mode during all runs. Phone powered over USB.
- **Lumora:** `gradlew :Vitals:assembleRelease -x lintVitalRelease`, signed with the debug key. Native code built in release.
- **Flutter:** 3.41.7 stable (Dart 3.11.5), `flutter build apk --release --target-platform android-arm64`, Impeller on Vulkan.
- **Flutter port:** `D:\works\vitals_flutter\lib\main.dart`, kept outside this repo. It copies the model, the spring and tween animations, the hand-drawn icons and charts, and the layout values from the stylesheet. Like Lumora, every chart and icon redraws on every frame. Differences:
  - The number field is a simple -/+ stepper.
  - The slider and switch are Flutter's Material ones.
  - The sleep chart has no hover scrub.
- **Date:** 2026-10-23. Two runs, with the order swapped between them (Lumora first, then Flutter first).

## Results

| | Lumora | Flutter |
|---|---|---|
| CPU, idle, settled (% of one core) | **60-66** | 72-80 |
| CPU, idle, first ~20 s after launch | 28-39 | 57-69 |
| CPU while scrolling | 54 | 66-77 |
| CPU while switching tabs | 60 | 59-75 |
| Where the CPU goes | main thread ~50, GPU driver (`mali-cmar-backe`) ~8 | UI thread ~41, raster thread ~25 |
| Frame rate | 59.9 fps | 58.4-59.7 fps |
| Dropped frames (>25 ms) | **0** in 8 phases of 20 s (1 frame in one phase) | 0.3-2.3 % of frames |
| Slowest 1 % of frames / worst frame | 17 ms / 33 ms | 33 ms / 50-67 ms |
| Memory, PSS | 86-93 MB | **64-80 MB** |
| Memory, RSS | 240-248 MB | 208-235 MB |
| "Graphics" line in `dumpsys meminfo` | 38 MB | 1.9 MB |
| Cold start, `am start -W` TotalTime (median of launches 2-6) | **302-331 ms** | 500-533 ms |
| Lumora's first launch after install | 6.1 s | - |
| APK size, arm64 only | **~12.4 MB** (libVitals.so 6.2 MB + classes.dex 5.6 MB) | 14.6 MB (libflutter.so 11.3 MB + libapp.so 3.5 MB) |

## Notes

- **The settled row is the fair CPU comparison.** Idle CPU climbs for both apps over the first ~45 s even with no input at all (`results/ctrl_all.txt`). Lumora goes from 30 % to 66 %, Flutter from 63 % to 80 %. CPU clock speeds drop over the same window, so this is the phone's launch boost ending, not work piling up. Tab switching doesn't change it either: after 40, 80 and 120 switches, CPU stays at the same level (`results/leak_*.txt`).
- **The "Graphics" memory line isn't comparable between the two.** Each 1080x2400 screen buffer is about 10 MB, so Lumora's 38 MB is probably mostly its screen buffers. Flutter's may be counted under another line by this phone's driver. Compare total PSS and RSS instead.
- **Cold start measures when the app window first draws,** not when real content appears.
- **About 5.6 MB of Lumora's APK is Java code** from appcompat and core-ktx that never gets shrunk, because the release build doesn't run R8. Flutter runs R8, and its Java code is 0.5 MB.
- **`:Vitals:lintVitalRelease` fails** with `LumoraActivity must extend android.app.Activity [Instantiatable]`, so the release build needs `-x lintVitalRelease` for now.

## Method

`bench.sh` runs on the phone for one app (`sh bench.sh <package> <activity> <tag>`):

1. Six cold launches (`am force-stop`, then `am start -W`).
2. Four 20 s phases on the Today screen: idle, scrolling (`input swipe`), tab switching (`input tap` on the bottom nav), then idle again.

In each phase it records:

- The app's CPU time from `/proc/<pid>/stat`, plus per-thread CPU time.
- Whole-system CPU time from `/proc/stat`.
- Frame timestamps, read once per second with `dumpsys SurfaceFlinger --latency` on the app's `SurfaceView(BLAST)` layer. The buffer only holds about 128 frames, so it has to be polled. It is cleared at the start of each phase.
- `dumpsys meminfo` at the end of the phase.

The other scripts:

- `leak.sh`: idle CPU before and after rounds of 40 tab switches.
- `ctrl.sh`: idle CPU over time with no input (last argument `0`) or after one round of 4 tab switches (`1`), plus CPU clock speeds.
- `parse.py`: turns `results/bench_*.txt` into the numbers above.

```sh
adb push bench.sh /data/local/tmp/
adb shell sh /data/local/tmp/bench.sh com.lumora.vitals com.lumora.android.LumoraActivity lumora
adb shell sh /data/local/tmp/bench.sh com.lumora.vitals_flutter .MainActivity flutter
adb pull /data/local/tmp/bench_lumora.txt results/
python parse.py results/bench_lumora.txt results/bench_flutter.txt
```

The tap and swipe coordinates assume a 1080x2400 screen at 480 dpi.
