#pragma once

namespace lumora::jshost {

// Runs a JS bundle in a window (or headless) and reloads it when the file changes.
// Flags: --bundle <path>, --title <s>, --dump-text <path>, plus every RunSample flag.
int RunJsHost(int argc, char** argv);

} // namespace lumora::jshost
