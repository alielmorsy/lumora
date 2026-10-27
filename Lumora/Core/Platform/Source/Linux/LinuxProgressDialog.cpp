#include <lumora/platform/ProgressDialog.h>

#include "LinuxTools.h"

#include "lumora/log/Log.h"

#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

extern char** environ;

LUMORA_LOG_CATEGORY("Platform.Linux")

namespace lumora::platform
{
    namespace
    {
        /// A zenity --progress child fed over its stdin. It draws on whichever display server the session uses, so the dialog
        /// needs no backend of its own and stays alive while the main thread is blocked.
        class ZenityProgressDialog final : public ProgressDialog
        {
        public:
            ~ZenityProgressDialog() override { Close(); }

            bool Start(const std::filesystem::path& zenity, const ProgressDialogDesc& desc)
            {
                int pair[2];
                if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) != 0) return false;

                posix_spawn_file_actions_t actions;
                ::posix_spawn_file_actions_init(&actions);
                ::posix_spawn_file_actions_adddup2(&actions, pair[1], STDIN_FILENO);

                const std::string exe   = zenity.string();
                const std::string title = "--title=" + desc.title;
                const std::string text  = "--text=" + Compose(desc.phase, desc.status);
                const std::string width = "--width=" + std::to_string(desc.width);
                std::vector<char*> argv{const_cast<char*>(exe.c_str()),   const_cast<char*>("--progress"),
                                        const_cast<char*>("--no-cancel"), const_cast<char*>(title.c_str()),
                                        const_cast<char*>(text.c_str()),  const_cast<char*>(width.c_str()),
                                        nullptr};

                const int rc = ::posix_spawn(&_pid, exe.c_str(), &actions, nullptr, argv.data(), environ);
                ::posix_spawn_file_actions_destroy(&actions);
                ::close(pair[1]);
                if (rc != 0)
                {
                    ::close(pair[0]);
                    _pid = 0;
                    return false;
                }

                _fd     = pair[0];
                _phase  = desc.phase;
                _status = desc.status;
                return true;
            }

            void AddTotal(const uint64_t delta) noexcept override
            {
                std::scoped_lock lock(_mutex);
                _total += delta;
                SendProgress();
            }

            void AddCompleted(const uint64_t delta) noexcept override
            {
                std::scoped_lock lock(_mutex);
                _completed += delta;
                SendProgress();
            }

            void SetPhase(const std::string_view text) override
            {
                std::scoped_lock lock(_mutex);
                _phase = std::string(text);
                SendText();
            }

            void SetStatus(const std::string_view text) override
            {
                std::scoped_lock lock(_mutex);
                _status = std::string(text);
                SendText();
            }

            void Close() override
            {
                std::scoped_lock lock(_mutex);
                if (_fd >= 0)
                {
                    ::close(_fd);
                    _fd = -1;
                }
                if (_pid > 0)
                {
                    ::kill(_pid, SIGTERM);
                    ::waitpid(_pid, nullptr, 0);
                    _pid = 0;
                }
            }

        private:
            static std::string Compose(const std::string& phase, const std::string& status)
            {
                return status.empty() ? phase : phase + "\n" + status;
            }

            // A socket, not a pipe, so a dialog the user already killed gives EPIPE through MSG_NOSIGNAL instead of SIGPIPE.
            void Send(const std::string& line) noexcept
            {
                if (_fd < 0) return;
                const std::string out = line + "\n";
                [[maybe_unused]] const ssize_t n = ::send(_fd, out.data(), out.size(), MSG_NOSIGNAL);
            }

            void SendText() noexcept
            {
                std::string text = Compose(_phase, _status);
                std::ranges::replace(text, '\n', ' ');
                Send("#" + text);
            }

            void SendProgress() noexcept
            {
                if (_total == 0) return;
                const uint64_t done = std::min(_completed, _total);
                Send(std::to_string(done * 100 / _total));
            }

            std::mutex  _mutex;
            pid_t       _pid       = 0;
            int         _fd        = -1;
            uint64_t    _total     = 0;
            uint64_t    _completed = 0;
            std::string _phase;
            std::string _status;
        };
    }

    Ref<ProgressDialog> ProgressDialog::Create(const ProgressDialogDesc& desc)
    {
        const auto zenity = FindTool({"zenity"});
        if (!zenity)
        {
            LOG_WARN("zenity not found, no boot progress dialog");
            return nullptr;
        }

        auto dialog = MakeRef<ZenityProgressDialog>();
        if (!dialog->Start(*zenity, desc)) return nullptr;
        return dialog;
    }
}
