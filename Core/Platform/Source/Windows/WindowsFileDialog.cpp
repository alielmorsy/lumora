#include <lumora/platform/FileDialog.h>

#include "WindowsHeaders.h"

#include <shlobj.h>
#include <shobjidl.h>

#include <cstdint>
#include <string>
#include <system_error>

namespace lumora::platform
{
    namespace
    {
        std::wstring Widen(std::string_view s)
        {
            if (s.empty()) return {};
            const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            std::wstring w(static_cast<std::size_t>(n), L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
            return w;
        }

        enum class Mode : std::uint8_t
        {
            Open,
            Folder,
            Save
        };

        /// Sets the initial folder, ignoring a missing path (SetFolder would fail and the dialog would open where it last was).
        void ApplyStartDir(IFileDialog& dialog, const std::filesystem::path& startDir)
        {
            if (startDir.empty()) return;
            std::error_code ec;
            if (!std::filesystem::is_directory(startDir, ec)) return;

            IShellItem* folder = nullptr;
            if (SUCCEEDED(::SHCreateItemFromParsingName(startDir.c_str(), nullptr, IID_PPV_ARGS(&folder))))
            {
                dialog.SetFolder(folder);
                folder->Release();
            }
        }

        std::optional<std::filesystem::path> ShowDialog(std::string_view title,
                                                        const std::vector<FileDialogFilter>& filters, Mode mode,
                                                        std::string_view suggestedName = {},
                                                        std::string_view defaultExtension = {},
                                                        const std::filesystem::path& startDir = {})
        {
            const HRESULT init    = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            const bool    didInit = SUCCEEDED(init);

            // Open and Save share IFileDialog behind two class ids, so filter/title/folder setup is shared.
            // Both arrays outlive Show(): SetFileTypes stores them by pointer.
            std::vector<std::wstring>            storage;
            std::vector<COMDLG_FILTERSPEC>       specs;

            std::optional<std::filesystem::path> result;
            IFileDialog*                         dialog = nullptr;
            const CLSID                          clsid  = mode == Mode::Save ? CLSID_FileSaveDialog
                                                                             : CLSID_FileOpenDialog;
            if (SUCCEEDED(::CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
            {
                if (mode == Mode::Folder)
                {
                    DWORD opts = 0;
                    dialog->GetOptions(&opts);
                    dialog->SetOptions(opts | FOS_PICKFOLDERS);
                }
                else if (!filters.empty())
                {
                    storage.reserve(filters.size() * 2);
                    for (const FileDialogFilter& f : filters)
                    {
                        storage.push_back(Widen(f.label));
                        storage.push_back(Widen(f.pattern));
                    }
                    specs.reserve(filters.size());
                    for (std::size_t i = 0; i < filters.size(); ++i)
                        specs.push_back({storage[i * 2].c_str(), storage[i * 2 + 1].c_str()});
                    dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
                }

                const std::wstring wtitle = Widen(title);
                dialog->SetTitle(wtitle.c_str());

                if (mode == Mode::Save)
                {
                    const std::wstring wname = Widen(suggestedName);
                    const std::wstring wext  = Widen(defaultExtension);
                    if (!wname.empty()) dialog->SetFileName(wname.c_str());
                    if (!wext.empty()) dialog->SetDefaultExtension(wext.c_str());
                }
                ApplyStartDir(*dialog, startDir);

                if (SUCCEEDED(dialog->Show(nullptr)))
                {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(dialog->GetResult(&item)))
                    {
                        PWSTR path = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
                        {
                            result = std::filesystem::path(path);
                            ::CoTaskMemFree(path);
                        }
                        item->Release();
                    }
                }
                dialog->Release();
            }

            if (didInit) ::CoUninitialize();
            return result;
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
