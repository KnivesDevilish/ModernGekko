#include "netplay_session.hpp"

#include "game_files_hash.hpp"
#include "launcher_music.hpp"
#include "launcher_text.hpp"
#include "launcher_ui.hpp"
#include "netplay_address.hpp"
#include "netplay_compatibility.hpp"
#include "netplay_lobby_ui.hpp"

#include "Common/MsgHandler.h"
#include "Common/TraversalClient.h"
#include "Core/Boot/Boot.h"
#include "Core/Config/MainSettings.h"
#include "Core/Config/NetplaySettings.h"
#include "Core/Config/WiimoteSettings.h"
#include "Core/HW/Wiimote.h"
#include "Core/IOS/FS/FileSystem.h"
#include "Core/NetPlay/NetPlayClient.h"
#include "Core/NetPlay/NetPlayServer.h"
#include "Core/PowerPC/PowerPC.h"
#include "UICommon/GameFile.h"
#include "UICommon/UICommon.h"
#include "runtime/dolphin_runtime_internal.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace moderngekko::frontend {
namespace {
// The system's first preferred language, such as "es" (as in the launcher).
std::string SystemLanguageCode() {
  int count = 0;
  SDL_Locale **locales = SDL_GetPreferredLocales(&count);
  std::string language =
      locales && count > 0 && locales[0]->language ? locales[0]->language : "";
  SDL_free(locales);
  return language;
}

#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
// The game reads GameCube controllers. The server hands out seats in its Wii
// Remote table, which also sets the room size and when Start is allowed, so
// the host gives each seat the GameCube port with the same number and the Wii
// Remotes stay off in the game, as in solo play.
constexpr bool GAMECUBE_PORTS = true;
#else
constexpr bool GAMECUBE_PORTS = false;
#endif

#ifdef MODERNGEKKO_DEFAULT_WINDOW_TITLE
constexpr const char *LOBBY_WINDOW_TITLE =
    MODERNGEKKO_DEFAULT_WINDOW_TITLE " - Netplay";
// The name the system's volume mixer shows for the lobby's music.
constexpr const char *LOBBY_APP_NAME = MODERNGEKKO_DEFAULT_WINDOW_TITLE;
#else
constexpr const char *LOBBY_WINDOW_TITLE = "ModernGekko - Netplay";
constexpr const char *LOBBY_APP_NAME = "ModernGekko";
#endif

// Dolphin's traversal server ports (its main one, and the one it uses to
// test the router), unless invite_server names another main port.
constexpr u16 INVITE_SERVER_PORT = 6262;
constexpr u16 INVITE_SERVER_ALT_PORT = 6226;

const char *CPUCoreLabel(PowerPC::CPUCore core) {
  switch (core) {
  case PowerPC::CPUCore::StaticRecomp:
    return "StaticRecomp";
  case PowerPC::CPUCore::JITARM64:
    return "JITARM64";
  case PowerPC::CPUCore::JIT64:
    return "JIT64";
  default:
    return "Interpreter";
  }
}

struct ControllerOption {
  std::string label;
  std::string device;
};

std::vector<ControllerOption> EnumerateControllers() {
  std::vector<ControllerOption> result;
  std::unordered_map<std::string, int> device_ids;
  int count = 0;
  SDL_JoystickID *joystick_ids = SDL_GetJoysticks(&count);
  for (int i = 0; i < count; ++i) {
    const SDL_JoystickID joystick_id = joystick_ids[i];
    if (!SDL_IsGamepad(joystick_id))
      continue;
    const char *name_value = SDL_GetGamepadNameForID(joystick_id);
    const std::string name =
        name_value && *name_value ? name_value : "Unknown Controller";
    const int device_id = device_ids[name]++;
    result.push_back({device_id == 0
                          ? name
                          : name + " (" + std::to_string(device_id + 1) + ")",
                      "SDL/" + std::to_string(device_id) + "/" + name});
  }
  SDL_free(joystick_ids);
  return result;
}

class SessionUI final : public NetPlay::NetPlayUI {
public:
  explicit SessionUI(std::shared_ptr<UICommon::GameFile> game)
      : m_game(std::move(game)) {}

  void SetClient(NetPlay::NetPlayClient *client) {
    std::lock_guard lock(m_mutex);
    m_client = client;
  }

  void SetHosting(bool hosting) {
    std::lock_guard lock(m_mutex);
    m_hosting = hosting;
  }

  void SetRuntime(Runtime *runtime) {
    std::lock_guard lock(m_mutex);
    m_runtime = runtime;
  }

  bool TakeStartRequest() { return m_start_requested.exchange(false); }

  std::unique_ptr<BootSessionData> TakeBootData() {
    std::lock_guard lock(m_mutex);
    return std::move(m_boot_data);
  }

  std::string Error() {
    std::lock_guard lock(m_mutex);
    return m_error;
  }

  std::string Status() {
    std::lock_guard lock(m_mutex);
    return m_status;
  }

  u32 BufferSize() const { return m_buffer_size.load(); }

  bool ConnectionLost() const { return m_connection_lost.load(); }

  // Host with an invite code: the code once the invite server has handed it
  // out, and whether the server stopped answering.
  std::string InviteCode() {
    std::lock_guard lock(m_mutex);
    return m_invite_code;
  }
  bool InviteServerFailed() const { return m_invite_server_failed.load(); }

  void ClearReadyState() {
    if (m_client)
      m_client->SetReady(false);
  }

  void BootGame(const std::string &,
                std::unique_ptr<BootSessionData> boot_session_data) override {
    std::lock_guard lock(m_mutex);
    m_boot_data = std::move(boot_session_data);
  }

  void StopGame() override {
    std::lock_guard lock(m_mutex);
    if (m_runtime)
      m_runtime->RequestStop();
  }

  bool IsHosting() const override { return m_hosting; }

  void Update() override {}

  void AppendChat(const std::string &msg) override {
    std::lock_guard lock(m_mutex);
    m_status = msg;
  }

  void OnMsgChangeGame(const NetPlay::SyncIdentifier &,
                       const std::string &name) override {
    std::lock_guard lock(m_mutex);
    m_status = Format(Tr("Game: %s"), name.c_str());
  }

  void OnMsgChangeGBARom(int, const NetPlay::GBAConfig &) override {}

  void OnMsgStartGame() override { m_start_requested = true; }

  void OnMsgStopGame() override { StopGame(); }

  void OnMsgPowerButton() override { StopGame(); }

  void OnPlayerConnect(const std::string &player) override {
    std::lock_guard lock(m_mutex);
    m_status = Format(Tr("%s joined"), player.c_str());
  }

  void OnPlayerDisconnect(const std::string &player) override {
    std::lock_guard lock(m_mutex);
    m_status = Format(Tr("%s left"), player.c_str());
  }

  void OnPadBufferChanged(u32 buffer) override { m_buffer_size = buffer; }

  void OnHostInputAuthorityChanged(bool) override {}

  void OnDesync(u32 frame, const std::string &player) override {
    {
      std::lock_guard lock(m_mutex);
      m_error = player.empty()
                    ? Format(Tr("Desync at frame %u"), frame)
                    : Format(Tr("Desync at frame %u reported for %s"), frame,
                             player.c_str());
    }
    StopGame();
  }

  void OnConnectionLost() override {
    m_connection_lost = true;
    {
      std::lock_guard lock(m_mutex);
      m_error = Tr("Connection to the netplay host was lost");
    }
    StopGame();
  }

  void OnConnectionError(const std::string &message) override {
    std::lock_guard lock(m_mutex);
    m_error = message;
  }

  // The invite server did not answer, or forgot this computer. The lobby
  // says so in its invite section; a code join fails before the lobby opens.
  void OnTraversalError(Common::TraversalClient::FailureReason) override {
    m_invite_server_failed = true;
  }

  // Called on the netplay thread, which is also the one that receives the
  // code from the invite server.
  void OnTraversalStateChanged(Common::TraversalClient::State state) override {
    if (state != Common::TraversalClient::State::Connected ||
        !Common::g_TraversalClient)
      return;
    const Common::TraversalHostId id = Common::g_TraversalClient->GetHostID();
    std::lock_guard lock(m_mutex);
    m_invite_code.assign(id.data(), strnlen(id.data(), id.size()));
    m_invite_server_failed = false;
  }

  void OnGameStartAborted() override {
    std::lock_guard lock(m_mutex);
    m_error = Tr("Game start was aborted");
  }

  void OnGolferChanged(bool, const std::string &) override {}
  void OnTtlDetermined(u8) override {}
  bool IsRecording() override { return false; }

  std::shared_ptr<const UICommon::GameFile>
  FindGameFile(const NetPlay::SyncIdentifier &sync_identifier,
               NetPlay::SyncIdentifierComparison *found) override {
    const auto comparison = m_game->CompareSyncIdentifier(sync_identifier);
    if (found)
      *found = comparison;
    return m_game;
  }

  std::string FindGBARomPath(const std::array<u8, 20> &, std::string_view,
                             int) override {
    return {};
  }

  void ShowGameDigestDialog(const std::string &) override {}
  void SetGameDigestProgress(int, int) override {}
  void SetGameDigestResult(int, const std::string &) override {}
  void AbortGameDigest() override {}
  void OnIndexAdded(bool, std::string) override {}
  void OnIndexRefreshFailed(std::string) override {}

  void ShowChunkedProgressDialog(const std::string &title, u64 data_size,
                                 std::span<const int>) override {
    std::lock_guard lock(m_mutex);
    m_status = title;
    m_progress_size = data_size;
    m_progress = 0;
  }

  void HideChunkedProgressDialog() override {
    std::lock_guard lock(m_mutex);
    m_progress_size = 0;
    m_progress = 0;
  }

  void SetChunkedProgress(int, u64 progress) override {
    std::lock_guard lock(m_mutex);
    m_progress = progress;
  }

  void SetHostWiiSyncData(std::vector<u64> titles,
                          std::string redirect_folder) override {
    std::lock_guard lock(m_mutex);
    if (m_client)
      m_client->SetWiiSyncData(nullptr, std::move(titles),
                               std::move(redirect_folder));
  }

private:
  std::shared_ptr<UICommon::GameFile> m_game;
  mutable std::mutex m_mutex;
  NetPlay::NetPlayClient *m_client = nullptr;
  Runtime *m_runtime = nullptr;
  bool m_hosting = false;
  std::atomic<bool> m_start_requested{false};
  std::atomic<bool> m_connection_lost{false};
  std::atomic<bool> m_invite_server_failed{false};
  std::string m_invite_code;
  std::atomic<u32> m_buffer_size{5};
  std::unique_ptr<BootSessionData> m_boot_data;
  std::string m_error;
  std::string m_status;
  u64 m_progress_size = 0;
  u64 m_progress = 0;
};

// The lobby's layout and the launcher's look it shares (netplay_lobby_ui.hpp,
// launcher_ui.hpp). Not "using namespace ui": RunLobbyWindow's SessionUI is
// also called ui.
using ui::ApplyLauncherTheme;
using ui::DrawLobby;
using ui::LauncherFonts;
using ui::LauncherImage;
using ui::LobbyActions;
using ui::LobbyAddress;
using ui::LobbySeat;
using ui::LobbyState;
using ui::LobbyView;

// The folder of this executable, where the build puts Resources/ (the lobby
// artwork) and Sys/ (the fallback font).
fs::path ExecutableDirectory() {
  if (const char *base = SDL_GetBasePath())
    return fs::path(reinterpret_cast<const char8_t *>(base));
  std::error_code ec;
  return fs::current_path(ec);
}

// The addresses of this computer that friends can connect to, home network
// first. Container and virtual machine adapters are left out.
std::vector<LobbyAddress> HostAddresses(const NetPlay::NetPlayServer &server) {
  std::vector<LobbyAddress> addresses;
  for (const std::string &interface_name : server.GetInterfaceSet()) {
    const std::string host = server.GetInterfaceHost(interface_name);
    const std::string_view ip =
        std::string_view(host).substr(0, host.rfind(':'));
    const AddressKind kind = ClassifyAddress(interface_name, ip);
    if (kind == AddressKind::Ignore ||
        std::ranges::contains(addresses, host, &LobbyAddress::address))
      continue;
    addresses.push_back({kind, host});
  }
  std::ranges::sort(addresses, [](const LobbyAddress &a, const LobbyAddress &b) {
    return std::tie(a.kind, a.address) < std::tie(b.kind, b.address);
  });
  return addresses;
}

class LobbyWindow {
public:
  // music_volume is config.ini's, 0 (off) to 100.
  bool Open(WindowSystem window_system, int music_volume) {
#if defined(__linux__)
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER,
                window_system == WindowSystem::Wayland ? "wayland" : "x11");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
      return false;
    const SDL_DisplayID display = SDL_GetPrimaryDisplay();
    const float display_scale = SDL_GetDisplayContentScale(display);
    m_scale = display_scale > 0.0f ? display_scale : 1.0f;
    // The artwork's height sets the window's proportions; a small screen
    // gets a smaller window, and the lobby scrolls if it must. The size
    // can't be changed after.
    int width = static_cast<int>(1000 * m_scale);
    int height = static_cast<int>(640 * m_scale);
    SDL_Rect usable{};
    if (SDL_GetDisplayUsableBounds(display, &usable) && usable.w > 0 &&
        usable.h > 0) {
      width = std::min(width, usable.w * 9 / 10);
      height = std::min(height, usable.h * 9 / 10);
    }
    m_window = SDL_CreateWindow(LOBBY_WINDOW_TITLE, width, height,
                                SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!m_window)
      return false;
    m_renderer = SDL_CreateRenderer(m_window, "vulkan");
    if (!m_renderer)
      m_renderer = SDL_CreateRenderer(m_window, nullptr);
    if (!m_renderer)
      return false;
    SDL_SetRenderVSync(m_renderer, 1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    // The launcher's look: its theme, its fonts and the lobby artwork.
    ImGui::StyleColorsDark();
    ApplyLauncherTheme(ImGui::GetStyle());
    ImGui::GetStyle().ScaleAllSizes(m_scale);
    ImGui::GetStyle().FontScaleDpi = m_scale;
    const fs::path release_directory = ExecutableDirectory();
    m_fonts = ui::LoadInterfaceFont(release_directory, 18.0f);
    ImGui_ImplSDL3_InitForSDLRenderer(m_window, m_renderer);
    ImGui_ImplSDLRenderer3_Init(m_renderer);
    m_art = ui::LoadLauncherImage(m_renderer, release_directory,
                                  "lobby-art.png", "lobby art");
    // Behind everything, with a floor that slides as in the launcher.
    m_background = ui::LoadLauncherImage(m_renderer, release_directory,
                                         "lobby-background.png",
                                         "lobby background", true);
    // Resources/lobby-music.mp3 loops while the lobby is open; the game
    // has its own sound.
    m_music.Play(release_directory / "Resources" / "lobby-music.mp3",
                 music_volume);
    m_open = true;
    return true;
  }

  ~LobbyWindow() { Close(); }

  float Scale() const { return m_scale; }
  ImFont *Bold() const { return m_fonts.bold; }
  const LauncherImage &Art() const { return m_art; }
  bool HasBackground() const { return m_background.texture != nullptr; }

  void Close() {
    if (!m_open)
      return;
    m_music.Stop();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    if (m_art.texture)
      SDL_DestroyTexture(m_art.texture);
    m_art = {};
    if (m_background.texture)
      SDL_DestroyTexture(m_background.texture);
    m_background = {};
    m_fonts = {};
    SDL_DestroyRenderer(m_renderer);
    SDL_DestroyWindow(m_window);
    SDL_Quit();
    m_renderer = nullptr;
    m_window = nullptr;
    m_open = false;
  }

  bool Frame() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT ||
          event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
        return false;
    }
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    return true;
  }

  void Present() {
    ImGui::Render();
    const ImGuiIO &io = ImGui::GetIO();
    SDL_SetRenderScale(m_renderer, io.DisplayFramebufferScale.x,
                       io.DisplayFramebufferScale.y);
    SDL_SetRenderDrawColor(m_renderer, 8, 8, 9, 255);
    SDL_RenderClear(m_renderer);
    ui::RenderBackground(m_renderer, m_background,
                         static_cast<double>(SDL_GetTicksNS()) / 1e9);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), m_renderer);
    SDL_RenderPresent(m_renderer);
  }

private:
  SDL_Window *m_window = nullptr;
  SDL_Renderer *m_renderer = nullptr;
  float m_scale = 1.0f;
  LauncherFonts m_fonts;
  LauncherImage m_art;
  LauncherImage m_background;
  MusicPlayer m_music{LOBBY_APP_NAME};
  bool m_open = false;
};

// A controller ticked or unticked in the lobby: the generated controller
// profile and config.ini follow, and the server learns the new count.
void ToggleController(const ControllerOption &controller,
                      RuntimeConfig &runtime_config,
                      ConfigResult &frontend_config, NetplayOptions &options,
                      NetPlay::NetPlayClient &client) {
  const bool selected =
      !std::ranges::contains(options.controllers, controller.device);
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
  // A keyboard-only player who ticks a gamepad switches to it rather than
  // taking a second seat; the gamepad's port keeps the keys.
  if (selected && options.controllers.size() == 1 &&
      options.controllers.front() == KeyboardDevice())
    options.controllers.clear();
#endif
  if (selected)
    options.controllers.push_back(controller.device);
  else if (options.controllers.size() > 1)
    std::erase(options.controllers, controller.device);
  frontend_config.controllers = options.controllers;
  frontend_config.controller = options.controllers.front();
  std::string error;
  if (GenerateControllerConfig(runtime_config.user_directory,
                               options.controllers, &error) &&
      SaveConfig(runtime_config.user_directory, frontend_config, &error)) {
    client.SetLocalControllerCount(static_cast<u8>(options.controllers.size()));
    client.SetReady(false);
  }
}

bool RunLobbyWindow(RuntimeConfig &runtime_config,
                    ConfigResult &frontend_config, NetplayOptions &options,
                    SessionUI &ui, NetPlay::NetPlayClient &client,
                    NetPlay::NetPlayServer *server) {
  LobbyWindow window;
  if (!window.Open(runtime_config.window_system,
                   frontend_config.music_volume))
    return false;

  std::vector<ControllerOption> available = EnumerateControllers();
  // The host's addresses, refreshed with the controllers (each lookup asks
  // the system for its network adapters).
  // With an invite code the lobby shows the code, not the addresses.
  std::vector<LobbyAddress> addresses;
  if (server && !options.invite_code)
    addresses = HostAddresses(*server);
  LobbyState lobby_state;
  auto last_refresh = std::chrono::steady_clock::now();
  bool running = true;
  while (running) {
    if (!window.Frame())
      break;

    const auto now = std::chrono::steady_clock::now();
    if (now - last_refresh >= std::chrono::seconds(1)) {
      available = EnumerateControllers();
      if (server && !options.invite_code)
        addresses = HostAddresses(*server);
      last_refresh = now;
    }

    const std::vector<NetPlay::Player> players = client.GetPlayersSnapshot();
    const NetPlay::PadMappingArray wiimote_mapping =
        client.GetWiimoteMappingSnapshot();
    const u8 assigned = client.GetAssignedControllerCount();
    if (assigned > 0 && options.controllers.size() > assigned) {
      options.controllers.resize(assigned);
      frontend_config.controllers = options.controllers;
      frontend_config.controller = options.controllers.front();
      std::string ignored;
      SaveConfig(runtime_config.user_directory, frontend_config, &ignored);
    }

    // Host: every seat also owns the GameCube port with the same number. The
    // server sends the ports to all players, as Dolphin does for its pad
    // mapping, before anyone can be ready to start.
    if (GAMECUBE_PORTS && server) {
      const NetPlay::PadMappingArray seats = server->GetWiimoteMapping();
      if (server->GetPadMapping() != seats)
        server->SetPadMapping(seats);
    }

    if (ui.TakeStartRequest()) {
      client.StartGame(runtime_config.game_root.string() + "/sys/main.dol");
      // StartGame turns on an emulated Wii Remote for every seat. The seats
      // play on GameCube ports here, so the Wii Remotes go back off on every
      // machine alike (the current-run layer outranks netplay's settings).
      if (GAMECUBE_PORTS) {
        for (int i = 0; i < 4; ++i)
          Config::SetCurrent(Config::GetInfoForWiimoteSource(i),
                             WiimoteSource::None);
      }
    }
    auto boot_data = ui.TakeBootData();
    if (boot_data) {
      detail::SetBootSessionData(std::move(boot_data));
      window.Close();
      return true;
    }

    // What the lobby shows, from the client's and the server's state.
    LobbyView view;
    view.hosting = server != nullptr;
    if (server)
      view.cpu_core = CPUCoreLabel(detail::SelectedCPUCore());
    view.buffer = ui.BufferSize();
    view.automatic_buffer = server && options.buffer == "auto";
    view.invite_code = options.invite_code;
    if (options.invite_code) {
      // A joining player's address is the host's code.
      view.code = server ? ui.InviteCode() : options.address;
      view.invite_server_failed = server && ui.InviteServerFailed();
    } else {
      view.addresses = addresses;
      if (!server)
        view.host_address =
            options.address + ':' + std::to_string(options.port);
    }
    const auto seat_of = [&](const NetPlay::Player &player) {
      LobbySeat seat;
      seat.taken = true;
      seat.name = player.name;
      seat.ping = player.ping;
      seat.ready = player.ready;
      // Unknown until the player's copy of the game has been compared.
      seat.same_game =
          player.game_status == NetPlay::SyncIdentifierComparison::SameGame ||
          player.game_status == NetPlay::SyncIdentifierComparison::Unknown;
      seat.you = client.IsLocalPlayer(player.pid);
      seat.host = player.IsHost();
      return seat;
    };
    for (std::size_t i = 0; i < view.seats.size() && i < wiimote_mapping.size();
         ++i) {
      const auto player =
          std::ranges::find(players, wiimote_mapping[i], &NetPlay::Player::pid);
      if (wiimote_mapping[i] != 0 && player != players.end())
        view.seats[i] = seat_of(*player);
    }
    for (const NetPlay::Player &player : players) {
      if (!std::ranges::contains(wiimote_mapping, player.pid))
        view.without_seat.push_back(seat_of(player));
    }
    view.gamecube_ports = GAMECUBE_PORTS;
    std::size_t total_assigned = 0;
    for (const NetPlay::Player &player : players)
      total_assigned += player.controller_count;
    const std::size_t local_limit =
        std::min<std::size_t>(4, assigned + 4 - total_assigned);
    for (const ControllerOption &controller : available) {
      const bool selected =
          std::ranges::contains(options.controllers, controller.device);
      view.controllers.push_back(
          {controller.label, selected,
           selected || options.controllers.size() < local_limit});
    }
    const auto local_player =
        std::ranges::find_if(players, [&](const NetPlay::Player &player) {
          return client.IsLocalPlayer(player.pid);
        });
    view.local_ready = local_player != players.end() && local_player->ready;
    view.can_start = server && server->CanStart();
    view.status = ui.Status();
    view.error = ui.Error();

    const LobbyActions actions =
        DrawLobby(view, lobby_state, window.Art(), window.Bold(),
                  window.Scale(), window.HasBackground());
    if (actions.toggled_controller >= 0 &&
        static_cast<std::size_t>(actions.toggled_controller) < available.size())
      ToggleController(available[actions.toggled_controller], runtime_config,
                       frontend_config, options, client);
    if (actions.toggle_ready)
      client.SetReady(!view.local_ready);
    if (actions.start && server)
      server->RequestStartGame();
    window.Present();

    if (ui.ConnectionLost())
      running = false;
  }
  return false;
}
} // namespace

int RunNetplayLobby(RuntimeConfig runtime_config, ConfigResult frontend_config,
                    NetplayOptions options) {
  if (options.controllers.empty())
    return static_cast<int>(NetplayExitCode::InvalidConfiguration);

  std::cerr << "netplay: initializing Dolphin services\n";
  UICommon::SetUserDirectory(runtime_config.user_directory.string());
  UICommon::Init();
  detail::SetExternalUICommon(true);
  // The lobby speaks the launcher's language (config.ini, or the system's).
  ActiveLanguage() = ResolveLanguage(frontend_config.language, SystemLanguageCode());

  Config::SetBase(Config::MAIN_CPU_THREAD, true);
  // The server sends the host's core to every player when the match starts,
  // so the host's choice in config.ini (Settings) is the one everyone plays.
  Config::SetBase(Config::MAIN_CPU_CORE, detail::SelectedCPUCore());
  Config::SetBase(Config::NETPLAY_SAVEDATA_LOAD, true);
  Config::SetBase(Config::NETPLAY_SAVEDATA_WRITE, true);
  Config::SetBase(Config::NETPLAY_SAVEDATA_SYNC_ALL_WII, false);
  Config::SetBase(Config::NETPLAY_SYNC_CODES, false);
  Config::SetBase(Config::NETPLAY_STRICT_SETTINGS_SYNC, true);
  Config::SetBase(Config::NETPLAY_NETWORK_MODE, std::string("fixeddelay"));
  Config::SetBase(Config::NETPLAY_USE_INDEX, false);
  // The host sends the Wii's 16:9 setting when the match starts, before
  // Runtime::Create runs, so the choice must already be in place here.
  detail::ApplyAspectRatio(runtime_config.graphics.aspect_ratio);

  auto game = std::make_shared<UICommon::GameFile>(
      (runtime_config.game_root / "sys/main.dol").string());
  if (!game->IsValid()) {
    detail::SetExternalUICommon(false);
    UICommon::Shutdown();
    return static_cast<int>(NetplayExitCode::InvalidConfiguration);
  }

  const GameInspectResult inspected =
      InspectGame(runtime_config.game_root, {.hash_assets = false});
  if (!inspected) {
    detail::SetExternalUICommon(false);
    UICommon::Shutdown();
    return static_cast<int>(NetplayExitCode::InvalidConfiguration);
  }
  // Every player's game files must match, so all of files/ is hashed. That
  // is several GB; each file's hash is remembered until the file changes, so
  // only the first lobby (or one after the files changed) reads them all.
  const auto files_check_started = std::chrono::steady_clock::now();
  std::size_t files_read = 0;
  GameMetadata metadata = *inspected.metadata;
  const auto files_hash = CachedDirectorySha256(
      metadata.root / "files",
      runtime_config.user_directory / "Cache" / "game-files.sha256", &files_read);
  if (!files_hash) {
    detail::SetExternalUICommon(false);
    UICommon::Shutdown();
    return static_cast<int>(NetplayExitCode::InvalidConfiguration);
  }
  metadata.assets_sha256 = *files_hash;
  std::cerr << "netplay: game files checked in "
            << std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - files_check_started)
                   .count()
            << " ms (" << files_read << " read)\n";
  std::cerr << "netplay: validating compatibility\n";
  NetPlay::SetCompatibilityFingerprint(
      CompatibilityFingerprint(runtime_config, metadata));
  std::cerr << "netplay: compatibility validated\n";

  SessionUI ui(game);
  const NetPlay::NetTraversalConfig direct{};
  // Invite codes come from the invite server (Dolphin's traversal server):
  // config.ini's invite_server, "host" or "host:port".
  NetPlay::NetTraversalConfig invite{};
  if (options.invite_code) {
    std::string invite_host = options.invite_server;
    u16 invite_port = INVITE_SERVER_PORT;
    if (const auto split = SplitHostAndPort(options.invite_server)) {
      invite_host = split->host;
      invite_port = split->port;
    }
    invite = NetPlay::NetTraversalConfig{true, invite_host, invite_port,
                                         INVITE_SERVER_ALT_PORT};
    std::cerr << "netplay: invite server " << invite_host << ':' << invite_port
              << '\n';
  }
  std::unique_ptr<NetPlay::NetPlayServer> server;
  std::unique_ptr<NetPlay::NetPlayClient> client;
  if (options.role == NetplayRole::Host) {
    std::cerr << "netplay: opening host on UDP port " << options.port << '\n';
    ui.SetHosting(true);
    server = std::make_unique<NetPlay::NetPlayServer>(
        options.port, false, &ui, options.invite_code ? invite : direct);
    if (!server->is_connected) {
      server.reset();
      detail::SetExternalUICommon(false);
      UICommon::Shutdown();
      return static_cast<int>(NetplayExitCode::Failed);
    }
    server->SetHostInputAuthority(false);
    server->SetAdaptiveBuffer(options.buffer == "auto");
    if (options.buffer != "auto")
      server->AdjustPadBufferSize(
          static_cast<unsigned int>(std::stoul(options.buffer)));
    server->ChangeGame(game->GetSyncIdentifier(),
                       inspected.metadata->game_name);
    client = std::make_unique<NetPlay::NetPlayClient>(
        "127.0.0.1", server->GetPort(), &ui, options.nickname, direct,
        static_cast<u8>(options.controllers.size()));
  } else {
    if (options.invite_code)
      std::cerr << "netplay: joining with an invite code\n";
    else
      std::cerr << "netplay: connecting to " << options.address << ':'
                << options.port << '\n';
    // A code nobody answers to raises one of Dolphin's alerts, a message box
    // in English on Windows. The launcher explains the failure instead.
    if (options.invite_code)
      Common::SetEnableAlert(false);
    client = std::make_unique<NetPlay::NetPlayClient>(
        options.address, options.port, &ui, options.nickname,
        options.invite_code ? invite : direct,
        static_cast<u8>(options.controllers.size()));
    if (options.invite_code)
      Common::SetEnableAlert(true);
  }

  if (!client->IsConnected()) {
    const NetPlay::ConnectionError connection_error =
        client->GetConnectionError();
    const std::string error = ui.Error();
    if (!error.empty())
      std::cerr << "netplay: " << error << '\n';
    client.reset();
    server.reset();
    detail::SetExternalUICommon(false);
    UICommon::Shutdown();
    switch (connection_error) {
    case NetPlay::ConnectionError::VersionMismatch:
      return static_cast<int>(NetplayExitCode::VersionMismatch);
    case NetPlay::ConnectionError::CompatibilityMismatch:
      return static_cast<int>(NetplayExitCode::CompatibilityMismatch);
    case NetPlay::ConnectionError::RoomFull:
      return static_cast<int>(NetplayExitCode::RoomFull);
    case NetPlay::ConnectionError::GameRunning:
      return static_cast<int>(NetplayExitCode::GameRunning);
    case NetPlay::ConnectionError::ServerFull:
      return static_cast<int>(NetplayExitCode::ServerFull);
    case NetPlay::ConnectionError::NameTooLong:
      return static_cast<int>(NetplayExitCode::NicknameRejected);
    case NetPlay::ConnectionError::NoError:
      return static_cast<int>(options.invite_code
                                  ? NetplayExitCode::InviteCodeFailed
                                  : NetplayExitCode::HostUnavailable);
    }
  }
  ui.SetClient(client.get());
  std::cerr << "netplay: connected; opening lobby\n";

  int result = 0;
  bool remain = true;
  while (remain) {
    const bool boot = RunLobbyWindow(runtime_config, frontend_config, options,
                                     ui, *client, server.get());
    if (!boot)
      break;

    auto created = Runtime::Create(runtime_config);
    if (!created) {
      result = 1;
      break;
    }
    ui.SetRuntime(created.runtime.get());
    const RuntimeRunResult run_result = created.runtime->Run();
    ui.SetRuntime(nullptr);
    client->Stop();
    client->StopGame();
    created.runtime.reset();
    ui.ClearReadyState();
    if (run_result.error)
      result = 1;
    remain = !ui.ConnectionLost();
  }

  client.reset();
  server.reset();
  detail::SetExternalUICommon(false);
  UICommon::Shutdown();
  return result;
}
} // namespace moderngekko::frontend
