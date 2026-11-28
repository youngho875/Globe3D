#include "pch.h"
#include "SceneModelsState.h"
#include "SceneGround.h"
#include <algorithm>
#include <functional>
#include <sstream>
#include <cmath>
#include <limits>
#include <stdexcept>
using namespace modelview;
namespace
{
Vector TransformNormal(const Matrix &matrix, Vector normal)
{
    auto cross = [](Vector a, Vector b) {
        return Vector{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    };
    Vector a{matrix[0], matrix[1], matrix[2]}, b{matrix[4], matrix[5], matrix[6]},
        c{matrix[8], matrix[9], matrix[10]};
    const double determinant = Dot(a, cross(b, c));
    if (std::abs(determinant) < 1e-20)
        throw std::runtime_error("Singular tile transform");
    return Normalize(Scale(
        Add(Add(Scale(cross(b, c), normal.x), Scale(cross(c, a), normal.y)), Scale(cross(a, b), normal.z)),
        1 / determinant));
}
std::unique_ptr<CpuModel> Decode(const ModelLoadJob &job)
{
    MA_Handle handle = nullptr;
    char error[2048]{};
    if (!MA_Load(job.path.c_str(), &handle, error, sizeof(error)))
        throw std::runtime_error(error);
    struct Owner
    {
        MA_Handle handle;
        ~Owner()
        {
            MA_Destroy(handle);
        }
    } owner{handle};
    auto result = std::make_unique<CpuModel>();
    MA_GetInfo(handle, &result->info);
    result->georeferenced = job.georeferenced;
    Vector east, north, up, origin;
    if (job.georeferenced)
    {
        auto &info = result->info;
        Vector center{(info.minimum[0] + info.maximum[0]) / 2, (info.minimum[1] + info.maximum[1]) / 2,
                      (info.minimum[2] + info.maximum[2]) / 2};
        auto ecef = Transform(job.transform, {center.x + info.rtcCenter[0], -center.z + info.rtcCenter[1],
                                              center.y + info.rtcCenter[2]});
        if (!Geodetic(ecef, result->longitude, result->latitude, result->height))
            throw std::runtime_error("Invalid tile ECEF placement");
        origin = Earth(result->longitude, result->latitude, result->height);
        Basis(result->longitude, result->latitude, east, north, up);
        for (int i = 0; i < 3; ++i)
        {
            info.minimum[i] = std::numeric_limits<double>::infinity();
            info.maximum[i] = -info.minimum[i];
        }
    }
    for (unsigned index = 0; index < result->info.meshCount; ++index)
    {
        MA_Mesh mesh{};
        if (!MA_GetMesh(handle, index, &mesh))
            throw std::runtime_error("Mesh API failed");
        CpuMesh copy;
        copy.material = mesh.material;
        copy.vertices.assign(mesh.vertices, mesh.vertices + mesh.vertexCount);
        copy.indices.assign(mesh.indices, mesh.indices + mesh.indexCount);
        if (job.georeferenced)
            for (auto &vertex : copy.vertices)
            {
                const auto &rtc = result->info.rtcCenter;
                Vector pos{vertex.position[0] + rtc[0], -vertex.position[2] + rtc[1],
                           vertex.position[1] + rtc[2]};
                pos = Transform(job.transform, pos);
                double lon, lat, h;
                if (!Geodetic(pos, lon, lat, h))
                    throw std::runtime_error("Invalid ECEF vertex");
                auto delta = Subtract(Earth(lon, lat, h), origin);
                vertex.position[0] = float(Dot(delta, east));
                vertex.position[1] = float(Dot(delta, up));
                vertex.position[2] = float(-Dot(delta, north));
                auto normal =
                    TransformNormal(job.transform, {vertex.normal[0], -vertex.normal[2], vertex.normal[1]});
                vertex.normal[0] = float(Dot(normal, east));
                vertex.normal[1] = float(Dot(normal, up));
                vertex.normal[2] = float(-Dot(normal, north));
                for (int i = 0; i < 3; ++i)
                {
                    result->info.minimum[i] = std::min(result->info.minimum[i], double(vertex.position[i]));
                    result->info.maximum[i] = std::max(result->info.maximum[i], double(vertex.position[i]));
                }
            }
        result->meshes.push_back(std::move(copy));
    }
    for (unsigned i = 0; i < result->info.materialCount; ++i)
    {
        MA_Material material{};
        MA_GetMaterial(handle, i, &material);
        result->materials.push_back(material);
    }
    for (unsigned i = 0; i < result->info.textureCount; ++i)
    {
        MA_Texture image{};
        MA_GetTexture(handle, i, &image);
        CpuTexture copy;
        copy.width = image.width;
        copy.height = image.height;
        copy.rgba.assign(image.rgba, image.rgba + size_t(image.width) * image.height * 4);
        result->textures.push_back(std::move(copy));
    }
    return result;
}
class Visibility
{
  public:
    Vector eye;
    double planes[6][4]{};
    double pixelFactor = 1;
    Visibility(const Matrix &mv, const Matrix &projection, int height)
    {
        eye = {-(mv[0] * mv[12] + mv[1] * mv[13] + mv[2] * mv[14]) * 1000,
               -(mv[4] * mv[12] + mv[5] * mv[13] + mv[6] * mv[14]) * 1000,
               -(mv[8] * mv[12] + mv[9] * mv[13] + mv[10] * mv[14]) * 1000};
        const auto clip = Multiply(projection, mv);
        for (int axis = 0; axis < 3; ++axis)
            for (int sign = 0; sign < 2; ++sign)
            {
                auto &p = planes[axis * 2 + sign];
                for (int col = 0; col < 4; ++col)
                    p[col] = clip[col * 4 + 3] + (sign ? 1 : -1) * clip[col * 4 + axis];
                double n = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
                if (n > 0)
                    for (double &v : p)
                        v /= n;
            }
        pixelFactor = height * std::abs(projection[5]) / 2;
    }
    bool Contains(Vector center, double radius) const
    {
        for (const auto &p : planes)
            if (p[0] * center.x / 1000 + p[1] * center.y / 1000 + p[2] * center.z / 1000 + p[3] <
                -radius / 1000)
                return false;
        if (Dot(center, eye) < 6371000.0 * 6371000.0 - radius * (Length(eye) + 6371000.0))
            return false;
        return true;
    }
    double Distance(Vector center, double radius = 0) const
    {
        return std::max(1., Length(Subtract(center, eye)) - radius);
    }
};
} // namespace
CSceneModels::Impl::Impl()
{
    for (int i = 0; i < 2; ++i)
        workers.emplace_back([this] { Work(); });
}
CSceneModels::Impl::~Impl()
{
    stop = true;
    ready.notify_all();
    for (auto &worker : workers)
        worker.join();
}
void CSceneModels::Impl::Work()
{
    while (!stop)
    {
        ModelLoadJob job;
        {
            std::unique_lock<std::mutex> lock(mutex);
            ready.wait(lock, [&] { return stop || !jobs.empty(); });
            if (stop)
                return;
            job = std::move(jobs.front());
            jobs.pop_front();
        }
        ModelLoadResult result;
        result.key = job.key;
        result.generation = job.generation;
        try
        {
            if (job.configuration)
                result.catalog = std::make_unique<Catalog>(LoadCatalog(job.path));
            else
                result.model = Decode(job);
        }
        catch (const std::exception &e)
        {
            result.error = e.what();
        }
        catch (...)
        {
            result.error = "Unexpected model worker error";
        }
        if (stop || job.generation != generation)
            continue;
        std::unique_lock<std::mutex> lock(mutex);
        ready.wait(
            lock, [&] { return stop || job.generation != generation || queuedBytes < 128ull * 1024 * 1024; });
        if (stop)
            return;
        if (job.generation != generation)
            continue;
        if (result.model)
            queuedBytes += result.model->info.bytes;
        completed.push_back(std::move(result));
    }
}
void CSceneModels::Impl::Request(const ModelLoadJob &requested)
{
    if (configuring || gpu.count(requested.key) || pending.count(requested.key) ||
        failed.count(requested.key))
        return;
    std::lock_guard<std::mutex> lock(mutex);
    if (jobs.size() >= 64)
        return;
    auto job = requested;
    job.generation = generation;
    pending.insert(job.key);
    jobs.push_back(std::move(job));
    ready.notify_one();
}
void CSceneModels::Impl::ClearGL()
{
    for (auto &item : gpu)
        graphics.Destroy(item.second);
    gpu.clear();
}
void CSceneModels::Impl::Collect()
{
    for (int count = 0; count < 2; ++count)
    {
        ModelLoadResult result;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (completed.empty())
                break;
            result = std::move(completed.front());
            completed.pop_front();
            if (result.model)
                queuedBytes -= result.model->info.bytes;
        }
        ready.notify_all();
        if (result.generation != generation)
            continue;
        pending.erase(result.key);
        if (result.key == "@configuration")
            configuring = false;
        if (!result.error.empty())
        {
            error = result.error;
            failed.insert(result.key);
            OutputDebugStringA(("Globe3D models: " + error + "\n").c_str());
            continue;
        }
        if (result.catalog)
        {
            ClearGL();
            catalog = std::move(result.catalog);
            visible = catalog->enabled;
            error.clear();
            if (!catalog->heightSource.empty())
            {
                if (!ownGround)
                    ownGround = std::make_unique<CSceneGround>();
                ownGround->Configure(catalog->heightSource, catalog->heightKind, catalog->heightDll);
            }
            continue;
        }
        if (result.model)
        {
            if (catalog && result.model->info.bytes > catalog->gpuBudget)
            {
                failed.insert(result.key);
                error = "Model exceeds configured GPU cache budget";
                continue;
            }
            uint64_t resident = 0;
            for (const auto &cached : gpu)
                resident += cached.second.bytes;
            while (catalog && resident + result.model->info.bytes > catalog->gpuBudget && !gpu.empty())
            {
                auto oldest = std::min_element(gpu.begin(), gpu.end(), [](const auto &a, const auto &b) {
                    return a.second.used < b.second.used;
                });
                resident -= oldest->second.bytes;
                graphics.Destroy(oldest->second);
                gpu.erase(oldest);
            }
            GpuModel model;
            try
            {
                graphics.Upload(*result.model, model);
                model.used = frame;
                gpu.emplace(result.key, std::move(model));
            }
            catch (const std::exception &e)
            {
                graphics.Destroy(model);
                failed.insert(result.key);
                error = e.what();
            }
        }
    }
}
CSceneModels::CSceneModels() : impl_(new Impl)
{
}
CSceneModels::~CSceneModels() = default;
void CSceneModels::Configure(const std::string &file, bool force)
{
    auto &p = *impl_;
    if (!force && file == p.file)
        return;
    p.file = file;
    p.configuring = true;
    ++p.generation;
    p.pending.clear();
    p.failed.clear();
    p.error.clear();
    {
        std::lock_guard<std::mutex> lock(p.mutex);
        p.jobs.clear();
        p.completed.clear();
        p.queuedBytes = 0;
        ModelLoadJob job;
        job.path = file;
        job.key = "@configuration";
        job.configuration = true;
        job.generation = p.generation;
        p.jobs.push_back(job);
    }
    p.ready.notify_all();
}
void CSceneModels::SetVisible(bool visible)
{
    impl_->visible = visible;
}
bool CSceneModels::Visible() const
{
    return impl_->visible;
}
void CSceneModels::ReleaseGL()
{
    impl_->ClearGL();
    impl_->graphics.Release();
}
std::string CSceneModels::Status() const
{
    auto &p = *impl_;
    std::ostringstream out;
    out << "3D models: " << p.visibleCount << " visible, " << p.gpu.size() << " cached, " << p.pending.size()
        << " loading, " << p.failed.size() << " failed; " << p.drawCalls << " draws"
        << (p.graphics.Instanced() ? " (instanced)" : " (compatible)");
    if(p.visible && p.catalog && !p.visibleCount && p.error.empty())
        out << " - outside view/range (M: go to model)";
    if (!p.error.empty())
        out << " - " << p.error.substr(0, 100);
    return out.str();
}
void CSceneModels::Draw(int, int height, CSceneGround &sharedGround)
{
    auto &p = *impl_;
    ++p.frame;
    p.Collect();
    p.visibleCount = p.drawCalls = 0;
    if (!p.visible || !p.catalog)
        return;
    auto &catalog = *p.catalog;
    CSceneGround &ground = (!catalog.heightSource.empty() && p.ownGround) ? *p.ownGround : sharedGround;
    Matrix mv, projection;
    glGetDoublev(GL_MODELVIEW_MATRIX, mv.data());
    glGetDoublev(GL_PROJECTION_MATRIX, projection.data());
    Visibility view(mv, projection, height);
    std::map<std::string, std::vector<Matrix>> groups;
    for (const auto &cell : catalog.cells)
    {
        auto center = Earth(cell.first.first * 2 - 179, cell.first.second * 2 - 89, 0);
        if (!view.Contains(center, 260000))
            continue;
        for (auto index : cell.second)
        {
            if (p.visibleCount >= catalog.maximumInstances)
                break;
            const auto &item = catalog.instances[index];
            const double radius = item.radius * item.scale;
            auto location = item.center;
            const double distance = view.Distance(location, radius);
            if (distance > item.maximumDistance ||
                radius * 2 * view.pixelFactor / distance < catalog.minimumPixels ||
                !view.Contains(location, radius + 12000))
                continue;
            const Lod *lod = nullptr;
            for (const auto &candidate : item.lod)
                if (distance <= candidate.maximumDistance)
                {
                    lod = &candidate;
                    break;
                }
            if (!lod)
                continue;
            auto loaded = p.gpu.find(lod->path);
            if (loaded == p.gpu.end())
            {
                ModelLoadJob job;
                job.key = job.path = lod->path;
                p.Request(job);
            }
            // Keep a resident coarser/finer LOD until the selected one is ready.
            if (loaded == p.gpu.end())
                for (auto it = item.lod.rbegin(); it != item.lod.rend(); ++it)
                {
                    loaded = p.gpu.find(it->path);
                    if (loaded != p.gpu.end())
                        break;
                }
            if (loaded == p.gpu.end())
                continue;
            auto &model = loaded->second;
            const double actualRadius = model.radius * item.scale;
            if (actualRadius * 2 * view.pixelFactor / distance < catalog.minimumPixels)
                continue;
            auto placement = item.placement;
            if (item.clamp)
            {
                const double offset =
                    ground.Height(item.longitude, item.latitude) / item.scale - model.info.minimum[1];
                for (int axis = 0; axis < 3; ++axis)
                    placement[12 + axis] += placement[4 + axis] * offset;
            }
            groups[loaded->first].push_back(placement);
            model.used = p.frame;
            ++p.visibleCount;
        }
    }
    std::function<bool(size_t, std::vector<size_t> &)> select = [&](size_t index,
                                                                    std::vector<size_t> &selected) {
        const auto &tile = catalog.tiles[index];
        if (!view.Contains(tile.center, tile.radius))
            return true;
        bool ownReady = false;
        if (!tile.path.empty())
        {
            ModelLoadJob job;
            job.key = tile.key;
            job.path = tile.path;
            job.transform = tile.transform;
            job.georeferenced = true;
            p.Request(job);
            ownReady = p.gpu.count(tile.key) != 0;
        }
        const bool refine =
            !tile.children.empty() &&
            (tile.path.empty() ||
             tile.error * view.pixelFactor / view.Distance(tile.center, tile.radius) > catalog.screenError);
        if (refine)
        {
            std::vector<size_t> children;
            bool ready = true;
            for (auto child : tile.children)
                if (!select(child, children))
                    ready = false;
            if (ready)
            {
                if (tile.add && ownReady)
                    selected.push_back(index);
                selected.insert(selected.end(), children.begin(), children.end());
                return true;
            }
            if (ownReady)
            {
                selected.push_back(index);
                return true;
            }
            selected.insert(selected.end(), children.begin(), children.end());
            return false;
        }
        if (ownReady)
        {
            selected.push_back(index);
            return true;
        }
        return tile.path.empty();
    };
    std::vector<size_t> selected;
    for (auto root : catalog.roots)
        select(root, selected);
    for (auto index : selected)
    {
        if (p.visibleCount >= catalog.maximumInstances)
            break;
        const auto &tile = catalog.tiles[index];
        auto found = p.gpu.find(tile.key);
        if (found == p.gpu.end())
            continue;
        auto &model = found->second;
        const double h = tile.clamp ? ground.Height(model.longitude, model.latitude) - model.info.minimum[1]
                                    : model.height;
        groups[tile.key].push_back(Placement(model.longitude, model.latitude, h, 0, 1));
        model.used = p.frame;
        ++p.visibleCount;
    }
    p.graphics.Begin(mv, projection);
    for (auto &group : groups)
    {
        const auto &asset = p.gpu.at(group.first);
        if (std::any_of(asset.materials.begin(), asset.materials.end(),
                        [](const MA_Material &m) { return m.alphaMode == 2; }))
            std::sort(group.second.begin(), group.second.end(), [&](const Matrix &a, const Matrix &b) {
                return view.Distance({a[12] * 1000, a[13] * 1000, a[14] * 1000}) >
                       view.Distance({b[12] * 1000, b[13] * 1000, b[14] * 1000});
            });
        p.drawCalls += p.graphics.Draw(p.gpu.at(group.first), group.second, view.eye);
    }
    p.graphics.End();
    uint64_t bytes = 0;
    for (const auto &model : p.gpu)
        bytes += model.second.bytes;
    while (bytes > catalog.gpuBudget)
    {
        auto oldest = p.gpu.end();
        for (auto it = p.gpu.begin(); it != p.gpu.end(); ++it)
            if (it->second.used != p.frame &&
                (oldest == p.gpu.end() || it->second.used < oldest->second.used))
                oldest = it;
        if (oldest == p.gpu.end())
            break;
        bytes -= oldest->second.bytes;
        p.graphics.Destroy(oldest->second);
        p.gpu.erase(oldest);
    }
}

double CSceneModels::GroundHeight(double lon, double lat, CSceneGround &shared)
{
    auto &p = *impl_;
    return (p.catalog && !p.catalog->heightSource.empty() && p.ownGround) ? p.ownGround->Height(lon, lat)
                                                                          : shared.Height(lon, lat);
}

bool CSceneModels::FirstModelView(double& longitude, double& latitude, double& rangeMetres) const
{
    const auto& p=*impl_;
    if(p.configuring || !p.catalog) return false;
    if(!p.catalog->instances.empty())
    {
        const auto& model=p.catalog->instances.front();
        longitude=model.longitude;
        latitude=model.latitude;
        rangeMetres=std::max(280.,model.radius*model.scale*8.);
        if(!model.clamp)rangeMetres+=std::abs(model.height);
        return true;
    }
    if(!p.catalog->roots.empty())
    {
        size_t index=p.catalog->roots.front();
        while(!p.catalog->tiles[index].children.empty())index=p.catalog->tiles[index].children.front();
        const auto& tile=p.catalog->tiles[index];
        const double radius=modelview::Length(tile.center);
        if(radius<=0)return false;
        longitude=std::atan2(tile.center.y,tile.center.x)*180/3.141592653589793;
        latitude=std::asin(std::max(-1.,std::min(1.,tile.center.z/radius)))*180/3.141592653589793;
        rangeMetres=std::max(280.,tile.radius*5.);
        return true;
    }
    return false;
}
