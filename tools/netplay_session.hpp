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
  // Joining with an invite code: no room has that code (mistyped, or the
  // room was closed).
  InviteCodeFailed = 17,
  // The room exists, but its host's computer did not answer the invite
  // server.
  InviteHostSilent = 18,
  // The room exists and its host answered, but the two computers could not
  // reach each other (their connections don't allow it).
  InviteConnectionBlocked = 19,
  // The invite server did not answer this computer.
  InviteServerUnreachable = 20,
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
