#pragma once

#include <lumora/Animation/Animation.h>
#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Rect.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Reactive/Computed.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Widgets/Widgets.h>

#include <array>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace vitals {

using namespace lumora;

constexpr float kPi = 3.14159265f;

enum class Tab { Today, Activity, Sleep, Profile };

enum class Ico {
    Today, Activity, Sleep, Profile, Heart, Steps, Water, Flame, Bolt, More, Plus,
    Clock, Scale, Mood, Run, Bike, Swim, Lift, Pin, Stairs, Moon, Sun, Check
};

enum class Stage { Awake, Rem, Light, Deep };

struct Palette {
    Color bg, card, cardVar, onSurface, muted, outline;
    Color primary, onPrimary, primaryContainer, onPrimaryContainer;
    Color move, exercise, stand, water, heart;
    std::array<Color, 4> stages; // awake, rem, light, deep
};

struct Workout {
    int         id = 0;
    Ico         icon = Ico::Run;
    std::string title;
    std::string when;
    int         minutes = 0;
    int         kcal = 0;
    float       km = 0.f;
    bool operator==(const Workout&) const = default;
};

struct SleepSeg {
    Stage stage;
    float start; // minutes after bedtime
    float len;
};

// Whole-app state; screens capture the shared pointer.
struct Vitals {
    Vitals();

    // Navigation / chrome
    Signal<int>  tab{0};
    int          tabDir = 1; // +1 slide from right, -1 from left
    Signal<bool> sheetOpen{false};
    Signal<bool> snackShown{false};
    Signal<std::string> snackText{""};
    std::function<void()> undo;

    // Theme
    Signal<bool> dark{false};
    Signal<int>  accent{0};
    Signal<bool> imperial{false};
    Palette      pal{}; // eased toward the theme target every frame

    // Live data
    Signal<float> steps{6240.f};
    Signal<float> heartRate{72.f};
    Signal<int>   water{5}; // glasses of 250 ml
    Signal<float> weight{72.4f};
    Signal<int>   mood{3};
    Signal<float> exerciseMin{24.f};
    Signal<float> standHours{8.f};
    std::vector<float> hrHistory;
    float hrPhase = 0.f; // sample interpolation for smooth scrolling

    // Goals (Bind<double> for Slider / NumberField)
    Signal<double> stepGoal{10000.0};
    Signal<double> moveGoal{600.0};
    Signal<double> waterGoal{8.0};

    // Activity
    std::array<float, 7> week{};
    Signal<int> selectedDay{6};
    Signal<std::vector<Workout>> workouts{};
    int nextWorkoutId = 100;

    // Sleep
    std::vector<SleepSeg> night;

    // Clock
    Signal<std::uint64_t> frame{0};
    double time = 0.0;
    double snackUntil = 0.0;
    double sheetCloseAt = -1.0;
    double hrAcc = 0.0;
    double stepAcc = 0.0;
    std::mt19937 rng{7};

    void  Step(float dt);
    float Rand(float lo, float hi);
    void  SetTab(int t);
    void  Snack(std::string text, std::function<void()> undoFn = {});
    float MoveKcal(float stepsValue) const { return 140.f + stepsValue * 0.045f; }
};

using AppPtr = std::shared_ptr<Vitals>;

const Palette& ThemePalette(bool dark, int accent);
std::string Thousands(double v);
std::string Distance(const Vitals& m, float km);
std::string WeightText(const Vitals& m, float kg);
Color Mix(Color a, Color b, float t);

// Painter-drawn icons.
void DrawIcon(Paint2D& p, Ico icon, Rect box, Color color);
Elem IconView(AppPtr m, Ico icon, float size, std::function<Color(const Palette&)> color);

// Shared components.
Elem Card(AppPtr m, std::string cls, std::function<void()> onTap, Children children);
Elem CardHeader(AppPtr m, Ico icon, std::function<Color(const Palette&)> tint, Str title, Str trailing);
Elem ProgressBar(Reactive<float> fraction, std::string cls);
Elem TopBar(AppPtr m);
Elem BottomNav(AppPtr m);
Elem Fab(AppPtr m);
Elem BottomSheet(AppPtr m);
Elem Snackbar(AppPtr m);
Elem RingChart(AppPtr m, float size);
Elem ScoreRing(AppPtr m, float score, float size);

// Screens.
Elem TodayScreen(AppPtr m);
Elem ActivityScreen(AppPtr m);
Elem SleepScreen(AppPtr m);
Elem ProfileScreen(AppPtr m);

extern const char* const kStylesheet;

} // namespace vitals
