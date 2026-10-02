/* ============================================================================
 * effect.h —— 击破特效：扩散光环、飞散碎片、飘分、墙上残迹
 *
 * 这一层**只做逻辑，不碰 GL**（和 balloon.cpp 一样）。绘制在 render.cpp 与
 * hud.cpp，这样整套特效都能被 --selftest 直接驱动，不用建窗口。
 *
 * 数值取自 2D 圆环的节奏：圆环 + 9 片碎片 + 0.32 秒，这里照搬，
 * 只把"像素/秒"换成"米/秒"。换算是按气球半径做的倍率，所以
 * "碎片飞出多远"的观感与原比例一致。
 * ==========================================================================*/
#ifndef EFFECT_H
#define EFFECT_H

#include "core.h"
#include "config.h"

#define FX_POP_DURATION   0.40f    /* 一次爆散的总时长（秒） */
#define FX_RING_EXPAND    2.40f    /* 光环最终扩散到起始半径的几倍 */
#define FX_POPUP_DURATION 0.85f    /* 飘分停留时长（秒） */

/* 墙上残迹的存活时长。上限之外还要超时消失：一局打久了，墙上应该是
   "最近打过的那些球留下的印子"，而不是一面糊死的墙。*/
#define FX_DECAL_LIFE     26.0f

typedef struct {
    int     active;
    Vec3    pos;                    /* 爆散中心（世界坐标） */
    Color3  color;
    float   r0;                     /* 起始半径 = 气球半径 */
    float   elapsed, duration;
    float   zOff;                   /* 沿墙法线弹出来的距离，让碎片有厚度 */
    int     shardCount;
    float   shardAngle[MAX_SHARDS];
    float   shardSpeed[MAX_SHARDS]; /* 米/秒 */
    float   shardSize[MAX_SHARDS];
    int     shade[MAX_SHARDS];      /* 0 亮 / 1 原色 / 2 暗，碎片颜色不完全一样 */
} PopEffect;

typedef struct {
    int     active;
    Vec3    pos;                    /* 飘分起点（世界坐标） */
    float   elapsed, duration;
    int     gain;
    int     combo;
} ScorePopup;

/* 墙上的彩色残迹。气球在墙上被击破，颜料自然溅在墙上 —— 这比"地面上留斑"
   更符合这个场景（也让玩家看得出打到哪儿了）。*/
typedef struct {
    int     active;
    float   x, y;                   /* 墙面局部坐标 */
    float   r;
    Color3  color;
    float   life;                   /* 剩余秒数 */
    float   rot;                    /* 贴图旋转（弧度），让每块印子都不一样 */
    float   seed;
} WallDecal;

typedef struct {
    PopEffect  pops[MAX_EFFECTS];
    ScorePopup popups[MAX_POPUPS];
    WallDecal  decals[MAX_DECALS];
    int        decalNext;           /* 环形写入下标 */
    Rng        rng;
} FxPool;

void fxInit(FxPool *fx, unsigned seed);
void fxReset(FxPool *fx, unsigned seed);

/* 在 pos 处产生一次爆散，并顺手在墙上留一块印子。
   gain/combo 不为 0 时同时弹一个飘分。*/
void fxSpawnPop(FxPool *fx, const Params *p, Vec3 pos, float r0, Color3 color,
                int gain, int combo);

void fxUpdate(FxPool *fx, const Params *p, float dt);

#endif /* EFFECT_H */
