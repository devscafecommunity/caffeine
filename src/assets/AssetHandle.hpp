#pragma once

#include "assets/AssetTypes.hpp"

namespace Caffeine::Assets {

using namespace Caffeine;

class AssetManager;

template<typename T>
class AssetHandle {
public:
    AssetHandle();
    AssetHandle(AssetManager* mgr, u32 id);
    ~AssetHandle();

    AssetHandle(const AssetHandle& o);
    AssetHandle& operator=(const AssetHandle& o);

    AssetHandle(AssetHandle&& o) noexcept;
    AssetHandle& operator=(AssetHandle&& o) noexcept;

    bool     isValid() const;
    bool     isReady() const;
    const T* get()     const;

    explicit operator bool() const;

    u32 id() const { return m_id; }

private:
    void reset();

    AssetManager* m_mgr = nullptr;
    u32           m_id  = ~u32(0);
};

} // namespace Caffeine::Assets
