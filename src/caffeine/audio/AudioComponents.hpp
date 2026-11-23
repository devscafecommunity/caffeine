#pragma once

#include "core/Types.hpp"
#include "math/Vec2.hpp"
#include "containers/FixedString.hpp"

namespace Caffeine::Audio {

using namespace Caffeine;

struct AudioClip {
    const u8* data          = nullptr;
    u64       size          = 0;
    u32       sampleRate    = 44100;
    u16       channels      = 2;
    u16       bitsPerSample = 16;
    f32       duration      = 0.0f;
};

struct AudioEmitter {
    FixedString<128> clipPath;
    f32              volume      = 1.0f;
    f32              maxDistance = 500.0f;
    bool             loop        = false;
    bool             playOnSpawn = true;
    bool             spatial     = true;

    // ---- Appended with the volumetric audio: a scene written before keeps these defaults.
    bool             doppler     = true;   ///< a moving source is Doppler shifted
    u8               layer       = 0;      ///< 0 World (spatial), 1 Ambient (covers the scene), 2 Music, 3 Ui
    u8               _pad[2]     = {};
    f32              refDistance = 2.0f;   ///< full volume up to here (metres)
};

}  // namespace Caffeine::Audio
