#include "Bridge.h"

#include <lumora/MountedApp.h>
#include <lumora/log/Log.h>

#include <algorithm>
#include <string>
#include <utility>

LUMORA_LOG_CATEGORY("Script")

namespace lumora::script
{
    namespace
    {
        /// A JS reference to a host node. Dropping the last one releases the node at the next safe point.
        class NodeRef final : public jsi::HostObject
        {
        public:
            /// `state` is null for refs that own nothing (the root).
            NodeRef(HostHandle h, std::shared_ptr<BridgeState> state) : handle(h), m_state(std::move(state)) {}

            ~NodeRef() override
            {
                // Runs inside the GC, maybe off-thread: queue only. During teardown ReleaseAll frees everything.
                if (!m_state || m_state->tearingDown) return;
                std::lock_guard lock(m_state->releasedMutex);
                m_state->released.push_back(handle);
            }

            const HostHandle handle;

        private:
            std::shared_ptr<BridgeState> m_state;
        };

        [[nodiscard]] const jsi::Value& Arg(const jsi::Value* args, std::size_t count, std::size_t i)
        {
            static const jsi::Value undefined;
            return i < count ? args[i] : undefined;
        }

        [[nodiscard]] std::string Str(jsi::Runtime& rt, const jsi::Value& v, const char* what)
        {
            if (!v.isString()) throw jsi::JSError::createTypeError(rt, std::string(what) + " must be a string");
            return v.getString(rt).utf8(rt);
        }

        [[nodiscard]] HostHandle Handle(jsi::Runtime& rt, const jsi::Value& v)
        {
            if (v.isObject())
            {
                const jsi::Object o = v.getObject(rt);
                if (o.isHostObject<NodeRef>(rt)) return o.getHostObject<NodeRef>(rt)->handle;
            }
            throw jsi::JSError::createTypeError(rt, "expected a node");
        }

        [[nodiscard]] HostValue ToHostValue(jsi::Runtime& rt, const jsi::Value& v)
        {
            if (v.isNull() || v.isUndefined()) return std::monostate{};
            if (v.isBool()) return v.getBool();
            if (v.isNumber()) return v.getNumber();
            if (v.isString()) return v.getString(rt).utf8(rt);
            throw jsi::JSError::createTypeError(rt, "setProp: value must be a string, number, boolean or null");
        }

        [[nodiscard]] bool ModOn(KeyMods mods, KeyMods bit) noexcept { return (mods & bit) != KeyMods::None; }

        [[nodiscard]] jsi::Object EventObject(jsi::Runtime& rt, const HostEventArgs& a)
        {
            jsi::Object e(rt);
            e.setProperty(rt, "x", a.position.x);
            e.setProperty(rt, "y", a.position.y);
            e.setProperty(rt, "dx", a.delta.x);
            e.setProperty(rt, "dy", a.delta.y);
            e.setProperty(rt, "button", a.button);
            e.setProperty(rt, "key", jsi::String::createFromUtf8(rt, a.key));
            e.setProperty(rt, "text", jsi::String::createFromUtf8(rt, a.text));
            e.setProperty(rt, "ctrl", ModOn(a.mods, KeyMods::Ctrl));
            e.setProperty(rt, "shift", ModOn(a.mods, KeyMods::Shift));
            e.setProperty(rt, "alt", ModOn(a.mods, KeyMods::Alt));
            e.setProperty(rt, "meta", ModOn(a.mods, KeyMods::Super));
            return e;
        }

        [[nodiscard]] HostHandler MakeHandler(std::shared_ptr<JsHandler> h, std::shared_ptr<BridgeState> state)
        {
            return [h = std::move(h), state = std::move(state)](const HostEventArgs& a)
            {
                if (!h->fn) return false;
                bool consumed = false;
                state->call([&](jsi::Runtime& rt)
                {
                    const jsi::Value r = h->fn->call(rt, EventObject(rt, a));
                    consumed = r.isBool() && r.getBool();
                });
                return consumed;
            };
        }
    } // namespace

    BridgeState::BridgeState(MountedApp& a) : app(a), host(a.Host()) {}

    void BridgeState::DrainReleased()
    {
        std::vector<HostHandle> batch;
        {
            std::lock_guard lock(releasedMutex);
            batch.swap(released);
        }
        for (const HostHandle h : batch)
        {
            if (owned.erase(h)) host.Release(h);
        }
    }

    void BridgeState::ReleaseAll()
    {
        for (const HostHandle h : owned) host.Release(h);
        owned.clear();
        host.Clear();
    }

    void BridgeState::DropHandlers()
    {
        for (const std::weak_ptr<JsHandler>& w : handlers)
        {
            if (const std::shared_ptr<JsHandler> h = w.lock()) h->fn.reset();
        }
        handlers.clear();
    }

    void InstallBridge(jsi::Runtime& rt, const std::shared_ptr<BridgeState>& state)
    {
        jsi::Object lumora(rt);
        const auto def = [&](const char* name, unsigned argc, jsi::HostFunctionType fn)
        {
            lumora.setProperty(rt, name, jsi::Function::createFromHostFunction(rt, jsi::PropNameID::forAscii(rt, name), argc, std::move(fn)));
        };
        BridgeState* s = state.get(); // the runtime dies before the state; refs that outlive a call hold `state`

        def("root", 0, [s](jsi::Runtime& r, const jsi::Value&, const jsi::Value*, std::size_t) -> jsi::Value
        {
            return jsi::Object::createFromHostObject(r, std::make_shared<NodeRef>(s->host.Root(), nullptr));
        });

        def("createNode", 1, [state](jsi::Runtime& r, const jsi::Value&, const jsi::Value* args, std::size_t n) -> jsi::Value
        {
            const std::string name = Str(r, Arg(args, n, 0), "createNode: tag");
            const std::optional<HostTag> tag = ParseHostTag(name);
            if (!tag) throw jsi::JSError::createTypeError(r, "createNode: unknown tag '" + name + "'");
            const HostHandle h = state->host.Create(*tag);
            if (h == kNoHost) throw jsi::JSError(r, "createNode: out of node handles");
            state->owned.insert(h);
            return jsi::Object::createFromHostObject(r, std::make_shared<NodeRef>(h, state));
        });

        def("insert", 3, [s](jsi::Runtime& r, const jsi::Value&, const jsi::Value* args, std::size_t n) -> jsi::Value
        {
            const jsi::Value& before = Arg(args, n, 2);
            s->host.Insert(Handle(r, Arg(args, n, 0)), Handle(r, Arg(args, n, 1)),
                           before.isNull() || before.isUndefined() ? kNoHost : Handle(r, before));
            return jsi::Value::undefined();
        });

        def("remove", 2, [s](jsi::Runtime& r, const jsi::Value&, const jsi::Value* args, std::size_t n) -> jsi::Value
        {
            s->host.Remove(Handle(r, Arg(args, n, 0)), Handle(r, Arg(args, n, 1)));
            return jsi::Value::undefined();
        });

        def("setProp", 3, [s](jsi::Runtime& r, const jsi::Value&, const jsi::Value* args, std::size_t n) -> jsi::Value
        {
            const HostHandle h = Handle(r, Arg(args, n, 0));
            const std::string name = Str(r, Arg(args, n, 1), "setProp: name");
            if (!s->host.SetProp(h, name, ToHostValue(r, Arg(args, n, 2)))) LOG_WARN("setProp: unknown prop '{}'", name);
            return jsi::Value::undefined();
        });

        def("setHandler", 3, [state](jsi::Runtime& r, const jsi::Value&, const jsi::Value* args, std::size_t n) -> jsi::Value
        {
            const HostHandle h = Handle(r, Arg(args, n, 0));
            const std::string name = Str(r, Arg(args, n, 1), "setHandler: event");
            const std::optional<HostEvent> event = ParseHostEvent(name);
            if (!event) throw jsi::JSError::createTypeError(r, "setHandler: unknown event '" + name + "'");
            const jsi::Value& fn = Arg(args, n, 2);
            if (fn.isNull() || fn.isUndefined())
            {
                state->host.SetHandler(h, *event, {});
                return jsi::Value::undefined();
            }
            if (!fn.isObject() || !fn.getObject(r).isFunction(r)) throw jsi::JSError::createTypeError(r, "setHandler: handler must be a function or null");

            auto handler = std::make_shared<JsHandler>();
            handler->fn = fn.getObject(r).getFunction(r);
            if (state->handlers.size() >= state->pruneAt)
            {
                std::erase_if(state->handlers, [](const std::weak_ptr<JsHandler>& w) { return w.expired(); });
                state->pruneAt = std::max<std::size_t>(64, state->handlers.size() * 2);
            }
            state->handlers.push_back(handler);
            state->host.SetHandler(h, *event, MakeHandler(std::move(handler), state));
            return jsi::Value::undefined();
        });

        def("setStylesheet", 1, [s](jsi::Runtime& r, const jsi::Value&, const jsi::Value* args, std::size_t n) -> jsi::Value
        {
            s->app.SetStylesheetSource(Str(r, Arg(args, n, 0), "setStylesheet: css"));
            return jsi::Value::undefined();
        });

        def("log", 2, [](jsi::Runtime& r, const jsi::Value&, const jsi::Value* args, std::size_t n) -> jsi::Value
        {
            const jsi::Value& level = Arg(args, n, 0);
            const jsi::Value& msg = Arg(args, n, 1);
            const std::string text = msg.isString() ? msg.getString(r).utf8(r) : msg.toString(r).utf8(r);
            switch (level.isNumber() ? static_cast<int>(level.getNumber()) : 1)
            {
            case 0: LOG_DEBUG("{}", text); break;
            case 2: LOG_WARN("{}", text); break;
            case 3: LOG_ERROR("{}", text); break;
            default: LOG_INFO("{}", text); break;
            }
            return jsi::Value::undefined();
        });

        rt.global().setProperty(rt, "__lumora", std::move(lumora));
    }
}
