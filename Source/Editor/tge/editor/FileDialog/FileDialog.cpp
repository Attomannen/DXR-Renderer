#include <tge/editor/FileDialog/FileDialog.h>

#include <tge/editor/Editor.h>
#include <tge/settings/settings.h>
#include <tge/util/StringCast.h>

#include <imgui.h>
#include <filesystem>

#include <shobjidl.h>

namespace fs = std::filesystem;

namespace FileDialog 
{
	enum class DialogType { open, save };

	static void GetFileType(const FileType aFileType, COMDLG_FILTERSPEC *filters)
	{
		switch (aFileType)
		{
			case FileType::tgs:
			{
				filters[0] = { L"TGE Scene file", L"*.tgs" };
				break;
			}
			case FileType::tgo:
			{
				filters[0] = { L"TGE Object Definition file", L"*.tgo" };
				break;
			}
			case FileType::tgac:
			{
				filters[0] = { L"TGE Animation Clip file", L"*.tgac" };
				break;
			}
			case FileType::tgmat:
			{
				filters[0] = { L"TGE Material file", L"*.tgmat" };
				break;
			}
			case FileType::tgm:
			{
				filters[0] = { L"TGE FBX Import Settings", L"*.tgm" };
				break;
			}
			default:
			{
				filters[0] = { L"Any file", L"*.*" };
				break;
			}
		}
	}

	static void ShowDialog(DialogType type, const FileType filetype, Callback callback, const char* initialFolder = nullptr)
	{
		IFileDialog* file = nullptr;
		
		HRESULT hr = S_OK;
		switch (type) 
		{
			case DialogType::save:
				hr = CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_ALL, IID_IFileSaveDialog, reinterpret_cast<void**>(&file));
			break;
			case DialogType::open:
				hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&file));
			break;
		}

		if (SUCCEEDED(hr)) 
		{
			// Show the file open dialog
			COMDLG_FILTERSPEC filters[1];
			//std::vector<COMDLG_FILTERSPEC> filters;
			GetFileType(filetype, filters);

			file->SetFileTypes(1, filters);
			
			// Set folder to the project path
			// Todo: should not allow navigating outside this path
			{
				IShellItem* psiFolder;
				const std::string path = (initialFolder && *initialFolder) ? initialFolder : Tga::Settings::GameAssetRoot();
				std::filesystem::path absolutePath = std::filesystem::absolute(path);
				absolutePath.make_preferred();
				std::wstring wpath = string_cast<std::wstring>(absolutePath.string());

				hr = SHCreateItemFromParsingName(wpath.c_str(), NULL, IID_PPV_ARGS(&psiFolder));
				if (SUCCEEDED(hr))
				{
					file->SetFolder(psiFolder);
					psiFolder->Release();
				}
			}

			hr = file->Show(NULL);
			if (SUCCEEDED(hr)) {
				// Get the selected file or folder
				IShellItem* pItem;
				hr = file->GetResult(&pItem);
				if (SUCCEEDED(hr)) {
					PWSTR pszFilePath;
					hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
					if (SUCCEEDED(hr)) {
						// Execute the callback function with the selected path
						std::wstring wpath(pszFilePath);
						std::string path = string_cast<std::string>(wpath);
						callback(path.c_str());
						CoTaskMemFree(pszFilePath);
					}
					pItem->Release();
				}
			}
			file->Release();
		}
	}
}

void FileDialog::SaveFile(FileType aFileType, Callback callback, const char* initialFolder)
{
	ShowDialog(DialogType::save, aFileType, callback, initialFolder);
}

void FileDialog::OpenFile(Callback callback)
{
	ShowDialog(DialogType::open, FileType::na, callback);
}

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
