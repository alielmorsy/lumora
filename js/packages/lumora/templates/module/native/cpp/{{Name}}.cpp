#include <lumora/script/NativeModule.h>

using namespace facebook;

LUMORA_NATIVE_MODULE({{Name}})
{
    auto hello = jsi::Function::createFromHostFunction(
        ctx.runtime, jsi::PropNameID::forAscii(ctx.runtime, "hello"), 0,
        [](jsi::Runtime& rt, const jsi::Value&, const jsi::Value*, size_t) -> jsi::Value
        { return jsi::String::createFromUtf8(rt, "Hello from C++"); });
    ctx.exports.setProperty(ctx.runtime, "hello", std::move(hello));
}
