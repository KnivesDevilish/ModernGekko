// The launcher's translations stay in step with its code:
//  - every text the sources mark, as Tr("...") or MODERNGEKKO_UI_TEXT("..."),
//    has a Spanish and a Japanese entry in tools/launcher_text.hpp;
//  - every entry is still used somewhere;
//  - both sides of an entry have the same printf placeholders (%s, %d...),
//    so Format() gets the values it expects in any language;
//  - every character of the Japanese texts is in the embedded Japanese font
//    (tools/launcher_cjk_font.inc lists them).
#include "launcher_text.hpp"

#include <cctype>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

#ifndef MODERNGEKKO_SOURCE_DIR
#error "MODERNGEKKO_SOURCE_DIR must point at the repository"
#endif

namespace
{
constexpr const char* SOURCES[] = {
    "tools/moderngekko_launcher.cpp",
    "tools/frontend_config.cpp",
    "tools/launcher_summary.hpp",
    "tools/launcher_ui.hpp",
    "tools/netplay_lobby_ui.hpp",
    "tools/netplay_session.cpp",
    "tools/moderngekko_run.cpp",
    "src/runtime/game_menu_draw.hpp",
    "src/runtime/game_menu_controls.hpp",
    "src/runtime/game_menu.cpp",
};

int HexValue(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

// Reads the C++ string literal that starts at text[pos] (a '"'), resolving
// the escapes the sources use. pos ends just past the closing quote.
bool ReadLiteral(const std::string& text, std::size_t& pos, std::string& out)
{
  ++pos;
  while (pos < text.size() && text[pos] != '"')
  {
    if (text[pos] != '\\')
    {
      out += text[pos++];
      continue;
    }
    if (++pos >= text.size())
      return false;
    const char escape = text[pos++];
    switch (escape)
    {
    case 'n':
      out += '\n';
      break;
    case 't':
      out += '\t';
      break;
    case '\\':
    case '"':
    case '\'':
      out += escape;
      break;
    case 'x':
    {
      // Like the compiler: every hex digit that follows belongs to it.
      int value = 0;
      int digits = 0;
      while (pos < text.size() && HexValue(text[pos]) >= 0)
      {
        value = value * 16 + HexValue(text[pos++]);
        ++digits;
      }
      if (digits == 0)
        return false;
      out += static_cast<char>(value);
      break;
    }
    default:
      return false;
    }
  }
  if (pos >= text.size())
    return false;
  ++pos;
  return true;
}

// The argument of a marker call that starts at pos, when it is made only of
// string literals (adjacent ones joined, as the compiler does).
bool ReadLiteralArgument(const std::string& text, std::size_t pos, std::string& out)
{
  bool any = false;
  while (pos < text.size())
  {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])))
      ++pos;
    if (pos >= text.size() || text[pos] != '"')
      break;
    if (!ReadLiteral(text, pos, out))
      return false;
    any = true;
  }
  return any && pos < text.size() && text[pos] == ')';
}

bool IsIdentifierChar(char c)
{
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ':';
}

void CollectMarked(const std::string& text, const std::string& marker,
                   std::set<std::string>& used)
{
  for (std::size_t pos = text.find(marker); pos != std::string::npos;
       pos = text.find(marker, pos + 1))
  {
    // "Tr(" but not "SomeTr(" or "moderngekko::frontend::Tr(" twice.
    if (pos > 0 && IsIdentifierChar(text[pos - 1]) && marker == "Tr(")
      continue;
    std::string value;
    if (ReadLiteralArgument(text, pos + marker.size(), value))
      used.insert(value);
  }
}

// The code points of a UTF-8 text (invalid bytes come out as themselves).
std::vector<char32_t> DecodeUtf8(const std::string& text)
{
  std::vector<char32_t> out;
  for (std::size_t i = 0; i < text.size();)
  {
    const auto byte = static_cast<unsigned char>(text[i]);
    const int length = byte < 0x80 ? 1 : byte >= 0xF0 ? 4 : byte >= 0xE0 ? 3 : byte >= 0xC0 ? 2 : 1;
    char32_t c = length == 1 ? byte : byte & (0x7F >> length);
    for (int k = 1; k < length && i + k < text.size(); ++k)
      c = (c << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
    out.push_back(c);
    i += length;
  }
  return out;
}

// The printf conversions of a text, in order: "%s", "%d"...
std::vector<std::string> Placeholders(const std::string& text)
{
  std::vector<std::string> found;
  for (std::size_t i = 0; i < text.size(); ++i)
  {
    if (text[i] != '%')
      continue;
    if (i + 1 < text.size() && text[i + 1] == '%')
    {
      ++i;
      continue;
    }
    std::size_t end = i + 1;
    while (end < text.size() && std::strchr("-+ #0123456789.hlLzjt", text[end]))
      ++end;
    if (end < text.size())
      found.push_back(text.substr(i, end - i + 1));
    i = end;
  }
  return found;
}
}  // namespace

int main()
{
  using namespace moderngekko::frontend;
  int result = 0;

  std::set<std::string> used;
  for (const char* source : SOURCES)
  {
    std::ifstream file(std::string(MODERNGEKKO_SOURCE_DIR) + "/" + source);
    if (!file)
    {
      std::cerr << "can't read " << source << '\n';
      return 10;
    }
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    CollectMarked(text, "Tr(", used);
    CollectMarked(text, "MODERNGEKKO_UI_TEXT(", used);
  }
  if (used.size() < 50)
  {
    std::cerr << "only " << used.size() << " marked texts found; is the scan broken?\n";
    return 11;
  }

  const auto check_language = [&](const char* name, std::span<const Translation> texts)
  {
    std::map<std::string, std::string> table;
    for (const Translation& entry : texts)
    {
      if (!table.emplace(entry.english, entry.translated).second)
      {
        std::cerr << name << ": listed twice: \"" << entry.english << "\"\n";
        result = result ? result : 6;
      }
    }
    for (const std::string& text : used)
    {
      if (!table.contains(text))
      {
        std::cerr << "no " << name << " text for: \"" << text << "\"\n";
        result = result ? result : 1;
      }
    }
    for (const auto& [english, translated] : table)
    {
      if (!used.contains(english))
      {
        std::cerr << name << ": translated but no longer used: \"" << english << "\"\n";
        result = result ? result : 2;
      }
      if (Placeholders(english) != Placeholders(translated))
      {
        std::cerr << name << ": placeholders differ: \"" << english << "\" / \"" << translated
                  << "\"\n";
        result = result ? result : 3;
      }
    }
  };
  check_language("Spanish", SPANISH_TEXTS);
  check_language("Japanese", JAPANESE_TEXTS);

  // Every character of the Japanese texts that the interface font lacks
  // (anything past Latin-1) must be in the embedded Japanese font, whose
  // header lists them as "U+30D7 U+30EC ...".
  {
    std::ifstream file(std::string(MODERNGEKKO_SOURCE_DIR) + "/tools/launcher_cjk_font.inc");
    const std::string font((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    std::set<char32_t> in_font;
    for (std::size_t pos = font.find("U+"); pos != std::string::npos && pos < 200000;
         pos = font.find("U+", pos + 2))
      in_font.insert(static_cast<char32_t>(std::stoul(font.substr(pos + 2, 6), nullptr, 16)));
    for (const Translation& entry : JAPANESE_TEXTS)
    {
      for (const char32_t c : DecodeUtf8(entry.translated))
      {
        if (c > 0xFF && !in_font.contains(c))
        {
          std::cerr << "U+" << std::hex << static_cast<unsigned>(c) << std::dec
                    << " is not in the Japanese font (run tools/make_launcher_cjk_font.py): \""
                    << entry.translated << "\"\n";
          result = result ? result : 8;
        }
      }
    }
  }

  if (ResolveLanguage("auto", "es") != Language::Spanish ||
      ResolveLanguage("auto", "es_VE") != Language::Spanish ||
      ResolveLanguage("auto", "en") != Language::English ||
      ResolveLanguage("auto", "") != Language::English ||
      ResolveLanguage("en", "es") != Language::English ||
      ResolveLanguage("es", "en") != Language::Spanish ||
      ResolveLanguage("auto", "ja") != Language::Japanese ||
      ResolveLanguage("auto", "ja_JP") != Language::Japanese ||
      ResolveLanguage("ja", "es") != Language::Japanese)
    result = result ? result : 4;
  // The codes go back to the same languages.
  for (const Language language : {Language::English, Language::Spanish, Language::Japanese})
  {
    if (ResolveLanguage(LanguageCode(language), "") != language)
      result = result ? result : 4;
  }

  const char* play = "PLAY";
  ActiveLanguage() = Language::English;
  if (Tr(play) != play)
    result = result ? result : 5;
  ActiveLanguage() = Language::Spanish;
  if (std::string(Tr(play)) != "JUGAR" || std::string(Tr("No such text")) != "No such text" ||
      Tr(std::string("Not played yet")) != "Sin jugar a\xC3\xBA" "n" ||
      Tr(std::string("12 h 34 min")) != "12 h 34 min")
    result = result ? result : 5;
  if (Format(Tr("%d installed"), 3) != "3 instalados" ||
      Format(Tr("GAME READY \xC2\xB7 %s"), "RS9E8P") != "JUEGO LISTO \xC2\xB7 RS9E8P")
    result = result ? result : 7;
  ActiveLanguage() = Language::Japanese;
  if (std::string(Tr(play)) != "\xE3\x83\x97\xE3\x83\xAC\xE3\x82\xA4" ||  // purei
      std::string(Tr("No such text")) != "No such text")
    result = result ? result : 5;
  ActiveLanguage() = Language::English;

  if (result == 0)
    std::cout << used.size() << " texts, all translated\n";
  return result;
}
