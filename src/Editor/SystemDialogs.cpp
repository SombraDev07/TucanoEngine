#include "Editor/SystemDialogs.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <shobjidl.h>
#include <wrl/client.h>

#include <string>
#include <vector>

namespace tucano::editor {
namespace {

using Microsoft::WRL::ComPtr;

// COM has to be initialised on the calling thread for IFileDialog. The editor may or may not have
// done it already, so this initialises when needed and only uninitialises what it initialised —
// tearing down someone else's apartment breaks them at an unrelated time.
class ScopedCom {
public:
	ScopedCom() {
		const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		// RPC_E_CHANGED_MODE: already initialised in another mode. The dialog still works; we just
		// must not uninitialise.
		m_owned = SUCCEEDED(hr);
	}
	~ScopedCom() {
		if (m_owned) CoUninitialize();
	}
	ScopedCom(const ScopedCom&) = delete;
	ScopedCom& operator=(const ScopedCom&) = delete;

private:
	bool m_owned = false;
};

std::wstring widen(const std::string& s) {
	if (s.empty()) return {};
	const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
	std::wstring out(static_cast<size_t>(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
	return out;
}

std::string narrow(const wchar_t* s) {
	if (s == nullptr) return {};
	const int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
	if (n <= 1) return {};
	std::string out(static_cast<size_t>(n - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), n, nullptr, nullptr);
	return out;
}

// The COM filter array borrows the strings, so the widened copies have to outlive the dialog call.
struct FilterStorage {
	std::vector<std::wstring> strings;
	std::vector<COMDLG_FILTERSPEC> specs;
};

FilterStorage buildFilters(const std::vector<FileFilter>& filters) {
	FilterStorage storage;
	storage.strings.reserve(filters.size() * 2 + 2);
	storage.specs.reserve(filters.size() + 1);

	for (const FileFilter& f : filters) {
		storage.strings.push_back(widen(f.label != nullptr ? f.label : ""));
		storage.strings.push_back(widen(f.pattern != nullptr ? f.pattern : "*.*"));
	}
	// "All files" last: people reach for it when their file does not match, and it should not be the
	// default that hides the meaningful filters.
	storage.strings.push_back(L"All files");
	storage.strings.push_back(L"*.*");

	for (size_t i = 0; i < storage.strings.size(); i += 2) {
		storage.specs.push_back({storage.strings[i].c_str(), storage.strings[i + 1].c_str()});
	}
	return storage;
}

void applyStartDirectory(IFileDialog* dialog, const std::string& startDirectory) {
	if (startDirectory.empty()) return;
	ComPtr<IShellItem> item;
	if (SUCCEEDED(SHCreateItemFromParsingName(widen(startDirectory).c_str(), nullptr,
	                                          IID_PPV_ARGS(&item)))) {
		// "Default" rather than "Folder": a folder the user has already navigated away from should
		// not yank them back every time they open the dialog.
		dialog->SetDefaultFolder(item.Get());
	}
}

std::string resultPathOf(IShellItem* item) {
	if (item == nullptr) return {};
	PWSTR raw = nullptr;
	if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw))) return {};
	std::string path = narrow(raw);
	CoTaskMemFree(raw);
	return path;
}

// Shared open path; `allowMultiple` picks between one and many results.
std::vector<std::string> runOpenDialog(const char* title, const std::vector<FileFilter>& filters,
                                       const std::string& startDirectory, bool allowMultiple,
                                       bool foldersOnly) {
	ScopedCom com;
	ComPtr<IFileOpenDialog> dialog;
	if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
	                            IID_PPV_ARGS(&dialog)))) {
		return {};
	}

	DWORD options = 0;
	dialog->GetOptions(&options);
	options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
	if (allowMultiple) options |= FOS_ALLOWMULTISELECT;
	if (foldersOnly) options |= FOS_PICKFOLDERS;
	dialog->SetOptions(options);

	if (title != nullptr) dialog->SetTitle(widen(title).c_str());
	FilterStorage storage;
	if (!foldersOnly) {
		storage = buildFilters(filters);
		dialog->SetFileTypes(static_cast<UINT>(storage.specs.size()), storage.specs.data());
	}
	applyStartDirectory(dialog.Get(), startDirectory);

	// Parent to the foreground window so the dialog cannot end up behind the editor.
	if (FAILED(dialog->Show(GetForegroundWindow()))) {
		return {}; // includes the user cancelling
	}

	std::vector<std::string> paths;
	if (allowMultiple) {
		ComPtr<IShellItemArray> items;
		if (SUCCEEDED(dialog->GetResults(&items))) {
			DWORD count = 0;
			items->GetCount(&count);
			for (DWORD i = 0; i < count; ++i) {
				ComPtr<IShellItem> item;
				if (SUCCEEDED(items->GetItemAt(i, &item))) {
					std::string path = resultPathOf(item.Get());
					if (!path.empty()) paths.push_back(std::move(path));
				}
			}
		}
	} else {
		ComPtr<IShellItem> item;
		if (SUCCEEDED(dialog->GetResult(&item))) {
			std::string path = resultPathOf(item.Get());
			if (!path.empty()) paths.push_back(std::move(path));
		}
	}
	return paths;
}

} // namespace

std::string openFileDialog(const char* title, const std::vector<FileFilter>& filters,
                           const std::string& startDirectory) {
	const std::vector<std::string> paths = runOpenDialog(title, filters, startDirectory, false, false);
	return paths.empty() ? std::string{} : paths.front();
}

std::vector<std::string> openFilesDialog(const char* title, const std::vector<FileFilter>& filters,
                                         const std::string& startDirectory) {
	return runOpenDialog(title, filters, startDirectory, true, false);
}

std::string pickFolderDialog(const char* title, const std::string& startDirectory) {
	const std::vector<std::string> paths = runOpenDialog(title, {}, startDirectory, false, true);
	return paths.empty() ? std::string{} : paths.front();
}

std::string saveFileDialog(const char* title, const std::vector<FileFilter>& filters,
                           const std::string& defaultName, const std::string& startDirectory) {
	ScopedCom com;
	ComPtr<IFileSaveDialog> dialog;
	if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
	                            IID_PPV_ARGS(&dialog)))) {
		return {};
	}

	DWORD options = 0;
	dialog->GetOptions(&options);
	dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT);

	if (title != nullptr) dialog->SetTitle(widen(title).c_str());
	const FilterStorage storage = buildFilters(filters);
	dialog->SetFileTypes(static_cast<UINT>(storage.specs.size()), storage.specs.data());
	if (!defaultName.empty()) dialog->SetFileName(widen(defaultName).c_str());

	// Extension appended when the user types a bare name — otherwise they get a file the engine will
	// not recognise, and will not understand why.
	if (!filters.empty() && filters.front().pattern != nullptr) {
		const std::string pattern = filters.front().pattern;
		const size_t dot = pattern.find('.');
		if (dot != std::string::npos) {
			const size_t end = pattern.find(';', dot);
			dialog->SetDefaultExtension(
			    widen(pattern.substr(dot + 1, end == std::string::npos ? std::string::npos : end - dot - 1))
			        .c_str());
		}
	}
	applyStartDirectory(dialog.Get(), startDirectory);

	if (FAILED(dialog->Show(GetForegroundWindow()))) {
		return {};
	}
	ComPtr<IShellItem> item;
	if (FAILED(dialog->GetResult(&item))) return {};
	return resultPathOf(item.Get());
}

void showErrorBox(const char* title, const char* message) {
	MessageBoxW(GetForegroundWindow(), widen(message != nullptr ? message : "").c_str(),
	            widen(title != nullptr ? title : "Error").c_str(), MB_OK | MB_ICONERROR);
}

} // namespace tucano::editor
