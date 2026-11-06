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
    {"MUSIC", "音楽"},
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
     "room is still open. If it keeps failing, the invite server may be down or your connection "
     "may not allow it: try Direct IP with Tailscale.",
     "その招待コードに応答するルームがありませんでした。入力が正しいか、ホストのルーム"
     "がまだ開いているか確認してください。それでも失敗する場合は、招待サーバーが停止し"
     "ているか、お使いの接続が対応していない可能性があります。TailscaleでのI"
     "P直接接続をお試しください。"},
    {"Could not reach the netplay host. Check the host name, UDP port, and firewall.",
     "ホストに接続できませんでした。ホスト名、UDPポート、ファイアウォールを確認して"
     "ください。"},
    {"Could not create the netplay session. Check the UDP port and firewall settings.",
     "ネットプレイのセッションを作成できませんでした。UDPポートとファイアウォールの"
     "設定を確認してください。"},
    {"The game process exited with code %d.", "ゲームのプロセスがコード %d で終了しました。"},
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
