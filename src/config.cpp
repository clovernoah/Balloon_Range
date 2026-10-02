/* ============================================================================
 * config.cpp —— 参数的默认值、夹取、指纹与持久化
 *
 * 这里没有任何"行为"，只有数据操作。玩法层读参数，但不改参数。
 * ==========================================================================*/
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>   /* wcslen —— 拼单位后缀时要用 */

/* 规则名。**已经不出现在参数面板上了**（面板上只有「预设方案」），
   留着是因为结算屏和统计面板要标一行"这一局按哪套规则算的" ——
   比如"计时挑战"和"恒量靶场"都可能在用同一套预设参数微调之后出现，
   光看预设名分不出来。*/
const wchar_t *const g_modeNames[MODE_COUNT] = {
    L"恒量靶场", L"计时挑战", L"渐进训练", L"精准挑战"
};

const wchar_t *const g_motionNames[MOVE_COUNT] = {
    L"静止", L"匀速漂移", L"正弦摆动", L"随机跳变"
};
/* 顺序必须与 SpawnRule 枚举一致 —— 自检里有断言盯着。*/
const wchar_t *const g_spawnNames[SPAWN_COUNT] = {
    L"全场随机", L"邻位生成"
};
const wchar_t *const g_wallStyleNames[WALL_STYLE_COUNT] = {
    L"抹灰墙", L"砖墙", L"瓷砖", L"毛坯混凝土"
};
const wchar_t *const g_crossStyleNames[CROSS_COUNT] = {
    L"十字", L"十字点", L"圆环点", L"单点"
};
const wchar_t *const g_crossColorNames[CROSSCOL_COUNT] = {
    L"白色", L"绿色", L"红色", L"黄色", L"青色"
};

/* ============================================================ 预设方案表
 *
 * 「预设方案、模式和难度是同一个概念，就是一组提前写好的可玩的参数」，
 * 所以这里只有**一张表**：早先的"模式表 + 难度表"已经合并掉了 ——
 * 面板上也就只剩「预设方案」这一个名词。
 *
 * 每条覆盖项是 { 字段偏移, 值 }。偏移用 offsetof 写（见下面的 PO 宏），
 * 字段名拼错当场编译不过，不会出现"表里写了、结构体里没这个字段"。
 *
 * 覆盖范围：**只覆盖玩法类**（气球 / 运动 / 节奏 / 判定计分 / 生命与时限），
 * 不碰玩家手感与画面声音。换一套玩法不该顺手改掉玩家的鼠标灵敏度 ——
 * 这是有意的边界。
 *
 * 每条预设都把玩法参数**写全**（而不是只写与别的预设不同的那几项）：
 * 否则"从小快靶切回固定靶"会留下一堆上一条预设的残留值，用户看到的
 * 就不是他点的那一套。
 *
 * 表里 radiusMax 一律写在 radiusMin **前面**。paramsClamp 有"上限低于下限
 * 就互换"的规矩，而每写一项都会过一遍它：从"精准挑战"（0.14/0.18）切到
 * "固定靶"（0.40/0.40）时若先写 min，会先被夹成 0.18/0.40 再写 max=0.40，
 * 最后留下来的是 0.18/0.40 —— 用户点的明明是 0.40/0.40。先写大的那个就没
 * 这个问题。顺序看着琐碎，但它是这张表唯一一处"表里顺序会改结果"的地方。
 */
#define PO(f, v)  { (int)offsetof(Params, f), (float)(v) }
#define POW(f, i, v) \
    { (int)(offsetof(Params, f) + sizeof(((Params *)0)->f[0]) * (size_t)(i)), (float)(v) }

/* ---- 0 固定靶（默认） ---- */
static const PresetOverride ovFixed[] = {
    PO(balloonCount, 8),
    PO(spawnRule, SPAWN_RANDOM),
    PO(spawnDistMax, 1.0f), PO(spawnDistMin, 1.0f),
    PO(radiusMax, 0.40f), PO(radiusMin, 0.40f),
    PO(allowSpecial, 0),                       /* 清一色普通球，墙上完全一样 */
    POW(typeWeight, 0, 100), POW(typeWeight, 1, 0),
    POW(typeWeight, 2, 0),   POW(typeWeight, 3, 0),
    PO(motion, MOVE_STILL), PO(driftSpeed, 0.0f), PO(driftRange, 1.00f),
    PO(bobAmp, 0.0f), PO(bobFreq, 0.55f),
    PO(lifetimeSec, 0.0f), PO(missCostsLife, 0), PO(refillDelaySec, 0.0f),
    PO(spawnAnimSec, 0.18f), PO(autoFireInterval, 0.0f),
    PO(hitForgive, 1.06f), PO(comboWindowSec, 1.20f), PO(scoreBase, 520.0f),
    PO(missBreaksCombo, 0),
    PO(lives, 5), PO(timeLimitSec, 60.0f), PO(infiniteLives, 1)
};

/* ---- 1 移动靶 ---- */
static const PresetOverride ovMoving[] = {
    PO(balloonCount, 8),
    PO(spawnRule, SPAWN_RANDOM),
    PO(spawnDistMax, 1.0f), PO(spawnDistMin, 1.0f),
    PO(radiusMax, 0.50f), PO(radiusMin, 0.22f),
    PO(allowSpecial, 1),
    POW(typeWeight, 0, 70), POW(typeWeight, 1, 14),
    POW(typeWeight, 2, 4),  POW(typeWeight, 3, 12),
    PO(motion, MOVE_LINEAR), PO(driftSpeed, 0.55f), PO(driftRange, 1.00f),
    PO(bobAmp, 0.045f), PO(bobFreq, 0.55f),
    PO(lifetimeSec, 0.0f), PO(missCostsLife, 0), PO(refillDelaySec, 0.0f),
    PO(spawnAnimSec, 0.18f), PO(autoFireInterval, 0.0f),
    PO(hitForgive, 1.06f), PO(comboWindowSec, 1.20f), PO(scoreBase, 520.0f),
    PO(missBreaksCombo, 0),
    PO(lives, 5), PO(timeLimitSec, 60.0f), PO(infiniteLives, 1)
};

/* ---- 2 小快靶 ---- */
static const PresetOverride ovSmallFast[] = {
    PO(balloonCount, 10),
    PO(spawnRule, SPAWN_RANDOM),
    PO(spawnDistMax, 1.0f), PO(spawnDistMin, 1.0f),
    PO(radiusMax, 0.24f), PO(radiusMin, 0.16f),
    PO(allowSpecial, 0),                       /* 已经全是最小的了，不必再出小快球 */
    POW(typeWeight, 0, 100), POW(typeWeight, 1, 0),
    POW(typeWeight, 2, 0),   POW(typeWeight, 3, 0),
    PO(motion, MOVE_JUMP), PO(driftSpeed, 1.80f), PO(driftRange, 1.00f),
    PO(bobAmp, 0.08f), PO(bobFreq, 0.80f),
    PO(lifetimeSec, 0.0f), PO(missCostsLife, 0), PO(refillDelaySec, 0.10f),
    PO(spawnAnimSec, 0.10f), PO(autoFireInterval, 0.0f),
    PO(hitForgive, 1.10f), PO(comboWindowSec, 1.00f), PO(scoreBase, 520.0f),
    PO(missBreaksCombo, 0),
    PO(lives, 5), PO(timeLimitSec, 60.0f), PO(infiniteLives, 1)
};

/* ---- 3 跟踪训练 ---- */
static const PresetOverride ovTrack[] = {
    /* 底子与「固定靶」完全相同：等径大球（0.40）、只用普通球、不移动、不限时、
       不扣命。只动三处 —— 球数 = 2、生成位置 = 邻位生成、生成距离 = 1.0~2.0 倍
       （最后那处从一个写死的"紧贴"扩成了可调范围）。

       bobAmp 在 ovFixed 里就是 0，这里**显式再写一遍**：纵向浮动会让两颗球在
       竖直方向错开，"紧挨着"当场破功。写出来是为了将来有人调固定靶的浮动幅度
       时，不会顺手把这一套也带跑。

       refillDelaySec 保持 0（当帧补位）：补位延迟越大，"剩下的那颗"在空窗期里
       就越可能被下一发打掉，届时搭档没了、按第六十六节要回落到全随机 —— 那
       就不再是"跟踪"了。0 延迟让每一次补位都必然发生在一颗球还活着的时候。

       ★ 范围由两项参数给出，**上限写在前**（与 radiusMax/radiusMin
       同一条规矩，表里顺序会改结果的那个历史坑）。等径 0.40 时
       1.0 倍 = 2R = 0.80 米（两球相切）、2.0 倍 = 4R = 1.60 米，
       于是这一套的圆心距就落在 2R~4R 之间。*/
    PO(balloonCount, 2),
    PO(spawnRule, SPAWN_ADJACENT),
    PO(spawnDistMax, 2.0f), PO(spawnDistMin, 1.0f),
    PO(radiusMax, 0.40f), PO(radiusMin, 0.40f),
    PO(allowSpecial, 0),
    POW(typeWeight, 0, 100), POW(typeWeight, 1, 0),
    POW(typeWeight, 2, 0),   POW(typeWeight, 3, 0),
    PO(motion, MOVE_STILL), PO(driftSpeed, 0.0f), PO(driftRange, 1.00f),
    PO(bobAmp, 0.0f), PO(bobFreq, 0.55f),
    PO(lifetimeSec, 0.0f), PO(missCostsLife, 0), PO(refillDelaySec, 0.0f),
    PO(spawnAnimSec, 0.18f), PO(autoFireInterval, 0.0f),
    PO(hitForgive, 1.06f), PO(comboWindowSec, 1.20f), PO(scoreBase, 520.0f),
    PO(missBreaksCombo, 0),
    PO(lives, 5), PO(timeLimitSec, 60.0f), PO(infiniteLives, 1)
};

/* ---- 4 计时挑战 ---- */
static const PresetOverride ovTime[] = {
    PO(balloonCount, 8),
    PO(spawnRule, SPAWN_RANDOM),
    PO(spawnDistMax, 1.0f), PO(spawnDistMin, 1.0f),
    PO(radiusMax, 0.55f), PO(radiusMin, 0.24f),
    PO(allowSpecial, 1),
    POW(typeWeight, 0, 70), POW(typeWeight, 1, 14),
    POW(typeWeight, 2, 4),  POW(typeWeight, 3, 12),
    PO(motion, MOVE_LINEAR), PO(driftSpeed, 0.70f), PO(driftRange, 1.00f),
    PO(bobAmp, 0.030f), PO(bobFreq, 0.50f),
    PO(lifetimeSec, 3.5f), PO(missCostsLife, 1), PO(refillDelaySec, 0.15f),
    PO(spawnAnimSec, 0.18f), PO(autoFireInterval, 0.12f),   /* 按住左键连发 */
    PO(hitForgive, 1.06f), PO(comboWindowSec, 1.20f), PO(scoreBase, 520.0f),
    PO(missBreaksCombo, 1),
    PO(lives, 5), PO(timeLimitSec, 90.0f), PO(infiniteLives, 1)
};

/* ---- 5 渐进训练 ---- */
static const PresetOverride ovProgress[] = {
    /* 3 个球配 5 条命：**墙上的球是同时出生的，也就同时到时**。
       要是照搬 8 个球配 5 条命，第一波全漏就直接送命 —— 那"渐进"还没
       开始就结束了。

       写成"3 个球配 3 条命"看着像是同一句话的另一种说法，其实还是送命：
       球数必须**严格小于**生命数，否则第一波全漏照样归零。
       自检里那条「第一波全漏也不会直接送命（球数 < 生命）」就是抓这个的。*/
    PO(balloonCount, 3),
    PO(spawnRule, SPAWN_RANDOM),
    PO(spawnDistMax, 1.0f), PO(spawnDistMin, 1.0f),
    PO(radiusMax, 0.60f), PO(radiusMin, 0.30f),
    PO(allowSpecial, 1),
    POW(typeWeight, 0, 70), POW(typeWeight, 1, 14),
    POW(typeWeight, 2, 4),  POW(typeWeight, 3, 12),
    PO(motion, MOVE_LINEAR), PO(driftSpeed, 0.45f), PO(driftRange, 1.00f),
    PO(bobAmp, 0.050f), PO(bobFreq, 0.55f),
    PO(lifetimeSec, 4.5f), PO(missCostsLife, 1), PO(refillDelaySec, 0.20f),
    PO(spawnAnimSec, 0.18f), PO(autoFireInterval, 0.0f),
    PO(hitForgive, 1.06f), PO(comboWindowSec, 1.20f), PO(scoreBase, 520.0f),
    PO(missBreaksCombo, 1),
    PO(lives, 5), PO(timeLimitSec, 60.0f), PO(infiniteLives, 0)
};

/* ---- 6 混合靶场 ---- */
static const PresetOverride ovMixed[] = {
    PO(balloonCount, 12),
    PO(spawnRule, SPAWN_RANDOM),
    PO(spawnDistMax, 1.0f), PO(spawnDistMin, 1.0f),
    PO(radiusMax, 0.55f), PO(radiusMin, 0.18f),
    PO(allowSpecial, 1),
    /* 四类都占相当比重，逼着玩家做取舍：金球值钱但打得慢，慢球好打但不值钱。*/
    POW(typeWeight, 0, 45), POW(typeWeight, 1, 25),
    POW(typeWeight, 2, 10), POW(typeWeight, 3, 20),
    PO(motion, MOVE_SINE), PO(driftSpeed, 0.85f), PO(driftRange, 0.85f),
    PO(bobAmp, 0.060f), PO(bobFreq, 0.45f),
    PO(lifetimeSec, 0.0f), PO(missCostsLife, 0), PO(refillDelaySec, 0.0f),
    PO(spawnAnimSec, 0.18f), PO(autoFireInterval, 0.10f),
    PO(hitForgive, 1.06f), PO(comboWindowSec, 1.20f), PO(scoreBase, 520.0f),
    PO(missBreaksCombo, 0),
    PO(lives, 5), PO(timeLimitSec, 60.0f), PO(infiniteLives, 1)
};

/* ---- 7 精准挑战 ---- */
static const PresetOverride ovPrecision[] = {
    PO(balloonCount, 1),                       /* paramsClamp 也会强制这一条 */
    PO(spawnRule, SPAWN_RANDOM),               /* 只有一个球时这条规则无对象可用 */
    PO(spawnDistMax, 1.0f), PO(spawnDistMin, 1.0f),
    PO(radiusMax, 0.18f), PO(radiusMin, 0.14f),
    PO(allowSpecial, 0),
    POW(typeWeight, 0, 100), POW(typeWeight, 1, 0),
    POW(typeWeight, 2, 0),   POW(typeWeight, 3, 0),
    PO(motion, MOVE_JUMP), PO(driftSpeed, 2.20f), PO(driftRange, 1.00f),
    PO(bobAmp, 0.050f), PO(bobFreq, 0.70f),
    PO(lifetimeSec, 0.0f), PO(missCostsLife, 0), PO(refillDelaySec, 0.0f),
    PO(spawnAnimSec, 0.12f), PO(autoFireInterval, 0.0f),
    PO(hitForgive, 1.00f),                     /* 严格贴着球面，一点不宽容 */
    PO(comboWindowSec, 1.20f), PO(scoreBase, 520.0f),
    PO(missBreaksCombo, 0),
    PO(lives, 5), PO(timeLimitSec, 60.0f), PO(infiniteLives, 1)
};


#define PRESET_ENTRY(nm, ds, md, rp, arr) \
    { nm, ds, md, rp, arr, (int)(sizeof(arr) / sizeof(arr[0])) }

/* ★ 这张表的**顺序就是界面上的顺序**，也是存进存档的那个下标。
   「跟踪训练」从末尾（第 8 位）挪到了第 4 位，也就是「小快靶和计时挑战
   中间」。挪位置不只是界面好看：`记录.dat` 的八档最高分与 `历史.dat`
   每一条记的都是这个下标，所以三份存档各自升了版本号，读到旧档时按
   config.cpp 的同名映射函数把下标重排一次 —— 详见
   presetIndexMigrateLegacyOrder()。

   顺序钉死在自检里（「预设顺序」那一组逐位比对名字），谁再动一下就会红。*/
const GamePreset g_presets[PRESET_COUNT] = {
    PRESET_ENTRY(L"固定靶",
        L"墙上固定 8 个等径气球，不移动，击破后当帧补位。无时限，不因漏球结束。"
        L"用于熟悉准星与视角控制。",
        MODE_RANGE, 0.0f, ovFixed),

    PRESET_ENTRY(L"移动靶",
        L"气球横向漂移并作纵向浮动，半径在区间内随机取值。用于练习持续跟枪。",
        MODE_RANGE, 0.0f, ovMoving),

    PRESET_ENTRY(L"小快靶",
        L"气球半径小、横向速度快，并按间隔跳到出球区另一位置，出现动画时长很短。"
        L"用于练习预判与提前量。",
        MODE_RANGE, 0.0f, ovSmallFast),

    PRESET_ENTRY(L"跟踪训练",
        L"墙上恒定 2 个等径气球，不移动，气球不会超时消失。击破一个后，"
        L"新气球出现在剩余那颗气球周围 2R 至 4R 的环带内（R 为气球半径），"
        L"且不与刚被击破的位置重叠。",
        MODE_RANGE, 0.0f, ovTrack),

    PRESET_ENTRY(L"计时挑战",
        L"限时 90 秒。气球横向漂移，存活 3.5 秒未被击破则消失并扣除一条生命。"
        L"按住左键连续射击。",
        MODE_TIME, 0.0f, ovTime),

    PRESET_ENTRY(L"渐进训练",
        L"墙上固定 3 个气球，初始 5 条生命，气球超时消失扣除一条。"
        L"每 30 秒速度提升一档、半径缩小一档。",
        MODE_PROGRESS, 0.55f, ovProgress),

    PRESET_ENTRY(L"混合靶场",
        L"墙上 12 个气球，混有普通球、小快球、金球、慢球四类，"
        L"各类的半径、速度与分值不同。",
        MODE_RANGE, 0.0f, ovMixed),

    PRESET_ENTRY(L"精准挑战",
        L"墙上只保留 1 个小气球，命中判定收窄至贴球面。气球消失即结束本局。",
        MODE_PRECISION, 0.0f, ovPrecision)
};

const int g_presetCount = PRESET_COUNT;

/* ---- 旧档里的预设下标 → 今天这张表里的下标 ----
 *
 * 表怎么来的：把旧的那份 `g_presets[]` 的名字与今天这份的名字**逐个对上**，
 * 记下"老的第 i 位今天排第几"。老顺序是
 *     0 固定靶 1 移动靶 2 小快靶 3 计时挑战 4 渐进训练 5 混合靶场 6 精准挑战 7 跟踪训练
 * 新顺序把跟踪训练插到了第 4 位，于是 3..6 各往后挪一位、7 落到 3：
 *     0→0  1→1  2→2  3→4  4→5  5→6  6→7  7→3
 *
 * 写成**按位置的常量表**而不是"按名字去找"：名字是给用户看的，将来改一个字
 * （比如把「跟踪训练」叫成别的）不该让存档换算跟着变 —— 存档认的是位置。
 * 表是不是一个**双射**由自检逐位验（漏一个数就会把两条记录搬到同一格里，
 * 那种错在界面上看不出来）。*/
static const int kPresetIndexOldOrder[PRESET_COUNT] = { 0, 1, 2, 4, 5, 6, 7, 3 };

int presetIndexMigrateLegacyOrder(int oldIndex) {
    if (oldIndex < 0 || oldIndex >= PRESET_COUNT) return oldIndex;
    return kPresetIndexOldOrder[oldIndex];
}

#undef PRESET_ENTRY
#undef PO
#undef POW

/* ============================================================ 套用预设 */

/* 提前声明：定义在下面的参数读写那一节，但套预设要用它（写值不夹取的那个）。*/
static void paramDescStore(Params *p, const ParamDesc *d, float v);

void paramsApplyPreset(Params *p, int preset) {
    int i;
    if (!p) return;
    preset = clampi(preset, 0, PRESET_COUNT - 1);

    p->preset = preset;
    p->mode   = clampi(g_presets[preset].mode, 0, MODE_COUNT - 1);
    p->ramp   = g_presets[preset].ramp;

    /* 先按偏移查出类型再写，而不是照值猜类型：覆盖表里既有 int 字段也有
       float 字段（allowSpecial、lives 是 int，driftSpeed、radiusMin 是 float），
       而 3.0 两种类型都合法 —— 猜错了就是把 float 的位模式按 int 读，
       写进去的是个天文数字。走描述表就不存在猜。*/
    for (i = 0; i < g_presets[preset].ovCount; ++i) {
        const PresetOverride *o = &g_presets[preset].ov[i];
        const ParamDesc *d = paramDescByOffset(o->offset);
        if (!d) continue;   /* 偏移不在描述表里 = 表写错了，自检有断言盯这个 */
        paramDescStore(p, d, o->value);
    }

    /* ★ 整套写完**只夹一次**，中间一次都不夹 —— 这不是省事，是必须的。
     *
     * paramsClamp 里有条规矩：上限低于下限就把两者互换。而预设表里
     * radiusMax 永远写在 radiusMin 前面（因为"下限先写"会立刻被夹成
     * 上一次的上限）。可是**光靠表里的顺序还不够**：从"精准挑战"
     * （0.14 / 0.18）切到"固定靶"（0.40 / 0.40）时，先写 radiusMax = 0.40，
     * 此刻内存里还是 min = 0.14 / max = 0.18，0.40 > 0.18 没事；但从
     * "固定靶"（0.40 / 0.40）切到"精准挑战"时，先写 radiusMax = 0.18，
     * 此刻 min 还是 0.40 —— 0.18 < 0.40，夹取立刻把两者换成 min = 0.18 /
     * max = 0.40，接着再写 radiusMin = 0.14 就成了 0.14 / 0.40，
     * 球比预设写的大了一倍多。
     *
     * 症状是"同一套预设拨过去两次，得到的结果不一样"（自检里那条
     * 「重复套用同一套预设结果不变」就是冲着它去的）。根子在于把一套预设
     * 当成了**一串独立的改动**，而它本来就是**一个原子操作**：要么全生效，
     * 要么不生效，中间态不该被任何人看见。所以改成整体写完再夹。*/
    paramsClamp(p);
}

/* ======================================================== 撤销单条修改 */

/* 这一项是不是"当前这套预设明确写过的"。
   判断依凭是**偏移**，不是值：覆盖表里存的就是偏移，拿偏移去比才是同一件事。
   比值的写法在"预设写的值恰好等于字段当前值"时会误判成没覆盖，进而按
   默认值去恢复 —— 那正是当初那个 bug 的另一种形态。*/
int paramCoveredByPreset(int preset, int descIndex) {
    int i;
    if (descIndex < 0 || descIndex >= g_paramDescCount) return 0;
    preset = clampi(preset, 0, PRESET_COUNT - 1);
    for (i = 0; i < g_presets[preset].ovCount; ++i) {
        if (g_presets[preset].ov[i].offset == g_paramDescs[descIndex].offset) return 1;
    }
    return 0;
}

/* 这一项属于「用户偏好设置」那一段吗。R 键的判据。 */
int paramIsPreference(int descIndex) {
    const ParamDesc *d;
    if (descIndex < 0 || descIndex >= g_paramDescCount) return 0;
    d = &g_paramDescs[descIndex];
    return paramGroupSection(d->group) != 0;
}

/* 「按 R 撤销对这一项的修改」。
 *
 * 这里早先调的是 paramDescReset —— 恢复**出厂默认值**。可小黄点标记的是
 * "与**当前预设**不一致"，两者只在当前预设恰好是默认那套时才重合。换成
 * "移动靶"再改一项按 R，值退回的是"固定靶"的值，与移动靶不同，黄点当然还在。
 * 用户看到的就是"按了恢复，点没消"。
 *
 * 分成两支：
 *   覆盖过的项 → 退回"当前预设在这一项上的取值"，于是黄点必然消失；
 *   没覆盖的项 → 预设本来就管不着它（玩家/画面/声音三组），退回出厂默认值。
 * 返回用的是哪一支，调用方据此换提示语（0 = 预设值，1 = 默认值）。*/
int paramDescResetToPreset(Params *p, int descIndex) {
    const ParamDesc *d;
    Params ref;

    if (!p || descIndex < 0 || descIndex >= g_paramDescCount) return 1;
    d = &g_paramDescs[descIndex];

    if (!paramCoveredByPreset(p->preset, descIndex)) {
        paramDescReset(p, d);
        return 1;
    }

    /* 目标值用**与 appParamDiffersFromPreset 同一套算法**现算：复制当前这份、
       套一遍当前预设、读这一项。两处各算各的话，迟早又对不上 —— 而"对不上"
       在界面上就表现为黄点没消，正是要修的这个毛病。*/
    ref = *p;
    paramsApplyPreset(&ref, p->preset);
    paramDescSet(p, d, paramDescGet(&ref, d));
    return 0;
}

/* ============================================================ 默认值 */

void paramsDefault(Params *p) {
    if (!p) return;
    memset(p, 0, sizeof(*p));

    /* -------------------------------------------------- 玩家 / 画面 / 声音
       这三组是"个人口味"，预设不覆盖它们，所以默认值要写全 ——
       先写这几项，玩法那几组最后交给预设 0 去写。*/
    p->mouseSens   = 0.14f;
    p->invertY     = 0;
    p->moveSpeed   = 3.4f;
    p->fovDeg      = 75.0f;
    p->headBob     = 1;

    p->wallStyle   = WALL_PLASTER;
    p->showWeapon  = 1;
    p->crossStyle  = CROSS_DOT;
    p->crossScale  = 1.00f;
    p->crossColorIdx = CROSSCOL_GREEN;    /* #7ce27c 那一系 */
    p->showTracer  = 1;

    p->soundOn     = 1;      /* 默认可闻。音量本身不再可调，固定 VOLUME_DEF。*/

    p->showStats   = 1;

    /* 这几项既不在预设覆盖里，也得有个初始值：颜色相关的三枚。*/
    /* HSV 范围是 S∈[0.62,0.88]、V∈[0.88,1.00]，
       这里存"上限"，下限在 balloon.cpp 里按同样的比例取。*/
    p->colorSat    = 0.88f;
    p->colorVal    = 1.00f;
    p->seed        = 0;                    /* 0 = 每次启动随机 */

    /* ------------------------------------------------------------ 玩法
       玩法参数**全部由预设 0（固定靶）写出来**，这里不另抄一份。
       抄一份的坏处很实在：以后想把固定靶从 8 个球改成 10 个球，得改两处，
       漏一处就会出现"新装的默认值是 10、按一下恢复默认变成 8"。
       默认预设定成固定靶，所以这里直接落预设 0。*/
    paramsApplyPreset(p, 0);

    /* applyPreset 最后会调 paramsClamp，而 clamp 不看玩家/画面那几组 ——
       这个顺序是安全的：玩法先写、体验后写、谁也不会盖掉谁。*/
}

/* ============================================================ 夹取 */

void paramsClamp(Params *p) {
    int i;
    if (!p) return;

    /* ------------------------------------------------------------ 气球 */
    p->balloonCount = clampi(p->balloonCount, 1, MAX_BALLOONS);
    p->spawnRule    = clampi(p->spawnRule, 0, SPAWN_COUNT - 1);
    p->spawnDistMin = clampf(p->spawnDistMin, SPAWN_DIST_LO, SPAWN_DIST_HI);
    p->spawnDistMax = clampf(p->spawnDistMax, SPAWN_DIST_LO, SPAWN_DIST_HI);
    /* 上限不能低于下限：交换而不是报错，用户拖反了也能用 ——
       与半径那一对是同一条规矩。（旧档读进来这两项是 0，被上面夹成 1.0/1.0，
       也就是"两球相切"，正是「紧贴相邻」那套几何。）*/
    if (p->spawnDistMax < p->spawnDistMin) {
        float t = p->spawnDistMin;
        p->spawnDistMin = p->spawnDistMax;
        p->spawnDistMax = t;
    }
    p->radiusMin    = clampf(p->radiusMin, 0.10f, 2.00f);
    p->radiusMax    = clampf(p->radiusMax, 0.10f, 2.00f);
    /* 上限不能低于下限：交换而不是报错，用户拖反了也能用。*/
    if (p->radiusMax < p->radiusMin) {
        float t = p->radiusMin; p->radiusMin = p->radiusMax; p->radiusMax = t;
    }
    p->colorSat   = clampf(p->colorSat, 0.0f, 1.0f);
    p->colorVal   = clampf(p->colorVal, 0.2f, 1.0f);   /* 再暗就看不清球边了 */
    /* 种子的上界给得比"面板好看"更宽：出图模式的默认种子是 20260930
       （作者的日期），上界若定成 999999 就会被悄悄夹掉 —— 面板上那个
       "随机种子"会显示 999999 而不是 20260930。所以这里的上界按
       "能放下出图种子"来定。*/
    p->seed = clampi(p->seed, 0, 99999999);
    p->allowSpecial = p->allowSpecial ? 1 : 0;
    for (i = 0; i < BALLOON_TYPE_COUNT; ++i)
        p->typeWeight[i] = clampi(p->typeWeight[i], 0, 1000);

    /* ------------------------------------------------------------ 运动 */
    p->motion     = clampi(p->motion, 0, MOVE_COUNT - 1);
    p->driftSpeed = clampf(p->driftSpeed, 0.0f, 3.0f);
    p->driftRange = clampf(p->driftRange, 0.0f, 1.0f);
    p->bobAmp     = clampf(p->bobAmp, 0.0f, 0.60f);
    p->bobFreq    = clampf(p->bobFreq, 0.0f, 4.0f);

    /* ------------------------------------------------------------ 节奏 */
    p->lifetimeSec      = clampf(p->lifetimeSec, 0.0f, 60.0f);
    p->missCostsLife    = p->missCostsLife ? 1 : 0;
    p->refillDelaySec   = clampf(p->refillDelaySec, 0.0f, 3.0f);
    p->spawnAnimSec     = clampf(p->spawnAnimSec, 0.0f, 1.50f);
    p->autoFireInterval = clampf(p->autoFireInterval, 0.0f, 1.00f);

    /* ---------------------------------------------------- 命中与计分 */
    p->hitForgive      = clampf(p->hitForgive, 0.50f, 2.00f);
    p->comboWindowSec  = clampf(p->comboWindowSec, 0.20f, 5.00f);
    p->scoreBase       = clampf(p->scoreBase, 50.0f, 2000.0f);
    p->missBreaksCombo = p->missBreaksCombo ? 1 : 0;

    /* ------------------------------------------------------------ 玩家 */
    p->mouseSens = clampf(p->mouseSens, 0.02f, 1.00f);
    p->invertY   = p->invertY ? 1 : 0;
    p->moveSpeed = clampf(p->moveSpeed, 0.5f, 12.0f);
    p->fovDeg    = clampf(p->fovDeg, 45.0f, 110.0f);
    p->headBob   = p->headBob ? 1 : 0;

    /* ------------------------------------------------------------ 画面 */
    p->wallStyle     = clampi(p->wallStyle, 0, WALL_STYLE_COUNT - 1);
    p->showWeapon    = p->showWeapon ? 1 : 0;
    p->crossStyle    = clampi(p->crossStyle, 0, CROSS_COUNT - 1);
    p->crossScale    = clampf(p->crossScale, 0.40f, 2.50f);
    p->crossColorIdx = clampi(p->crossColorIdx, 0, CROSSCOL_COUNT - 1);
    p->showTracer    = p->showTracer ? 1 : 0;

    /* ------------------------------------------------------------ 声音 */
    p->soundOn = p->soundOn ? 1 : 0;

    /* ------------------------------------------------------------ 训练 */
    p->preset        = clampi(p->preset, 0, PRESET_COUNT - 1);
    p->mode          = clampi(p->mode, 0, MODE_COUNT - 1);
    p->ramp          = clampf(p->ramp, 0.0f, DIFF_FACTOR_MAX);
    p->lives         = clampi(p->lives, 1, 20);   /* 存活数范围 1..20 */
    p->timeLimitSec  = clampf(p->timeLimitSec, 10.0f, 600.0f);
    p->infiniteLives = p->infiniteLives ? 1 : 0;
    p->showStats     = p->showStats ? 1 : 0;

    /* 精准挑战：墙上"恒为 1 个"是这套预设定义的一部分，不给改的余地。*/
    if (p->mode == MODE_PRECISION) p->balloonCount = 1;
}

/* ============================================================ 相等与指纹 */

int paramsEqual(const Params *a, const Params *b) {
    return memcmp(a, b, sizeof(Params)) == 0;
}

unsigned paramsHash(const Params *p) {
    /* FNV-1a：够散、够短、够稳定。用来做"参数拨动前后画面必须变"的对照。*/
    const unsigned char *d = (const unsigned char *)p;
    unsigned h = 2166136261u;
    size_t i;
    for (i = 0; i < sizeof(Params); ++i) {
        h ^= d[i];
        h *= 16777619u;
    }
    return h;
}

/* ============================================================ 难度爬升
 *
 * 这里早先是一个「难度档」枚举，三档各带一组倍率，后来换成了
 * 预设自带的 ramp 浮点数：
 *   · 名词少一个 —— 面板上不再有"难度"这一项；
 *   · 预设想涨多快就写多快，不必挤进低/中/高三个整数档；
 *   · "轻松档就该慢一点"那类**档位自带的手感倍率删掉了** —— 它和玩法
 *     参数是两套平行的旋钮，同一个"球变快"要改两处，调起来会互相打架。
 *     现在只有一处：paramsApplyPreset 写进去的 driftSpeed / radius。
 * ramp = 0 表示整局不爬升，倍率恒为 1.0（固定靶、计时挑战等都用 0）。
 */

float difficultyFactor(float ramp, double elapsedSec) {
    float f;
    if (elapsedSec < 0.0) elapsedSec = 0.0;     /* 负时间不该让难度低于 1 */
    if (ramp < 0.0f) ramp = 0.0f;
    f = 1.0f + (float)(elapsedSec / (double)DIFF_RAMP_REF_SEC) * ramp;
    return clampf(f, 1.0f, DIFF_FACTOR_MAX);
}

float diffSpeedScale(float ramp, double elapsedSec) {
    return difficultyFactor(ramp, elapsedSec);
}

float diffLifetimeScale(float ramp, double elapsedSec) {
    float f = difficultyFactor(ramp, elapsedSec);
    if (f < 1e-4f) f = 1e-4f;
    return 1.0f / f;                            /* 越难，球活得越短 */
}

float diffRadiusScale(float ramp, double elapsedSec) {
    float f = difficultyFactor(ramp, elapsedSec);
    if (f < 1e-4f) f = 1e-4f;
    return 1.0f / sqrtf(f);                     /* 越难，球越小（平方根，别缩太快） */
}

float effectiveDriftSpeed(const Params *p, double elapsedSec) {
    return p->driftSpeed * diffSpeedScale(p->ramp, elapsedSec);
}

float effectiveLifetime(const Params *p, double elapsedSec) {
    if (p->lifetimeSec <= 0.0f) return 0.0f;       /* 0 表示不限，缩放后还是不限 */
    return p->lifetimeSec * diffLifetimeScale(p->ramp, elapsedSec);
}

float effectiveRadiusScale(const Params *p, double elapsedSec) {
    return diffRadiusScale(p->ramp, elapsedSec);
}

/* ============================================================ 计分换算 */

float radiusToRefPixels(float radiusMeters, const Params *p) {
    /* 把 3D 半径（米）线性映射回像素半径区间 14..42，
       这样 scoreBase/r 出去的分值量级才稳定（见 config.h 的说明）。

       映射的参照系是**用户自己设的半径区间**：区间里偏小的球给高分。
       于是"把最小半径调小"立刻就反映到分数上，调参时不至于白调。

       上下限一样时（固定靶就是这种情况：全部同尺寸）区间退化成一点，
       没有"相对大小"可言。这时取区间的**中点**当参照 —— 既不是白送
       最高分（那样 8 个一样大的球个个满分，连击一叠就没边了），
       也不是最低分。这里早先返回的是 SCORE_REF_R_MIN，等于给固定靶
       白送最高分，是随"默认值改成固定靶"才暴露出来的问题。*/
    float lo = p->radiusMin, hi = p->radiusMax;
    float t;
    if (hi - lo < RADIUS_SAME_EPS)
        return 0.5f * (SCORE_REF_R_MIN + SCORE_REF_R_MAX);
    t = clampf((radiusMeters - lo) / (hi - lo), 0.0f, 1.0f);
    return lerpf(SCORE_REF_R_MIN, SCORE_REF_R_MAX, t);
}

/* ============================================================ 持久化
 * 二进制 + 魔数 + 版本 + 长度 + CRC32。改结构体时把版本号加一，
 * 旧文件会被判为不兼容并回落到默认值（不崩、不读到脏数据）。
 */
#define SETTINGS_MAGIC   0x31535242u   /* "BRS1" 小端 */
/* 版本号的含义：**结构体布局变了就加一**。加"音量/静音"两项时从 1 加到 2，
    v1 的存档会被判为不兼容并回落到默认值 —— 宁可让用户重设一次，
   也不要把旧字节按新布局解释（那样每个字段都会串位，比丢设置糟得多）。
    后来删了九个字段、加了 preset/ramp/crossColorIdx，布局又变了，加到 3；
    v2 的"参数.dat"会被判为不兼容 —— **只是不读它，不会删它**。
    再把 volume(float) 与 mute(int) 合并成一个 soundOn(int)，布局又变了，
    加到 4，v3 的档同样只判不兼容，不删。

    ★ 加到 5 时**布局没变**：加一是为了标明"这份档里那个 preset 字段写的是
    换位前的下标"。「跟踪训练」从第 8 位挪到第 4 位之后，v4 档里的 preset
    得换算一次才对应得上今天的第几套（见 presetIndexMigrateLegacyOrder）。
    不升版本号的话，读回来的是个"看起来合法、其实指错预设"的数 ——
    那正是最难发现的一类错。*/
#define SETTINGS_VERSION 5u

/* paramsLoad 还认得的最老版本。v3 与 v4 的 Params 一样长（都是 176 字节），
   光看长度分不出来，只有版本号能分；v3 及更早的字段布局与今天不同，
   照旧整份不读（不删）。*/
#define SETTINGS_VERSION_MIN 4u

/* 版本 4 期间出现过的 Params 长度里**最小的那个**：末尾追 spawnRule
   之前是 176 字节（44 个 4 字节字段），之后是 180。paramsLoad 拿它当下界。
   写成历史事实而不是"sizeof(Params) - 4"：将来再加字段时，这个数不该跟着
   往上爬 —— 它记的是"最老的、还愿意读的那份档有多长"。*/
#define SETTINGS_LEGACY_SIZE_MIN 176u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t size;      /* sizeof(Params) */
    uint32_t crc;       /* 对 payload 求 CRC32 */
} SettingsHeader;

int paramsSave(const Params *p, const wchar_t *path) {
    SettingsHeader h;
    FILE *f;
    if (!p || !path) return -1;
    f = _wfopen(path, L"wb");
    if (!f) return -1;

    h.magic   = SETTINGS_MAGIC;
    h.version = SETTINGS_VERSION;
    h.size    = (uint32_t)sizeof(Params);
    h.crc     = crc32Buf(p, sizeof(Params));

    if (fwrite(&h, 1, sizeof(h), f) != sizeof(h) ||
        fwrite(p, 1, sizeof(Params), f) != sizeof(Params)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

int paramsLoad(Params *p, const wchar_t *path) {
    SettingsHeader h;
    Params tmp;
    FILE *f;
    if (!p || !path) return -1;
    f = _wfopen(path, L"rb");
    if (!f) return -1;

    if (fread(&h, 1, sizeof(h), f) != sizeof(h)) { fclose(f); return -1; }
    if (h.magic != SETTINGS_MAGIC) { fclose(f); return -1; }
    /* ★ 版本判据由"必须等于"放宽为"落在 [MIN, 当前] 这个区间里"。
       v4 与 v5 的布局**完全一样**，只差 preset 那一个字段的口径（换位前 / 后），
       所以 v4 照读、读完把 preset 换算一次即可 —— 用户那 44 项设置原样回来。
       比 v4 更老的（v1~v3）字段布局不同，仍旧整份不读、也不删。*/
    if (h.version < SETTINGS_VERSION_MIN || h.version > SETTINGS_VERSION) {
        fclose(f);
        return -1;
    }
    /* ★ 判据从"必须相等"放宽为"不大于即可"。
     *
     * 理由是往 Params **末尾**追了一个字段（spawnRule），旧档于是成了
     * 新布局的一个前缀：对得上的部分逐字节相同，只是短了一截。旧的那种
     * "必须相等"会让这份档整个作废 —— 用户面板上那 44 项设置白白回默认值，
     * 换来的却只是"多读一个 4 字节的字段"。
     *
     * 三条边界：
     *   · h.size > sizeof(Params)：文件比当前程序还新（装过更新的版本）。
     *     这时按新布局解释旧代码能认的那部分**也是安全的**（前缀性质），
     *     但那种文件的字段含义可能已经变了，宁可当没有档 —— 不读、不删。
     *   · 短读出来那截之外的字节由 memset 归零 → 新字段取 0，
     *     而 0 就是 SPAWN_RANDOM = 原有行为，不引入任何变化。
     *   · CRC 照旧按**文件里写的长度**算。旧档的校验和当初就是对 176 字节
     *     求的，拿新尺寸去算必然对不上，等于白读。*/
    if (h.size > (uint32_t)sizeof(Params)) { fclose(f); return -1; }
    if (h.size < SETTINGS_LEGACY_SIZE_MIN) { fclose(f); return -1; }
    memset(&tmp, 0, sizeof(tmp));
    if (fread(&tmp, 1, h.size, f) != h.size) { fclose(f); return -1; }
    fclose(f);

    /* 校验和不对说明文件被改过或写坏了 —— 宁可回默认值也不要用脏参数。*/
    if (crc32Buf(&tmp, h.size) != h.crc) return -1;

    /* ★ v4 档里那个 preset 是**换位前**的下标，换算一次。
       参数值本身一个字节都不用动 —— 这份档存的是整份快照，换位换的是
       "第几号对应哪一套"，不是值。*/
    if (h.version < SETTINGS_VERSION)
        tmp.preset = presetIndexMigrateLegacyOrder(tmp.preset);

    *p = tmp;
    paramsClamp(p);
    return 0;
}

/* ============================================================ 参数描述表
 *
 * 见 config.h 里那段说明。表本身是纯数据：面板按它画行，自检按它逐项验
 * "范围与 paramsClamp 一致""改了必然影响参数指纹"。加一个新参数，只要
 * 在这里补一行，面板上就自动多一行 —— 不用去动 render.cpp 的排版。
 *
 * 排布顺序 = 面板上的显示顺序，与 Params 结构体里的分组一一对应。对照着
 * 看，少一项多一项一眼就能发现；自检里有一条断言把项数对一遍。
 */

/* 顺序必须与 ParamGroup 的枚举一致。有一条自检断言把两边逐项对一遍 ——
   重排枚举时，这里要不是同步改，面板上会出现"组名和内容对不上"，
   而这种错在截图里看着很正常（名字都认识），只有断言拦得住。*/
const wchar_t *const g_paramGroupNames[PARAM_GROUP_COUNT] = {
    L"气球", L"运动", L"节奏", L"判定与计分", L"训练",
    L"玩家", L"画面", L"声音"
};
const int g_paramGroupCount = PARAM_GROUP_COUNT;

/* 面板二级上的两个大标题。前半段是预设会覆盖的玩法参数，
   后半段是换了预设也不该被动的个人口味。*/
const wchar_t *const g_paramSectionNames[PARAM_SECTION_COUNT] = {
    L"预设方案设置", L"用户偏好设置"
};

int paramGroupSection(int group) {
    if (group < 0 || group >= PARAM_GROUP_COUNT) return PARAM_SECTION_COUNT - 1;
    return (group < PARAM_GROUP_SECTION_SPLIT) ? 0 : 1;
}

/* 偏移量写成宏，图的是"字段名拼错立刻编译不过"。用裸数字的话，写错一个
   字节偏移照样能编过，运行时改的是隔壁那个参数 —— 而且多半看不出来。*/
#define OF(f)     ((int)offsetof(Params, f))
#define OI(f, i)  ((int)(offsetof(Params, f) + sizeof(((Params *)0)->f[0]) * (size_t)(i)))

/* lo/hi 必须与 paramsClamp 逐项一致。自检里有一条断言专门比这个：
   把 lo 下面一点、hi 上面一点塞进去夹一次，看回来的还是不是 lo/hi。*/
const ParamDesc g_paramDescs[] = {
/* ---------------------------------------------------------------- 气球
   最后一列是侧栏里的说明。**允许为 NULL** —— 简单名词可以无需说明，
   而写一句"数量：气球的数量"这种废话比留空更糟。规则只有两条：
   要么 NULL，要么非空；自检里有一条断言逐项盯着。*/
/* name           组                     类型     偏移                lo     hi            步长   枚举表          个数         单位     说明 */
{ L"数量",       PARAM_GROUP_BALLOON, PK_INT,   OF(balloonCount), 1.0f, (float)MAX_BALLOONS, 1.0f, NULL, 0, L"个",
  NULL },
{ L"生成位置",   PARAM_GROUP_BALLOON, PK_ENUM,  OF(spawnRule),    0.0f, (float)(SPAWN_COUNT - 1), 1.0f,
  g_spawnNames, SPAWN_COUNT, NULL,
  L"新气球的位置来源。「全场随机」在出球区内均匀取位；「邻位生成」在墙上另一颗气球的周围取位，"
  L"距离范围由下面两项给出，且不与刚被击破的位置重叠。墙上少于两颗气球时按「全场随机」处理。" },
{ L"生成距离下限", PARAM_GROUP_BALLOON, PK_FLOAT, OF(spawnDistMin), SPAWN_DIST_LO, SPAWN_DIST_HI, 0.10f, NULL, 0, L"倍",
  L"「邻位生成」时新球到另一颗气球的最小圆心距，1 倍 = 两颗球的半径之和。取 1 倍即两球相切。" },
{ L"生成距离上限", PARAM_GROUP_BALLOON, PK_FLOAT, OF(spawnDistMax), SPAWN_DIST_LO, SPAWN_DIST_HI, 0.10f, NULL, 0, L"倍",
  L"「邻位生成」时新球到另一颗气球的最大圆心距。取值大于下限时，两者之间的环带按面积均匀取位；"
  L"环带落在出球区之外的部分不参与取位。" },
{ L"最大半径",   PARAM_GROUP_BALLOON, PK_FLOAT, OF(radiusMax),    0.10f, 2.00f,   0.05f, NULL, 0, L"米",
  L"气球半径的上限。与「最小半径」取值相同时，全部气球等径。" },
{ L"最小半径",   PARAM_GROUP_BALLOON, PK_FLOAT, OF(radiusMin),    0.10f, 2.00f,   0.05f, NULL, 0, L"米",
  L"气球半径的下限。取值小于「最大半径」时，半径在该区间内随机取值。" },
{ L"饱和度",     PARAM_GROUP_BALLOON, PK_FLOAT, OF(colorSat),     0.00f, 1.00f,   0.05f, NULL, 0, NULL,
  L"气球颜色的饱和度。取 0 为无彩色，取 1 为最高饱和度。" },
{ L"亮度",       PARAM_GROUP_BALLOON, PK_FLOAT, OF(colorVal),     0.20f, 1.00f,   0.05f, NULL, 0, NULL,
  L"气球颜色的明度。取值越低，气球与墙面的对比度越低。" },
{ L"随机种子",   PARAM_GROUP_BALLOON, PK_INT,   OF(seed),         0.0f, 99999999.0f, 1.0f, NULL, 0, NULL,
  L"气球颜色与位置的随机数种子。取 0 时每次运行重新取种；取非 0 值时，同一数值产生相同布局。" },
{ L"特殊气球",   PARAM_GROUP_BALLOON, PK_BOOL,  OF(allowSpecial), 0.0f,  1.0f,    1.0f, NULL, 0, NULL,
  L"关闭后只生成普通气球，下面四项权重的取值不再生效。" },
{ L"普通球权重", PARAM_GROUP_BALLOON, PK_INT,   OI(typeWeight, 0), 0.0f, 1000.0f, 5.0f, NULL, 0, NULL,
  L"四类气球的生成权重，按四项总和折算为比例，因此只比较四者之比。" },
{ L"小快球权重", PARAM_GROUP_BALLOON, PK_INT,   OI(typeWeight, 1), 0.0f, 1000.0f, 5.0f, NULL, 0, NULL,
  NULL },
{ L"金球权重",   PARAM_GROUP_BALLOON, PK_INT,   OI(typeWeight, 2), 0.0f, 1000.0f, 5.0f, NULL, 0, NULL,
  L"金球的生成权重。金球半径与普通球相同，计分倍率为普通球的两倍。" },
{ L"慢球权重",   PARAM_GROUP_BALLOON, PK_INT,   OI(typeWeight, 3), 0.0f, 1000.0f, 5.0f, NULL, 0, NULL,
  L"慢球的生成权重。慢球半径大于普通球、速度低于普通球，单次击破得分低于普通球。" },

/* ---------------------------------------------------------------- 运动 */
{ L"运动方式",   PARAM_GROUP_MOTION, PK_ENUM,  OF(motion),        0.0f, (float)(MOVE_COUNT - 1), 1.0f,
  g_motionNames, MOVE_COUNT, NULL,
  L"气球的运动方式。「随机跳变」指气球按固定间隔直接跳到出球区内的另一位置。" },
{ L"漂移速度",   PARAM_GROUP_MOTION, PK_FLOAT, OF(driftSpeed),    0.00f, 3.00f,  0.05f, NULL, 0, L"米/秒",
  L"气球横向移动的速度。取 0 时气球不发生横向位移。" },
{ L"漂移幅度",   PARAM_GROUP_MOTION, PK_FLOAT, OF(driftRange),    0.00f, 1.00f,  0.05f, NULL, 0, NULL,
  L"气球横向移动的范围，以出球区半宽为 1 的比例值。" },
{ L"浮动幅度",   PARAM_GROUP_MOTION, PK_FLOAT, OF(bobAmp),        0.00f, 0.60f,  0.01f, NULL, 0, L"米",
  L"气球纵向浮动的振幅。取 0 时气球不发生纵向位移。" },
{ L"浮动频率",   PARAM_GROUP_MOTION, PK_FLOAT, OF(bobFreq),       0.00f, 4.00f,  0.05f, NULL, 0, L"次/秒",
  L"气球纵向浮动的频率，即每秒往复次数。取值越高，单位时间内的纵向位移次数越多。" },

/* ---------------------------------------------------------------- 节奏 */
{ L"存活时限",   PARAM_GROUP_RHYTHM, PK_FLOAT, OF(lifetimeSec),      0.00f, 60.0f, 0.50f, NULL, 0, L"秒",
  L"单个气球在墙上的最长停留时间，超过则自动消失。取 0 表示不限时。" },
{ L"漏球扣命",   PARAM_GROUP_RHYTHM, PK_BOOL,  OF(missCostsLife),    0.0f,  1.0f,  1.0f,  NULL, 0, NULL,
  L"气球超时消失时是否扣除生命。该值需与「存活时限」「补位延迟」配合设置。" },
{ L"补位延迟",   PARAM_GROUP_RHYTHM, PK_FLOAT, OF(refillDelaySec),   0.00f, 3.00f, 0.05f, NULL, 0, L"秒",
  L"击破后到新气球出现之间的间隔。取 0 时当帧补位。" },
{ L"出现动画",   PARAM_GROUP_RHYTHM, PK_FLOAT, OF(spawnAnimSec),     0.00f, 1.50f, 0.02f, NULL, 0, L"秒",
  L"新气球半径由 0 增至设定值所用的时间。该段时间内的气球不参与命中判定。" },
{ L"连射间隔",   PARAM_GROUP_RHYTHM, PK_FLOAT, OF(autoFireInterval), 0.00f, 1.00f, 0.02f, NULL, 0, L"秒",
  L"按住左键时的连续射击间隔。取 0 时每次点击只射击一次。" },

/* ---------------------------------------------------------- 判定与计分 */
{ L"判定宽容",   PARAM_GROUP_SCORE, PK_FLOAT, OF(hitForgive),      0.50f, 2.00f,   0.02f, NULL, 0, L"倍",
  L"命中判定半径相对气球显示半径的倍率。取 1.0 时判定球面与显示球面重合。" },
{ L"连击窗口",   PARAM_GROUP_SCORE, PK_FLOAT, OF(comboWindowSec),  0.20f, 5.00f,   0.10f, NULL, 0, L"秒",
  L"判定为连击的最大击破间隔，每次击破后重新计时。" },
{ L"计分基准",   PARAM_GROUP_SCORE, PK_FLOAT, OF(scoreBase),      50.00f, 2000.0f, 20.0f, NULL, 0, L"分",
  L"全部得分的倍率。该值只改变分数量级，不影响气球的数量、速度与判定。" },
{ L"漏球断连击", PARAM_GROUP_SCORE, PK_BOOL,  OF(missBreaksCombo), 0.0f,  1.0f,    1.0f,  NULL, 0, NULL,
  L"气球超时消失时是否将连击数清零。" },

/* ---------------------------------------------------------------- 训练
   这一组从表尾挪到了这里 —— 它是玩法参数（预设会覆盖），
   必须与上面四组连成"预设方案设置"那一段。见 config.h 里枚举上的说明。*/
{ L"初始生命",   PARAM_GROUP_TRAINING, PK_INT,   OF(lives),        1.0f,  20.0f, 1.0f,   NULL, 0, L"条",
  L"每局开始时的生命数。气球超时消失并扣除生命，生命归零时本局结束。" },
{ L"时限",       PARAM_GROUP_TRAINING, PK_FLOAT, OF(timeLimitSec), 10.0f, 600.0f, 5.0f,  NULL, 0, L"秒",
  L"单局时长上限。仅在限时规则（如「计时挑战」）下生效，其余预设不使用该值。" },
{ L"无限生命",   PARAM_GROUP_TRAINING, PK_BOOL,  OF(infiniteLives), 0.0f, 1.0f,  1.0f,   NULL, 0, NULL,
  L"开启后不再扣除生命，本局不会因漏球结束。" },
{ L"统计面板",   PARAM_GROUP_TRAINING, PK_BOOL,  OF(showStats),     0.0f, 1.0f,  1.0f,   NULL, 0, NULL,
  L"是否显示左上角的实时数据（剩余时间、连击数、命中率）。" },

/* ---------------------------------------------------------------- 玩家 */
{ L"鼠标灵敏度", PARAM_GROUP_PLAYER, PK_FLOAT, OF(mouseSens),   0.02f, 1.00f, 0.01f, NULL, 0, NULL,
  L"鼠标位移到视角转角的换算系数。更换鼠标或修改 DPI 后需重新设定。" },
{ L"反转 Y 轴",  PARAM_GROUP_PLAYER, PK_BOOL,  OF(invertY),     0.0f,  1.0f,  1.0f,  NULL, 0, NULL,
  NULL },
{ L"移动速度",   PARAM_GROUP_PLAYER, PK_FLOAT, OF(moveSpeed),   0.50f, 12.0f, 0.20f, NULL, 0, L"米/秒",
  NULL },
{ L"视野角",     PARAM_GROUP_PLAYER, PK_FLOAT, OF(fovDeg),     45.00f, 110.0f, 1.0f,  NULL, 0, L"度",
  L"垂直视野角。取值增大时可视范围变宽，物体的屏幕投影尺寸减小，画面边缘的透视变形增强。" },
{ L"头部晃动",   PARAM_GROUP_PLAYER, PK_BOOL,  OF(headBob),     0.0f,  1.0f,  1.0f,  NULL, 0, NULL,
  L"移动时是否叠加视角的纵向周期性位移。" },

/* ---------------------------------------------------------------- 画面 */
{ L"墙面风格",   PARAM_GROUP_GRAPHICS, PK_ENUM,  OF(wallStyle),      0.0f, (float)(WALL_STYLE_COUNT - 1), 1.0f,
  g_wallStyleNames, WALL_STYLE_COUNT, NULL,
  L"靶场墙面的材质。取值越深，墙面与气球颜色的对比度越高。" },
{ L"手持装置",   PARAM_GROUP_GRAPHICS, PK_BOOL,  OF(showWeapon),     0.0f,  1.0f,  1.0f,  NULL, 0, NULL,
  L"是否绘制画面右下角的发射器模型。" },
{ L"曳光",       PARAM_GROUP_GRAPHICS, PK_BOOL,  OF(showTracer),     0.0f,  1.0f,  1.0f,  NULL, 0, NULL,
  L"是否绘制射击时的弹道轨迹。" },
{ L"准星样式",   PARAM_GROUP_GRAPHICS, PK_ENUM,  OF(crossStyle),     0.0f, (float)(CROSS_COUNT - 1), 1.0f,
  g_crossStyleNames, CROSS_COUNT, NULL,
  L"准星的形状。「单点」的绘制面积最小，遮挡的目标区域最少。" },
{ L"准星大小",   PARAM_GROUP_GRAPHICS, PK_FLOAT, OF(crossScale),     0.40f, 2.50f, 0.05f, NULL, 0, L"倍",
  NULL },
{ L"准星颜色",   PARAM_GROUP_GRAPHICS, PK_ENUM,  OF(crossColorIdx),  0.0f, (float)(CROSSCOL_COUNT - 1), 1.0f,
  g_crossColorNames, CROSSCOL_COUNT, NULL,
  L"准星的绘制颜色。取值需与当前墙面的亮度形成对比才能看清。" },

/* ---------------------------------------------------------------- 声音
   这一组只剩这一个开关。「音量」滑杆删了 —— 值固化成 VOLUME_DEF；
   原来的「静音」反过来写成「声音」，免得"关掉静音"这种双重否定。
   拨到"开"的时候会放一声界面音，等于把原来那行「试听」并了进来。*/
{ L"声音",       PARAM_GROUP_AUDIO, PK_BOOL,  OF(soundOn),      0.0f, 1.0f,  1.0f,  NULL, 0, NULL,
  L"音效总开关。切换为「开」时播放一次界面音效。" },
};

const int g_paramDescCount = (int)(sizeof(g_paramDescs) / sizeof(g_paramDescs[0]));

/* 每组第一行的下标，末尾补一个 = 总行数的哨兵，方便算"本组占哪几行"。
   这几个数是数出来的 —— 自检里有一条断言逐组复核"该组每一行的 group
   字段都等于组号"，数错了当场就报出来，不会让面板把行分到别的组去。*/
const int g_paramGroupFirst[PARAM_GROUP_COUNT + 1] = { 0, 14, 19, 24, 28, 32, 37, 43, 44 };

#undef OF
#undef OI

/* ============================================================ 准星调色板 */

Color3 crossColorOf(int idx) {
    switch (clampi(idx, 0, CROSSCOL_COUNT - 1)) {
    case CROSSCOL_GREEN:  return color3(0.486f, 0.886f, 0.486f);  /* #7ce27c */
    case CROSSCOL_RED:    return color3(0.941f, 0.353f, 0.353f);
    case CROSSCOL_YELLOW: return color3(0.980f, 0.855f, 0.290f);
    case CROSSCOL_CYAN:   return color3(0.400f, 0.910f, 0.941f);
    case CROSSCOL_WHITE:
    default:              return color3(0.960f, 0.965f, 0.970f);
    }
}

/* ============================================================ 描述表读写 */

float paramDescGet(const Params *p, const ParamDesc *d) {
    const unsigned char *base = (const unsigned char *)p + d->offset;
    switch (d->kind) {
    case PK_BOOL:
    case PK_INT:
    case PK_ENUM:   { int v;   memcpy(&v, base, sizeof(v)); return (float)v; }
    case PK_FLOAT:  { float v; memcpy(&v, base, sizeof(v)); return v; }
    default: return 0.0f;
    }
}

/* 只写值，**不调 paramsClamp**。给 paramsApplyPreset 用 —— 它要一口气写完
   十几项再统一收尾，中途每写一项都夹一次会出问题，见那边的注释。
   单看每一项的区间夹取留着：那是纯字段级的，不依赖别的字段。*/
static void paramDescStore(Params *p, const ParamDesc *d, float v) {
    unsigned char *base = (unsigned char *)p + d->offset;

    if (v < d->lo) v = d->lo;
    if (v > d->hi) v = d->hi;

    switch (d->kind) {
    case PK_BOOL:
    case PK_INT:
    case PK_ENUM: {
        /* 四舍五入而不是截断：步长累加出来可能是 70.99999，
           截断就永远卡在 70 上不去。*/
        int iv = (int)(v >= 0.0f ? (v + 0.5f) : (v - 0.5f));
        memcpy(base, &iv, sizeof(iv));
        break;
    }
    case PK_FLOAT: {
        float fv = v;
        memcpy(base, &fv, sizeof(fv));
        break;
    }
    default: break;
    }
}

void paramDescSet(Params *p, const ParamDesc *d, float v) {
    if (!p || !d) return;
    paramDescStore(p, d, v);
    /* 夹两遍不是多余：paramsClamp 里还有"上下限互换""精准挑战强制 1 个球"
       这类跨字段的规矩，光靠字段级的区间管不到。*/
    paramsClamp(p);
}

void paramDescReset(Params *p, const ParamDesc *d) {
    Params def;
    paramsDefault(&def);
    paramDescSet(p, d, paramDescGet(&def, d));
}

/* 整数项加一个整数步长。**不经过 float** —— 见 config.h 里的说明：
   float 的 24 位有效位装不下「随机种子」那个 0..99999999 的量程，
   在 2^24 以上做 v + 1 会先被舍入掉，再写回 int 就还是原值。
   界面表现为"按 → 这一项没反应"，而且**只在种子偏大的时候出现**，
   特别容易当成偶发。*/
int paramDescAddInt(Params *p, const ParamDesc *d, int delta) {
    int v;

    if (!p || !d) return 0;
    if (d->kind != PK_INT && d->kind != PK_BOOL && d->kind != PK_ENUM) return 0;

    /* 先用 Get 取出 int（它内部就是 memcpy 出来的，没经过浮点）。*/
    v = (int)paramDescGet(p, d);
    v += delta;
    if ((float)v < d->lo) v = (int)d->lo;
    if ((float)v > d->hi) v = (int)d->hi;
    memcpy((unsigned char *)p + d->offset, &v, sizeof(v));
    paramsClamp(p);
    return 1;
}

/* 开关与枚举项专用的"绕圈"加法。凡是可循环的项，一直按向左和向右
   会在它的几个取值里循环；有滑动条的项除外。

   ★ "有滑动条"在界面上就是画了那根细条的两类：PK_FLOAT 与 PK_INT
   （isNum，见 render.cpp）。它们是一条**连续区间**，"到头绕回去"没有意义 ——
   把「随机种子」从 99999999 绕回 0 会让人以为出了故障。所以绕圈只给
   PK_BOOL（两态）与 PK_ENUM（几个离散选项）—— 它们的取值本来就是枚举，
   绕圈才是"一格一格转一圈"的自然手感。

   返回 1 = 处理了（含"该项只有一个取值、绕了等于没绕"那种）；0 = 不是绕圈的类，
   调用方该走 paramDescAddInt / paramDescSet。*/
int paramDescAddWrap(Params *p, const ParamDesc *d, int delta) {
    int v, lo, hi, n;

    if (!p || !d) return 0;
    if (d->kind != PK_BOOL && d->kind != PK_ENUM) return 0;

    lo = (int)d->lo;
    hi = (int)d->hi;
    n  = hi - lo + 1;
    v  = (int)paramDescGet(p, d);
    if (n <= 1) return 1;          /* 只有一个取值：吃掉这次按键，什么都不用做 */

    /* C 的 % 对负数给负余数，先加一个 n 再取模，两头就都绕得对。
       delta 可能很大（Shift 是十倍步长），取模之后自然落回区间内。*/
    v = lo + (((v - lo + delta) % n) + n) % n;
    paramDescSet(p, d, (float)v);
    return 1;
}

const wchar_t *paramEnumName(const ParamDesc *d, int v) {
    if (!d || v < 0 || v >= d->enumCount) return L"?";
    if (d->enumNames && v < d->enumCount) return d->enumNames[v];
    /* 走到这里说明某个 PK_ENUM 项忘了填 enumNames —— 那不是"没有名字"，
       是表写漏了。回 "?" 让它在面板上显眼地露出来，别静默地少一列。*/
    return L"?";
}

const ParamDesc *paramDescByOffset(int offset) {
    int i;
    for (i = 0; i < g_paramDescCount; ++i)
        if (g_paramDescs[i].offset == offset) return &g_paramDescs[i];
    return NULL;
}

void paramDescText(const Params *p, const ParamDesc *d, wchar_t *buf, int n) {
    if (!buf || n <= 0) return;
    switch (d->kind) {
    case PK_BOOL:
        _snwprintf(buf, (size_t)(n - 1), L"%ls",
                   paramDescGet(p, d) >= 0.5f ? L"开" : L"关");
        break;
    case PK_ENUM:
        _snwprintf(buf, (size_t)(n - 1), L"%ls",
                   paramEnumName(d, (int)(paramDescGet(p, d) + 0.5f)));
        break;
    case PK_INT:
        _snwprintf(buf, (size_t)(n - 1), L"%d", (int)(paramDescGet(p, d) + 0.5f));
        break;
    case PK_FLOAT:
    default: {
        float v = paramDescGet(p, d);
        /* 按步长决定小数位，免得"0.020"这种读起来费劲。*/
        if (d->step < 0.01f)      _snwprintf(buf, (size_t)(n - 1), L"%.3f", v);
        else if (d->step < 0.1f)  _snwprintf(buf, (size_t)(n - 1), L"%.2f", v);
        else                      _snwprintf(buf, (size_t)(n - 1), L"%.1f", v);
        break;
    }
    }
    buf[n - 1] = L'\0';

    if (d->unit && n > (int)wcslen(buf) + 2) {
        size_t used = wcslen(buf);
        _snwprintf(buf + used, (size_t)(n - 1) - used, L" %ls", d->unit);
        buf[n - 1] = L'\0';
    }
}

/* ============================================================ 默认存档路径 */

void paramsDefaultPath(wchar_t *path, int count) {
    wchar_t *slash;
    if (!path || count < 8) return;
    path[0] = L'\0';
    GetModuleFileNameW(NULL, path, (DWORD)count);
    slash = wcsrchr(path, L'\\');
    if (slash) slash[1] = L'\0';
    else path[0] = L'\0';
    wcsncat(path, L"参数.dat", (size_t)(count - 1 - (int)wcslen(path)));
}
