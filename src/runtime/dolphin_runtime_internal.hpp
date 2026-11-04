#pragma once

#include <memory>

class BootSessionData;

namespace moderngekko {
enum class AspectRatio;
}

namespace moderngekko::detail {
void SetExternalUICommon(bool external);
// Writes the picture shape into Dolphin's settings. Netplay calls it before
// the lobby so the host shares its 16:9 / 4:3 choice with every player.
void ApplyAspectRatio(AspectRatio aspect_ratio);
void SetBootSessionData(std::unique_ptr<BootSessionData> boot_session_data);
} // namespace moderngekko::detail
