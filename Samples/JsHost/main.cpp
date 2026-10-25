#include "JsHost.h"

#include <lumora/platform/Launch.h>

int lumora::platform::Main(const lumora::platform::LaunchArgs& args)
{
    return lumora::jshost::RunJsHost(args.argc, args.argv);
}
