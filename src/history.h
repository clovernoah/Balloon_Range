/* ============================================================================
 * history.h —— 最近若干局的成绩流水
 *
 * 为什么不并进 score.h：那是**两类东西**。
 *   ScoreBook 存的是"每套预设的历史最高分"——一档一条，新的成绩比它高才覆盖，
 *              是"最好成绩"这个概念的存储；
 *   RunHistory 存的是"最近 20 局"——一局一条，先进先出，不看成绩高低，
 *              是"都打过什么"这个概念的存储。
 * 两者的更新规则、界面、清空时机全都不一样，合在一个文件里只会让两边
 * 的注释互相打架。分开放，各自都好读。
 *
 * 与另两个存档同一套路数：宽字符文件名、magic + 版本 + CRC32、
 * 坏了就当空的读、**绝不删用户的文件**。
 * ==========================================================================*/
#ifndef HISTORY_H
#define HISTORY_H

#include "core.h"

/* 容量：最近 20 局，关闭程序后仍然保存。*/
#define HISTORY_CAP 20

typedef struct {
    int       score;
    int       popped;
    int       missed;
    int       shots;
    int       hits;
    int       bestCombo;
    int       accuracyPct;   /* 存百分数的整数：0..100，省得为了显示再算一遍浮点 */
    int       durationMs;    /* 这一局打了多久（毫秒） */
    int       preset;        /* 用的哪一套预设 */
    int       mode;          /* 那一局实际生效的规则（"这局到底怎么算的"） */
    int       endReason;     /* EndReason：怎么结束的 */
    unsigned  seed;          /* 那一局的种子，可以复现 */
    long long when;          /* 结算那一刻的 Unix 时间（秒）；0 = 时间没取到 */
} HistoryEntry;

typedef struct {
    /* 槽位 0 **恒为最新一局**，往后越来越旧。写入时整体后移一位，
       满员时挤掉最后一个。这样"第 1 次"天然就是"最近一次"，
       渲染层不用做任何下标换算 —— "最近一次在最上面"由这个顺序直接成立。*/
    HistoryEntry e[HISTORY_CAP];
    int          count;      /* 已有几局（0..HISTORY_CAP） */
} RunHistory;

void historyInit(RunHistory *h);

/* 0 = 读到了有效存档；非 0 = 没有 / 读不了 / 内容不可信（调用方当作空历史）。
   任何情况下都**不会**删改文件。*/
int  historyLoad(RunHistory *h, const wchar_t *path);
int  historySave(const RunHistory *h, const wchar_t *path);

/* 记一局。满 20 局时挤掉最旧的那一条。h 为 NULL 时什么都不做。*/
void historyPush(RunHistory *h, const HistoryEntry *e);

/* 取第 i 条（0 = 最新）。越界返回 NULL，不越界读表。*/
const HistoryEntry *historyAt(const RunHistory *h, int i);

/* 默认存档路径：exe 同目录下的 历史.dat。path 至少要 MAX_PATH 个 wchar_t。*/
void historyDefaultPath(wchar_t *path, int count);

/* 把 when 格式化成 "2026-10-01 14:32"。when <= 0 时写 "时间未知"。
   buf 建议至少 32 个 wchar_t。*/
void historyFormatTime(long long when, wchar_t *buf, int count);

#endif /* HISTORY_H */
