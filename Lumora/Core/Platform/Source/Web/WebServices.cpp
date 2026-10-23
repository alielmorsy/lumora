#pragma clang diagnostic ignored "-Wunused-private-field" // ProcessMonitor state is unused on the web

#include <lumora/platform/FileDialog.h>
#include <lumora/platform/GamepadState.h>
#include <lumora/platform/ProgressDialog.h>
#include <lumora/platform/SystemMetrics.h>
#include <lumora/platform/UserPaths.h>

#include <emscripten/heap.h>
#include <emscripten/html5.h>

#include <string>
#include <system_error>

namespace lumora::platform
{
    std::optional<std::filesystem::path>
    OpenFileDialog(std::string_view /*title*/, const std::vector<FileDialogFilter>& /*filters*/)
    {
        return std::nullopt;
    }

    std::optional<std::filesystem::path>
    SaveFileDialog(std::string_view /*title*/, const std::vector<FileDialogFilter>& /*filters*/,
                   std::string_view /*suggestedName*/, std::string_view /*defaultExtension*/,
                   const std::filesystem::path& /*startDir*/)
    {
        return std::nullopt;
    }

    std::optional<std::filesystem::path> PickFolderDialog(std::string_view /*title*/)
    {
        return std::nullopt;
    }

    Ref<ProgressDialog> ProgressDialog::Create(const ProgressDialogDesc& /*desc*/)
    {
        return nullptr;
    }

    DefaultFontFiles GetDefaultUiFont()
    {
        return DefaultFontFiles{.regular = "/fonts/Roboto-Regular.ttf", .bold = {}};
    }

    ProcessMonitor::ProcessMonitor() = default;

    ProcessMetrics ProcessMonitor::Sample()
    {
        return ProcessMetrics{.cpuPercent = 0.f, .ramBytes = emscripten_get_heap_size()};
    }

    std::filesystem::path GetUserConfigDir(std::string_view app)
    {
        std::filesystem::path dir = std::filesystem::path("/home/web_user/.config") / std::string(app);
        std::error_code       ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }

    void PollGamepads(GamepadState* out) noexcept
    {
        const bool sampled = emscripten_sample_gamepad_data() == EMSCRIPTEN_RESULT_SUCCESS;
        for (std::uint8_t i = 0; i < kMaxGamepads; ++i)
        {
            out[i] = {};
            EmscriptenGamepadEvent pad{};
            if (!sampled || emscripten_get_gamepad_status(i, &pad) != EMSCRIPTEN_RESULT_SUCCESS || !pad.connected)
                continue;

            // W3C standard mapping: buttons 0-3 face, 4/5 shoulders, 6/7 triggers, 8/9 back/start, 10/11 sticks, 12-15 d-pad.
            static constexpr int kButtons[] = {0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 14, 15};
            for (int b = 0; b < static_cast<int>(GamepadButton::Count); ++b)
                if (kButtons[b] < pad.numButtons && pad.digitalButton[kButtons[b]])
                    out[i].buttons |= 1u << b;

            const auto axis = [&](int a) { return a < pad.numAxes ? static_cast<float>(pad.axis[a]) : 0.f; };
            const auto analog = [&](int b) { return b < pad.numButtons ? static_cast<float>(pad.analogButton[b]) : 0.f; };
            out[i].leftStickX = axis(0);
            out[i].leftStickY = -axis(1);
            out[i].rightStickX = axis(2);
            out[i].rightStickY = -axis(3);
            out[i].leftTrigger = analog(6);
            out[i].rightTrigger = analog(7);
            out[i].connected = true;
        }
    }

    bool GamepadConnected(std::uint8_t index) noexcept
    {
        EmscriptenGamepadEvent pad{};
        return index < kMaxGamepads && emscripten_sample_gamepad_data() == EMSCRIPTEN_RESULT_SUCCESS &&
               emscripten_get_gamepad_status(index, &pad) == EMSCRIPTEN_RESULT_SUCCESS && pad.connected;
    }
}
