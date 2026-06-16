#include "CarminatNowPlaying.h"
#include "utils/TextUtils.h"
#include "bluetooth.h"
#include <time.h>

// All bodies below are cut verbatim from CarminatDisplay; the only edits are the
// seam swaps: showMenu(...) -> _panel.showMenu(...), tracker -> _aux, mainMenu ->
// _menu. The emitted CAN frames are therefore byte-identical.

void CarminatNowPlaying::setMediaInfo(const AppleMediaService::MediaInformation &info)
{
  // визначаємо: новий трек чи той самий
  bool titleChanged = (_mediaInfo.mTitle != info.mTitle);
  bool artistChanged = (_mediaInfo.mArtist != info.mArtist);
  bool playerChanged = (_mediaInfo.mPlayerName != info.mPlayerName);

  _mediaInfo = info;

  if (playerChanged)
  {
    _mediaPlayerName = _mediaInfo.mPlayerName.c_str();
    if (_mediaPlayerName.isEmpty())
    {
      _mediaPlayerName = "PLAYER";
    }
  }

  if (titleChanged || artistChanged)
  {
    // побудувати повний рядок 2: "Artist - Title"
    _mediaLine2Full = "";
    if (!_mediaInfo.mArtist.empty())
    {
      _mediaLine2Full += _mediaInfo.mArtist.c_str();
    }
    if (!_mediaInfo.mArtist.empty() && !_mediaInfo.mTitle.empty())
    {
      _mediaLine2Full += " - ";
    }
    if (!_mediaInfo.mTitle.empty())
    {
      _mediaLine2Full += _mediaInfo.mTitle.c_str();
    }

    _scrollPos = 0; // reset scroll on track change
    //_lastScrollMs = millis(); // оновити таймер
  }
}

String CarminatNowPlaying::buildProgressLine() const
{
  // AMS дає секунди (float)
  float posSec = _mediaInfo.mElapsedTime;
  float durSec = _mediaInfo.mDuration;

  if (durSec <= 0.0f)
  {
    return "[----------] --:--/--:--"; // calc by lentght
  }

  float progress = posSec / durSec;
  if (progress < 0.0f)
    progress = 0.0f;
  if (progress > 1.0f)
    progress = 1.0f;

  const int BAR_WIDTH = 10;

  int filled = (int)(progress * BAR_WIDTH + 0.5f);

  String bar;
  bar.reserve(BAR_WIDTH);
  for (int i = 0; i < BAR_WIDTH; ++i)
  {
    bar += (i < filled) ? 'X' : '_';
  }

  auto fmtTime = [](float secF) -> String
  {
    if (secF < 0.0f)
      secF = 0.0f;
    uint32_t totalSec = (uint32_t)(secF + 0.5f); // округляємо
    uint32_t min = totalSec / 60;
    uint32_t sec = totalSec % 60;
    char buf[8];
    snprintf(buf, sizeof(buf), "%lu:%02lu",
             (unsigned long)min,
             (unsigned long)sec);
    return String(buf);
  };

  String line;
  line.reserve(24);
  line += bar;
  line += " ";
  line += fmtTime(posSec);
  line += "/";
  line += fmtTime(durSec);

  const int MAX_CHARS = 20; // скільки реально влазить в 3-й рядок
  if (line.length() > MAX_CHARS)
    line = line.substring(0, MAX_CHARS);

  return line;
}

void CarminatNowPlaying::tick()
{
  // Don't draw over open menu or outside AUX mode
  if (_menu.isActive())
    return;
  if (!_aux.isInAuxMode())
    return;

  uint32_t now = millis();

  // When BT not connected: show what we're doing instead of an empty screen
  if (!Bluetooth::IsConnected())
  {
    if (now - _lastMediaRenderMs >= 1000)
    {
      _lastMediaRenderMs = now;
      _panel.showMenu("MeganeCAN", Bluetooth::GetStatusText(), "for AMS device", 0x00);
    }
    return;
  }

  // New ANCS notification → pop it over the media screen for a few seconds.
  auto recents = AppleNotificationService::GetRecent(); // newest first
  if (!recents.empty() && recents[0].uid != _lastNotifUid)
  {
    _lastNotifUid = recents[0].uid;
    _notifUntilMs = now + 6000; // show for ~6s
  }
  if (now < _notifUntilMs && !recents.empty())
  {
    if (now - _lastMediaRenderMs >= 300)
    {
      _lastMediaRenderMs = now;
      renderNotificationScreen(recents[0]);
    }
    return;
  }

  // Оновлюємо дані про медіа з AMS
  AppleMediaService::MediaInformation current = AppleMediaService::GetMediaInformation();
  _mediaInfo = current;
  _mediaPlayerName = current.mPlayerName.c_str();

  // Локально дораховуємо elapsed time, якщо грає
   if (current.mPlaybackState == AppleMediaService::MediaInformation::PlaybackState::Playing)
   {
   if (current.mLastPlaybackInfoMs != 0)
   {
     uint32_t dtMs = now - current.mLastPlaybackInfoMs;
     float dtSec = dtMs / 1000.0f;
     _mediaInfo.mElapsedTime = current.mElapsedTime + dtSec * current.mPlaybackRate;
   }
   }

  // Обмежимо частоту перерисовки, наприклад раз на 300ms
  if (!(now - _lastMediaRenderMs >= 300))
    return;

  _lastMediaRenderMs = now;

  // Крок скролу раз на ~400ms
  if (now - _lastScrollStepMs >= 400)
  {
    _lastScrollStepMs = now;
    _scrollPos++;
  }

  renderMediaScreen(false);
}

void CarminatNowPlaying::renderMediaScreen(bool forceRedraw)
{
  // Не ліземо поверх меню
  if (_menu.isActive())
    return;
  if (!_aux.isInAuxMode())
    return;

  String status_icon;
  switch (_mediaInfo.mPlaybackState)
  {
  case AppleMediaService::MediaInformation::PlaybackState::Playing:
  {
    status_icon = ">";
    break;
  }
  case AppleMediaService::MediaInformation::PlaybackState::Paused:
  {

    status_icon = "||";
    break;
  }
  default:
  {

    status_icon = "D";
    break;
  }
  }

  // 1-й рядок: назва плеєра + час справа (26 символів максимум)
  String headerStr = status_icon + " " + (_mediaPlayerName.length() ? _mediaPlayerName : String("AUX PLAYER"));
  struct tm ti;
  if (getLocalTime(&ti, 0))
  {
    char timeBuf[8];
    snprintf(timeBuf, sizeof(timeBuf), "[%02d:%02d]", ti.tm_hour, ti.tm_min);
    const int timeLen = strlen(timeBuf); // 7
    const int maxNameLen = 26 - timeLen - 1; // 1 space separator
    if ((int)headerStr.length() > maxNameLen)
      headerStr = headerStr.substring(0, maxNameLen);
    int spaces = 26 - (int)headerStr.length() - timeLen;
    while (spaces-- > 0) headerStr += ' ';
    headerStr += timeBuf;
  }
  const char *header = headerStr.c_str();

  // 2-й рядок: Artist - Title (з можливим скролом)
  String line2 = buildScrollingTitle();
  const char *row2 = line2.c_str();

  // 3-й рядок: прогрес
  String line3 = buildProgressLine();
  const char *row3 = line3.c_str();
  // scrollLockIndicator = 0 -> без стрілок, бо це не меню
  _panel.showMenu(header, row2, row3, /*scrollLockIndicator*/ 0x00);
  // --- 6. Текстове представлення в Serial (для тестів без дисплея) ---
  // Serial.println();
  // Serial.println("===== MEDIA SCREEN =====");
  // Serial.println(header);   // 1-й рядок
  // Serial.println(line2);       // 2-й рядок
  // Serial.println(line3);       // 3-й рядок
  // Serial.println("========================");
}

void CarminatNowPlaying::renderNotificationScreen(const AppleNotificationService::NotificationInfo &n)
{
  if (_menu.isActive())
    return;

  // Line 1: source app (e.g. "Telegram") + sender — answers "звідки + від кого".
  // The Carminat charset can't render UTF-8, so transliterate Cyrillic -> Latin.
  String app    = String(AppleNotificationService::AppName(n.appId).c_str());
  String sender = transliterateToAscii(String(n.title.c_str()));
  String header = app;
  if (sender.length()) header += ": " + sender;
  if (header.length() > 26) header = header.substring(0, 26);

  // Lines 2-3: the message body across both 20-char rows (more of it visible).
  String msg = transliterateToAscii(String(n.message.c_str()));
  String line2 = msg.substring(0, 20);
  String line3 = (msg.length() > 20) ? msg.substring(20, 40) : String("");

  _panel.showMenu(header.c_str(), line2.c_str(), line3.c_str(), 0x00);
}

String CarminatNowPlaying::buildScrollingTitle()
{

  String full = String(_mediaInfo.mArtist.c_str());
  if (full.length() > 0 && _mediaInfo.mTitle.size() > 0)
  {
    full += ": ";
  }
  full += String(_mediaInfo.mTitle.c_str());
  full = normalizeTitle(full);
  full += "  "; // trailing spaces AFTER trim/normalize so they aren't stripped
  const int MAX_VISIBLE = 20; // match progress bar row width

  if (full.length() <= MAX_VISIBLE)
    return full;

  // Кільцевий скрол по рядку
  uint16_t start = _scrollPos % full.length();
  String out;
  out.reserve(MAX_VISIBLE);

  for (int i = 0; i < MAX_VISIBLE; ++i)
  {
    out += full[(start + i) % full.length()];
  }

  return out;
}
