#include "pch.h"
#include "ModelCatalog.h"
#include "AppPaths.h"
#include <cpl_json.h>
#include <cpl_minixml.h>
#include <filesystem>
#include <memory>
#include <set>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
namespace modelview
{
namespace
{
std::string Resolve(const std::filesystem::path &base, const std::string &name)
{
    if (name.find("://") != std::string::npos)
        throw std::runtime_error("Model sources currently require local files");
    auto expanded = ResolveAppPath(name);
    auto path = std::filesystem::u8path(expanded);
    if (!path.is_absolute())
        path = base / path;
    return path.lexically_normal().u8string();
}
double Number(const CPLXMLNode *node, const char *name, double fallback)
{
    const char *value = CPLGetXMLValue(node, name, nullptr);
    if (!value)
        return fallback;
    char *end = nullptr;
    double result = strtod(value, &end);
    if (!end || *end || !std::isfinite(result))
        throw std::runtime_error(std::string("Invalid XML number: ") + name);
    return result;
}
bool Boolean(const CPLXMLNode *node, const char *name, bool fallback)
{
    const char *value = CPLGetXMLValue(node, name, nullptr);
    return value ? (std::string(value) == "true" || std::string(value) == "1") : fallback;
}
Vector VectorFrom(const CPLJSONArray &a, int start = 0)
{
    return {a[start].ToDouble(), a[start + 1].ToDouble(), a[start + 2].ToDouble()};
}
class TilesetReader
{
  public:
    Catalog &catalog;
    std::set<std::string> stack;
    explicit TilesetReader(Catalog &c) : catalog(c)
    {
    }
    size_t ReadFile(const std::string &filename, const Matrix &parent, bool add, bool clamp, unsigned depth)
    {
        if (depth > 64 || !stack.insert(filename).second)
            throw std::runtime_error("Nested tileset cycle/depth limit");
        std::error_code ec;
        auto length = std::filesystem::file_size(std::filesystem::u8path(filename), ec);
        if (ec || length > 64ull * 1024 * 1024)
            throw std::runtime_error("Missing or oversized tileset.json");
        CPLJSONDocument doc;
        if (!doc.Load(filename))
            throw std::runtime_error("Invalid tileset.json");
        auto root = doc.GetRoot()["root"];
        if (!root.IsValid())
            throw std::runtime_error("tileset.json has no root");
        size_t result =
            Read(root, std::filesystem::u8path(filename).parent_path(), parent, add, clamp, depth + 1);
        stack.erase(filename);
        return result;
    }
    size_t Read(const CPLJSONObject &json, const std::filesystem::path &base, const Matrix &parent, bool add,
                bool clamp, unsigned depth)
    {
        if (depth > 128 || catalog.tiles.size() >= 100000)
            throw std::runtime_error("Tileset hierarchy limit exceeded");
        if (json["implicitTiling"].IsValid() || json["extensions/3DTILES_implicit_tiling"].IsValid())
            throw std::runtime_error(
                "Implicit tilesets are not supported; provide an explicit tileset hierarchy");
        Tile tile;
        tile.transform = parent;
        auto tr = json.GetArray("transform");
        if (tr.Size())
        {
            if (tr.Size() != 16)
                throw std::runtime_error("Tile transform requires 16 values");
            Matrix local;
            for (int i = 0; i < 16; ++i)
            {
                local[i] = tr[i].ToDouble();
                if (!std::isfinite(local[i]))
                    throw std::runtime_error("Nonfinite tile transform");
            }
            tile.transform = Multiply(parent, local);
        }
        tile.add = json.GetString("refine", add ? "ADD" : "REPLACE") == "ADD";
        tile.clamp = clamp;
        tile.error =
            json.GetDouble("geometricError") * std::max({Length(Transform(tile.transform, {1, 0, 0}, true)),
                                                         Length(Transform(tile.transform, {0, 1, 0}, true)),
                                                         Length(Transform(tile.transform, {0, 0, 1}, true))});
        auto bounds = json["boundingVolume"];
        auto region = bounds.GetArray("region"), sphere = bounds.GetArray("sphere"),
             box = bounds.GetArray("box");
        Vector center;
        double radius = 0;
        if (region.Size() == 6)
        {
            double west = region[0].ToDouble(), east = region[2].ToDouble();
            if (east < west)
                east += 6.283185307179586;
            const double lon = (west + east) * 90 / 3.141592653589793,
                         lat = (region[1].ToDouble() + region[3].ToDouble()) * 90 / 3.141592653589793;
            tile.center = Earth(lon, lat, (region[4].ToDouble() + region[5].ToDouble()) / 2);
            for (int x = 0; x < 2; ++x)
                for (int y = 0; y < 2; ++y)
                    for (int h = 0; h < 2; ++h)
                    {
                        auto corner = Earth((x ? east : west) * 180 / 3.141592653589793,
                                            region[y ? 3 : 1].ToDouble() * 180 / 3.141592653589793,
                                            region[h ? 5 : 4].ToDouble());
                        tile.radius = std::max(tile.radius, Length(Subtract(corner, tile.center)));
                    }
        }
        else
        {
            if (sphere.Size() == 4)
            {
                center = Transform(tile.transform, VectorFrom(sphere));
                radius =
                    sphere[3].ToDouble() * std::max({Length(Transform(tile.transform, {1, 0, 0}, true)),
                                                     Length(Transform(tile.transform, {0, 1, 0}, true)),
                                                     Length(Transform(tile.transform, {0, 0, 1}, true))});
            }
            else if (box.Size() == 12)
            {
                center = Transform(tile.transform, VectorFrom(box));
                for (int i = 3; i < 12; i += 3)
                    radius += Length(Transform(tile.transform, VectorFrom(box, i), true));
            }
            else
                throw std::runtime_error("Tile boundingVolume must be region, box or sphere");
            double lon, lat, h;
            if (!Geodetic(center, lon, lat, h))
                throw std::runtime_error("Tileset does not resolve to WGS84 ECEF coordinates");
            tile.center = Earth(lon, lat, h);
            tile.radius = radius;
        }
        auto content = json["content"];
        std::string uri = content.GetString("uri", content.GetString("url"));
        if (!uri.empty())
            tile.path = Resolve(base, uri);
        if (json.GetArray("contents").Size() > 0)
            throw std::runtime_error("Multiple contents per tile are not supported");
        std::ostringstream key;
        key << tile.path << std::setprecision(17);
        for (double v : tile.transform)
            key << '|' << v;
        tile.key = key.str();
        const size_t index = catalog.tiles.size();
        catalog.tiles.push_back(tile);
        if (!tile.path.empty() && std::filesystem::u8path(tile.path).extension() == L".json")
        {
            auto child = ReadFile(tile.path, tile.transform, tile.add, clamp, depth + 1);
            catalog.tiles[index].path.clear();
            catalog.tiles[index].children.push_back(child);
        }
        for (auto child : json.GetArray("children"))
        {
            auto childIndex = Read(child, base, tile.transform, tile.add, clamp, depth + 1);
            catalog.tiles[index].children.push_back(childIndex);
        }
        return index;
    }
};
} // namespace
Catalog LoadCatalog(const std::string &filename)
{
    std::error_code fileError;
    auto fileSize = std::filesystem::file_size(std::filesystem::u8path(filename), fileError);
    if (fileError || fileSize > 128ull * 1024 * 1024)
        throw std::runtime_error("Missing or oversized model-sources.xml");
    using Tree = std::unique_ptr<CPLXMLNode, decltype(&CPLDestroyXMLNode)>;
    Tree xml(CPLParseXMLFile(filename.c_str()), CPLDestroyXMLNode);
    if (!xml)
        throw std::runtime_error("Cannot read model-sources.xml");
    auto root = xml.get();
    while (root && (root->eType != CXT_Element || std::string(root->pszValue) != "ModelSources"))
        root = root->psNext;
    if (!root)
        throw std::runtime_error("Expected <ModelSources> root");
    Catalog result;
    result.enabled = Boolean(root, "enabled", true);
    result.screenError = std::clamp(Number(root, "screenError", 16), 1., 100.);
    result.minimumPixels = std::clamp(Number(root, "minimumPixels", 1), 0., 100.);
    result.gpuBudget = size_t(std::clamp(Number(root, "gpuCacheMB", 512), 64., 4096.)) * 1024 * 1024;
    result.maximumInstances = size_t(std::clamp(Number(root, "maximumInstances", 50000), 1., 1000000.));
    const auto base = std::filesystem::u8path(filename).parent_path();
    std::map<std::string, std::vector<Lod>> assets;
    for (auto node = root->psChild; node; node = node->psNext)
    {
        if (node->eType != CXT_Element)
            continue;
        std::string type = node->pszValue;
        if (type == "Asset")
        {
            std::string id = CPLGetXMLValue(node, "id", "");
            if (id.empty() || assets.count(id))
                throw std::runtime_error("Asset IDs must be nonempty and unique");
            std::vector<Lod> levels;
            std::string path = CPLGetXMLValue(node, "path", "");
            if (!path.empty())
                levels.push_back({Resolve(base, path), 1e30});
            for (auto lod = node->psChild; lod; lod = lod->psNext)
                if (lod->eType == CXT_Element && std::string(lod->pszValue) == "Lod")
                {
                    auto value = CPLGetXMLValue(lod, "path", "");
                    if (!*value)
                        throw std::runtime_error("LOD path is empty");
                    levels.push_back({Resolve(base, value), Number(lod, "maximumDistance", 1e30)});
                }
            if (levels.empty())
                throw std::runtime_error("Asset has no model path");
            std::sort(levels.begin(), levels.end(),
                      [](const Lod &a, const Lod &b) { return a.maximumDistance < b.maximumDistance; });
            assets[id] = std::move(levels);
        }
        else if (type == "Elevation")
        {
            std::string source = CPLGetXMLValue(node, "path", "");
            result.heightSource = source.find("://") != std::string::npos ? source : Resolve(base, source);
            result.heightKind = int(Number(node, "kind", 0));
            result.heightDll = AppPath(result.heightKind ? "TerrainTilesDll" : "TerrainElevationDll");
        }
    }
    TilesetReader tiles(result);
    for (auto node = root->psChild; node; node = node->psNext)
    {
        if (node->eType != CXT_Element || !Boolean(node, "visible", true))
            continue;
        const std::string type = node->pszValue;
        if (type == "Object")
        {
            Instance item;
            item.name = CPLGetXMLValue(node, "name", "");
            const std::string asset = CPLGetXMLValue(node, "asset", "");
            auto found = assets.find(asset);
            if (found == assets.end())
                throw std::runtime_error("Unknown object asset: " + asset);
            item.lod = found->second;
            item.longitude = Number(node, "longitude", 0);
            item.latitude = Number(node, "latitude", 0);
            item.height = Number(node, "height", 0);
            item.heading = Number(node, "heading", 0);
            item.scale = Number(node, "scale", 1);
            item.radius = Number(node, "radius", 100);
            item.maximumDistance = Number(node, "maximumDistance", 100000);
            item.clamp = Boolean(node, "clampToGround", true);
            if (std::abs(item.longitude) > 180 || std::abs(item.latitude) > 90 || item.scale <= 0 ||
                item.radius <= 0 || item.maximumDistance <= 0)
                throw std::runtime_error("Invalid model placement or bounds");
            item.placement = Placement(item.longitude, item.latitude, item.height, item.heading, item.scale);
            item.center = {item.placement[12] * 1000, item.placement[13] * 1000, item.placement[14] * 1000};
            size_t index = result.instances.size();
            if (index >= 1000000)
                throw std::runtime_error("XML object limit exceeded");
            result.instances.push_back(item);
            result
                .cells[{int(std::floor((item.longitude + 180) / 2)),
                        int(std::floor((item.latitude + 90) / 2))}]
                .push_back(index);
        }
        else if (type == "Tileset")
        {
            std::string path = CPLGetXMLValue(node, "path", "");
            if (path.empty())
                throw std::runtime_error("Tileset path is empty");
            result.roots.push_back(tiles.ReadFile(Resolve(base, path), Identity(), false,
                                                  Boolean(node, "clampToGround", false), 0));
        }
    }
    return result;
}
} // namespace modelview
