#include "TextUtils.h"

String transliterateToAscii(const String &in)
{
  String out;
  out.reserve(in.length());

  const uint8_t *s = (const uint8_t *)in.c_str();
  size_t i = 0;

  while (s[i])
  {
    uint8_t b = s[i];

    if (b < 0x80)
    {
      out += (char)b;
      ++i;
      continue;
    }

    uint32_t cp = 0;
    int extra = 0;

    if ((b & 0xE0) == 0xC0)
    {
      cp = b & 0x1F;
      extra = 1;
    }
    else if ((b & 0xF0) == 0xE0)
    {
      cp = b & 0x0F;
      extra = 2;
    }
    else if ((b & 0xF8) == 0xF0)
    {
      cp = b & 0x07;
      extra = 3;
    }
    else
    {
      ++i;
      continue;
    }

    ++i;
    for (int j = 0; j < extra && s[i]; ++j, ++i)
      cp = (cp << 6) | (s[i] & 0x3F);

    switch (cp)
    {
      // --- Polish letters ---
      case 0x0104: out += "A"; break; // Ą
      case 0x0105: out += "a"; break; // ą
      case 0x0106: out += "C"; break; // Ć
      case 0x0107: out += "c"; break; // ć
      case 0x0118: out += "E"; break; // Ę
      case 0x0119: out += "e"; break; // ę
      case 0x0141: out += "L"; break; // Ł
      case 0x0142: out += "l"; break; // ł
      case 0x0143: out += "N"; break; // Ń
      case 0x0144: out += "n"; break; // ń
      case 0x00D3: out += "O"; break; // Ó
      case 0x00F3: out += "o"; break; // ó
      case 0x015A: out += "S"; break; // Ś
      case 0x015B: out += "s"; break; // ś
      case 0x0179: out += "Z"; break; // Ź
      case 0x017A: out += "z"; break; // ź
      case 0x017B: out += "Z"; break; // Ż
      case 0x017C: out += "z"; break; // ż

      // --- Cyrillic ---
      case 0x0410: out += "A";   break; // А
      case 0x0430: out += "a";   break; // а
      case 0x0411: out += "B";   break; // Б
      case 0x0431: out += "b";   break; // б
      case 0x0412: out += "V";   break; // В
      case 0x0432: out += "v";   break; // в
      case 0x0413: out += "G";   break; // Г
      case 0x0433: out += "g";   break; // г
      case 0x0414: out += "D";   break; // Д
      case 0x0434: out += "d";   break; // д
      case 0x0415: out += "E";   break; // Е
      case 0x0435: out += "e";   break; // е
      case 0x0401: out += "E";   break; // Ё
      case 0x0451: out += "e";   break; // ё
      case 0x0416: out += "Zh";  break; // Ж
      case 0x0436: out += "zh";  break; // ж
      case 0x0417: out += "Z";   break; // З
      case 0x0437: out += "z";   break; // з
      case 0x0418: out += "I";   break; // И
      case 0x0438: out += "i";   break; // и
      case 0x0419: out += "J";   break; // Й
      case 0x0439: out += "j";   break; // й
      case 0x041A: out += "K";   break; // К
      case 0x043A: out += "k";   break; // к
      case 0x041B: out += "L";   break; // Л
      case 0x043B: out += "l";   break; // л
      case 0x041C: out += "M";   break; // М
      case 0x043C: out += "m";   break; // м
      case 0x041D: out += "N";   break; // Н
      case 0x043D: out += "n";   break; // н
      case 0x041E: out += "O";   break; // О
      case 0x043E: out += "o";   break; // о
      case 0x041F: out += "P";   break; // П
      case 0x043F: out += "p";   break; // п
      case 0x0420: out += "R";   break; // Р
      case 0x0440: out += "r";   break; // р
      case 0x0421: out += "S";   break; // С
      case 0x0441: out += "s";   break; // с
      case 0x0422: out += "T";   break; // Т
      case 0x0442: out += "t";   break; // т
      case 0x0423: out += "U";   break; // У
      case 0x0443: out += "u";   break; // у
      case 0x0424: out += "F";   break; // Ф
      case 0x0444: out += "f";   break; // ф
      case 0x0425: out += "Kh";  break; // Х
      case 0x0445: out += "kh";  break; // х
      case 0x0426: out += "Ts";  break; // Ц
      case 0x0446: out += "ts";  break; // ц
      case 0x0427: out += "Ch";  break; // Ч
      case 0x0447: out += "ch";  break; // ч
      case 0x0428: out += "Sh";  break; // Ш
      case 0x0448: out += "sh";  break; // ш
      case 0x0429: out += "Sch"; break; // Щ
      case 0x0449: out += "sch"; break; // щ
      case 0x042A: /*Ъ*/ break;
      case 0x044A: /*ъ*/ break;
      case 0x042B: out += "Y";   break; // Ы
      case 0x044B: out += "y";   break; // ы
      case 0x042C: /*Ь*/ break;
      case 0x044C: /*ь*/ break;
      case 0x042D: out += "E";   break; // Э
      case 0x044D: out += "e";   break; // э
      case 0x042E: out += "Yu";  break; // Ю
      case 0x044E: out += "yu";  break; // ю
      case 0x042F: out += "Ya";  break; // Я
      case 0x044F: out += "ya";  break; // я

      // Ukrainian extras
      case 0x0404: out += "Ye";  break; // Є
      case 0x0454: out += "ye";  break; // є
      case 0x0406: out += "I";   break; // І
      case 0x0456: out += "i";   break; // і
      case 0x0407: out += "Yi";  break; // Ї
      case 0x0457: out += "yi";  break; // ї
      case 0x0490: out += "G";   break; // Ґ
      case 0x0491: out += "g";   break; // ґ

      default:
        out += '?';
        break;
    }
  }

  return out;
}

String normalizeTitle(const String &in)
{
    String s = in;

    s.replace("• Є відео", "");
    s.replace("•Євідео", "");
    s.replace(" • Є відео", "");
    s.replace("• Video", "");
    s.trim();

    return transliterateToAscii(s);
}
