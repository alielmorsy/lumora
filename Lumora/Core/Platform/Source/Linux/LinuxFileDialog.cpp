#include <lumora/platform/FileDialog.h>

#include "LinuxTools.h"

#include "lumora/os/Environment.h"
#include "lumora/os/Process.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <system_error>

namespace lumora::platform
{
    namespace
    {
        enum class Mode : std::uint8_t
        {
            Open,
            Folder,
            Save
        };

        std::string SpaceSeparated(std::string pattern)
        {
            std::ranges::replace(pattern, ';', ' ');
            std::ranges::replace(pattern, ',', ' ');
            return pattern;
        }

        std::string Trimmed(std::string text)
        {
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
            return text;
        }

        bool PrefersKde()
        {
            const std::string desktop = os::GetEnv("XDG_CURRENT_DESKTOP").value_or("");
            return desktop.find("KDE") != std::string::npos;
        }

        std::string WithDefaultExtension(std::string name, std::string_view extension)
        {
            if (name.empty() || extension.empty() || std::filesystem::path(name).has_extension()) return name;
            return name + "." + std::string(extension);
        }

        bool IsDirectory(const std::filesystem::path& path)
        {
            std::error_code ec;
            return !path.empty() && std::filesystem::is_directory(path, ec);
        }

        std::optional<std::filesystem::path> RunDialog(const std::filesystem::path& tool, const std::vector<std::string>& args)
        {
            const os::ProcessResult result = os::Run(tool, args);
            if (!result.launched || result.exitCode != 0) return std::nullopt;

            const std::string chosen = Trimmed(result.out);
            if (chosen.empty()) return std::nullopt;
            return std::filesystem::path(chosen);
        }

        std::optional<std::filesystem::path> ZenityDialog(const std::filesystem::path& zenity, std::string_view title,
                                                          const std::vector<FileDialogFilter>& filters, Mode mode,
                                                          std::string_view suggestedName,
                                                          std::string_view defaultExtension,
                                                          const std::filesystem::path& startDir)
        {
            std::vector<std::string> args{"--file-selection", "--title=" + std::string(title)};
            if (mode == Mode::Folder) args.push_back("--directory");
            if (mode == Mode::Save)
            {
                args.push_back("--save");
                args.push_back("--confirm-overwrite");
            }

            if (mode != Mode::Folder)
            {
                for (const FileDialogFilter& f : filters)
                    args.push_back("--file-filter=" + f.label + " | " + SpaceSeparated(f.pattern));
            }

            const bool hasStart = IsDirectory(startDir);
            if (mode == Mode::Save)
            {
                const std::string name = WithDefaultExtension(std::string(suggestedName), defaultExtension);
                const std::filesystem::path initial = hasStart ? startDir / name : std::filesystem::path(name);
                if (!initial.empty()) args.push_back("--filename=" + initial.string());
            }
            else if (hasStart)
            {
                args.push_back("--filename=" + (startDir / "").string());
            }
            return RunDialog(zenity, args);
        }

        std::optional<std::filesystem::path> KdialogDialog(const std::filesystem::path& kdialog, std::string_view title,
                                                           const std::vector<FileDialogFilter>& filters, Mode mode,
                                                           std::string_view suggestedName,
                                                           std::string_view defaultExtension,
                                                           const std::filesystem::path& startDir)
        {
            const bool hasStart = IsDirectory(startDir);

            std::string filter;
            for (const FileDialogFilter& f : filters)
            {
                if (!filter.empty()) filter += '\n';
                filter += SpaceSeparated(f.pattern) + "|" + f.label;
            }

            std::vector<std::string> args;
            if (mode == Mode::Folder)
            {
                args = {"--getexistingdirectory", hasStart ? startDir.string() : std::string("."), "--title", std::string(title)};
            }
            else
            {
                const std::string name = WithDefaultExtension(std::string(suggestedName), defaultExtension);
                const std::filesystem::path initial = hasStart ? startDir / name : std::filesystem::path(name.empty() ? "." : name);
                args = {mode == Mode::Save ? "--getsavefilename" : "--getopenfilename", initial.string()};
                if (!filter.empty()) args.push_back(filter);
                args.push_back("--title");
                args.push_back(std::string(title));
            }
            return RunDialog(kdialog, args);
        }

        std::optional<std::filesystem::path> ShowDialog(std::string_view title, const std::vector<FileDialogFilter>& filters,
                                                        Mode mode, std::string_view suggestedName = {},
                                                        std::string_view defaultExtension = {},
                                                        const std::filesystem::path& startDir = {})
        {
            // ponytail: shells out to zenity / kdialog; the xdg-desktop-portal FileChooser over D-Bus is the upgrade when neither is installed.
            const auto zenity  = FindTool({"zenity"});
            const auto kdialog = FindTool({"kdialog"});

            if (kdialog && (PrefersKde() || !zenity))
                return KdialogDialog(*kdialog, title, filters, mode, suggestedName, defaultExtension, startDir);
            if (zenity)
                return ZenityDialog(*zenity, title, filters, mode, suggestedName, defaultExtension, startDir);
            return std::nullopt;
        }
    }

    std::optional<std::filesystem::path>
    OpenFileDialog(std::string_view title, const std::vector<FileDialogFilter>& filters)
    {
        return ShowDialog(title, filters, Mode::Open);
    }

    std::optional<std::filesystem::path>
    SaveFileDialog(std::string_view title, const std::vector<FileDialogFilter>& filters,
                   std::string_view suggestedName, std::string_view defaultExtension,
                   const std::filesystem::path& startDir)
    {
        return ShowDialog(title, filters, Mode::Save, suggestedName, defaultExtension, startDir);
    }

    std::optional<std::filesystem::path> PickFolderDialog(std::string_view title)
    {
        return ShowDialog(title, {}, Mode::Folder);
    }
}
