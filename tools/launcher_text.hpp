#pragma once

// The launcher's interface languages. Texts are written in English in the
// code, as Tr("...") or, when they are defined elsewhere, MODERNGEKKO_UI_TEXT
// ("...") (ui_text.hpp). Tr() looks the English text up in the active
// language's table and falls back to the English text itself.
//
// To change or add a translation, edit SPANISH_TEXTS and JAPANESE_TEXTS below;
// the English side must match the code exactly, escapes included.
// tests/launcher_text_test.cpp fails when a text in the code has no
// translation in either language, when a translation is no longer used, or
// when the %s / %d placeholders of the two sides differ.
//
// The Japanese texts need their characters in the embedded Japanese font:
// after changing them, run tools/make_launcher_cjk_font.py (it reads them
// from this file) to rebuild tools/launcher_cjk_font.inc.

#include "ui_text.hpp"

#include <atomic>
#include <cstddef>
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
  Japanese,
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
    {"PATCH NOTES", "NOTAS DEL PARCHE"},
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
    {"Internal resolution", "Resolución interna"},
    {"Aspect ratio", "Relación de aspecto"},
    {"16:9 (Widescreen)", "16:9 (Panorámica)"},
    {"Stretch", "Estirada"},
    {"Ultrawide", "Ultrapanorámica"},
    {"Ultrawide (experimental)", "Ultrapanorámica (experimental)"},
    {"For 21:9 and wider screens: the race is shown wider instead of stretched. Menus and the "
     "HUD may still look stretched.",
     "Para pantallas 21:9 y más anchas: la carrera se ve más ancha en vez de estirada. Los menús "
     "y el HUD pueden seguir viéndose estirados."},
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
    {"4:3 borders", "Bordes en 4:3"},
    {"Random", "Aleatorio"},
    {"Black bars beside the game.", "Franjas negras a los lados del juego."},
    {"One picked at random each time the game opens.",
     "Uno elegido al azar cada vez que se abre el juego."},
    {"Pictures beside the game when it is shown in 4:3 on a wider screen. In Netplay too, only "
     "you see your choice.",
     "Imágenes a los lados del juego cuando se ve en 4:3 en una pantalla más ancha. También en "
     "Netplay, solo tú ves tu elección."},
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
    {"Image rejected: the selected file is not a valid disc image.",
     "Imagen rechazada: el archivo elegido no es una imagen de disco válida."},
    {"Image rejected: the prepared disc image could not be opened.",
     "Imagen rechazada: no se pudo abrir la imagen de disco preparada."},
    {"The game's files could not be read from the disc image.",
     "No se pudieron leer los archivos del juego en la imagen de disco."},
    {"the selected image has an invalid disc ID",
     "la imagen elegida tiene un ID de disco no válido"},
    {"this frontend requires disc ID %s; selected %s",
     "este programa necesita el disco con ID %s; el elegido es %s"},
    {"existing extraction patching failed: %s", "falló el parche de la extracción existente: %s"},
    {"Using existing extraction", "Usando la extracción existente"},
    {"can't create extraction directory: %s", "no se pudo crear la carpeta de extracción: %s"},
    {"Extracting system data", "Extrayendo datos del sistema"},
    {"The disc's system data could not be extracted.",
     "No se pudieron extraer los datos del sistema del disco."},
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
     "room is still open. In the same house, use Direct IP with the host's At home address: "
     "invite codes often fail there. If it keeps failing, your connection may not allow invite "
     "codes: try Direct IP with Tailscale.",
     "Ninguna sala respondió a ese código de invitación. Revisa que esté bien escrito y que la "
     "sala del anfitrión siga abierta. En la misma casa, usa IP directa con la dirección En casa "
     "del anfitrión: ahí los códigos de invitación suelen fallar. Si sigue fallando, puede que "
     "tu conexión no permita códigos de invitación: prueba IP directa con Tailscale."},
    {"Could not reach the host. Check the address and the UDP port: in the same house, use the "
     "address the host's lobby shows under At home. If they are right, the host's firewall is "
     "probably blocking the game: on Windows, the host can allow it in Host Netplay; on Linux, "
     "the host opens the UDP port (the README says how).",
     "No se pudo contactar con el anfitrión. Revisa la dirección y el puerto UDP: en la misma "
     "casa, usa la dirección que la sala del anfitrión muestra en En casa. Si están bien, lo más "
     "probable es que el firewall del anfitrión esté bloqueando el juego: en Windows, el "
     "anfitrión puede permitirlo en Crear Netplay; en Linux, el anfitrión abre el puerto UDP (el "
     "README explica cómo)."},
    {"If your friends can't connect, the Windows firewall may be blocking the game. Windows asks "
     "for permission to change it.",
     "Si tus amigos no pueden conectarse, puede que el firewall de Windows esté bloqueando el "
     "juego. Windows pide permiso para cambiarlo."},
    {"Allow the game in the Windows firewall", "Permitir el juego en el firewall de Windows"},
    {"Waiting for Windows...", "Esperando a Windows..."},
    {"Done: Windows lets Netplay connections in.",
     "Listo: Windows deja entrar las conexiones de Netplay."},
    {"Windows did not get permission: nothing changed.",
     "Windows no recibió permiso: no se cambió nada."},
    {"Windows could not change its firewall. The README says how to allow the game by hand.",
     "Windows no pudo cambiar su firewall. El README explica cómo permitir el juego a mano."},
    {"If your friends can't connect, your firewall may be blocking UDP port %d: the README says "
     "how to open it.",
     "Si tus amigos no pueden conectarse, puede que tu firewall esté bloqueando el puerto UDP "
     "%d: el README explica cómo abrirlo."},
    {"Could not create the netplay session. Check the UDP port and firewall settings.",
     "No se pudo crear la sesión de Netplay. Revisa el puerto UDP y el firewall."},
    {"The game process exited with code %d.", "El proceso del juego terminó con el código %d."},
    {"The graphics backend (%s) could not start: your graphics card or its driver may not "
     "support it. Choose another one in Settings, under GRAPHICS, and press PLAY again.",
     "No se pudo iniciar el motor gráfico (%s): puede que tu tarjeta gráfica o su driver no lo "
     "soporten. Elige otro en Ajustes, en GRÁFICOS, y pulsa JUGAR otra vez."},
    {"On Windows, Direct3D 11 works on almost every PC.",
     "En Windows, Direct3D 11 funciona en casi cualquier PC."},
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
    // In-game menu (src/runtime/game_menu_draw.hpp)
    {"Paused", "Pausa"},
    {"The match keeps going while this menu is open.", "La partida sigue mientras este menú está abierto."},
    {"Continue", "Continuar"},
    {"Back to the launcher", "Volver al launcher"},
    {"Back to the lobby", "Volver al lobby"},
    {"Quit game", "Salir del juego"},
    {"Back to the launcher?", "¿Volver al launcher?"},
    {"Back to the lobby?", "¿Volver al lobby?"},
    {"Quit the game?", "¿Salir del juego?"},
    {"Progress the game hasn't saved will be lost.", "Se perderá el progreso que el juego no haya guardado."},
    {"The match ends for every player.", "La partida termina para todos los jugadores."},
    {"No", "No"},
    {"Yes", "Sí"},
    {"Accept", "Aceptar"},
    {"Go back", "Volver"},
    {"Quick settings", "Ajustes rápidos"},
    {"Changes apply at once and are kept for next time.", "Los cambios se aplican al momento y se guardan para la próxima vez."},
    {"Change", "Cambiar"},
    {"Higher looks sharper and needs a faster graphics card.", "Más alta se ve más nítida y pide una tarjeta gráfica más rápida."},
    {"See controls", "Ver controles"},
    {"Take screenshot", "Capturar pantalla"},
    {"Taking screenshot...", "Haciendo la captura..."},
    {"Screenshot saved \xC2\xB7 %d x %d", "Captura guardada \xC2\xB7 %d x %d"},
    {"Couldn't take the screenshot", "No se pudo hacer la captura"},
    // Keys on the in-game menu's Controls page (src/runtime/game_menu_controls.hpp)
    {"Enter", "Intro"},
    {"Left Shift", "Mayús izquierda"},
    {"Right Shift", "Mayús derecha"},
    {"Space", "Espacio"},
    {"Left Ctrl", "Ctrl izquierdo"},
    {"Backspace", "Retroceso"},
    {"Up arrow", "Flecha arriba"},
    {"Down arrow", "Flecha abajo"},
    {"Left arrow", "Flecha izquierda"},
    {"Right arrow", "Flecha derecha"},
    // Screenshots page (tools/launcher_gallery.hpp)
    {"Screenshots", "Capturas"},
    {"Taken with F9 or the in-game menu. Choose one to see it large.", "Hechas con F9 o desde el menú del juego. Elige una para verla en grande."},
    {"Open folder", "Abrir carpeta"},
    {"Screenshots: %d", "Capturas: %d"},
    {"No screenshots yet. Press F9 while playing, or Take screenshot in the in-game menu.", "Todavía no hay capturas. Pulsa F9 mientras juegas, o Capturar pantalla en el menú del juego."},
    {"All screenshots", "Todas las capturas"},
    {"Delete", "Borrar"},
    {"Loading...", "Cargando..."},
    {"This picture can't be read.", "Esta imagen no se puede leer."},
    {"< Newer", "< Más reciente"},
    {"Older >", "Más antigua >"},
    {"Delete this screenshot?", "¿Borrar esta captura?"},
    {"It is removed from your computer for good.", "Se borra de tu equipo para siempre."},
    {"Couldn't delete it: %s", "No se pudo borrar: %s"},
    // Complete save (Game Files, tools/launcher_save.hpp)
    {"COMPLETE SAVE", "PARTIDA COMPLETA"},
    {"The whole game completed, with everything unlocked. Your own save is kept, and Restore my save brings it back.", "El juego completado entero, con todo desbloqueado. Tu partida se guarda aparte y «Recuperar mi partida» la devuelve."},
    {"Play once, until the game makes its save, then come back.", "Juega una vez, hasta que el juego cree su partida, y vuelve aquí."},
    {"Install complete save", "Instalar partida completa"},
    {"Restore my save", "Recuperar mi partida"},
    {"Install the complete save?", "¿Instalar la partida completa?"},
    {"Your progress is replaced by the complete save. A copy of your save is kept first, and Restore my save brings it back.", "Tu progreso se sustituye por la partida completa. Antes se guarda una copia de tu partida, y «Recuperar mi partida» la devuelve."},
    {"Install", "Instalar"},
    {"Bring your save back?", "¿Recuperar tu partida?"},
    {"Your save from before the complete save comes back, in place of the current one.", "Vuelve tu partida de antes de la partida completa, en lugar de la actual."},
    {"Restore", "Recuperar"},
    {"Complete save installed. Your own save is kept in %s.", "Partida completa instalada. Tu partida está guardada en %s."},
    {"Your save couldn't be copied, so nothing changed: %s", "No se pudo copiar tu partida, así que no se cambió nada: %s"},
    {"Couldn't write the save: %s", "No se pudo escribir la partida: %s"},
    {"Your save is back.", "Tu partida ha vuelto."},
    // Audio (Settings)
    {"AUDIO", "AUDIO"},
    {"Also in the in-game menu's Quick settings.", "También en los Ajustes rápidos del menú del juego."},
    {"Launcher and lobby music", "Música del launcher y del lobby"},
    // Window size (Settings)
    {"Window size", "Tamaño de la ventana"},
    {"Automatic", "Automático"},
    {"Fits your screen.", "Se ajusta a tu pantalla."},
    {"The game window's size when it isn't fullscreen. How sharp the game looks is the internal resolution, in Graphics.", "El tamaño de la ventana del juego cuando no está en pantalla completa. Lo nítido que se ve el juego lo decide la resolución interna, en Gráficos."},
    // Screenshots (Settings)
    {"Screenshot resolution", "Resolución de las capturas"},
    {"Same as the game", "Igual que el juego"},
    {"F9 or Take screenshot in the in-game menu. The game is drawn at this resolution just for the picture, never below its own.", "F9 o Capturar pantalla en el menú del juego. El juego se dibuja a esta resolución solo para la foto, nunca por debajo de la suya."},
    // Picture effects and the game's volume (Settings and Quick settings)
    {"Visual effect", "Efecto visual"},
    {"Game volume", "Volumen del juego"},
    {"Drawn over the game's picture. Light enough for integrated graphics.", "Se dibuja sobre la imagen del juego. Es ligero, apto para gráficos integrados."},
    {"The game's picture as it is.", "La imagen del juego tal cual."},
    {"Bloom", "Bloom"},
    {"Lights and bright highlights glow softly.", "Las luces y los brillos resplandecen suavemente."},
    {"Chromatic aberration", "Aberración cromática"},
    {"Red and blue drift apart toward the edges, as through a camera lens.", "El rojo y el azul se separan hacia los bordes, como a través de la lente de una cámara."},
    {"Vivid colors", "Colores vivos"},
    {"Richer colors and a little more contrast.", "Colores más intensos y un poco más de contraste."},
    {"Sharpen", "Nitidez"},
    {"Details and edges stand out more.", "Los detalles y los bordes se ven más definidos."},
    {"Smooth edges (FXAA)", "Suavizar bordes (FXAA)"},
    {"Softens jagged edges.", "Suaviza los bordes dentados."},
    {"CRT", "CRT"},
    {"Scanlines and darker corners, like an old TV.", "Líneas de escaneo y esquinas más oscuras, como un televisor antiguo."},
    {"Outline", "Contorno"},
    {"Dark lines along the edges, like a cartoon. They go round everything on screen, not only the characters.", "Líneas oscuras en los bordes, como en un dibujo animado. Rodean todo lo que hay en pantalla, no solo a los personajes."},
    // Mods & expansions page
    {"Mods & expansions", "Mods y expansiones"},
    {"Open Mods & expansions", "Abrir Mods y expansiones"},
    {"Mods change the game or add to it when it starts, without touching your copy of it.", "Los mods cambian el juego o le añaden cosas al iniciarlo, sin tocar tu copia."},
    {"INSTALLED MODS", "MODS INSTALADOS"},
    {"No mods installed yet.", "Todavía no hay mods instalados."},
    {"Mod packages (.mgm) go in this folder and load when the game starts:", "Los paquetes de mods (.mgm) van en esta carpeta y se cargan al iniciar el juego:"},
    {"Mod support is experimental: SR-ZGS doesn't have mods of its own yet.", "El soporte de mods es experimental: SR-ZGS todavía no tiene mods propios."},
    {"EXPANSIONS", "EXPANSIONES"},
    {"PLANNED", "PLANEADO"},
    {"Story campaigns, characters, tracks and more, made by the community, without changing the original game files.", "Campañas de historia, personajes, pistas y más, creados por la comunidad, sin modificar los archivos originales del juego."},
    {"A long-term goal of the project.", "Una meta a largo plazo del proyecto."},
    // Moving through the launcher with a gamepad
    {"Pages", "Páginas"},
    {"Newer / older", "Más reciente / más antigua"},
    {"Hold to stop testing", "Mantén para dejar de probar"},
    {"Test buttons", "Probar botones"},
};
// clang-format on

// The same texts in Japanese, in the same order and groups.
// clang-format off
inline constexpr Translation JAPANESE_TEXTS[] = {
    // Top bar, sidebar and footer
    {"PLAY", "プレイ"},
    {"Settings", "設定"},
    {"Controls", "操作"},
    {"Game Files", "ゲームファイル"},
    {"Credits", "クレジット"},
    {"LANGUAGE", "言語"},
    {"Off", "オフ"},
    {"System (%s)", "システム（%s）"},
    {"Controller: %s", "コントローラー：%s"},
    // PLAY page
    {"GAME READY \xC2\xB7 %s", "プレイ準備完了 · %s"},
    {"Start from savestate", "ステートセーブから始める"},
    {"Normal boot", "通常起動"},
    {"Latest - %s", "最新 - %s"},
    {"Savestates are available for solo play only.", "ステートセーブは一人プレイでのみ使えます。"},
    {"Host Netplay", "ネットプレイを作成"},
    {"Join Netplay", "ネットプレイに参加"},
    {"Host a session", "セッションを作成"},
    {"Join a session", "セッションに参加"},
    {"INVITE CODE", "招待コード"},
    {"DIRECT IP", "IP直接接続"},
    {"When the room opens you get a code. Your friends type it in Join Netplay, and nobody sees "
     "your IP.",
     "ルームを開くとコードが発行されます。友達が「ネットプレイに参加」でそれを入力すれ"
     "ば、あなたのIPは誰にも見えません。"},
    {"Type the code the host gave you.", "ホストから受け取ったコードを入力してください。"},
    {"Other players connect to this computer's address and UDP port. For the same network or "
     "Tailscale.",
     "ほかのプレイヤーはこのPCのアドレスとUDPポートに接続します。同じネットワーク"
     "内かTailscale向けです。"},
    {"Enter the address and UDP port of the player who hosts. For the same network or Tailscale.",
     "ホストするプレイヤーのアドレスとUDPポートを入力してください。同じネットワーク"
     "内かTailscale向けです。"},
    {"Careful: an IP address identifies your connection. Don't show it on stream or share it in "
     "public.",
     "注意：IPアドレスからあなたの接続が特定できます。配信に映したり、公開の場で共有"
     "したりしないでください。"},
    {"Invite code", "招待コード"},
    {"Invite codes", "招待コード"},
    {"Dolphin's public server (stun.dolphin-emu.org)", "Dolphinの公開サーバー（stun.dolphin-emu.org）"},
    {"An invite code has 8 characters (numbers and letters a-f).", "招待コードは8文字です（数字とa〜fの英字）。"},
    {"Every player needs the same SR-ZGS release and game files. Everyone plays with the host's "
     "CPU core.",
     "全員が同じSR-ZGSのバージョンとゲームファイルを使う必要があります。CPUコ"
     "アは全員ホストのものを使います。"},
    {"Nickname", "ニックネーム"},
    {"Host / IP", "ホスト / IP"},
    {"UDP port", "UDPポート"},
    {"Automatic input buffer", "入力バッファを自動調整"},
    {"Buffer frames", "バッファ（フレーム）"},
    {"Host", "作成"},
    {"Join", "参加"},
    {"Cancel", "キャンセル"},
    {"GAME NOT INSTALLED", "ゲーム未インストール"},
    {"No extracted game is configured yet.", "展開済みのゲームがまだ設定されていません。"},
    {"Open Game Files to select and extract your game.", "「ゲームファイル」でゲームを選んで展開してください。"},
    {"Go to Game Files", "ゲームファイルへ"},
    // PLAY page summary cards
    {"TIME PLAYED", "プレイ時間"},
    {"CONTROLS", "操作"},
    {"DISPLAY", "画面"},
    {"MODS", "MOD"},
    {"Not played yet", "未プレイ"},
    {"Under a minute", "1分未満"},
    {"Keyboard only", "キーボードのみ"},
    {"Keyboard", "キーボード"},
    {"Not detected", "未検出"},
    {"Your controller isn't connected; the keyboard still works.\nOpen Controls",
     "コントローラーが接続されていません。キーボードは使えます。\n「操作」を開く"},
    {"Open Controls", "「操作」を開く"},
    {"Open Settings", "「設定」を開く"},
    {"PATCH NOTES", "パッチノート"},
    {"%s \xC2\xB7 Fullscreen", "%s · フルスクリーン"},
    {"%s \xC2\xB7 Window", "%s · ウィンドウ"},
    {"%s \xC2\xB7 Full", "%s · 全画面"},
    {"%s \xC2\xB7 Win", "%s · ウィンドウ"},
    {"None", "なし"},
    {"1 installed", "1個導入済み"},
    {"%d installed", "%d個導入済み"},
    {"Mods go in %s", "MODは %s に入れてください"},
    // Settings page
    {"Changes are saved right away and apply the next time you press PLAY.",
     "変更はすぐに保存され、次に「プレイ」を押したときに反映されます。"},
    {"CPU CORE", "CPUコア"},
    {"JIT64 (recommended)", "JIT64（推奨）"},
    {"StaticRecomp (experimental)", "StaticRecomp（実験的）"},
    {"JIT64 is the validated full-speed mode. StaticRecomp runs the recompiled game code and is "
     "still being optimized.",
     "JIT64は動作確認済みのフルスピードモードです。StaticRecompは再コ"
     "ンパイルしたゲームコードを実行するモードで、現在も最適化中です。"},
    {"Netplay: everyone plays with the host's CPU core.", "ネットプレイ：全員がホストのCPUコアでプレイします。"},
    {"MODERNGEKKO_STATICRECOMP=%s is set in the environment and overrides this setting.",
     "環境変数 MODERNGEKKO_STATICRECOMP=%s が設定されてい"
     "るため、この設定より優先されます。"},
    {"GRAPHICS", "グラフィック"},
    {"Graphics backend", "グラフィックバックエンド"},
    {"Internal resolution", "内部解像度"},
    {"Aspect ratio", "アスペクト比"},
    {"16:9 (Widescreen)", "16:9（ワイド）"},
    {"Stretch", "引き伸ばし"},
    {"Ultrawide", "ウルトラワイド"},
    {"Ultrawide (experimental)", "ウルトラワイド（試験的）"},
    {"For 21:9 and wider screens: the race is shown wider instead of stretched. Menus and the "
     "HUD may still look stretched.",
     "21:9以上の画面向け：レースを引き伸ばさずに広く表示します。メニューやHUDは引き伸ばされて"
     "見えることがあります。"},
    {"The game draws in widescreen. Best for most monitors and TVs.",
     "ゲームをワイド画面で描画します。ほとんどのモニターやテレビに最適です。"},
    {"The original 4:3 picture, with bars on the sides of a wide window.",
     "オリジナルの4:3画面です。横長のウィンドウでは左右に帯が入ります。"},
    {"The widescreen picture stretched to fill the window, whatever its shape. On a 16:9 screen "
     "it looks the same as 16:9.",
     "ワイド画面をウィンドウの形に合わせて引き伸ばします。16:9の画面では16:9と"
     "同じ表示になります。"},
    {"Netplay: the host's 16:9 or 4:3 choice applies to every player.",
     "ネットプレイ：ホストが選んだ16:9か4:3が全員に適用されます。"},
    {"4:3 borders", "4:3の壁紙"},
    {"Random", "ランダム"},
    {"Black bars beside the game.", "ゲームの左右は黒い帯のままです。"},
    {"One picked at random each time the game opens.",
     "ゲームを起動するたびにランダムに選ばれます。"},
    {"Pictures beside the game when it is shown in 4:3 on a wider screen. In Netplay too, only "
     "you see your choice.",
     "4:3の映像を横長の画面に表示するとき、左右に出す画像です。ネットプレイでも、選んだ"
     "壁紙は自分の画面だけに表示されます。"},
    {"Start in fullscreen", "フルスクリーンで起動"},
    {"Show FPS in window title", "ウィンドウタイトルにFPSを表示"},
    // Credits page
    {"SR-ZGS is an independent fan project. It is not affiliated with or endorsed by SEGA.",
     "SR-ZGSは非公式のファンプロジェクトです。セガとは関係がなく、セガの承認も受"
     "けていません。"},
    {"CREATED BY", "制作"},
    {"With the SR-ZGS contributors and testers: research, development and testing.",
     "SR-ZGSの協力者とテスターの皆さんとともに：調査、開発、テスト。"},
    {"BUILT ON", "使用技術"},
    {"Static recompilation framework", "静的再コンパイルのフレームワーク"},
    {"Runtime for GameCube/Wii recomps, by Hyperway", "GameCube/Wii再コンパイル用ランタイム（Hyperway制作）"},
    {"Recomp template", "再コンパイル用テンプレート"},
    {"Literally God / MrPoloGit, who also added macOS support",
     "Literally God / MrPoloGit（macOS対応も追加）"},
    {"The Dolphin Team and its contributors", "Dolphinチームと協力者の皆さん"},
    {"LICENSES", "ライセンス"},
    {"GNU GPL v2 or later", "GNU GPL v2以降"},
    {"zlib License", "zlibライセンス"},
    {"MIT License", "MITライセンス"},
    {"Sonic Riders: Zero Gravity and its characters are trademarks of SEGA. Play with your own "
     "legally obtained copy of the game.",
     "Sonic Riders: Zero Gravity（ソニックライダーズ シュー"
     "ティングスターストーリー）とそのキャラクターはセガの商標です。正規に入手したご自"
     "身のゲームでプレイしてください。"},
    {"Opened in your browser and copied to the clipboard", "ブラウザで開き、クリップボードにコピーしました"},
    // Check for updates: the Credits page's VERSION panel and the top bar
    {"VERSION", "バージョン"},
    {"Version %s", "バージョン %s"},
    {"Check for updates", "アップデートを確認"},
    {"Looking on GitHub...", "GitHubを確認中..."},
    {"You have the latest version.", "最新バージョンです。"},
    {"A new version is available: %s", "新しいバージョンがあります：%s"},
    {"Open the download page", "ダウンロードページを開く"},
    {"No version has been published on GitHub yet.", "GitHubにはまだバージョンが公開されていません。"},
    {"Couldn't check. Check your internet connection.", "確認できませんでした。インターネット接続を確認してください。"},
    {"Check for updates when the launcher opens", "ランチャー起動時にアップデートを確認"},
    {"NEW VERSION \xC2\xB7 %s", "新バージョン · %s"},
    // Controls page
    {"Choose the controller to play with. The launcher writes its button mapping for you.",
     "使うコントローラーを選んでください。ボタン設定はランチャーが自動で作成します。"},
    {"CONTROLLER", "コントローラー"},
    {"CONNECTED", "接続中"},
    {"KEYBOARD ONLY", "キーボードのみ"},
    {"NOT DETECTED", "未検出"},
    {"No SDL gamepad detected", "SDLゲームパッドが見つかりません"},
    {"Ready to generate controller profile", "コントローラーのプロファイルを作成できます"},
    {"BUTTON MAPPING", "ボタン設定"},
    {"Using %s from your user folder.", "ユーザーフォルダーの %s を使用しています。"},
    {"%s is created the first time you play.", "%s は初回プレイ時に作成されます。"},
    {"Replace with generated GameCube profile", "自動作成したゲームキューブ用プロファイルに置き換える"},
    {"Replace with generated sideways profile", "自動作成した横持ち用プロファイルに置き換える"},
    {"1 GameCube controller mapped", "ゲームキューブコントローラー1台を設定済み"},
    {"1 sideways Wii Remote mapped", "横持ちWiiリモコン1台を設定済み"},
    {"%s now uses %s with the same buttons", "%s は同じボタン配置のまま %s を使います"},
    {"Keyboard only. A controller you connect is added to the same profile.",
     "キーボードのみ。コントローラーを接続すると同じプロファイルに追加されます。"},
    {"Using existing %s", "既存の %s を使用中"},
    {"%s has no configured controller device", "%s にはコントローラーが設定されていません"},
    {"Connect an SDL-compatible controller before playing", "プレイする前にSDL対応コントローラーを接続してください"},
    // Controls page: button tester
    {"Controller", "コントローラー"},
    {"Key", "キー"},
    {"Your controller", "コントローラー"},
    {"edited by hand", "手動で編集"},
    {"not set", "未設定"},
    {"Press keys to test them.", "キーを押してテストできます。"},
    {"Press buttons to test them.", "ボタンを押してテストできます。"},
    {"No controller in %s.", "%s にコントローラーがありません。"},
    {"Connect %s.", "%s を接続してください。"},
    {"Stick", "スティック"},
    {"Menu", "メニュー"},
    {"D-Pad", "十字キー"},
    {"Shake", "振る"},
    {"L trigger", "Lトリガー"},
    {"R trigger", "Rトリガー"},
    {"Left stick left", "左スティック左"},
    {"Left stick right", "左スティック右"},
    {"Left stick up", "左スティック上"},
    {"Left stick down", "左スティック下"},
    {"Right stick left", "右スティック左"},
    {"Right stick right", "右スティック右"},
    {"Right stick up", "右スティック上"},
    {"Right stick down", "右スティック下"},
    {"Cross", "×ボタン"},
    {"Circle", "○ボタン"},
    {"Square", "□ボタン"},
    {"Triangle", "△ボタン"},
    {"South button", "下ボタン"},
    {"East button", "右ボタン"},
    {"West button", "左ボタン"},
    {"North button", "上ボタン"},
    {"Back", "バック"},
    {"L stick press", "Lスティック押し込み"},
    {"R stick press", "Rスティック押し込み"},
    {"L shoulder", "Lショルダー"},
    {"R shoulder", "Rショルダー"},
    {"D-pad up", "十字キー上"},
    {"D-pad down", "十字キー下"},
    {"D-pad left", "十字キー左"},
    {"D-pad right", "十字キー右"},
    {"Arrow keys", "矢印キー"},
    {"D-pad", "十字キー"},
    {"Left stick", "左スティック"},
    {"Right stick", "右スティック"},
    // Game Files page and disc extraction
    {"Use your own copy of the game as an ISO, WBFS or RVZ image. It is extracted once and "
     "reused afterwards.",
     "ご自身のゲームをISO、WBFS、RVZイメージで用意してください。展開は一度だ"
     "けで、以降はそのまま使われます。"},
    {"INSTALLED GAME", "インストール済みのゲーム"},
    {"NOT INSTALLED", "未インストール"},
    {"Select a disc image below and extract it.", "下でディスクイメージを選んで展開してください。"},
    {"DISC IMAGE", "ディスクイメージ"},
    {"No ISO, WBFS, or RVZ selected", "ISO、WBFS、RVZが選択されていません"},
    {"Browse for ISO / WBFS / RVZ", "ISO / WBFS / RVZを選ぶ"},
    {"Disc images", "ディスクイメージ"},
    {"Extract Game", "ゲームを展開"},
    {"Opening %s", "%s を開いています"},
    {"Applying Tournament Edition 2.4.6.1", "Tournament Edition 2.4.6.1を適用中"},
    {"can't start Tournament Edition patcher: %s", "Tournament Editionのパッチツールを起動できません：%s"},
    {"Tournament Edition patching failed", "Tournament Editionのパッチに失敗しました"},
    {"this disc is not the pinned patched release", "このディスクは指定のパッチ済みバージョンではありません"},
    {"Image rejected: the selected file is not a valid disc image.",
     "イメージを読み込めません：選択したファイルは有効なディスクイメージではありません。"},
    {"Image rejected: the prepared disc image could not be opened.",
     "イメージを読み込めません：準備したディスクイメージを開けませんでした。"},
    {"The game's files could not be read from the disc image.",
     "ディスクイメージからゲームのファイルを読み込めませんでした。"},
    {"the selected image has an invalid disc ID", "選択したイメージのディスクIDが正しくありません"},
    {"this frontend requires disc ID %s; selected %s", "このランチャーにはディスクID %s が必要です（選択したもの：%s）"},
    {"existing extraction patching failed: %s", "既存の展開データへのパッチに失敗しました：%s"},
    {"Using existing extraction", "既存の展開データを使用中"},
    {"can't create extraction directory: %s", "展開先フォルダーを作成できません：%s"},
    {"Extracting system data", "システムデータを展開中"},
    {"The disc's system data could not be extracted.", "ディスクのシステムデータを展開できませんでした。"},
    {"Extracting game files", "ゲームファイルを展開中"},
    {"Extracting %s", "%s を展開中"},
    {"extracted DOL patching failed: %s", "展開したDOLへのパッチに失敗しました：%s"},
    {"extracted game validation failed: %s", "展開したゲームの確認に失敗しました：%s"},
    {"can't publish extracted game: %s", "展開したゲームを保存できません：%s"},
    {"Extraction complete", "展開が完了しました"},
    {"expected disc ID %s, found %s", "ディスクID %s が必要ですが、%s でした"},
    {"main DOL does not match the pinned release", "メインDOLが指定のバージョンと一致しません"},
    {"_Main.rel does not match the pinned release", "_Main.relが指定のバージョンと一致しません"},
    {"game assets do not match the pinned release", "ゲームデータが指定のバージョンと一致しません"},
    // Starting the game
    {"Launch failed", "起動に失敗しました"},
    {"Could not start %s: %s", "%s を起動できませんでした：%s"},
    {"The game process could not be monitored: %s", "ゲームのプロセスを監視できませんでした：%s"},
    {"The host is running an incompatible netplay build. Both players must use the same "
     "release.",
     "ホストのネットプレイ版と互換性がありません。両方のプレイヤーが同じバージョンを使"
     "う必要があります。"},
    {"The extracted game or recomp module does not match the host. Both players need the same "
     "game revision and release.",
     "展開したゲームまたは再コンパイルモジュールがホストと一致しません。両方のプレイヤ"
     "ーに同じゲームのリビジョンとバージョンが必要です。"},
    {"All four controller slots are already occupied.", "4つのコントローラー枠はすべて埋まっています。"},
    {"The host has already started the game.", "ホストはすでにゲームを開始しています。"},
    {"The netplay server is full.", "ネットプレイのサーバーが満員です。"},
    {"The nickname was rejected by the host.", "ホストにニックネームを拒否されました。"},
    {"No room answered to that invite code. Check that it is typed right and that the host's "
     "room is still open. In the same house, use Direct IP with the host's At home address: "
     "invite codes often fail there. If it keeps failing, your connection may not allow invite "
     "codes: try Direct IP with Tailscale.",
     "その招待コードに応答するルームがありませんでした。入力が正しいか、ホストのルーム"
     "がまだ開いているか確認してください。同じ家の中では、ホストの「自宅」のアドレスで"
     "IP直接接続を使ってください。招待コードはそこではよく失敗します。それでも失敗する"
     "場合は、お使いの接続が招待コードに対応していない可能性があります。TailscaleでのI"
     "P直接接続をお試しください。"},
    {"Could not reach the host. Check the address and the UDP port: in the same house, use the "
     "address the host's lobby shows under At home. If they are right, the host's firewall is "
     "probably blocking the game: on Windows, the host can allow it in Host Netplay; on Linux, "
     "the host opens the UDP port (the README says how).",
     "ホストに接続できませんでした。アドレスとUDPポートを確認してください。同じ家の中では"
     "、ホストのロビーの「自宅」に表示されるアドレスを使ってください。正しい場合は、ホスト"
     "のファイアウォールがゲームをブロックしている可能性が高いです。Windowsでは、ホストが"
     "「ネットプレイを作成」で許可できます。Linuxでは、ホストがUDPポートを開きます（方法は"
     "READMEにあります）。"},
    {"If your friends can't connect, the Windows firewall may be blocking the game. Windows asks "
     "for permission to change it.",
     "友達が接続できない場合は、Windowsのファイアウォールがゲームをブロックしている可能性"
     "があります。変更にはWindowsが許可を求めます。"},
    {"Allow the game in the Windows firewall", "Windowsのファイアウォールでゲームを許可"},
    {"Waiting for Windows...", "Windowsを待っています..."},
    {"Done: Windows lets Netplay connections in.",
     "完了：Windowsはネットプレイの接続を受け入れます。"},
    {"Windows did not get permission: nothing changed.",
     "Windowsは許可を得られませんでした。何も変更されていません。"},
    {"Windows could not change its firewall. The README says how to allow the game by hand.",
     "Windowsはファイアウォールを変更できませんでした。手動で許可する方法はREADMEにあります。"},
    {"If your friends can't connect, your firewall may be blocking UDP port %d: the README says "
     "how to open it.",
     "友達が接続できない場合は、ファイアウォールがUDPポート%dをブロックしている可能性があり"
     "ます。開く方法はREADMEにあります。"},
    {"Could not create the netplay session. Check the UDP port and firewall settings.",
     "ネットプレイのセッションを作成できませんでした。UDPポートとファイアウォールの"
     "設定を確認してください。"},
    {"The game process exited with code %d.", "ゲームのプロセスがコード %d で終了しました。"},
    {"The graphics backend (%s) could not start: your graphics card or its driver may not "
     "support it. Choose another one in Settings, under GRAPHICS, and press PLAY again.",
     "グラフィックバックエンド（%s）を開始できませんでした。お使いのグラフィックカードまたはドライバーが"
     "対応していない可能性があります。設定の「グラフィック」で別のものを選び、もう一度「プレイ」を押して"
     "ください。"},
    {"On Windows, Direct3D 11 works on almost every PC.",
     "Windowsでは、Direct3D 11はほとんどのPCで動作します。"},
    {"\n\nDetails were written to:\n%s", "\n\n詳細の保存先：\n%s"},
    {"Session failed", "セッションに失敗しました"},
    // Netplay lobby, top to bottom
    {"HOST", "ホスト"},
    {"CPU core for every player: %s", "全員のCPUコア：%s"},
    {"CPU core: the host's choice", "CPUコア：ホストの設定"},
    {"Input buffer: %u frames%s", "入力バッファ：%u フレーム%s"},
    {"INVITE YOUR FRIENDS", "友達を招待"},
    {"Show", "表示"},
    {"Hide", "隠す"},
    {"At home", "自宅"},
    {"Internet (Tailscale)", "インターネット（Tailscale）"},
    {"Internet (public)", "インターネット（公開）"},
    {"Copy", "コピー"},
    {"Copied", "コピーしました"},
    {"Your friends type this address in Join Netplay.", "友達は「ネットプレイに参加」でこのアドレスを入力します。"},
    {"Your friends type it in Join Netplay, under Invite code. Anyone who sees it can join: keep "
     "it off your stream.",
     "友達は「ネットプレイに参加」の招待コード欄にこれを入力します。見た人は誰でも参加"
     "できるので、配信には映さないでください。"},
    {"Asking the invite server for a code...", "招待サーバーにコードを問い合わせ中..."},
    {"The invite server did not answer. Check the internet connection, or close the room and "
     "host with Direct IP.",
     "招待サーバーから応答がありません。インターネット接続を確認するか、ルームを閉じて"
     "IP直接接続で作成してください。"},
    {"No network found. Connect this computer to your Wi-Fi or to Tailscale.",
     "ネットワークが見つかりません。このPCをWi-FiかTailscaleに接続して"
     "ください。"},
    {"Waiting for the host to start the match.", "ホストが試合を始めるのを待っています。"},
    {"PLAYERS  \xC2\xB7  %d / 4", "プレイヤー  ·  %d / 4"},
    {"Free", "空き"},
    {"(you)", "（あなた）"},
    {"(host)", "（ホスト）"},
    {"READY", "準備完了"},
    {"NOT READY", "準備中"},
    {"OTHER GAME", "別のゲーム"},
    {"THIS COMPUTER", "このPC"},
    {"Local sideways Wii Remotes", "このPCの横持ちWiiリモコン"},
    {"No controller connected: you play with the keyboard.", "コントローラーが接続されていません：キーボードでプレイします。"},
    {"No controller connected.", "コントローラーが接続されていません。"},
    {"(the first one also answers to the keyboard)", "（1台目はキーボードでも操作できます）"},
    {"I'M READY", "準備OK"},
    {"READY  \xC2\xB7  CANCEL", "準備OK  ·  取り消す"},
    {"START MATCH", "試合開始"},
    {"Two controller slots and every machine ready are required", "コントローラー枠2つと全員の準備完了が必要です"},
    {"Press I'M READY when your controller is set.", "コントローラーの準備ができたら「準備OK」を押してください。"},
    // Netplay lobby events
    {"Game: %s", "ゲーム：%s"},
    {"%s joined", "%s が参加しました"},
    {"%s left", "%s が退出しました"},
    {"Desync at frame %u", "フレーム %u で同期ずれ"},
    {"Desync at frame %u reported for %s", "フレーム %u で同期ずれ（%s で検出）"},
    {"Connection to the netplay host was lost", "ホストとの接続が切れました"},
    {"Game start was aborted", "ゲームの開始が中止されました"},
    // In-game menu (src/runtime/game_menu_draw.hpp)
    {"Paused", "一時停止中"},
    {"The match keeps going while this menu is open.", "このメニューを開いている間も試合は続きます。"},
    {"Continue", "続ける"},
    {"Back to the launcher", "ランチャーに戻る"},
    {"Back to the lobby", "ロビーに戻る"},
    {"Quit game", "ゲームを終了"},
    {"Back to the launcher?", "ランチャーに戻りますか？"},
    {"Back to the lobby?", "ロビーに戻りますか？"},
    {"Quit the game?", "ゲームを終了しますか？"},
    {"Progress the game hasn't saved will be lost.", "ゲームで保存していない進行状況は失われます。"},
    {"The match ends for every player.", "全員の試合が終了します。"},
    {"No", "いいえ"},
    {"Yes", "はい"},
    {"Accept", "決定"},
    {"Go back", "戻る"},
    {"Quick settings", "クイック設定"},
    {"Changes apply at once and are kept for next time.", "変更はすぐに反映され、次回も保持されます。"},
    {"Change", "変更"},
    {"Higher looks sharper and needs a faster graphics card.", "高いほど鮮明になりますが、より高性能なグラフィックスが必要です。"},
    {"See controls", "操作を見る"},
    {"Take screenshot", "スクリーンショット"},
    {"Taking screenshot...", "スクリーンショットを撮影中..."},
    {"Screenshot saved \xC2\xB7 %d x %d", "スクリーンショットを保存しました \xC2\xB7 %d x %d"},
    {"Couldn't take the screenshot", "スクリーンショットを撮れませんでした"},
    // Keys on the in-game menu's Controls page (src/runtime/game_menu_controls.hpp)
    {"Enter", "Enter"},
    {"Left Shift", "左Shift"},
    {"Right Shift", "右Shift"},
    {"Space", "スペース"},
    {"Left Ctrl", "左Ctrl"},
    {"Backspace", "BackSpace"},
    {"Up arrow", "上矢印"},
    {"Down arrow", "下矢印"},
    {"Left arrow", "左矢印"},
    {"Right arrow", "右矢印"},
    // Screenshots page (tools/launcher_gallery.hpp)
    {"Screenshots", "スクリーンショット"},
    {"Taken with F9 or the in-game menu. Choose one to see it large.", "F9キーまたはゲーム内メニューで撮影したものです。選ぶと大きく表示します。"},
    {"Open folder", "フォルダーを開く"},
    {"Screenshots: %d", "スクリーンショット：%d枚"},
    {"No screenshots yet. Press F9 while playing, or Take screenshot in the in-game menu.", "スクリーンショットはまだありません。プレイ中にF9キーを押すか、ゲーム内メニューの「スクリーンショット」を選んでください。"},
    {"All screenshots", "一覧に戻る"},
    {"Delete", "削除"},
    {"Loading...", "読み込み中..."},
    {"This picture can't be read.", "この画像は読み込めません。"},
    {"< Newer", "< 新しい"},
    {"Older >", "古い >"},
    {"Delete this screenshot?", "このスクリーンショットを削除しますか？"},
    {"It is removed from your computer for good.", "パソコンから完全に削除されます。"},
    {"Couldn't delete it: %s", "削除できませんでした：%s"},
    // Complete save (Game Files, tools/launcher_save.hpp)
    {"COMPLETE SAVE", "完全セーブデータ"},
    {"The whole game completed, with everything unlocked. Your own save is kept, and Restore my save brings it back.", "ゲームを完全にクリアし、すべてを解放したセーブデータです。あなたのセーブデータは別に保存され、「自分のセーブデータに戻す」で元に戻せます。"},
    {"Play once, until the game makes its save, then come back.", "一度ゲームを起動してセーブデータを作成してから、ここに戻ってください。"},
    {"Install complete save", "完全セーブデータをインストール"},
    {"Restore my save", "自分のセーブデータに戻す"},
    {"Install the complete save?", "完全セーブデータをインストールしますか？"},
    {"Your progress is replaced by the complete save. A copy of your save is kept first, and Restore my save brings it back.", "進行状況が完全セーブデータに置き換わります。先にあなたのセーブデータのコピーが保存され、「自分のセーブデータに戻す」で元に戻せます。"},
    {"Install", "インストール"},
    {"Bring your save back?", "セーブデータを元に戻しますか？"},
    {"Your save from before the complete save comes back, in place of the current one.", "完全セーブデータを入れる前のセーブデータが、今のセーブデータの代わりに戻ります。"},
    {"Restore", "元に戻す"},
    {"Complete save installed. Your own save is kept in %s.", "完全セーブデータをインストールしました。あなたのセーブデータは %s に保存されています。"},
    {"Your save couldn't be copied, so nothing changed: %s", "セーブデータをコピーできなかったため、何も変更していません：%s"},
    {"Couldn't write the save: %s", "セーブデータを書き込めませんでした：%s"},
    {"Your save is back.", "セーブデータを元に戻しました。"},
    // Audio (Settings)
    {"AUDIO", "オーディオ"},
    {"Also in the in-game menu's Quick settings.", "ゲーム内メニューのクイック設定でも変更できます。"},
    {"Launcher and lobby music", "ランチャーとロビーの音楽"},
    // Window size (Settings)
    {"Window size", "ウィンドウサイズ"},
    {"Automatic", "自動"},
    {"Fits your screen.", "画面に合わせます。"},
    {"The game window's size when it isn't fullscreen. How sharp the game looks is the internal resolution, in Graphics.", "全画面でないときのゲームウィンドウの大きさです。ゲームの鮮明さは「グラフィック」の内部解像度で決まります。"},
    // Screenshots (Settings)
    {"Screenshot resolution", "スクリーンショットの解像度"},
    {"Same as the game", "ゲームと同じ"},
    {"F9 or Take screenshot in the in-game menu. The game is drawn at this resolution just for the picture, never below its own.", "F9 またはゲーム内メニューの「スクリーンショット」で撮影します。撮影の間だけゲームをこの解像度で描画し、ゲームの解像度より低くはなりません。"},
    // Picture effects and the game's volume (Settings and Quick settings)
    {"Visual effect", "映像エフェクト"},
    {"Game volume", "ゲームの音量"},
    {"Drawn over the game's picture. Light enough for integrated graphics.", "ゲームの映像の上に描画されます。内蔵グラフィックスでも動く軽さです。"},
    {"The game's picture as it is.", "ゲームの映像をそのまま表示します。"},
    {"Bloom", "ブルーム"},
    {"Lights and bright highlights glow softly.", "光や明るい部分がやわらかく輝きます。"},
    {"Chromatic aberration", "色収差"},
    {"Red and blue drift apart toward the edges, as through a camera lens.", "カメラのレンズのように、画面の端に向かって赤と青がずれます。"},
    {"Vivid colors", "鮮やかな色"},
    {"Richer colors and a little more contrast.", "色がより鮮やかになり、コントラストが少し上がります。"},
    {"Sharpen", "シャープ"},
    {"Details and edges stand out more.", "細部や輪郭がくっきりします。"},
    {"Smooth edges (FXAA)", "エッジを滑らかに（FXAA）"},
    {"Softens jagged edges.", "ギザギザした輪郭を滑らかにします。"},
    {"CRT", "CRT"},
    {"Scanlines and darker corners, like an old TV.", "古いテレビのような走査線と暗い四隅。"},
    {"Outline", "輪郭線"},
    {"Dark lines along the edges, like a cartoon. They go round everything on screen, not only the characters.", "アニメのように輪郭に暗い線を引きます。キャラクターだけでなく、画面のすべてに線が付きます。"},
    // Mods & expansions page
    {"Mods & expansions", "MODと拡張"},
    {"Open Mods & expansions", "「MODと拡張」を開く"},
    {"Mods change the game or add to it when it starts, without touching your copy of it.", "MODはゲームの起動時に内容を変えたり追加したりします。あなたのゲームのコピーは変更しません。"},
    {"INSTALLED MODS", "導入済みのMOD"},
    {"No mods installed yet.", "MODはまだ導入されていません。"},
    {"Mod packages (.mgm) go in this folder and load when the game starts:", ".mgm形式のMODパッケージをこのフォルダーに入れると、ゲームの起動時に読み込まれます："},
    {"Mod support is experimental: SR-ZGS doesn't have mods of its own yet.", "MODへの対応は実験的です。SR-ZGS用のMODはまだありません。"},
    {"EXPANSIONS", "拡張コンテンツ"},
    {"PLANNED", "予定"},
    {"Story campaigns, characters, tracks and more, made by the community, without changing the original game files.", "ストーリー、キャラクター、コースなど、コミュニティが作るコンテンツ。元のゲームファイルは変更しません。"},
    {"A long-term goal of the project.", "プロジェクトの長期的な目標です。"},
    // Moving through the launcher with a gamepad
    {"Pages", "ページ"},
    {"Newer / older", "新しい / 古い"},
    {"Hold to stop testing", "長押しでテスト終了"},
    {"Test buttons", "ボタンをテスト"},
};
// clang-format on

// Atomic because the disc extraction and netplay threads translate their
// status messages too, while the menu can change the language.
inline std::atomic<Language>& ActiveLanguage()
{
  static std::atomic<Language> language = Language::English;
  return language;
}

// config.ini's "language" setting ("auto", "en", "es" or "ja") and the
// system's language code (such as "es" from es_VE.UTF-8): "auto" follows the
// system when it is Spanish or Japanese, and English otherwise.
inline Language ResolveLanguage(std::string_view setting, std::string_view system_language)
{
  if (setting == "en")
    return Language::English;
  if (setting == "es")
    return Language::Spanish;
  if (setting == "ja")
    return Language::Japanese;
  if (system_language.starts_with("es"))
    return Language::Spanish;
  if (system_language.starts_with("ja"))
    return Language::Japanese;
  return Language::English;
}

// Each language's code, as config.ini writes it ("en", "es", "ja").
inline const char* LanguageCode(Language language)
{
  switch (language)
  {
  case Language::Spanish:
    return "es";
  case Language::Japanese:
    return "ja";
  case Language::English:
    break;
  }
  return "en";
}

// Each language's own name, as the language menu shows it.
inline const char* LanguageName(Language language)
{
  switch (language)
  {
  case Language::Spanish:
    return "Espa\xC3\xB1ol";
  case Language::Japanese:
    return "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";  // Nihongo
  case Language::English:
    break;
  }
  return "English";
}

namespace launcher_text_detail
{
template <std::size_t N>
std::unordered_map<std::string_view, const char*> TranslationTable(const Translation (&texts)[N])
{
  std::unordered_map<std::string_view, const char*> table;
  for (const Translation& entry : texts)
    table.emplace(entry.english, entry.translated);
  return table;
}
}  // namespace launcher_text_detail

inline const char* Tr(const char* english)
{
  const Language language = ActiveLanguage();
  if (language == Language::English)
    return english;
  static const auto spanish = launcher_text_detail::TranslationTable(SPANISH_TEXTS);
  static const auto japanese = launcher_text_detail::TranslationTable(JAPANESE_TEXTS);
  const auto& table = language == Language::Japanese ? japanese : spanish;
  const auto found = table.find(english);
  return found != table.end() ? found->second : english;
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
