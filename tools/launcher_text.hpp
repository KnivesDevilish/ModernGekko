#pragma once

// The launcher's interface languages. Texts are written in English in the
// code, as Tr("...") or, when they are defined elsewhere, MODERNGEKKO_UI_TEXT
// ("...") (ui_text.hpp). Tr() looks the English text up in the active
// language's table and falls back to the English text itself.
//
// To change or add a translation, edit SPANISH_TEXTS below; the English side
// must match the code exactly, escapes included. tests/launcher_text_test.cpp
// fails when a text in the code has no translation, when a translation is no
// longer used, or when the %s / %d placeholders of the two sides differ.

#include "ui_text.hpp"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_map>

namespace moderngekko::frontend
{
enum class Language
{
  English,
  Spanish,
};

struct Translation
{
  const char* english;
  const char* translated;
};

// clang-format off
inline constexpr Translation SPANISH_TEXTS[] = {
    // Top bar, sidebar and footer
    {"PLAY", "JUGAR"},
    {"Settings", "Ajustes"},
    {"Controls", "Controles"},
    {"Game Files", "Archivos del juego"},
    {"Credits", "Créditos"},
    {"LANGUAGE", "IDIOMA"},
    {"MUSIC", "MÚSICA"},
    {"Off", "Apagada"},
    {"System (%s)", "Sistema (%s)"},
    {"Controller: %s", "Mando: %s"},

    // PLAY page
    {"GAME READY \xC2\xB7 %s", "JUEGO LISTO · %s"},
    {"Start from savestate", "Empezar desde un estado guardado"},
    {"Normal boot", "Inicio normal"},
    {"Latest - %s", "Más reciente - %s"},
    {"Savestates are available for solo play only.",
     "Los estados guardados solo se pueden usar jugando en solitario."},
    {"Host Netplay", "Crear Netplay"},
    {"Join Netplay", "Unirse a Netplay"},
    {"Host a session", "Crear una sesión"},
    {"Join a session", "Unirse a una sesión"},
    {"INVITE CODE", "CÓDIGO DE INVITACIÓN"},
    {"DIRECT IP", "IP DIRECTA"},
    {"When the room opens you get a code. Your friends type it in Join Netplay, and nobody sees "
     "your IP.",
     "Al crear la sala recibirás un código. Tus amigos lo escriben en Unirse a Netplay y nadie ve "
     "tu IP."},
    {"Type the code the host gave you.", "Escribe el código que te pasó el anfitrión."},
    {"Other players connect to this computer's address and UDP port. For the same network or "
     "Tailscale.",
     "Los demás jugadores se conectan a la dirección y al puerto UDP de este equipo. Para la misma "
     "red o Tailscale."},
    {"Enter the address and UDP port of the player who hosts. For the same network or Tailscale.",
     "Escribe la dirección y el puerto UDP del jugador anfitrión. Para la misma red o Tailscale."},
    {"Careful: an IP address identifies your connection. Don't show it on stream or share it in "
     "public.",
     "Cuidado: una IP identifica tu conexión. No la muestres en directo ni la compartas en "
     "público."},
    {"Invite code", "Código de invitación"},
    {"Invite codes", "Códigos de invitación"},
    {"Dolphin's public server (stun.dolphin-emu.org)",
     "Servidor público de Dolphin (stun.dolphin-emu.org)"},
    {"An invite code has 8 characters (numbers and letters a-f).",
     "El código tiene 8 caracteres (números y letras a-f)."},
    {"Every player needs the same SR-ZGS release and game files. Everyone plays with the host's "
     "CPU core.",
     "Todos necesitan la misma versión de SR-ZGS y los mismos archivos del juego. Todos juegan "
     "con el núcleo de CPU del anfitrión."},
    {"Nickname", "Apodo"},
    {"Host / IP", "Anfitrión / IP"},
    {"UDP port", "Puerto UDP"},
    {"Automatic input buffer", "Búfer de entrada automático"},
    {"Buffer frames", "Frames de búfer"},
    {"Host", "Crear"},
    {"Join", "Unirse"},
    {"Cancel", "Cancelar"},
    {"GAME NOT INSTALLED", "JUEGO NO INSTALADO"},
    {"No extracted game is configured yet.", "Todavía no hay un juego extraído configurado."},
    {"Open Game Files to select and extract your game.",
     "Abre Archivos del juego para elegir y extraer tu juego."},
    {"Go to Game Files", "Ir a Archivos del juego"},

    // PLAY page summary cards
    {"TIME PLAYED", "TIEMPO JUGADO"},
    {"CONTROLS", "CONTROLES"},
    {"DISPLAY", "PANTALLA"},
    {"MODS", "MODS"},
    {"Not played yet", "Sin jugar aún"},
    {"Under a minute", "Menos de 1 min"},
    {"Keyboard only", "Solo teclado"},
    {"Keyboard", "Teclado"},
    {"Not detected", "No detectado"},
    {"Your controller isn't connected; the keyboard still works.\nOpen Controls",
     "Tu mando no está conectado; el teclado sigue funcionando.\nAbrir Controles"},
    {"Open Controls", "Abrir Controles"},
    {"Open Settings", "Abrir Ajustes"},
    {"%s \xC2\xB7 Fullscreen", "%s · Pantalla completa"},
    {"%s \xC2\xB7 Window", "%s · Ventana"},
    {"%s \xC2\xB7 Full", "%s · Completa"},
    {"%s \xC2\xB7 Win", "%s · Vent."},
    {"None", "Ninguno"},
    {"1 installed", "1 instalado"},
    {"%d installed", "%d instalados"},
    {"Mods go in %s", "Los mods van en %s"},

    // Settings page
    {"Changes are saved right away and apply the next time you press PLAY.",
     "Los cambios se guardan al momento y se aplican la próxima vez que pulses JUGAR."},
    {"CPU CORE", "NÚCLEO DE CPU"},
    {"JIT64 (recommended)", "JIT64 (recomendado)"},
    {"StaticRecomp (experimental)", "StaticRecomp (experimental)"},
    {"JIT64 is the validated full-speed mode. StaticRecomp runs the recompiled game code and is "
     "still being optimized.",
     "JIT64 es el modo validado a velocidad completa. StaticRecomp ejecuta el código recompilado "
     "del juego y todavía se está optimizando."},
    {"Netplay: everyone plays with the host's CPU core.",
     "Netplay: todos juegan con el núcleo de CPU del anfitrión."},
    {"MODERNGEKKO_STATICRECOMP=%s is set in the environment and overrides this setting.",
     "MODERNGEKKO_STATICRECOMP=%s está definido en el entorno y tiene prioridad sobre este "
     "ajuste."},
    {"GRAPHICS", "GRÁFICOS"},
    {"Graphics backend", "Motor gráfico"},
    {"Internal resolution (Dolphin EFB upscale)", "Resolución interna (escalado EFB de Dolphin)"},
    {"Aspect ratio", "Relación de aspecto"},
    {"16:9 (Widescreen)", "16:9 (Panorámica)"},
    {"Stretch", "Estirada"},
    {"The game draws in widescreen. Best for most monitors and TVs.",
     "El juego se dibuja en panorámico. Lo mejor para la mayoría de monitores y televisores."},
    {"The original 4:3 picture, with bars on the sides of a wide window.",
     "La imagen 4:3 original, con bandas a los lados en una ventana ancha."},
    {"The widescreen picture stretched to fill the window, whatever its shape. On a 16:9 screen "
     "it looks the same as 16:9.",
     "La imagen panorámica estirada para llenar la ventana, tenga la forma que tenga. En una "
     "pantalla 16:9 se ve igual que 16:9."},
    {"Netplay: the host's 16:9 or 4:3 choice applies to every player.",
     "Netplay: la elección 16:9 o 4:3 del anfitrión se aplica a todos."},
    {"Start in fullscreen", "Iniciar en pantalla completa"},
    {"Show FPS in window title", "Mostrar FPS en el título"},

    // Credits page
    {"SR-ZGS is an independent fan project. It is not affiliated with or endorsed by SEGA.",
     "SR-ZGS es un proyecto independiente hecho por fans. No está afiliado ni respaldado por "
     "SEGA."},
    {"CREATED BY", "CREADO POR"},
    {"With the SR-ZGS contributors and testers: research, development and testing.",
     "Con los colaboradores y testers de SR-ZGS: investigación, desarrollo y pruebas."},
    {"BUILT ON", "BASADO EN"},
    {"Static recompilation framework", "Framework de recompilación estática"},
    {"Runtime for GameCube/Wii recomps, by Hyperway",
     "Runtime para recomps de GameCube/Wii, de Hyperway"},
    {"Recomp template", "Plantilla de recomp"},
    {"Literally God / MrPoloGit, who also added macOS support",
     "Literally God / MrPoloGit, que también añadió soporte para macOS"},
    {"The Dolphin Team and its contributors", "El equipo de Dolphin y sus colaboradores"},
    {"LICENSES", "LICENCIAS"},
    {"GNU GPL v2 or later", "GNU GPL v2 o posterior"},
    {"zlib License", "Licencia zlib"},
    {"MIT License", "Licencia MIT"},
    {"Sonic Riders: Zero Gravity and its characters are trademarks of SEGA. Play with your own "
     "legally obtained copy of the game.",
     "Sonic Riders: Zero Gravity y sus personajes son marcas de SEGA. Juega con tu propia copia "
     "del juego obtenida legalmente."},
    {"Opened in your browser and copied to the clipboard",
     "Abierto en tu navegador y copiado al portapapeles"},

    // Check for updates: the Credits page's VERSION panel and the top bar
    {"VERSION", "VERSIÓN"},
    {"Version %s", "Versión %s"},
    {"Check for updates", "Buscar actualizaciones"},
    {"Looking on GitHub...", "Buscando en GitHub..."},
    {"You have the latest version.", "Tienes la versión más reciente."},
    {"A new version is available: %s", "Hay una versión nueva: %s"},
    {"Open the download page", "Abrir la página de descarga"},
    {"No version has been published on GitHub yet.",
     "Todavía no hay versiones publicadas en GitHub."},
    {"Couldn't check. Check your internet connection.",
     "No se pudo comprobar. Revisa tu conexión a internet."},
    {"Check for updates when the launcher opens", "Buscar actualizaciones al abrir el launcher"},
    {"NEW VERSION \xC2\xB7 %s", "NUEVA VERSIÓN · %s"},

    // Controls page
    {"Choose the controller to play with. The launcher writes its button mapping for you.",
     "Elige el mando con el que vas a jugar. El launcher configura los botones por ti."},
    {"CONTROLLER", "MANDO"},
    {"CONNECTED", "CONECTADO"},
    {"KEYBOARD ONLY", "SOLO TECLADO"},
    {"NOT DETECTED", "NO DETECTADO"},
    {"No SDL gamepad detected", "No se detectó ningún mando SDL"},
    {"Ready to generate controller profile", "Listo para generar el perfil del mando"},
    {"BUTTON MAPPING", "ASIGNACIÓN DE BOTONES"},
    {"Using %s from your user folder.", "Usando %s de tu carpeta de usuario."},
    {"%s is created the first time you play.", "%s se crea la primera vez que juegas."},
    {"Replace with generated GameCube profile", "Reemplazar por un perfil de GameCube generado"},
    {"Replace with generated sideways profile", "Reemplazar por un perfil horizontal generado"},
    {"1 GameCube controller mapped", "1 mando de GameCube configurado"},
    {"1 sideways Wii Remote mapped", "1 Wii Remote horizontal configurado"},
    {"%s now uses %s with the same buttons", "%s ahora usa %s con los mismos botones"},
    {"Keyboard only. A controller you connect is added to the same profile.",
     "Solo teclado. Si conectas un mando, se añade al mismo perfil."},
    {"Using existing %s", "Usando el %s existente"},
    {"%s has no configured controller device", "%s no tiene ningún mando configurado"},
    {"Connect an SDL-compatible controller before playing",
     "Conecta un mando compatible con SDL antes de jugar"},

    // Controls page: button tester
    {"Controller", "Mando"},
    {"Key", "Tecla"},
    {"Your controller", "Tu mando"},
    {"edited by hand", "editado a mano"},
    {"not set", "sin asignar"},
    {"Press keys to test them.", "Pulsa teclas para probarlas."},
    {"Press buttons to test them.", "Pulsa botones para probarlos."},
    {"No controller in %s.", "No hay ningún mando en %s."},
    {"Connect %s.", "Conecta %s."},
    {"Stick", "Stick"},
    {"Menu", "Menú"},
    {"D-Pad", "Cruceta"},
    {"Shake", "Agitar"},
    {"L trigger", "Gatillo L"},
    {"R trigger", "Gatillo R"},
    {"Left stick left", "Stick izquierdo a la izquierda"},
    {"Left stick right", "Stick izquierdo a la derecha"},
    {"Left stick up", "Stick izquierdo arriba"},
    {"Left stick down", "Stick izquierdo abajo"},
    {"Right stick left", "Stick derecho a la izquierda"},
    {"Right stick right", "Stick derecho a la derecha"},
    {"Right stick up", "Stick derecho arriba"},
    {"Right stick down", "Stick derecho abajo"},
    {"Cross", "Equis"},
    {"Circle", "Círculo"},
    {"Square", "Cuadrado"},
    {"Triangle", "Triángulo"},
    {"South button", "Botón sur"},
    {"East button", "Botón este"},
    {"West button", "Botón oeste"},
    {"North button", "Botón norte"},
    {"Back", "Atrás"},
    {"L stick press", "Pulsar stick L"},
    {"R stick press", "Pulsar stick R"},
    {"L shoulder", "Bumper L"},
    {"R shoulder", "Bumper R"},
    {"D-pad up", "Cruceta arriba"},
    {"D-pad down", "Cruceta abajo"},
    {"D-pad left", "Cruceta izquierda"},
    {"D-pad right", "Cruceta derecha"},
    {"Arrow keys", "Flechas"},
    {"D-pad", "Cruceta"},
    {"Left stick", "Stick izquierdo"},
    {"Right stick", "Stick derecho"},

    // Game Files page and disc extraction
    {"Use your own copy of the game as an ISO, WBFS or RVZ image. It is extracted once and "
     "reused afterwards.",
     "Usa tu propia copia del juego como imagen ISO, WBFS o RVZ. Se extrae una vez y se "
     "reutiliza después."},
    {"INSTALLED GAME", "JUEGO INSTALADO"},
    {"NOT INSTALLED", "NO INSTALADO"},
    {"Select a disc image below and extract it.", "Elige una imagen de disco abajo y extráela."},
    {"DISC IMAGE", "IMAGEN DE DISCO"},
    {"No ISO, WBFS, or RVZ selected", "No has elegido ningún ISO, WBFS o RVZ"},
    {"Browse for ISO / WBFS / RVZ", "Buscar ISO / WBFS / RVZ"},
    {"Disc images", "Imágenes de disco"},
    {"Extract Game", "Extraer juego"},
    {"Opening %s", "Abriendo %s"},
    {"Applying Tournament Edition 2.4.6.1", "Aplicando Tournament Edition 2.4.6.1"},
    {"can't start Tournament Edition patcher: %s",
     "no se pudo iniciar el parcheador de Tournament Edition: %s"},
    {"Tournament Edition patching failed", "falló el parche de Tournament Edition"},
    {"this disc is not the pinned patched release",
     "este disco no es la versión parcheada esperada"},
    {"Dolphin rejected the prepared disc image", "Dolphin rechazó la imagen de disco preparada"},
    {"Dolphin could not read the game partition filesystem",
     "Dolphin no pudo leer los archivos de la partición del juego"},
    {"the selected image has an invalid disc ID",
     "la imagen elegida tiene un ID de disco no válido"},
    {"this frontend requires disc ID %s; selected %s",
     "este programa necesita el disco con ID %s; el elegido es %s"},
    {"existing extraction patching failed: %s", "falló el parche de la extracción existente: %s"},
    {"Using existing extraction", "Usando la extracción existente"},
    {"can't create extraction directory: %s", "no se pudo crear la carpeta de extracción: %s"},
    {"Extracting system data", "Extrayendo datos del sistema"},
    {"Dolphin failed while extracting the disc system data",
     "Dolphin falló al extraer los datos del sistema del disco"},
    {"Extracting game files", "Extrayendo archivos del juego"},
    {"Extracting %s", "Extrayendo %s"},
    {"extracted DOL patching failed: %s", "falló el parche del DOL extraído: %s"},
    {"extracted game validation failed: %s", "la comprobación del juego extraído falló: %s"},
    {"can't publish extracted game: %s", "no se pudo guardar el juego extraído: %s"},
    {"Extraction complete", "Extracción completada"},
    {"expected disc ID %s, found %s", "se esperaba el disco con ID %s y se encontró %s"},
    {"main DOL does not match the pinned release",
     "el DOL principal no coincide con la versión esperada"},
    {"_Main.rel does not match the pinned release",
     "_Main.rel no coincide con la versión esperada"},
    {"game assets do not match the pinned release",
     "los archivos del juego no coinciden con la versión esperada"},

    // Starting the game
    {"Launch failed", "No se pudo iniciar"},
    {"Could not start %s: %s", "No se pudo iniciar %s: %s"},
    {"The game process could not be monitored: %s", "No se pudo vigilar el proceso del juego: %s"},
    {"The host is running an incompatible netplay build. Both players must use the same "
     "release.",
     "El anfitrión usa una versión de Netplay incompatible. Los dos jugadores deben usar la "
     "misma versión."},
    {"The extracted game or recomp module does not match the host. Both players need the same "
     "game revision and release.",
     "El juego extraído o el módulo recompilado no coinciden con los del anfitrión. Los dos "
     "jugadores necesitan la misma revisión del juego y la misma versión."},
    {"All four controller slots are already occupied.",
     "Los cuatro puestos de mando ya están ocupados."},
    {"The host has already started the game.", "El anfitrión ya empezó la partida."},
    {"The netplay server is full.", "El servidor de Netplay está lleno."},
    {"The nickname was rejected by the host.", "El anfitrión rechazó el apodo."},
    {"No room answered to that invite code. Check that it is typed right and that the host's "
     "room is still open. If it keeps failing, the invite server may be down or your connection "
     "may not allow it: try Direct IP with Tailscale.",
     "Ninguna sala respondió a ese código de invitación. Revisa que esté bien escrito y que la "
     "sala del anfitrión siga abierta. Si sigue fallando, puede que el servidor de invitaciones "
     "esté caído o que tu conexión no lo permita: prueba IP directa con Tailscale."},
    {"Could not reach the netplay host. Check the host name, UDP port, and firewall.",
     "No se pudo contactar con el anfitrión. Revisa la dirección, el puerto UDP y el firewall."},
    {"Could not create the netplay session. Check the UDP port and firewall settings.",
     "No se pudo crear la sesión de Netplay. Revisa el puerto UDP y el firewall."},
    {"The game process exited with code %d.", "El proceso del juego terminó con el código %d."},
    {"\n\nDetails were written to:\n%s", "\n\nLos detalles se guardaron en:\n%s"},
    {"Session failed", "La sesión falló"},

    // Netplay lobby, top to bottom
    {"HOST", "ANFITRIÓN"},
    {"CPU core for every player: %s", "Núcleo de CPU para todos: %s"},
    {"CPU core: the host's choice", "Núcleo de CPU: el que elija el anfitrión"},
    {"Input buffer: %u frames%s", "Búfer de entrada: %u frames%s"},
    {"INVITE YOUR FRIENDS", "INVITA A TUS AMIGOS"},
    {"Show", "Mostrar"},
    {"Hide", "Ocultar"},
    {"At home", "En casa"},
    {"Internet (Tailscale)", "Internet (Tailscale)"},
    {"Internet (public)", "Internet (pública)"},
    {"Copy", "Copiar"},
    {"Copied", "Copiado"},
    {"Your friends type this address in Join Netplay.",
     "Tus amigos escriben esta dirección en Unirse a Netplay."},
    {"Your friends type it in Join Netplay, under Invite code. Anyone who sees it can join: keep "
     "it off your stream.",
     "Tus amigos lo escriben en Unirse a Netplay, en Código de invitación. Quien lo vea puede "
     "entrar: no lo muestres en directo."},
    {"Asking the invite server for a code...", "Pidiendo un código al servidor de invitaciones..."},
    {"The invite server did not answer. Check the internet connection, or close the room and "
     "host with Direct IP.",
     "El servidor de invitaciones no respondió. Revisa la conexión a internet, o cierra la sala "
     "y créala con IP directa."},
    {"No network found. Connect this computer to your Wi-Fi or to Tailscale.",
     "No se encontró ninguna red. Conecta este equipo a tu Wi-Fi o a Tailscale."},
    {"Waiting for the host to start the match.",
     "Esperando a que el anfitrión empiece la partida."},
    {"PLAYERS  \xC2\xB7  %d / 4", "JUGADORES  \xC2\xB7  %d / 4"},
    {"Free", "Libre"},
    {"(you)", "(tú)"},
    {"(host)", "(anfitrión)"},
    {"READY", "LISTO"},
    {"NOT READY", "NO LISTO"},
    {"OTHER GAME", "OTRO JUEGO"},
    {"THIS COMPUTER", "ESTE EQUIPO"},
    {"Local sideways Wii Remotes", "Wii Remotes horizontales en este equipo"},
    {"No controller connected: you play with the keyboard.",
     "No hay mandos conectados: juegas con el teclado."},
    {"No controller connected.", "No hay mandos conectados."},
    {"(the first one also answers to the keyboard)", "(el primero también responde al teclado)"},
    {"I'M READY", "ESTOY LISTO"},
    {"READY  \xC2\xB7  CANCEL", "LISTO  \xC2\xB7  CANCELAR"},
    {"START MATCH", "EMPEZAR PARTIDA"},
    {"Two controller slots and every machine ready are required",
     "Hacen falta dos puestos de mando y que todos estén listos"},
    {"Press I'M READY when your controller is set.",
     "Pulsa ESTOY LISTO cuando tengas tu mando preparado."},
    // Netplay lobby events
    {"Game: %s", "Juego: %s"},
    {"%s joined", "%s se unió"},
    {"%s left", "%s salió"},
    {"Desync at frame %u", "Desincronización en el frame %u"},
    {"Desync at frame %u reported for %s", "Desincronización en el frame %u, detectada en %s"},
    {"Connection to the netplay host was lost", "Se perdió la conexión con el anfitrión"},
    {"Game start was aborted", "Se canceló el inicio de la partida"},
};
// clang-format on

// Atomic because the disc extraction and netplay threads translate their
// status messages too, while the menu can change the language.
inline std::atomic<Language>& ActiveLanguage()
{
  static std::atomic<Language> language = Language::English;
  return language;
}

// config.ini's "language" setting ("auto", "en" or "es") and the system's
// language code (such as "es" from es_VE.UTF-8): "auto" follows the system
// when it is Spanish, and English otherwise.
inline Language ResolveLanguage(std::string_view setting, std::string_view system_language)
{
  if (setting == "en")
    return Language::English;
  if (setting == "es")
    return Language::Spanish;
  return system_language.starts_with("es") ? Language::Spanish : Language::English;
}

// Each language's own name, as the language menu shows it.
inline const char* LanguageName(Language language)
{
  return language == Language::Spanish ? "Espa\xC3\xB1ol" : "English";
}

inline const char* Tr(const char* english)
{
  if (ActiveLanguage() == Language::English)
    return english;
  static const std::unordered_map<std::string_view, const char*> spanish = []
  {
    std::unordered_map<std::string_view, const char*> table;
    for (const Translation& entry : SPANISH_TEXTS)
      table.emplace(entry.english, entry.translated);
    return table;
  }();
  const auto found = spanish.find(english);
  return found != spanish.end() ? found->second : english;
}

// For a text built at run time, such as FormatTimePlayed()'s: translated when
// it is one of the marked texts, unchanged otherwise ("12 h 34 min").
inline std::string Tr(const std::string& english)
{
  return Tr(english.c_str());
}

// printf-style formatting into a std::string, for translated texts with
// values in them: Format(Tr("Mods go in %s"), folder.c_str()).
#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
inline std::string
Format(const char* format, ...)
{
  va_list arguments;
  va_start(arguments, format);
  va_list copy;
  va_copy(copy, arguments);
  const int size = std::vsnprintf(nullptr, 0, format, copy);
  va_end(copy);
  std::string text(size > 0 ? static_cast<std::size_t>(size) : 0, '\0');
  if (size > 0)
    std::vsnprintf(text.data(), text.size() + 1, format, arguments);
  va_end(arguments);
  return text;
}
}  // namespace moderngekko::frontend
