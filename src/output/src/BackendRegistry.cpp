#include "av/output/BackendRegistry.h"

#include "av/core/Log.h"

namespace av::output {

BackendRegistry& BackendRegistry::Instance()
{
    static BackendRegistry registry;
    return registry;
}

void BackendRegistry::Register(std::string name, Factory factory, bool makeDefault)
{
    if (name.empty() || !factory)
    {
        AV_LOG(core::LogLevel::Warn, "backend") << "忽略非法注册（名字为空或工厂为空）";
        return;
    }

    const bool isFirst = entries_.empty();
    Entry entry;
    entry.factory   = std::move(factory);
    entry.isDefault = makeDefault || isFirst;
    if (entry.isDefault) defaultName_ = name;

    AV_LOG(core::LogLevel::Debug, "backend") << "注册后端: " << name;
    entries_[std::move(name)] = std::move(entry);
}

bool BackendRegistry::Contains(std::string_view name) const
{
    return entries_.find(name) != entries_.end();
}

std::vector<std::string> BackendRegistry::Names() const
{
    std::vector<std::string> names;
    names.reserve(entries_.size());
    for (const auto& [name, entry] : entries_) names.push_back(name);
    return names;
}

std::string BackendRegistry::DefaultName() const
{
    return defaultName_;
}

std::unique_ptr<IBackend> BackendRegistry::Create(std::string_view name) const
{
    const std::string key = name.empty() ? defaultName_ : std::string(name);
    if (key.empty())
    {
        AV_LOG(core::LogLevel::Error, "backend") << "没有任何已注册的后端";
        return nullptr;
    }

    const auto it = entries_.find(key);
    if (it == entries_.end())
    {
        AV_LOG(core::LogLevel::Error, "backend") << "未知后端: " << key;
        return nullptr;
    }
    return it->second.factory();
}

} // namespace av::output