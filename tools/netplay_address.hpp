#pragma once

// The Netplay lobby's addresses: which network an address of this computer
// belongs to (so the lobby can say where it works), and how an address is
// hidden on screen for players who stream.

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace moderngekko::frontend
{
enum class AddressKind
{
  Home,       // a private address of the local network (192.168.x.x...)
  Tailscale,  // Tailscale's 100.64.0.0/10 range, reachable from its network
  Public,     // reachable from the whole internet
  Ignore,     // virtual adapters (containers, VMs) and loopback
};

// "192.168.1.20" -> {192, 168, 1, 20}.
inline std::optional<std::array<int, 4>> ParseIPv4(std::string_view ip)
{
  std::array<int, 4> octets{};
  for (std::size_t i = 0; i < octets.size(); ++i)
  {
    const auto parsed = std::from_chars(ip.data(), ip.data() + ip.size(), octets[i]);
    if (parsed.ec != std::errc{} || octets[i] < 0 || octets[i] > 255)
      return std::nullopt;
    ip.remove_prefix(static_cast<std::size_t>(parsed.ptr - ip.data()));
    if (i < 3)
    {
      if (ip.empty() || ip.front() != '.')
        return std::nullopt;
      ip.remove_prefix(1);
    }
  }
  if (!ip.empty())
    return std::nullopt;
  return octets;
}

// interface_name is the adapter's name as the system reports it ("wlp2s0",
// "Wi-Fi", "tailscale0", "docker0"...), ip its IPv4 address.
inline AddressKind ClassifyAddress(std::string_view interface_name, std::string_view ip)
{
  std::string name(interface_name);
  std::ranges::transform(name, name.begin(),
                         [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  // Container and virtual machine networks only reach this computer (Windows
  // names Hyper-V's and WSL's "vEthernet (...)"), and Bluetooth's only a
  // phone.
  constexpr std::array<std::string_view, 10> virtual_prefixes = {
      "docker", "br-", "veth", "virbr", "podman", "cni", "waydroid", "lxcbr", "lxdbr", "incusbr"};
  constexpr std::array<std::string_view, 5> virtual_names = {"virtualbox", "vmware", "vmnet",
                                                             "vboxnet", "bluetooth"};
  if (name == "lo" ||
      std::ranges::any_of(virtual_prefixes, [&](std::string_view p) { return name.starts_with(p); }) ||
      std::ranges::any_of(virtual_names, [&](std::string_view n) { return name.contains(n); }))
    return AddressKind::Ignore;
  const auto octets = ParseIPv4(ip);
  if (!octets)
    return AddressKind::Ignore;
  const auto [a, b, c, d] = *octets;
  if (a == 127 || a == 0 || (a == 169 && b == 254))
    return AddressKind::Ignore;
  if (name.contains("tailscale") || (a == 100 && b >= 64 && b <= 127))
    return AddressKind::Tailscale;
  if (a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168))
    return AddressKind::Home;
  return AddressKind::Public;
}

struct HostAndPort
{
  std::string host;
  std::uint16_t port = 0;
};

// "192.168.1.20:2626", as the lobby's Copy button puts it on the clipboard (or
// "192.168.1.20 : 2626", as the lobby shows it) -> {"192.168.1.20", 2626}, so
// it can be pasted whole into Join Netplay's address field. Nothing for text
// without a port, or with more than one colon (an IPv6 address).
inline std::optional<HostAndPort> SplitHostAndPort(std::string_view text)
{
  const auto trim = [](std::string_view part) {
    while (!part.empty() && std::isspace(static_cast<unsigned char>(part.front())))
      part.remove_prefix(1);
    while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back())))
      part.remove_suffix(1);
    return part;
  };
  const std::size_t colon = text.find(':');
  if (colon == std::string_view::npos || text.find(':', colon + 1) != std::string_view::npos)
    return std::nullopt;
  const std::string_view host = trim(text.substr(0, colon));
  const std::string_view port_text = trim(text.substr(colon + 1));
  unsigned port = 0;
  const auto parsed =
      std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
  if (host.empty() || port_text.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != port_text.data() + port_text.size() || port == 0 || port > 65535)
    return std::nullopt;
  return HostAndPort{std::string(host), static_cast<std::uint16_t>(port)};
}

// The invite server's codes are 8 lowercase hexadecimal characters
// ("a1b2c3d4"). Typed or pasted, a code may come in capitals or with spaces
// or dashes ("A1B2 C3D4"); those are dropped. Nothing for anything else.
inline std::optional<std::string> NormalizeInviteCode(std::string_view text)
{
  std::string code;
  for (const char c : text)
  {
    const unsigned char u = static_cast<unsigned char>(c);
    if (std::isspace(u) || c == '-')
      continue;
    if (!std::isxdigit(u))
      return std::nullopt;
    code += static_cast<char>(std::tolower(u));
  }
  if (code.size() != 8)
    return std::nullopt;
  return code;
}

// "192.168.1.20:2626" -> "192.168.1.20 : 2626", easier to read aloud.
inline std::string SpacedAddress(std::string_view host_and_port)
{
  const std::size_t colon = host_and_port.rfind(':');
  if (colon == std::string_view::npos)
    return std::string(host_and_port);
  return std::string(host_and_port.substr(0, colon)) + " : " +
         std::string(host_and_port.substr(colon + 1));
}

// The same with the address hidden and the port kept: "*** : 2626", drawn
// with bullets. Every address looks the same, so its length tells nothing.
inline std::string MaskedAddress(std::string_view host_and_port)
{
  constexpr std::string_view bullets = "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2";
  std::string masked;
  for (int part = 0; part < 4; ++part)
    masked += std::string(part ? "." : "") + std::string(bullets);
  const std::size_t colon = host_and_port.rfind(':');
  if (colon != std::string_view::npos)
    masked += " : " + std::string(host_and_port.substr(colon + 1));
  return masked;
}
}  // namespace moderngekko::frontend
