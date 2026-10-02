/* ============================================================================
 * score.cpp —— 计分与成绩记录
 * ==========================================================================*/
#include "score.h"
#include <stdio.h>
#include <string.h>

/* ============================================================ 纯公式 */

float scoreComboMultiplier(int combo) {
    float mult;
    if (combo <= 0) return 1.0f;
    mult = 1.0f + (float)combo * COMBO_MULT_STEP;
    if (mult > COMBO_MULT_MAX) mult = COMBO_MULT_MAX;
    return mult;
}

int scoreBaseOf(float radiusMeters, const Params *p) {
    float refPx = radiusToRefPixels(radiusMeters, p);
    int s;
    if (refPx <= 0.0f) return SCORE_MIN;
    s = (int)(p->scoreBase / refPx + 0.5f);
    return clampi(s, SCORE_MIN, SCORE_MAX);
}

int scoreGainOf(float radiusMeters, const Params *p, int combo) {
    int base = scoreBaseOf(radiusMeters, p);
    float mult = scoreComboMultiplier(combo);
    return (int)((float)base * mult + 0.5f);
}

/* ============================================================ 成绩单 */

void scoreFillResult(RunResult *r, int score, int popped, int missed,
                     int shots, int hits, int bestCombo,
                     int mode, int preset, double durationSec,
                     unsigned seed) {
    if (!r) return;
    memset(r, 0, sizeof(*r));
    r->score       = score;
    r->popped      = popped;
    r->missed      = missed;
    r->shots       = shots;
    r->hits        = hits;
    r->bestCombo   = bestCombo;
    r->mode        = clampi(mode, 0, MODE_COUNT - 1);
    r->preset      = clampi(preset, 0, PRESET_COUNT - 1);
    r->durationSec = durationSec;
    r->seed        = seed;
    r->accuracy    = (shots > 0) ? ((float)hits / (float)shots) : 0.0f;
}

/* ============================================================ 历史记录 */

void scoreBookInit(ScoreBook *b) {
    if (!b) return;
    memset(b, 0, sizeof(*b));
}

int scoreBookLoad(ScoreBook *b, const wchar_t *path) {
    FILE *f;
    unsigned char hdr[8];
    unsigned char payload[sizeof(ScoreBook)];
    uint32_t magic, crc, want;
    size_t n;

    if (!b) return 0;
    scoreBookInit(b);                     /* 先清零：读不到的档位就保持 0 */
    if (!path) return 0;

    f = _wfopen(path, L"rb");
    if (!f) return 0;                     /* 第一次跑：没有存档是正常的 */
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) { fclose(f); return 0; }

    memcpy(&magic, hdr, 4);
    if (magic != SCOREBOOK_MAGIC && magic != SCOREBOOK_MAGIC_LEGACY) {
        fclose(f);
        return 0;
    }
    memcpy(&crc, hdr + 4, 4);

    /* ★ **按文件里实际有几个 BestRecord 就读几个**。
     *
     * 预设从 7 套加到 8 套，ScoreBook 就由 140 字节涨到 160 字节。早先这里
     * 是"长度必须恰好等于 sizeof(ScoreBook)"，一行改动就会把那 7 条最高分
     * 整个判为无效（还只是不读、不删，但成绩在界面上就是没了）。
     *
     * 现在改成：能读多少读多少。文件里存着 7 条就读 7 条，剩下那一条留 0 ——
     * 新预设本来也没有历史成绩。CRC 照旧按**文件里写的长度**算，旧档当初就是
     * 对它那 140 字节求的和，拿 160 去算必然对不上。
     *
     * 尾部若还有多余字节，说明这份档比当前格式多出一截（套数更多），
     * 那时记录的语义未必还对得上，当没有档处理 —— 不读、不删。*/
    n = fread(payload, 1, sizeof(payload), f);
    if (fgetc(f) != EOF) { fclose(f); return 0; }
    fclose(f);

    /* 长度必须是 BestRecord 的整数倍，且至少放得下一条 —— 否则是截断或异物。*/
    if (n < sizeof(BestRecord) || n % sizeof(BestRecord) != 0) return 0;

    want = crc32Buf(payload, n);
    if (crc != want) return 0;            /* 内容对不上校验和，不认 */

    memcpy(b, payload, n);

    /* ★ RBS1 那份档按**换位前**的预设顺序摆着八档。
       逐档搬到今天的下标去 —— 不这么做，以前「计时挑战」的最高分
       会显示成「跟踪训练」的，且浑然不觉。
       有几条搬几条（档里条数允许比 PRESET_COUNT 少）。*/
    if (magic == SCOREBOOK_MAGIC_LEGACY) {
        ScoreBook moved;
        size_t k, cnt = n / sizeof(BestRecord);
        scoreBookInit(&moved);
        for (k = 0; k < cnt; ++k)
            moved.rec[presetIndexMigrateLegacyOrder((int)k)] = b->rec[k];
        *b = moved;
    }
    return 1;
}

int scoreBookSave(const ScoreBook *b, const wchar_t *path) {
    FILE *f;
    unsigned char buf[8 + sizeof(ScoreBook)];
    uint32_t magic = SCOREBOOK_MAGIC, crc;
    size_t put;

    if (!b || !path) return -1;
    memcpy(buf, &magic, 4);
    memcpy(buf + 8, b, sizeof(ScoreBook));
    crc = crc32Buf(buf + 8, sizeof(ScoreBook));
    memcpy(buf + 4, &crc, 4);

    f = _wfopen(path, L"wb");
    if (!f) return -1;
    put = fwrite(buf, 1, sizeof(buf), f);
    fclose(f);
    return (put == sizeof(buf)) ? 0 : -1;
}

int scoreBookSubmit(ScoreBook *b, int preset, const RunResult *r) {
    BestRecord *rec;
    if (!b || !r) return 0;
    if (preset < 0 || preset >= PRESET_COUNT) return 0;

    rec = &b->rec[preset];

    /* "破纪录"只认分数。分数相同不算破 —— 否则刷同一个分数会一直报纪录。*/
    if (r->score > rec->score) {
        rec->score      = r->score;
        rec->bestCombo  = r->bestCombo;
        rec->popped     = r->popped;
        rec->accuracyPct = (int)(r->accuracy * 100.0f + 0.5f);
        rec->seed       = r->seed;
        return 1;
    }
    /* 分数没破，但连击破了也值得单独记一笔。*/
    if (r->bestCombo > rec->bestCombo) {
        rec->bestCombo = r->bestCombo;
        return 0;
    }
    return 0;
}

void scoreBookDefaultPath(wchar_t *path, int count) {
    wchar_t *slash;
    if (!path || count < 8) return;
    GetModuleFileNameW(NULL, path, (DWORD)count);
    slash = wcsrchr(path, L'\\');
    if (slash) slash[1] = L'\0';
    else path[0] = L'\0';
    wcsncat(path, L"记录.dat", (size_t)(count - 1 - (int)wcslen(path)));
}
