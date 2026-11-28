#pragma once
#include "SceneModels.h"
#include "ModelCatalog.h"
#include "ModelGl.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <set>
#include <atomic>
struct ModelLoadJob
{
    std::string key, path;
    modelview::Matrix transform = modelview::Identity();
    unsigned generation = 0;
    bool configuration = false, georeferenced = false;
};
struct ModelLoadResult
{
    std::string key, error;
    unsigned generation = 0;
    std::unique_ptr<modelview::Catalog> catalog;
    std::unique_ptr<modelview::CpuModel> model;
};
class CSceneModels::Impl
{
  public:
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<ModelLoadJob> jobs;
    std::deque<ModelLoadResult> completed;
    std::atomic<bool> stop{false};
    std::atomic<unsigned> generation{0};
    std::vector<std::thread> workers;
    std::shared_ptr<modelview::Catalog> catalog;
    std::map<std::string, modelview::GpuModel> gpu;
    std::set<std::string> pending, failed;
    modelview::ModelGl graphics;
    std::unique_ptr<CSceneGround> ownGround;
    std::string file, error;
    bool visible = true;
    bool configuring = false;
    uint64_t frame = 0, queuedBytes = 0;
    size_t visibleCount = 0, drawCalls = 0;
    Impl();
    ~Impl();
    void Work();
    void Request(const ModelLoadJob &job);
    void ClearGL();
    void Collect();
};
