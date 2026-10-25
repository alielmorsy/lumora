#include <lumora/script/NativeModule.h>

// The stock host links no native modules. Custom hosts get a generated file instead (LUMORA_JS_MODULES_CMAKE).
void lumora::script::RegisterLinkedModules(ModuleRegistry&) {}
