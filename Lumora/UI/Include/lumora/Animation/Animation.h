#pragma once

/// Reactive animation: a Motion<T> drops into any Reactive<T> prop and patches it every frame.
/// Create animations inside a reactive scope. Global controls (SetTimeScale / SetReducedMotion) are in Motion.h.

#include <lumora/Animation/Easing.h>
#include <lumora/Animation/Interpolate.h>
#include <lumora/Animation/Keyframes.h>
#include <lumora/Animation/Motion.h>
#include <lumora/Animation/Presence.h>
#include <lumora/Animation/Spring.h>
#include <lumora/Animation/Tween.h>
