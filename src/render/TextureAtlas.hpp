#pragma once

#include "RenderTypes.hpp"
#include "../math/Vec4.hpp"
#include "../containers/FixedString.hpp"
#include <vector>
#include <algorithm>
#include <cstring>

namespace Caffeine::Render {

using namespace Caffeine;

class TextureAtlas {
public:
    explicit TextureAtlas(u32 width, u32 height)
        : m_width(width)
        , m_height(height)
        , m_pixels(static_cast<usize>(width) * height * 4, u8(0))
    {}

    ~TextureAtlas() = default;

    bool add(const char* name, Rect2D sourceRegion) {
        if (!name || name[0] == '\0') return false;
        for (const auto& e : m_entries) {
            if (strcmp(e.name.cStr(), name) == 0) return false;
        }
        Entry entry;
        entry.name = name;
        entry.srcW = static_cast<u32>(sourceRegion.size.x);
        entry.srcH = static_cast<u32>(sourceRegion.size.y);
        m_entries.push_back(entry);
        m_packed = false;
        return true;
    }

    void pack() {
        std::sort(m_entries.begin(), m_entries.end(), [](const Entry& a, const Entry& b) {
            return a.srcH > b.srcH;
        });

        u32 shelfX = 0;
        u32 shelfY = 0;
        u32 shelfH = 0;
        m_usedArea = 0;

        for (auto& e : m_entries) {
            if (e.srcW == 0 || e.srcH == 0) {
                e.packed = false;
                continue;
            }
            if (shelfX + e.srcW > m_width) {
                shelfY += shelfH;
                shelfX  = 0;
                shelfH  = 0;
            }
            if (shelfY + e.srcH > m_height) {
                e.packed = false;
                continue;
            }
            e.px     = shelfX;
            e.py     = shelfY;
            e.packed = true;

            shelfX += e.srcW;
            shelfH  = shelfH > e.srcH ? shelfH : e.srcH;
            m_usedArea += e.srcW * e.srcH;

            const f32 iw = 1.0f / static_cast<f32>(m_width);
            const f32 ih = 1.0f / static_cast<f32>(m_height);
            e.uv = {
                static_cast<f32>(e.px)          * iw,
                static_cast<f32>(e.py)          * ih,
                static_cast<f32>(e.px + e.srcW) * iw,
                static_cast<f32>(e.py + e.srcH) * ih,
            };
        }
        m_packed = true;
    }

    Vec4 getUV(const char* name) const {
        if (!name) return { -1.0f, -1.0f, -1.0f, -1.0f };
        for (const auto& e : m_entries) {
            if (strcmp(e.name.cStr(), name) == 0 && e.packed) return e.uv;
        }
        return { -1.0f, -1.0f, -1.0f, -1.0f };
    }

    struct PixelData {
        const u8* pixels  = nullptr;
        u32       width   = 0;
        u32       height  = 0;
        u32       byteSize = 0;
    };

    PixelData exportImage() const {
        return {
            m_pixels.data(),
            m_width,
            m_height,
            static_cast<u32>(m_pixels.size())
        };
    }

    bool isPacked()   const { return m_packed; }
    u32  width()      const { return m_width; }
    u32  height()     const { return m_height; }
    u32  count()      const { return static_cast<u32>(m_entries.size()); }
    u32  usedArea()   const { return m_usedArea; }
    u32  totalArea()  const { return m_width * m_height; }
    f32  utilization() const {
        return static_cast<f32>(m_usedArea) / static_cast<f32>(m_width * m_height);
    }

private:
    struct Entry {
        FixedString<64> name;
        u32  srcW   = 0;
        u32  srcH   = 0;
        u32  px     = 0;
        u32  py     = 0;
        Vec4 uv     = { 0.0f, 0.0f, 0.0f, 0.0f };
        bool packed = false;
    };

    u32                m_width;
    u32                m_height;
    u32                m_usedArea = 0;
    bool               m_packed   = false;
    std::vector<Entry> m_entries;
    std::vector<u8>    m_pixels;
};

}  // namespace Caffeine::Render
