#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "av/output/Backend.h"

namespace av::output {

// 后端注册表：把"选哪个后端"从编译期决定变成运行期配置。
//
//   BackendRegistry::Instance().Register("sdl", [] { return std::make_unique<SdlBackend>(); });
//   auto backend = BackendRegistry::Instance().Create(config.backendName);   // 空 = 默认
//
// 新增一个后端只需要：写实现 + Register，不改任何已有代码（开闭原则的落地）。
class BackendRegistry
{
public:
    using Factory = std::function<std::unique_ptr<IBackend>()>;

    static BackendRegistry& Instance();

    // 同名重复注册会覆盖（方便测试替身）
    void Register(std::string name, Factory factory, bool makeDefault = false);

    bool                     Contains(std::string_view name) const;
    std::vector<std::string> Names() const;
    std::string              DefaultName() const;

    // name 为空时用默认后端；找不到返回 nullptr
    std::unique_ptr<IBackend> Create(std::string_view name = {}) const;

private:
    struct Entry
    {
        Factory factory;
        bool    isDefault = false;
    };

    std::map<std::string, Entry, std::less<>> entries_;
    std::string                               defaultName_;
};

} // namespace av::output