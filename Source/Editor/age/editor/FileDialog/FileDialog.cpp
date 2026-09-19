#include <age/editor/FileDialog/FileDialog.h>

#include <age/settings/settings.h>
#include <age/util/StringCast.h>

#include <filesystem>

#include <shobjidl.h>

namespace fs = std::filesystem;

void FileDialog::OpenProjectFolder(Callback callback)
{
	IFileDialog* pFolderDialog;
	HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileDialog, reinterpret_cast<void**>(&pFolderDialog));

	if (SUCCEEDED(hr)) {
		DWORD dwOptions;
		hr = pFolderDialog->GetOptions(&dwOptions);
		if (SUCCEEDED(hr)) {
			// Add the FOS_PICKFOLDERS option to enable folder selection
			hr = pFolderDialog->SetOptions(dwOptions | FOS_PICKFOLDERS);
			if (SUCCEEDED(hr)) {
				// Show the folder dialog
				hr = pFolderDialog->Show(NULL);
				if (SUCCEEDED(hr)) {
					// Get the selected folder
					IShellItem* pItem;
					hr = pFolderDialog->GetResult(&pItem);
					if (SUCCEEDED(hr)) {
						PWSTR pszFolderPath;
						hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFolderPath);
						if (SUCCEEDED(hr)) {
							// Execute the callback function with the selected folder path
							std::wstring path(pszFolderPath);
							callback(string_cast<std::string>(path + L"\\").c_str());
							CoTaskMemFree(pszFolderPath);
						}
						pItem->Release();
					}
				}
			}
		}
		pFolderDialog->Release();
	}
}
