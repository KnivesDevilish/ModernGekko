#pragma once

// The Netplay lobby window, with the launcher's look: the lobby artwork on the
// left and, on the right, the addresses to invite friends with, the four
// controller seats, this computer's controllers and the Ready/Start buttons.
//
// It is drawn from plain data (LobbyView) and reports what was clicked
// (LobbyActions), so it needs nothing from Dolphin: netplay_session.cpp fills
// the view from the netplay client and server every frame and carries out the
// actions.

#include "launcher_text.hpp"
#include "launcher_ui.hpp"
#include "netplay_address.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace moderngekko::frontend::ui
{
// One address of this computer, and the network it works on.
struct LobbyAddress
{
  AddressKind kind = AddressKind::Home;
  std::string address;  // "ip:port"
};

// A controller seat (P1 to P4), or a connected player who holds none.
struct LobbySeat
{
  bool taken = false;
  std::string name;
  unsigned ping = 0;
  bool ready = false;
  bool same_game = true;
  bool you = false;   // a player on this computer
  bool host = false;  // the player who opened the room
};

// A gamepad connected to this computer.
struct LobbyController
{
  std::string label;
  bool selected = false;
  bool enabled = true;  // false when every free seat is already taken
};

struct LobbyView
{
  bool hosting = false;
  std::string cpu_core;  // the host's core; a client plays whichever the host picked
  unsigned buffer = 0;
  bool automatic_buffer = false;
  // Friends join with an invite code instead of an address.
  bool invite_code = false;
  std::string code;                     // the room's code; empty while the host waits for it
  bool invite_server_failed = false;    // host: the invite server stopped answering
  std::vector<LobbyAddress> addresses;  // host with an address: this computer's addresses
  std::string host_address;             // client with an address: the one it connected to
  std::array<LobbySeat, 4> seats;       // P1 to P4
  std::vector<LobbySeat> without_seat;  // connected players who hold no seat
  bool gamecube_ports = true;           // seats play on GameCube ports, not Wii Remotes
  std::vector<LobbyController> controllers;
  bool local_ready = false;
  bool can_start = false;
  std::string status;  // the last netplay event, such as "Player2 joined"
  std::string error;
};

// What the lobby remembers between frames.
struct LobbyState
{
  bool show_addresses = false;  // addresses and codes hidden until asked, for streams
  std::string copied;           // the address or code copied last...
  double copied_at = -1.0e9;    // ...and when, in ImGui::GetTime() seconds
};

struct LobbyActions
{
  bool toggle_ready = false;
  bool start = false;
  int toggled_controller = -1;  // index into LobbyView::controllers
};

inline constexpr ImVec4 LOBBY_AMBER(1.0f, 0.75f, 0.3f, 1.0f);
inline constexpr ImVec4 LOBBY_RED(1.0f, 0.42f, 0.38f, 1.0f);
inline constexpr ImVec4 LOBBY_FREE_SEAT(0.5f, 0.5f, 0.52f, 1.0f);
inline constexpr ImVec4 LOBBY_DARK_TEXT(0.04f, 0.05f, 0.02f, 1.0f);

// Where an address works, as the invite section labels it.
inline const char* LobbyAddressLabel(AddressKind kind)
{
  switch (kind)
  {
  case AddressKind::Tailscale:
    return Tr("Internet (Tailscale)");
  case AddressKind::Public:
    return Tr("Internet (public)");
  default:
    return Tr("At home");
  }
}

// The Show/Hide switch on the right of a section's heading line.
inline void DrawShowAddressesSwitch(LobbyState& state)
{
  const char* label = state.show_addresses ? Tr("Hide") : Tr("Show");
  ImGui::SameLine();
  const ImVec2 padding(ImGui::GetStyle().FramePadding.x * 0.6f, 0.0f);
  const float width = ImGui::CalcTextSize(label).x + padding.x * 2.0f;
  const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right - width));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, padding);
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.06f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.10f));
  ImGui::PushStyleColor(ImGuiCol_Text, LAUNCHER_ACCENT);
  if (ImGui::Button((std::string(label) + "##show_addresses").c_str()))
    state.show_addresses = !state.show_addresses;
  ImGui::PopStyleColor(4);
  ImGui::PopStyleVar();
}

// The address as the lobby shows it: hidden unless Show was clicked.
inline std::string LobbyShownAddress(const LobbyState& state, const std::string& address)
{
  return state.show_addresses ? SpacedAddress(address) : MaskedAddress(address);
}

// Copy at the right end of the current line (right is the line's end, from
// GetCursorPosX() + GetContentRegionAvail().x at its start). It puts value on
// the clipboard even while it is hidden, and says Copied for a moment.
inline void DrawCopyButton(const std::string& value, float right, LobbyState& state)
{
  const bool copied = state.copied == value && ImGui::GetTime() - state.copied_at < 1.5;
  const float width =
      std::max(ImGui::CalcTextSize(Tr("Copy")).x, ImGui::CalcTextSize(Tr("Copied")).x) +
      ImGui::GetStyle().FramePadding.x * 2.0f;
  ImGui::SameLine(std::max(ImGui::GetCursorPosX(), right - width));
  ImGui::PushID(value.c_str());
  if (copied)
    ImGui::PushStyleColor(ImGuiCol_Text, LAUNCHER_ACCENT);
  if (ImGui::Button(copied ? Tr("Copied") : Tr("Copy"), ImVec2(width, 0.0f)))
  {
    ImGui::SetClipboardText(value.c_str());
    state.copied = value;
    state.copied_at = ImGui::GetTime();
  }
  if (copied)
    ImGui::PopStyleColor();
  ImGui::PopID();
}

// One address on one line: where it works, the address, and Copy ("ip:port").
inline void DrawAddressRow(const char* label, const std::string& address, float label_width,
                           LobbyState& state)
{
  const float start = ImGui::GetCursorPosX();
  const float right = start + ImGui::GetContentRegionAvail().x;
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("%s", label);
  ImGui::SameLine(start + label_width);
  ImGui::TextUnformatted(LobbyShownAddress(state, address).c_str());
  DrawCopyButton(address, right, state);
}

// "a1b2c3d4" -> "a1b2 c3d4", easier to read out; hidden, eight bullets.
inline std::string LobbyShownCode(const LobbyState& state, const std::string& code)
{
  if (!state.show_addresses)
  {
    constexpr std::string_view four = "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2";
    return std::string(four) + " " + std::string(four);
  }
  return code.size() == 8 ? code.substr(0, 4) + " " + code.substr(4) : code;
}

// The invite code on one line: the label, the code in large type, and Copy
// when copy is set (the host's own room).
inline void DrawInviteCodeRow(const std::string& code, bool copy, LobbyState& state, ImFont* bold,
                              float scale)
{
  const float start = ImGui::GetCursorPosX();
  const float right = start + ImGui::GetContentRegionAvail().x;
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("%s", Tr("Invite code"));
  ImGui::SameLine(start + ImGui::CalcTextSize(Tr("Invite code")).x + 24.0f * scale);
  ImGui::PushFont(bold, ImGui::GetStyle().FontSizeBase * 1.2f);
  ImGui::TextUnformatted(LobbyShownCode(state, code).c_str());
  ImGui::PopFont();
  if (copy)
    DrawCopyButton(code, right, state);
}

// One row of the players table: the seat's port, the player, ping and state.
inline void DrawSeatRow(const char* port, bool seated, const LobbySeat& seat, ImFont* bold,
                        float scale)
{
  ImGui::TableNextRow(ImGuiTableRowFlags_None, ImGui::GetFrameHeight());
  ImGui::TableNextColumn();
  DrawStatusPill(port, seated && seat.taken ? LAUNCHER_ACCENT : LOBBY_FREE_SEAT, scale, false);
  // Text level with the pills' labels.
  const float text_offset = 3.0f * scale;
  ImGui::TableNextColumn();
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + text_offset);
  if (!seat.taken)
  {
    ImGui::TextDisabled("%s", Tr("Free"));
    return;
  }
  ImGui::PushFont(bold, 0.0f);
  ImGui::TextUnformatted(seat.name.c_str());
  ImGui::PopFont();
  const char* tag = seat.you ? Tr("(you)") : seat.host ? Tr("(host)") : nullptr;
  if (tag)
  {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", tag);
  }
  ImGui::TableNextColumn();
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + text_offset);
  const ImVec4 ping_color =
      seat.ping < 60 ? LAUNCHER_ACCENT : seat.ping < 120 ? LOBBY_AMBER : LOBBY_RED;
  ImGui::TextColored(ping_color, "%u ms", seat.ping);
  ImGui::TableNextColumn();
  if (!seat.same_game)
    DrawStatusPill(Tr("OTHER GAME"), LOBBY_RED, scale, false);
  else
    DrawStatusPill(seat.ready ? Tr("READY") : Tr("NOT READY"),
                   seat.ready ? LAUNCHER_ACCENT : LOBBY_AMBER, scale, false);
}

enum class LobbyButtonLook
{
  Primary,  // the next thing to do: lime, dark label
  Ready,    // Ready, which clicking again cancels: dark green, lime label
  Plain,    // Start while it can't be used yet
};

inline bool DrawLobbyButton(const char* label, ImVec2 size, LobbyButtonLook look, ImFont* bold)
{
  int colors = 4;
  switch (look)
  {
  case LobbyButtonLook::Primary:
    ImGui::PushStyleColor(ImGuiCol_Button, LAUNCHER_ACCENT);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 1.0f, 0.40f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.62f, 0.90f, 0.10f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, LOBBY_DARK_TEXT);
    break;
  case LobbyButtonLook::Ready:
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.19f, 0.06f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.25f, 0.08f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.30f, 0.09f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, LAUNCHER_ACCENT);
    break;
  case LobbyButtonLook::Plain:
    colors = 0;
    break;
  }
  ImGui::PushFont(bold, ImGui::GetStyle().FontSizeBase * 1.1f);
  const bool clicked = ImGui::Button(label, size);
  ImGui::PopFont();
  ImGui::PopStyleColor(colors);
  return clicked;
}

// The whole lobby window, filling the SDL window. over_background: a
// background picture is drawn under it (lobby-background.png), so the
// window lets it show; the panels keep their colour.
inline LobbyActions DrawLobby(const LobbyView& view, LobbyState& state, const LauncherImage& art,
                              ImFont* bold, float scale, bool over_background = false)
{
  LobbyActions actions;
  const ImGuiStyle& style = ImGui::GetStyle();
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  if (over_background)
    ImGui::SetNextWindowBgAlpha(0.0f);
  ImGui::Begin("Netplay", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings);

  // Left: the artwork, as tall as the window. A narrow window shrinks it
  // (never crops it) so the lobby keeps a usable width, and hides it when it
  // would be too small to make out.
  const float height = ImGui::GetContentRegionAvail().y;
  const float gap = 18.0f * scale;
  if (art.texture && art.aspect > 0.0f)
  {
    const float room = ImGui::GetContentRegionAvail().x - gap - 480.0f * scale;
    const float width = std::min(height / art.aspect, room);
    if (width >= 160.0f * scale)
    {
      const ImVec2 size(width, width * art.aspect);
      const ImVec2 position = ImGui::GetCursorScreenPos();
      ImGui::GetWindowDrawList()->AddImageRounded(
          ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(art.texture))),
          position, ImVec2(position.x + size.x, position.y + size.y), ImVec2(0.0f, 0.0f),
          ImVec2(1.0f, 1.0f), IM_COL32_WHITE, style.ChildRounding);
      ImGui::Dummy(ImVec2(size.x, height));
      ImGui::SameLine(0.0f, gap);
    }
  }

  // Right: the lobby.
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::BeginChild("##lobby", ImVec2(0.0f, height), ImGuiChildFlags_NavFlattened);
  ImGui::PopStyleColor();

  ImGui::PushFont(bold, style.FontSizeBase * 1.35f);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Netplay");
  ImGui::PopFont();
  ImGui::SameLine();
  DrawStatusPill(view.hosting ? Tr("HOST") : Tr("CONNECTED"), LAUNCHER_ACCENT, scale, true);
  const std::string core = view.hosting
                               ? Format(Tr("CPU core for every player: %s"), view.cpu_core.c_str())
                               : std::string(Tr("CPU core: the host's choice"));
  const std::string buffer = Format(Tr("Input buffer: %u frames%s"), view.buffer,
                                    view.automatic_buffer ? " (auto)" : "");
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled("%s  \xC2\xB7  %s", core.c_str(), buffer.c_str());
  ImGui::PopTextWrapPos();
  ImGui::Spacing();

  if (view.hosting && view.invite_code)
  {
    BeginSection("##invite", Tr("INVITE YOUR FRIENDS"));
    // A code the server has since forgotten no longer brings anyone in, so
    // a failure hides it.
    const bool code_works = !view.code.empty() && !view.invite_server_failed;
    if (code_works)
      DrawShowAddressesSwitch(state);
    ImGui::PushTextWrapPos(0.0f);
    if (view.invite_server_failed)
    {
      ImGui::TextColored(LOBBY_RED, "%s",
                         Tr("The invite server did not answer. Check the internet connection, "
                            "or close the room and host with Direct IP."));
    }
    else if (code_works)
    {
      DrawInviteCodeRow(view.code, true, state, bold, scale);
      ImGui::TextDisabled("%s", Tr("Your friends type it in Join Netplay, under Invite code. "
                                   "Anyone who sees it can join: keep it off your stream."));
    }
    else
    {
      ImGui::AlignTextToFramePadding();
      ImGui::TextDisabled("%s", Tr("Asking the invite server for a code..."));
    }
    ImGui::PopTextWrapPos();
    EndSection();
  }
  else if (view.hosting)
  {
    BeginSection("##invite", Tr("INVITE YOUR FRIENDS"));
    if (!view.addresses.empty())
      DrawShowAddressesSwitch(state);
    float label_width = 0.0f;
    for (const LobbyAddress& address : view.addresses)
      label_width = std::max(label_width, ImGui::CalcTextSize(LobbyAddressLabel(address.kind)).x);
    label_width += 24.0f * scale;
    for (const LobbyAddress& address : view.addresses)
      DrawAddressRow(LobbyAddressLabel(address.kind), address.address, label_width, state);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", view.addresses.empty()
                                  ? Tr("No network found. Connect this computer to your Wi-Fi "
                                       "or to Tailscale.")
                                  : Tr("Your friends type this address in Join Netplay."));
    ImGui::PopTextWrapPos();
    EndSection();
  }
  else
  {
    BeginSection("##host", Tr("HOST"));
    DrawShowAddressesSwitch(state);
    if (view.invite_code)
      DrawInviteCodeRow(view.code, false, state, bold, scale);
    else
      ImGui::TextUnformatted(LobbyShownAddress(state, view.host_address).c_str());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", Tr("Waiting for the host to start the match."));
    ImGui::PopTextWrapPos();
    EndSection();
  }

  const int taken = static_cast<int>(
      std::ranges::count_if(view.seats, [](const LobbySeat& seat) { return seat.taken; }));
  BeginSection("##players", Format(Tr("PLAYERS  \xC2\xB7  %d / 4"), taken).c_str());
  if (ImGui::BeginTable("##players_table", 4, ImGuiTableFlags_None))
  {
    ImGui::TableSetupColumn("port", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("ping", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("state", ImGuiTableColumnFlags_WidthFixed);
    for (std::size_t i = 0; i < view.seats.size(); ++i)
    {
      ImGui::PushID(static_cast<int>(i));
      DrawSeatRow(Format("P%d", static_cast<int>(i + 1)).c_str(), true, view.seats[i], bold,
                  scale);
      ImGui::PopID();
    }
    // Players without a controller seat still have to be ready to start.
    for (std::size_t i = 0; i < view.without_seat.size(); ++i)
    {
      ImGui::PushID(static_cast<int>(view.seats.size() + i));
      DrawSeatRow("\xE2\x80\x94", false, view.without_seat[i], bold, scale);
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  EndSection();

  BeginSection("##local", Tr("THIS COMPUTER"));
  if (!view.gamecube_ports)
    ImGui::TextDisabled("%s", Tr("Local sideways Wii Remotes"));
  if (view.controllers.empty())
  {
    ImGui::TextDisabled("%s", view.gamecube_ports
                                  ? Tr("No controller connected: you play with the keyboard.")
                                  : Tr("No controller connected."));
  }
  for (std::size_t i = 0; i < view.controllers.size(); ++i)
  {
    const LobbyController& controller = view.controllers[i];
    bool selected = controller.selected;
    ImGui::PushID(static_cast<int>(i));
    ImGui::BeginDisabled(!controller.enabled);
    if (ImGui::Checkbox(controller.label.c_str(), &selected))
      actions.toggled_controller = static_cast<int>(i);
    ImGui::EndDisabled();
    ImGui::PopID();
    if (i == 0 && view.gamecube_ports)
    {
      ImGui::SameLine(0.0f, 16.0f * scale);
      ImGui::TextDisabled("%s", Tr("(the first one also answers to the keyboard)"));
    }
  }
  EndSection();

  // Bottom: Ready (and Start for the host), then the last event or what is
  // still missing, anchored to the bottom of the window when there is room.
  std::string note = view.status;
  const char* missing = nullptr;
  if (view.hosting && !view.can_start)
    missing = Tr("Two controller slots and every machine ready are required");
  else if (!view.hosting && !view.local_ready)
    missing = Tr("Press I'M READY when your controller is set.");
  if (missing)
    note += (note.empty() ? "" : "  \xC2\xB7  ") + std::string(missing);
  const float wrap_width = ImGui::GetContentRegionAvail().x;
  float notes_height = 0.0f;
  for (const std::string* text : std::array<const std::string*, 2>{&view.error, &note})
  {
    if (!text->empty())
      notes_height +=
          ImGui::CalcTextSize(text->c_str(), nullptr, false, wrap_width).y + style.ItemSpacing.y;
  }
  const float buttons_height = 46.0f * scale;
  ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - buttons_height -
                                                            style.ItemSpacing.y - notes_height));

  const char* ready_label = view.local_ready ? Tr("READY  \xC2\xB7  CANCEL") : Tr("I'M READY");
  const LobbyButtonLook ready_look =
      view.local_ready ? LobbyButtonLook::Ready : LobbyButtonLook::Primary;
  if (view.hosting)
  {
    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
    if (DrawLobbyButton(ready_label, ImVec2(half, buttons_height), ready_look, bold))
      actions.toggle_ready = true;
    ImGui::SameLine();
    ImGui::BeginDisabled(!view.can_start);
    if (DrawLobbyButton(Tr("START MATCH"), ImVec2(-1.0f, buttons_height),
                        view.can_start ? LobbyButtonLook::Primary : LobbyButtonLook::Plain, bold))
      actions.start = true;
    ImGui::EndDisabled();
  }
  else if (DrawLobbyButton(ready_label, ImVec2(-1.0f, buttons_height), ready_look, bold))
  {
    actions.toggle_ready = true;
  }

  ImGui::PushTextWrapPos(0.0f);
  if (!view.error.empty())
    ImGui::TextColored(LOBBY_RED, "%s", view.error.c_str());
  if (!note.empty())
    ImGui::TextDisabled("%s", note.c_str());
  ImGui::PopTextWrapPos();

  ImGui::EndChild();
  ImGui::End();
  return actions;
}
}  // namespace moderngekko::frontend::ui
