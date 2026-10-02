/* ============================================================================
 * score.h —— 计分、连击、成绩记录
 *
 * 计分公式**逐位照搬早先靶场的口径**：
 *     基础分 = clamp(520 / 参考像素半径, 10, 40)
 *     实得分 = 基础分 × 连击倍率
 *     连击倍率 = clamp(1 + 连击数 × 0.05, 1, 2.0)
 * 3D 里半径是米，所以多一步 radiusToRefPixels() 把米映射回 14..42 像素
 * 区间 —— 那一层在 config.cpp 里，是唯一一处换算。
 *
 * 这里全部是纯函数与纯数据，不碰 GL 也不碰窗口，自检可以直接怼。
 * ==========================================================================*/
#ifndef SCORE_H
#define SCORE_H

#include "core.h"
#include "config.h"

/* ---- 纯公式 ---- */

float scoreComboMultiplier(int combo);
int   scoreBaseOf(float radiusMeters, const Params *p);
int   scoreGainOf(float radiusMeters, const Params *p, int combo);

/* ---- 一局的成绩单 ---- */

typedef struct {
    int    score;
    int    popped;
    int    missed;
    int    shots;
    int    hits;
    int    bestCombo;
    /* mode 与 preset **两个都留**：preset 是使用者点的那一套（"用的哪套
       参数"），mode 是那一局实际生效的规则（"这局是怎么算的"）。套了
       预设之后还可以手改参数，甚至把规则相关的项改到跟预设不一样，
       光凭 preset 一个数说不清当时到底按什么规则跑的。*/
    int    mode;
    int    preset;
    float  accuracy;        /* hits / shots，shots 为 0 时记 0 */
    double durationSec;
    unsigned seed;
} RunResult;

void scoreFillResult(RunResult *r, int score, int popped, int missed,
                     int shots, int hits, int bestCombo,
                     int mode, int preset, double durationSec,
                     unsigned seed);

/* ---- 历史最好成绩 ----
 * 存档放在 exe 同目录下的 记录.dat（宽字符文件名，内容是一段带版本号与
 * CRC 的二进制）。零第三方依赖，读写都是标准 C 的文件接口。
 * 存档坏了（被截断 / 版本不符 / CRC 对不上）一律当作"没有记录"，
 * 并且**不删文件**：宁可从头再来，也不擅自处理用户的文件。
 */

#define SCOREBOOK_MAGIC 0x32534252u     /* "RBS2"：当前格式的档魔数 */

/* 更早写出来的档（"RBS1"）。照旧读得回来 —— 只是里面那八档是按
   **换位前**的预设顺序摆的，读完要按 presetIndexMigrateLegacyOrder 重排一次
   （「跟踪训练」由第 8 位挪到第 4 位）。这一"读旧格式"的分支就是"能读
   多少读多少"那条规则的延续：历史成绩不该因为界面上的顺序调整而错位。 */
#define SCOREBOOK_MAGIC_LEGACY 0x31534252u /* "RBS1" */

typedef struct {
    int score;              /* 历史最高分；0 表示还没有记录 */
    int bestCombo;
    int popped;
    int accuracyPct;        /* 历史最高那一局的命中率（百分数） */
    unsigned seed;          /* 那一局的种子，可以复现 */
} BestRecord;

/* 最好成绩按**预设**分档，一份八项。早先是按 mode 分的四档 ——
   模式没了，档位自然跟着变成预设。副作用是 v2 的"记录.dat"读不出来，
   当作"还没有记录"处理（不删文件），重打一次就有了。*/
typedef struct {
    BestRecord rec[PRESET_COUNT];
} ScoreBook;

void scoreBookInit(ScoreBook *b);
int  scoreBookLoad(ScoreBook *b, const wchar_t *path);   /* 0 = 读到了有效存档 */
int  scoreBookSave(const ScoreBook *b, const wchar_t *path);

/* 用一局的成绩去更新记录。返回 1 表示破纪录（调用方据此放提示音）。
   preset 越界时返回 0，不动任何档位。*/
int  scoreBookSubmit(ScoreBook *b, int preset, const RunResult *r);

/* 默认存档路径：exe 同目录。path 至少要 MAX_PATH 个 wchar_t。*/
void scoreBookDefaultPath(wchar_t *path, int count);

#endif /* SCORE_H */
