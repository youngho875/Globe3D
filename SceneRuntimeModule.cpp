#include "SceneRuntimeModule.h"
#include <atomic>
#include <system_error>

CSceneRuntimeModule::CSceneRuntimeModule() : module_(nullptr)
{
}
CSceneRuntimeModule::~CSceneRuntimeModule()
{
    Reset();
}

void CSceneRuntimeModule::Reset()
{
    if (module_)
        FreeLibrary(module_);
    module_ = nullptr;
    if (!snapshot_.empty())
    {
        std::error_code error;
        std::filesystem::remove(snapshot_, error);
        snapshot_.clear();
    }
}

HMODULE CSceneRuntimeModule::Load(const std::string &source)
{
    Reset();
    wchar_t temp[MAX_PATH + 1] = {};
    wchar_t unique[MAX_PATH + 1] = {};
    if (!GetTempPathW(MAX_PATH, temp) || !GetTempFileNameW(temp, L"G3D", 0, unique))
        return nullptr;
    // GetTempFileName atomically reserves the name. Keep it as the private snapshot file.
    snapshot_ = unique;
    const auto original = std::filesystem::u8path(source);
    if (!CopyFileW(original.c_str(), snapshot_.c_str(), FALSE))
    {
        OutputDebugStringW(L"Globe3D: could not copy terrain DLL to private runtime snapshot.\n");
        Reset();
        return nullptr;
    }
    // GDAL is already loaded by the application. Other dependencies use safe default directories.
    module_ = LoadLibraryExW(snapshot_.c_str(), nullptr,
                             LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module_)
    {
        OutputDebugStringW(L"Globe3D: could not load private terrain DLL snapshot.\n");
        Reset();
    }
    return module_;
}
