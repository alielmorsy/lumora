#include <lumora/os/Shell.h>

#include <lumora/log/Log.h>
#include <lumora/os/Environment.h>
#include <lumora/os/Process.h>

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cctype>
#include <string>
#include <vector>

LUMORA_LOG_CATEGORY("OSCore")

namespace lumora::os
{
    namespace
    {
        /// Starts `argv` detached from this process: a double fork so the grandchild is reparented to init and never
        /// becomes our zombie, in its own session so it survives our terminal.
        bool SpawnDetached(const std::vector<std::string>& argv, const std::filesystem::path& workingDir)
        {
            std::vector<char*> raw;
            raw.reserve(argv.size() + 1);
            for (const std::string& arg : argv) raw.push_back(const_cast<char*>(arg.c_str()));
            raw.push_back(nullptr);
            const std::string dir = workingDir.string();

            const pid_t child = ::fork();
            if (child < 0) return false;

            if (child == 0)
            {
                ::setsid();
                const pid_t grandchild = ::fork();
                if (grandchild != 0) ::_exit(grandchild < 0 ? 1 : 0);

                if (!dir.empty() && ::chdir(dir.c_str()) != 0) ::_exit(127);
                ::execvp(raw[0], raw.data());
                ::_exit(127);
            }

            int status = 0;
            ::waitpid(child, &status, 0);
            return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        }

        std::string PercentEncode(const std::string& path)
        {
            static constexpr char hex[] = "0123456789ABCDEF";
            std::string           out;
            for (const char c : path)
            {
                const auto byte = static_cast<unsigned char>(c);
                if (std::isalnum(byte) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~')
                {
                    out += c;
                }
                else
                {
                    out += '%';
                    out += hex[byte >> 4];
                    out += hex[byte & 15];
                }
            }
            return out;
        }

        bool IsExecutableFile(const std::filesystem::path& path)
        {
            struct stat info{};
            return ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) && ::access(path.c_str(), X_OK) == 0;
        }
    }

    bool LaunchDetached(const std::filesystem::path& exe, std::span<const std::filesystem::path> args)
    {
        std::error_code ec;
        if (exe.empty() || !std::filesystem::exists(exe, ec) || ec)
        {
            LOG_WARN("Cannot launch '{}': file does not exist", exe.string());
            return false;
        }

        std::vector<std::string> argv;
        if (IsExecutableFile(exe))
        {
            argv.push_back(exe.string());
        }
        else
        {
            // Not a program: like ShellExecute "open", hand it to the desktop's file association.
            argv = {"xdg-open", exe.string()};
        }
        for (const std::filesystem::path& arg : args) argv.push_back(arg.string());

        if (!SpawnDetached(argv, exe.parent_path()))
        {
            LOG_WARN("Failed to launch '{}'", exe.string());
            return false;
        }
        return true;
    }

    bool RevealInFileManager(const std::filesystem::path& path)
    {
        std::error_code ec;
        if (path.empty() || !std::filesystem::exists(path, ec) || ec)
        {
            LOG_WARN("Cannot reveal '{}': path does not exist", path.string());
            return false;
        }

        const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
        const bool isDirectory = std::filesystem::is_directory(absolute, ec);

        // The FileManager1 D-Bus interface selects the file inside its folder; it is implemented by Nautilus, Dolphin,
        // Nemo, Thunar and PCManFM-Qt, and fails fast when no file manager provides it.
        const std::string uri = "file://" + PercentEncode(absolute.string());
        const std::string items = "array:string:" + uri;
        const std::string searchPath = GetEnv("PATH").value_or("/usr/local/bin:/usr/bin:/bin");
        const auto        dbusSend = WhichInPath("dbus-send", searchPath);
        const ProcessResult dbus = dbusSend ? Run(*dbusSend, {"--session", "--print-reply", "--dest=org.freedesktop.FileManager1",
                                                              "/org/freedesktop/FileManager1",
                                                              isDirectory ? "org.freedesktop.FileManager1.ShowFolders"
                                                                          : "org.freedesktop.FileManager1.ShowItems",
                                                              items, "string:"})
                                           : ProcessResult{};
        if (dbus.launched && dbus.exitCode == 0) return true;

        const std::filesystem::path target = isDirectory ? absolute : absolute.parent_path();
        if (!SpawnDetached({"xdg-open", target.string()}, {}))
        {
            LOG_WARN("Failed to reveal '{}'", path.string());
            return false;
        }
        return true;
    }
}
