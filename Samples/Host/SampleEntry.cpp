#if defined(__ANDROID__)
#include <lumora/platform/EntryPoint.h>      // android_main -> Main
#else
#include <lumora/platform/Launch.h>
int main(int argc, char** argv) { return lumora::platform::Main({argc, argv}); }
#endif
