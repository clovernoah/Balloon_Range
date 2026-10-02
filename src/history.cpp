/* ============================================================================
 * history.cpp —— 最近若干局的成绩流水
 * ==========================================================================*/
#include "history.h"
#include "config.h"     /* presetIndexMigrateLegacyOrder（旧档的预设下标换算） */
#include <stdio.h>
#include <string.h>
#include <time.h>

/* magic 与版本号。这里的版本号**独立于** SETTINGS_VERSION 与 ScoreBook 的
   隐含版本：三个存档各升各的，互不牵连。加字段就 +1，旧档判为不兼容、
   当作空历史读，**不删文件**。

   ★ 版本 1 → 2，但**结构体一个字节都没变** —— 升版本是为了标明
   "每条记录里的 preset 写的是换位前的下标"。「跟踪训练」的预设位次从第 8 位
   挪到第 4 位，而 preset 正是"当时用的是第几套预设"。v1 的档
   照读，读进来之后把每条的 preset 换算一次（historyLoad 里那一处）；
   不换算的话，历史记录屏上每一条的预设名都会指错。*/
#define HISTORY_MAGIC   0x31524842u     /* "BHR1" 小端 */
#define HISTORY_VERSION 2u
#define HISTORY_VERSION_LEGACY 1u           /* 旧格式（v1） */

/* 磁盘上的头：magic + version + size + crc，后接 payload。
   size 也存进来，是为了让"结构体长大/缩小"这件事能被读出来并判为不兼容，
   而不是按新布局去解释旧字节（那会让每个字段都串位，比丢了糟得多）。*/
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t crc;
} HistoryHeader;

void historyInit(RunHistory *h) {
    if (!h) return;
    memset(h, 0, sizeof(*h));
}

const HistoryEntry *historyAt(const RunHistory *h, int i) {
    if (!h || i < 0 || i >= h->count) return NULL;
    if (i >= HISTORY_CAP) return NULL;      /* count 不该超过 CAP，防的是坏数据 */
    return &h->e[i];
}

void historyPush(RunHistory *h, const HistoryEntry *e) {
    if (!h || !e) return;

    if (h->count > HISTORY_CAP) h->count = HISTORY_CAP;   /* 坏数据兜一下 */
    if (h->count < 0) h->count = 0;

    /* 满员时丢掉最旧的那一条，再整体后移 —— 用 memmove 而不是手写循环：
       结构体不小，而且重叠区间自己挪容易写错方向。*/
    if (h->count == HISTORY_CAP) {
        memmove(&h->e[1], &h->e[0], sizeof(HistoryEntry) * (HISTORY_CAP - 1));
    } else {
        memmove(&h->e[1], &h->e[0], sizeof(HistoryEntry) * (size_t)h->count);
        ++h->count;
    }
    h->e[0] = *e;
}

int historyLoad(RunHistory *h, const wchar_t *path) {
    FILE *f;
    HistoryHeader hdr;
    unsigned char payload[sizeof(RunHistory)];
    uint32_t want;
    size_t got;

    if (!h) return -1;
    historyInit(h);
    if (!path) return -1;

    f = _wfopen(path, L"rb");
    if (!f) return -1;                      /* 第一次跑：没有存档是正常的 */
    got = fread(&hdr, 1, sizeof(hdr), f);
    if (got != sizeof(hdr)) { fclose(f); return -1; }

    if (hdr.magic != HISTORY_MAGIC) { fclose(f); return -1; }
    /* v1（换位前）与 v2（换位后）都认。两者的结构体完全一样，
       差别只在 preset 那一个字段的口径 —— 读完换算一次即可（见下）。
       比 v1 更老的或更高的版本照旧不认、不删。*/
    if (hdr.version != HISTORY_VERSION && hdr.version != HISTORY_VERSION_LEGACY) {
        fclose(f);
        return -1;
    }
    /* size 存在头里而不是现算 sizeof，就是为了能在结构体
       长大/缩小时判出不兼容 —— 否则会按新布局去解释旧字节。*/
    if (hdr.size != (uint32_t)sizeof(HistoryEntry)) { fclose(f); return -1; }

    got = fread(payload, 1, sizeof(payload), f);
    fclose(f);
    /* 长度必须**正好**：短了是被截断，长了说明后面还跟着别的东西。
       两种都不认 —— 认了就等于拿猜测去补全用户的数据。*/
    if (got != sizeof(payload)) return -1;

    /* 校验拿的是**读进来的那串字节**，不是 memcpy 之后的结构体：
       结构体里有填充字节，编译器不保证拷贝过程把它们原样留着，
       拿结构体去比 CRC 会偶发地对不上（只在某些对齐下才犯病，最难查）。*/
    want = crc32Buf(payload, sizeof(RunHistory));
    if (hdr.crc != want) return -1;

    memcpy(h, payload, sizeof(RunHistory));
    /* count 是外部来的数，可能被改过。夹一下再交出去 ——
       渲染层照着 count 去读 e[]，放一个越界的 count 过去就是越界读。
       **先夹再换算**：count 越界时后面那些槽位本来就不该看，别去动它们。*/
    if (h->count < 0) h->count = 0;
    if (h->count > HISTORY_CAP) h->count = HISTORY_CAP;

    /* v1 档里每条记的是**换位前**的预设下标，逐条换算。
       只动 preset 这一个字段 —— 分数、命中率、时间戳这些与预设顺序无关。*/
    if (hdr.version == HISTORY_VERSION_LEGACY) {
        int i;
        for (i = 0; i < h->count; ++i)
            h->e[i].preset = presetIndexMigrateLegacyOrder(h->e[i].preset);
    }
    return 0;
}

int historySave(const RunHistory *h, const wchar_t *path) {
    FILE *f;
    HistoryHeader hdr;
    unsigned char payload[sizeof(RunHistory)];
    size_t put;

    if (!h || !path) return -1;

    hdr.magic   = HISTORY_MAGIC;
    hdr.version = HISTORY_VERSION;
    hdr.size    = (uint32_t)sizeof(HistoryEntry);
    memcpy(payload, h, sizeof(RunHistory));
    hdr.crc     = crc32Buf(payload, sizeof(RunHistory));

    f = _wfopen(path, L"wb");
    if (!f) return -1;
    put = fwrite(&hdr, 1, sizeof(hdr), f);
    if (put == sizeof(hdr)) put = fwrite(payload, 1, sizeof(payload), f);
    fclose(f);
    return (put == sizeof(payload)) ? 0 : -1;
}

void historyDefaultPath(wchar_t *path, int count) {
    wchar_t *slash;
    if (!path || count < 8) return;
    GetModuleFileNameW(NULL, path, (DWORD)count);
    slash = wcsrchr(path, L'\\');
    if (slash) slash[1] = L'\0';
    else path[0] = L'\0';
    wcsncat(path, L"历史.dat", (size_t)(count - 1 - (int)wcslen(path)));
}

void historyFormatTime(long long when, wchar_t *buf, int count) {
    struct tm tmv;
    __time64_t t;

    if (!buf || count < 2) return;
    buf[0] = L'\0';
    if (when <= 0) {
        /* 时间没取到（理论上不会，_time64 不失败）。照实写"时间未知"，
           而不是拿 1970 年假装 —— 那种数字看着像真数据，更容易误导。*/
        _snwprintf(buf, (size_t)(count - 1), L"时间未知");
        buf[count - 1] = L'\0';
        return;
    }

    t = (__time64_t)when;
    /* 用本地时间：玩家看的是"什么时候打的"，不是 UTC。*/
    if (_localtime64_s(&tmv, &t) != 0) {
        _snwprintf(buf, (size_t)(count - 1), L"时间未知");
        buf[count - 1] = L'\0';
        return;
    }
    _snwprintf(buf, (size_t)(count - 1), L"%04d-%02d-%02d %02d:%02d",
               tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
               tmv.tm_hour, tmv.tm_min);
    buf[count - 1] = L'\0';
}
