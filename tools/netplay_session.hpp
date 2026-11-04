#pragma once

#include "frontend_config.hpp"
#include "moderngekko/runtime.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace moderngekko::frontend {
enum class NetplayRole {
  Host,
  Join,
};

enum class NetplayExitCode {
  Failed = 1,
  InvalidConfiguration = 2,
  HostUnavailable = 10,
  VersionMismatch = 11,
  CompatibilityMismatch = 12,
  RoomFull = 13,
  GameRunning = 14,
  ServerFull = 15,
  NicknameRejected = 16,
  // Joining with an invite code: no room answered to the code (mistyped,
  // closed, or the host unreachable), or the invite server did not answer.
  InviteCodeFailed = 17,
};

struct NetplayOptions {
  NetplayRole role = NetplayRole::Join;
  std::string address = "127.0.0.1";
  std::uint16_t port = 2626;
  std::string nickname = "Player";
  std::string buffer = "auto";
  std::vector<std::string> controllers;
  // Players meet through the invite server: the host gets a code from it, and
  // a joining player's address is that code.
  bool invite_code = false;
  std::string invite_server = "stun.dolphin-emu.org";  // "host" or "host:port"
};

int RunNetplayLobby(RuntimeConfig runtime_config, ConfigResult frontend_config,
                    NetplayOptions options);
} // namespace moderngekko::frontend
