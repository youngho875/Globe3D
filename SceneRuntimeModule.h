#pragma once
#include <windows.h>
#include <filesystem>
#include <string>

// A process-private DLL snapshot keeps SDK/build output files replaceable.
class CSceneRuntimeModule
{
  public:
    CSceneRuntimeModule();
    ~CSceneRuntimeModule();
    CSceneRuntimeModule(const CSceneRuntimeModule &) = delete;
    CSceneRuntimeModule &operator=(const CSceneRuntimeModule &) = delete;
    HMODULE Load(const std::string &source);
    void Reset();

  private:
    HMODULE module_;
    std::filesystem::path snapshot_;
};
