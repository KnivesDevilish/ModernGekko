#pragma once

// Windows only: lets the game receive Netplay connections through Windows
// Defender Firewall. Windows blocks a program that waits for connections until
// the player allows it, and its question is easy to miss, to close, or to
// answer for home networks only while the computer's network counts as
// public. The launcher's Host Netplay window offers this instead.

#include <atomic>
#include <filesystem>
#include <string>
#include <thread>

namespace moderngekko::frontend
{
enum class FirewallResult
{
  Idle,      // not asked yet
  Working,   // waiting for Windows (and the player's permission)
  Allowed,   // the rule is in place
  Declined,  // the player didn't give permission
  Failed,    // Windows couldn't make the change (or not on Windows)
};

class FirewallRule
{
public:
  FirewallRule() = default;
  FirewallRule(const FirewallRule&) = delete;
  FirewallRule& operator=(const FirewallRule&) = delete;

  // Asks Windows, on another thread, to let `program` receive connections on
  // every kind of network (private and public), under the rule `rule_name`.
  // The rules Windows made for it before go first: a rule that blocks wins
  // over one that allows. Windows asks the player for administrator rights.
  // Nothing happens while an earlier request is still waiting.
  void Allow(std::filesystem::path program, std::string rule_name);

  FirewallResult Result() const { return m_result.load(); }

private:
  std::atomic<FirewallResult> m_result{FirewallResult::Idle};
  std::jthread m_thread;
};
}  // namespace moderngekko::frontend
