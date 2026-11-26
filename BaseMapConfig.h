#pragma once

#include <cpl_minixml.h>
#include <cpl_error.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>

struct MapGroupConfig
{
    std::filesystem::path path;
    std::filesystem::path boundaryPath;
    std::filesystem::path labelPath;
    std::filesystem::path cityPath;

    int sourceEpsg = 4326;
    bool visible = false;
};

struct BaseMapConfig
{
    MapGroupConfig smallScale;
    MapGroupConfig world;
    MapGroupConfig naturalEarth;

    bool names = false;
    bool scaleStatus = false;
    bool scaleBar = false;
    bool compass = false;
};

namespace basemap
{
    // 현재 노드의 형제 목록에서 이름이 같은 요소 검색
    inline const CPLXMLNode* FindElement(const CPLXMLNode* node, const char* name) 
    {
        for (const CPLXMLNode* current = node; current != nullptr; current = current->psNext)
        {
            if (current->eType == CXT_Element && current->pszValue && std::string(current->pszValue) == name)
            {
                return current;
            }
        }

        return nullptr;
    }

    // 부모의 직접 자식 요소 검색
    inline const CPLXMLNode* FindChild(const CPLXMLNode* parent, const char* name)
    {
        return parent ? FindElement(parent->psChild, name) : nullptr;
    }

    inline std::string Trim(const std::string& value)
    {
        const auto first = value.find_first_not_of(" \t\r\n");

        if (first == std::string::npos)
            return {};

        const auto last = value.find_last_not_of(" \t\r\n");

        return value.substr(first, last - first + 1);
    }

    // Path, World.Path, Names.visual 등으로 값 읽기
    inline std::string ReadText(const CPLXMLNode* parent, const char* path)
    {
        if (!parent)
            return {};

        const char* value = CPLGetXMLValue(parent, path, "");

        return Trim(value ? value : "");
    }

    inline bool ReadBool(const CPLXMLNode* parent, const char* path)
    {
        const std::string value = ReadText(parent, path);

        if (value.empty() || value == "false" || value == "0")
        {
            return false;
        }

        if (value == "true" || value == "1")
            return true;

        throw std::runtime_error(std::string("Invalid boolean: ") + path);
    }

    inline std::filesystem::path ReadPath(const CPLXMLNode* parent, const char* name, 
        const std::filesystem::path& baseDirectory)
    {
        const std::string value = ReadText(parent, name);

        if (value.empty())
            return {};

        // XML은 UTF-8로 저장되어 있어야 합니다.
        auto path = std::filesystem::u8path(value);

        // 상대 경로는 XML 파일 위치 기준
        if (path.is_relative())
            path = baseDirectory / path;

        return path.lexically_normal();
    }

    inline MapGroupConfig ReadGroup(const CPLXMLNode* root, const char* name, const std::filesystem::path& baseDirectory)
    {
        MapGroupConfig result;
        const CPLXMLNode* group = FindChild(root, name);

        if (!group)
            return result;

        const auto epsg = ReadText(group, "SourceEPSG");
        if (!epsg.empty()) result.sourceEpsg = std::stoi(epsg);
        result.visible = ReadBool(group, "visual");
        result.path = ReadPath(group, "Path", baseDirectory);
        result.boundaryPath = ReadPath(group, "BoundaryPath", baseDirectory);
        result.labelPath = ReadPath(group, "LabelPath", baseDirectory);
        result.cityPath = ReadPath(group, "CityPath", baseDirectory);

        return result;
    }

    inline BaseMapConfig Load(const std::filesystem::path& filename)
    {
        // Windows 한글 파일 경로를 지원하도록
        // filesystem::path로 파일을 열고 XML 문자열을 파싱합니다.
        std::ifstream stream(filename, std::ios::binary);

        if (!stream)
            throw std::runtime_error("Cannot open base map XML");

        std::string xml
        {
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()
        };

        if (stream.bad())
            throw std::runtime_error("Failed to read base map XML");

        // UTF-8 BOM 제거
        if (xml.size() >= 3 &&
            static_cast<unsigned char>(xml[0]) == 0xEF &&
            static_cast<unsigned char>(xml[1]) == 0xBB &&
            static_cast<unsigned char>(xml[2]) == 0xBF)
        {
            xml.erase(0, 3);
        }

        if (xml.empty())
            throw std::runtime_error("Empty base map XML");

        using XmlTreePtr = std::unique_ptr<CPLXMLNode, decltype(&CPLDestroyXMLNode)>;
        CPLErrorReset();

        XmlTreePtr tree(CPLParseXMLString(xml.c_str()), &CPLDestroyXMLNode);

        if (!tree)
        {
            const std::string detail = CPLGetLastErrorMsg();
            throw std::runtime_error("XML parse failed: " + detail);
        }

        // XML 선언과 주석이 앞에 있을 수 있으므로
        // 첫 노드를 곧바로 BaseMap으로 가정하지 않습니다.
        const CPLXMLNode* root = FindElement(tree.get(), "BaseMap");

        if (!root)
            throw std::runtime_error("BaseMap element not found");

        // CPLGetXMLValue는 속성 값도 읽을 수 있습니다.
        if (ReadText(root, "version") != "1")
            throw std::runtime_error("Unsupported BaseMap version");

        const auto baseDirectory = std::filesystem::absolute(filename).parent_path();
        BaseMapConfig config;
        config.smallScale = ReadGroup(root, "SmallScale", baseDirectory);
        config.world = ReadGroup(root, "World", baseDirectory);
        config.naturalEarth = ReadGroup(root, "NaturalEarth", baseDirectory);
        config.names = ReadBool(root, "Names.visual");
        config.scaleStatus = ReadBool(root, "ScaleStatus");
        config.scaleBar = ReadBool(root, "ScaleBar");
        config.compass = ReadBool(root, "Compass");

        // tree는 함수 종료 시 CPLDestroyXMLNode로 자동 해제
        return config;
    }

} // namespace basemap