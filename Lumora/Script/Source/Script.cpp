#include <lumora/script/Script.h>

#include <hermes/hermes.h>

#include <memory>

namespace lumora::script
{
    std::string EvalToString(std::string_view js)
    {
        namespace jsi = facebook::jsi;
        auto runtime = facebook::hermes::makeHermesRuntime();
        try
        {
            jsi::Value result = runtime->evaluateJavaScript(std::make_shared<jsi::StringBuffer>(std::string(js)), "eval.js");
            return result.toString(*runtime).utf8(*runtime);
        }
        catch (const jsi::JSIException& e)
        {
            return std::string("Error: ") + e.what();
        }
    }
}
