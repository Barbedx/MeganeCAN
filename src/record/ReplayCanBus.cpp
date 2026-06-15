#include "ReplayCanBus.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef NATIVE
#include <stdio.h>
#endif

namespace {
// Parse a hex token into a uint32; returns false if not a valid hex token.
bool hexTok(const char* s, uint32_t& out) {
    if (!s || !*s) return false;
    char* end = nullptr;
    out = (uint32_t)strtoul(s, &end, 16);
    return end && *end == '\0';
}
bool isAllDigits(const char* s) {
    if (!s || !*s) return false;
    for (const char* p = s; *p; ++p) if (!isdigit((unsigned char)*p)) return false;
    return true;
}
bool isTag(const char* s) {
    return s && (strcmp(s, "@TX") == 0 || strcmp(s, "@RX") == 0 ||
                 strcmp(s, "TX") == 0  || strcmp(s, "RX") == 0);
}
} // namespace

bool ReplayCanBus::parseLine(const char* line, int len)
{
    // Copy into a bounded mutable buffer for strtok.
    char buf[160];
    int n = 0;
    for (int i = 0; i < len && n < (int)sizeof(buf) - 1; i++) {
        char c = line[i];
        if (c == '\r' || c == '\n') break;
        buf[n++] = c;
    }
    buf[n] = '\0';

    // Tokenize.
    char* toks[16];
    int nt = 0;
    char* save = nullptr;
    for (char* t = strtok_r(buf, " \t", &save); t && nt < 16; t = strtok_r(nullptr, " \t", &save))
        toks[nt++] = t;

    if (nt == 0 || toks[0][0] == '#') return false;   // blank / comment

    int idx = 0;
    uint32_t ms = 0;
    if (isAllDigits(toks[idx])) { ms = (uint32_t)strtoul(toks[idx], nullptr, 10); idx++; }
    if (idx < nt && isTag(toks[idx])) idx++;          // direction tag (default TX)

    if (idx >= nt) return false;                      // no id
    uint32_t id;
    if (!hexTok(toks[idx], id)) return false;
    idx++;

    Frame f;
    f.id = id;
    f.extended = false;
    f.len = 0;
    for (; idx < nt && f.len < 8; idx++) {
        uint32_t b;
        if (!hexTok(toks[idx], b)) return false;
        f.data[f.len++] = (uint8_t)b;
    }

    if (_count >= MAX_FRAMES) return false;
    _rec[_count].ms = ms;
    _rec[_count].f = f;
    _count++;
    return true;
}

int ReplayCanBus::loadText(const char* data, int len)
{
    if (len < 0) len = (int)strlen(data);
    int loaded = 0;
    int start = 0;
    for (int i = 0; i <= len; i++) {
        if (i == len || data[i] == '\n') {
            if (i > start && parseLine(data + start, i - start)) loaded++;
            start = i + 1;
        }
    }
    return loaded;
}

int ReplayCanBus::loadFile(const char* path)
{
#ifdef NATIVE
    FILE* fp = fopen(path, "rb");
    if (!fp) return -1;
    static char body[64 * 1024];
    size_t got = fread(body, 1, sizeof(body) - 1, fp);
    fclose(fp);
    body[got] = '\0';
    return loadText(body, (int)got);
#else
    (void)path;
    return -1;   // no filesystem assumed on target; load via loadText()
#endif
}

bool ReplayCanBus::step()
{
    if (_pos >= _count) return false;
    const Rec& r = _rec[_pos++];
    if (_rx) _rx(r.f, _rxCtx);
    return true;
}
