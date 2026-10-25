#include <lumora/script/NativeModule.h>

#include <chrono>
#include <string>
#include <thread>

using namespace facebook;

namespace
{
    jsi::Function Fn(jsi::Runtime& rt, const char* name, unsigned args, jsi::HostFunctionType fn)
    {
        return jsi::Function::createFromHostFunction(rt, jsi::PropNameID::forAscii(rt, name), args, std::move(fn));
    }

    std::string Str(jsi::Runtime& rt, const jsi::Value* args, size_t n, size_t i)
    {
        if (i >= n || !args[i].isString()) throw jsi::JSError(rt, "Echo: expected a string argument");
        return args[i].getString(rt).utf8(rt);
    }

    /// exports.__pending holds the resolvers of `later` by id, since invoker jobs must not hold jsi values.
    jsi::Object Pending(jsi::Runtime& rt)
    {
        return rt.global()
            .getPropertyAsObject(rt, "__lumora").getPropertyAsFunction(rt, "requireModule").call(rt, "Echo")
            .getObject(rt).getPropertyAsObject(rt, "__pending");
    }
}

LUMORA_NATIVE_MODULE(Echo)
{
    jsi::Runtime& rt = ctx.runtime;
    ctx.exports.setProperty(rt, "echo", Fn(rt, "echo", 1,
        [](jsi::Runtime& r, const jsi::Value&, const jsi::Value* args, size_t n) -> jsi::Value
        { return jsi::String::createFromUtf8(r, Str(r, args, n, 0)); }));

    ctx.exports.setProperty(rt, "__pending", jsi::Object(rt));
    ctx.exports.setProperty(rt, "later", Fn(rt, "later", 2,
        [invoker = ctx.invoker, next = 0](jsi::Runtime& r, const jsi::Value&, const jsi::Value* args, size_t n) mutable -> jsi::Value
        {
            std::string s = Str(r, args, n, 0);
            const double ms = n > 1 && args[1].isNumber() ? args[1].getNumber() : 0;
            const std::string key = std::to_string(next++);
            jsi::Function executor = Fn(r, "executor", 2,
                [key](jsi::Runtime& r2, const jsi::Value&, const jsi::Value* a, size_t) -> jsi::Value
                {
                    Pending(r2).setProperty(r2, key.c_str(), a[0]);
                    return jsi::Value::undefined();
                });
            jsi::Value promise = r.global().getPropertyAsFunction(r, "Promise").callAsConstructor(r, executor);

            // ponytail: one detached thread per call, a timer thread or the host's timers if call rate matters
            std::thread([invoker, s = std::move(s), ms, key]
            {
                std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(ms));
                invoker([s, key](jsi::Runtime& jr)
                {
                    jsi::Object pending = Pending(jr);
                    jsi::Function resolve = pending.getPropertyAsFunction(jr, key.c_str());
                    pending.setProperty(jr, key.c_str(), jsi::Value::undefined());
                    resolve.call(jr, jsi::String::createFromUtf8(jr, s));
                });
            }).detach();
            return promise;
        }));
}
