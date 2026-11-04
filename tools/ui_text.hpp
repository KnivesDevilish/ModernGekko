#pragma once

// Marks an English interface text that is written in one place and shown
// elsewhere: the launcher translates it when it draws it (see Tr() in
// launcher_text.hpp). The text itself is unchanged here.
// tests/launcher_text_test.cpp finds every marked text and checks that each
// language has it.
#define MODERNGEKKO_UI_TEXT(text) text
