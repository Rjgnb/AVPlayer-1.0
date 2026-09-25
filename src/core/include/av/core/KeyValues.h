#pragma once

#include <map>
#include <string>
#include <string_view>

namespace av::core {

// 后端/插件专有配置的"逃生口"。
// 为什么要有它？加了它，新增一个后端就只需要它自己的键，
// 而不用去改核心的 PlayerConfig 结构（否则核心会被每个后端的细节污染）。
class KeyValues
{
public:
    void Set(std::string key, std::string value) { map_[std::move(key)] = std::move(value); }

    bool Contains(std::string_view key) const { return map_.find(key) != map_.end(); }

    std::string Get(std::string_view key, std::string defaultValue = {}) const
    {
        const auto it = map_.find(key);
        return it == map_.end() ? std::move(defaultValue) : it->second;
    }

    int GetInt(std::string_view key, int defaultValue) const
    {
        const auto it = map_.find(key);
        if (it == map_.end()) return defaultValue;
        try { return std::stoi(it->second); } catch (...) { return defaultValue; }
    }

    double GetDouble(std::string_view key, double defaultValue) const
    {
        const auto it = map_.find(key);
        if (it == map_.end()) return defaultValue;
        try { return std::stod(it->second); } catch (...) { return defaultValue; }
    }

    bool GetBool(std::string_view key, bool defaultValue) const
    {
        const auto it = map_.find(key);
        if (it == map_.end()) return defaultValue;
        const std::string& v = it->second;
        return v == "1" || v == "true" || v == "TRUE" || v == "yes" || v == "on";
    }

    std::size_t Size() const { return map_.size(); }
    bool        IsEmpty() const { return map_.empty(); }

private:
    std::map<std::string, std::string, std::less<>> map_;
};

} // namespace av::core