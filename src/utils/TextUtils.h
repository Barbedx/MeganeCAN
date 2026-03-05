#pragma once
#include <Arduino.h>

// Transliterate UTF-8 text (Cyrillic, Polish diacritics, etc.) to plain ASCII.
// Unknown non-ASCII codepoints are replaced with '?'.
String transliterateToAscii(const String &in);

// Strip Apple Music video annotations and transliterate to ASCII.
String normalizeTitle(const String &in);
