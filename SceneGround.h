#pragma once
#include <memory>
#include <string>

// Nonblocking height cache. DLL requests and waits run only on its worker.
class CSceneGround
{
public:
    CSceneGround();
    ~CSceneGround();
    void Configure(const std::string& source, int kind, const std::string& dll);
    double Height(double longitude, double latitude);
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
