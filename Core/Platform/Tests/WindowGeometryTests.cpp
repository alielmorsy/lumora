#include <gtest/gtest.h>
#include <lumora/platform/Window.h>

#include <Windows.h>

namespace platform = lumora::platform;

TEST(WindowGeometryTests, PlacementRoundTrips)
{
    auto window = platform::Window::Create({.title = "geometry", .width = 320, .height = 240,
                                            .position = platform::ScreenPoint{200, 150}});
    platform::Window::PumpAll();

    const platform::ScreenRect r = window->GetClientRect();
    EXPECT_EQ(r.x, 200);
    EXPECT_EQ(r.y, 150);
    EXPECT_EQ(r.width, 320);
    EXPECT_EQ(r.height, 240);

    const platform::ScreenPoint p = window->ClientToScreen(10.f, 20.f);
    EXPECT_EQ(p.x, 210);
    EXPECT_EQ(p.y, 170);

    window->SetClientRect({260, 180, 400, 300});
    platform::Window::PumpAll();
    const platform::ScreenRect moved = window->GetClientRect();
    EXPECT_EQ(moved.x, 260);
    EXPECT_EQ(moved.y, 180);
    EXPECT_EQ(moved.width, 400);
    EXPECT_EQ(moved.height, 300);
}

TEST(WindowGeometryTests, WindowAtFindsOnlyClientAreas)
{
    auto window = platform::Window::Create({.title = "hit", .width = 300, .height = 200,
                                            .position = platform::ScreenPoint{300, 300}});
    window->Focus();
    platform::Window::PumpAll();

    EXPECT_EQ(platform::Window::WindowAt({450, 400}), window.Get());
    EXPECT_EQ(platform::Window::WindowAt({-32000, -32000}), nullptr);
}

TEST(WindowGeometryTests, OwnedWindowHasOwner)
{
    auto owner = platform::Window::Create({.title = "owner", .width = 200, .height = 200});
    auto owned = platform::Window::Create({.title = "owned", .width = 200, .height = 200, .owner = owner.Get(),
                                           .showInTaskbar = false});
    const HWND ownerHwnd = static_cast<HWND>(owner->GetNativeHandle().window);
    const HWND ownedHwnd = static_cast<HWND>(owned->GetNativeHandle().window);
    EXPECT_EQ(::GetWindow(ownedHwnd, GW_OWNER), ownerHwnd);
}

TEST(WindowGeometryTests, MonitorQueries)
{
    const platform::ScreenRect work = platform::Window::PrimaryWorkArea();
    EXPECT_TRUE(work.width > 0 && work.height > 0);
    EXPECT_TRUE(platform::Window::IsOnAnyMonitor(work));
    EXPECT_FALSE(platform::Window::IsOnAnyMonitor({-100000, -100000, 10, 10}));
}
