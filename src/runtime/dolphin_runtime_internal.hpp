#pragma once

#include <memory>
#include <string>

class BootSessionData;

namespace PowerPC {
enum class CPUCore;
}

namespace moderngekko {
enum class AspectRatio;
}

namespace moderngekko::detail {
void SetExternalUICommon(bool external);
// Writes the picture shape into Dolphin's settings. Netplay calls it before
// the lobby so the host shares its 16:9 / 4:3 choice with every player.
void ApplyAspectRatio(AspectRatio aspect_ratio);
// The CPU core this process runs, from MODERNGEKKO_STATICRECOMP (the runner
// sets it from config.ini). Netplay gives the host's core to every player.
PowerPC::CPUCore SelectedCPUCore();
void SetBootSessionData(std::unique_ptr<BootSessionData> boot_session_data);
// The device GameCube port `port` (0 for GCPad1) reads now: its profile's, or
// a connected gamepad in place of one that isn't connected (see
// EffectivePadDevices in game_menu_controls.hpp). profile_device comes back
// unchanged while no runtime looks after the ports.
std::string EffectivePadDevice(int port, const std::string &profile_device);
} // namespace moderngekko::detail
