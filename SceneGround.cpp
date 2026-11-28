#include "pch.h"
#include "SceneGround.h"
#include "SceneRuntimeModule.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>
#include <deque>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <ogr_srs_api.h>

class CSceneGround::Impl
{
public:
    using Key = std::pair<long long, long long>;
    struct Request { Key key; unsigned generation; };
    std::mutex mutex;
    std::condition_variable ready;
    std::map<Key, double> cache;
    std::deque<Request> queue;
    std::atomic<bool> stop{false};
    std::atomic<unsigned> generation{0};
    std::string source, dllPath;
    int kind = 0;
    std::thread worker;
    Impl() : worker([this] { Run(); }) {}
    ~Impl()
    {
        stop = true;
        ready.notify_all();
        worker.join();
    }
    void Run()
    {
        // Keep this worker independent of machine-wide PostgreSQL/PROJ variables.
        wchar_t executable[32768] = {};
        GetModuleFileNameW(nullptr, executable, 32768);
        const auto base = std::filesystem::path(executable).parent_path();
        for (const auto& candidate : {base / L"proj9/share", base / L"../../gdal_sdk/bin/proj9/share", base / L"share/proj"})
        {
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate / L"proj.db", error))
            {
                const auto path = candidate.lexically_normal().u8string();
                const char* paths[] = {path.c_str(), nullptr};
                OSRSetPROJSearchPaths(paths);
                break;
            }
        }
        CSceneRuntimeModule runtimeModule;
        HMODULE dll = nullptr;
        void* handle = nullptr;
        void (*destroy)(void*) = nullptr;
        uint64_t (*request)(void*, double, double) = nullptr;
        int (*read)(void*, uint64_t, double*) = nullptr;
        int (*wait)(void*, uint64_t, double*, uint32_t) = nullptr;
        unsigned opened = ~0u;
        auto close = [&] {
            if (handle && destroy) destroy(handle);
            handle = nullptr;
            runtimeModule.Reset();
            dll = nullptr;
        };
        while (!stop)
        {
            Request job;
            std::string path, library;
            int type;
            {
                std::unique_lock<std::mutex> lock(mutex);
                ready.wait(lock, [&] { return stop || !queue.empty(); });
                if (stop) break;
                job = queue.front(); queue.pop_front();
                path = source; library = dllPath; type = kind;
            }
            if (job.generation != generation) continue;
            if (opened != job.generation)
            {
                close(); opened = job.generation;
                dll = runtimeModule.Load(library);
                if (dll)
                {
                    const bool tiles = type != 0;
                    destroy = reinterpret_cast<decltype(destroy)>(GetProcAddress(dll, tiles ? "TerrainTilesDestroy" : "TerrainDestroy"));
                    request = reinterpret_cast<decltype(request)>(GetProcAddress(dll, tiles ? "TerrainTilesRequestSample" : "TerrainRequestSample"));
                    read = reinterpret_cast<decltype(read)>(GetProcAddress(dll, tiles ? "TerrainTilesGetSample" : "TerrainGetSample"));
                    wait = reinterpret_cast<decltype(wait)>(GetProcAddress(dll, "TerrainTilesWaitSample"));
                    if (destroy && request && read)
                    {
                        if (tiles)
                        {
                            auto create = reinterpret_cast<void*(*)(const char*, int)>(GetProcAddress(dll, "TerrainTilesCreate"));
                            if (create) handle = create(path.c_str(), type == 2);
                        }
                        else
                        {
                            auto create = reinterpret_cast<void*(*)(const char*)>(GetProcAddress(dll, "TerrainCreate"));
                            if (create) handle = create(path.c_str());
                        }
                    }
                    if (!handle) OutputDebugStringW(L"Globe3D: elevation source unavailable; using reference surface.\n");
                }
            }
            double height = 0;
            if (handle)
            {
                const auto ticket = request(handle, job.key.first / 1e7, job.key.second / 1e7);
                const auto deadline = GetTickCount64() + 5000;
                while (ticket && !stop && generation == job.generation)
                {
                    double value = 0;
                    int result = wait ? wait(handle, ticket, &value, 25) : read(handle, ticket, &value);
                    if (result == 1) { if (std::isfinite(value)) height = value; break; }
                    if (result != 0 || GetTickCount64() >= deadline) break;
                    if (!wait) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            std::lock_guard<std::mutex> lock(mutex);
            if (job.generation == generation) cache[job.key] = height;
        }
        close();
    }
};
CSceneGround::CSceneGround() : impl_(new Impl) {}
CSceneGround::~CSceneGround() = default;
void CSceneGround::Configure(const std::string& source, int kind, const std::string& dll)
{
    auto& p = *impl_;
    std::lock_guard<std::mutex> lock(p.mutex);
    if (source == p.source && kind == p.kind && dll == p.dllPath) return;
    ++p.generation;
    p.source = source; p.kind = kind; p.dllPath = dll;
    p.cache.clear(); p.queue.clear();
}
double CSceneGround::Height(double lon, double lat)
{
    if (!std::isfinite(lon) || !std::isfinite(lat)) return 0;
    auto& p = *impl_;
    const Impl::Key key{llround(std::remainder(lon, 360.0) * 1e7), llround(lat * 1e7)};
    std::lock_guard<std::mutex> lock(p.mutex);
    auto found = p.cache.find(key);
    if (found != p.cache.end()) return found->second;
    if (p.source.empty() || p.queue.size() >= 8192) return 0;
    // Bound memory without dropping requests already being sampled.
    if (p.cache.size() >= 65536 && p.queue.empty()) p.cache.clear();
    p.cache[key] = 0;
    p.queue.push_back({key, p.generation.load()});
    p.ready.notify_one();
    return 0;
}
