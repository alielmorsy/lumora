#include <gtest/gtest.h>

#include <lumora/os/CrashHandler.h>
#include <lumora/platform/Clipboard.h>
#include <lumora/platform/GamepadState.h>
#include <lumora/platform/UserPaths.h>
#include <lumora/platform/Window.h>

#include "Linux/Keyboard.h"

#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace platform = lumora::platform;

namespace
{
    bool HasDisplayServer()
    {
        const char* wayland = std::getenv("WAYLAND_DISPLAY");
        const char* x11     = std::getenv("DISPLAY");
        return (wayland && *wayland) || (x11 && *x11);
    }
}

TEST(LinuxKeyboardTests, EvdevCodesMapToPhysicalKeys)
{
    EXPECT_EQ(platform::KeyFromEvdev(KEY_A), platform::Key::A);
    EXPECT_EQ(platform::KeyFromEvdev(KEY_0), platform::Key::N0);
    EXPECT_EQ(platform::KeyFromEvdev(KEY_F12), platform::Key::F12);
    EXPECT_EQ(platform::KeyFromEvdev(KEY_F24), platform::Key::F24);
    EXPECT_EQ(platform::KeyFromEvdev(KEY_KP5), platform::Key::Num5);
    EXPECT_EQ(platform::KeyFromEvdev(KEY_KPENTER), platform::Key::NumEnter);
    EXPECT_EQ(platform::KeyFromEvdev(KEY_RIGHTALT), platform::Key::RightAlt);
    EXPECT_EQ(platform::KeyFromEvdev(KEY_PAGEDOWN), platform::Key::PageDown);
    EXPECT_EQ(platform::KeyFromEvdev(KEY_MICMUTE), platform::Key::Unknown);
}

TEST(LinuxKeyboardTests, TypedTextFollowsModifiers)
{
    xkb_rule_names names{};
    names.layout = "us";

    platform::Keyboard keyboard;
    xkb_keymap* keymap = xkb_keymap_new_from_names(keyboard.Context(), &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    ASSERT_NE(keymap, nullptr);
    keyboard.SetKeymap(keymap);

    const platform::KeyTranslation plain = keyboard.Press(KEY_A);
    EXPECT_EQ(plain.key, platform::Key::A);
    EXPECT_STREQ(plain.text, "a");
    (void)keyboard.Release(KEY_A);

    const xkb_mod_mask_t shift = 1u << xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
    keyboard.UpdateMask(shift, 0, 0, 0, 0, 0);
    const platform::KeyTranslation shifted = keyboard.Press(KEY_1);
    EXPECT_EQ(shifted.key, platform::Key::N1);
    EXPECT_TRUE(platform::Has(shifted.mods, platform::KeyMods::Shift));
    EXPECT_STREQ(shifted.text, "!");

    const xkb_mod_mask_t ctrl = 1u << xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CTRL);
    keyboard.UpdateMask(ctrl, 0, 0, 0, 0, 0);
    const platform::KeyTranslation chord = keyboard.Press(KEY_C);
    EXPECT_EQ(chord.key, platform::Key::C);
    EXPECT_TRUE(platform::Has(chord.mods, platform::KeyMods::Ctrl));
    EXPECT_STREQ(chord.text, "");

    const xkb_mod_mask_t alt = 1u << xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_ALT);
    keyboard.UpdateMask(alt, 0, 0, 0, 0, 0);
    EXPECT_STREQ(keyboard.Press(KEY_X).text, "");
}

TEST(LinuxUserPathsTests, ConfigDirFollowsXdg)
{
    const std::filesystem::path base = std::filesystem::temp_directory_path() / "lumora-xdg-test";
    std::filesystem::remove_all(base);
    ::setenv("XDG_CONFIG_HOME", base.c_str(), 1);

    const std::filesystem::path dir = platform::GetUserConfigDir("LumoraTests");
    EXPECT_EQ(dir, base / "LumoraTests");
    EXPECT_TRUE(std::filesystem::is_directory(dir));

    std::filesystem::remove_all(base);
}

TEST(LinuxCrashHandlerTests, ManualReportContainsReason)
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "lumora-crash-test";
    std::filesystem::remove_all(dir);
    lumora::os::InstallCrashHandler({.dumpDir = dir, .appName = "LinuxTests", .keepReports = 5});

    const std::filesystem::path report = lumora::os::WriteCrashReport("manual test report");
    ASSERT_FALSE(report.empty());

    std::ifstream in(report);
    std::stringstream text;
    text << in.rdbuf();
    EXPECT_NE(text.str().find("manual test report"), std::string::npos);
    EXPECT_NE(text.str().find("Backtrace:"), std::string::npos);

    std::filesystem::remove_all(dir);
}

TEST(LinuxGamepadTests, NoDeviceReadsAsDisconnected)
{
    platform::GamepadState pads[platform::kMaxGamepads];
    platform::PollGamepads(pads);
    for (std::uint8_t i = 0; i < platform::kMaxGamepads; ++i)
    {
        if (!platform::GamepadConnected(i))
        {
            EXPECT_EQ(pads[i].buttons, 0u);
        }
    }
}

TEST(LinuxWindowTests, CreatesWindowWithRequestedSize)
{
    if (!HasDisplayServer()) GTEST_SKIP() << "no DISPLAY or WAYLAND_DISPLAY";

    auto window = platform::Window::Create({.title = "linux window", .width = 320, .height = 240});
    ASSERT_NE(window.Get(), nullptr);
    platform::Window::PumpAll();

    EXPECT_TRUE(platform::Window::AnyOpen());
    EXPECT_GT(window->GetDpiScale(), 0.f);
    const platform::NativeWindowHandle handle = window->GetNativeHandle();
    EXPECT_NE(handle.window, nullptr);
    EXPECT_NE(handle.instance, nullptr);

    const float scale = window->GetDpiScale();
    EXPECT_EQ(window->GetWidth(), static_cast<uint32_t>(320 * scale));
    EXPECT_EQ(window->GetHeight(), static_cast<uint32_t>(240 * scale));

    window->RequestClose();
    EXPECT_TRUE(window->ShouldClose());
    window->ClearCloseRequest();
    EXPECT_FALSE(window->ShouldClose());
}

TEST(LinuxWindowTests, WakeUnblocksWaitEvents)
{
    if (!HasDisplayServer()) GTEST_SKIP() << "no DISPLAY or WAYLAND_DISPLAY";

    auto window = platform::Window::Create({.title = "wake", .width = 200, .height = 100});
    ASSERT_NE(window.Get(), nullptr);

    window->Wake();
    window->WaitEvents();
    SUCCEED();
}

TEST(LinuxWindowTests, X11ClientRectRoundTrips)
{
    const char* x11 = std::getenv("DISPLAY");
    if (!x11 || !*x11 || std::getenv("WAYLAND_DISPLAY")) GTEST_SKIP() << "needs a pure X11 session";

    auto window = platform::Window::Create({.title = "geometry", .width = 320, .height = 240,
                                            .position = platform::ScreenPoint{120, 90}});
    ASSERT_NE(window.Get(), nullptr);
    if (window->GetNativeHandle().backend != platform::WindowBackend::Xlib) GTEST_SKIP() << "X11 not selected";

    window->SetClientRect({140, 100, 400, 300});
    for (int i = 0; i < 20; ++i) platform::Window::PumpAll();

    const platform::ScreenRect rect = window->GetClientRect();
    EXPECT_EQ(rect.width, 400);
    EXPECT_EQ(rect.height, 300);

    const platform::ScreenPoint p = window->ClientToScreen(10.f, 20.f);
    EXPECT_EQ(p.x, rect.x + 10);
    EXPECT_EQ(p.y, rect.y + 20);
}

TEST(LinuxWindowTests, X11ClipboardRoundTrips)
{
    const char* x11 = std::getenv("DISPLAY");
    if (!x11 || !*x11 || std::getenv("WAYLAND_DISPLAY")) GTEST_SKIP() << "needs a pure X11 session";

    auto window = platform::Window::Create({.title = "clipboard", .width = 200, .height = 100});
    ASSERT_NE(window.Get(), nullptr);
    if (window->GetNativeHandle().backend != platform::WindowBackend::Xlib) GTEST_SKIP() << "X11 not selected";

    const std::string text = "lumora clipboard \xC3\xA9\xE2\x82\xAC";
    ASSERT_TRUE(platform::SetClipboardText(text));
    EXPECT_EQ(platform::GetClipboardText(), text);
}
