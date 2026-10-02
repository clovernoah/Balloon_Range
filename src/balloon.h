/* ============================================================================
 * balloon.h —— 气球对象池
 *
 * 这个文件围绕一条硬性要求：**墙上始终有且仅有 N 个气球，打掉一个
 * 立刻补一个**。做法不是"死了再生成"，而是：
 *
 *   池里永远有 N 个**槽位**（slot 0..N-1），一个不多一个不少。
 *   每个槽位在四种状态之间转：
 *
 *     WAIT ──倒计时到──▶ GROW ──动画走完──▶ LIVE ──被打中/超时──▶ WAIT
 *                          ▲                                    │
 *                          └──────────── ESCAPE（逃走动画）◀─────┘
 *
 *   所以"墙上恒为 N"是**结构性保证**，不是靠调用方记得补位。
 *   默认参数下 refillDelaySec = 0，WAIT 会被立刻跳过，击破当帧就进 GROW。
 *
 * 另外一条硬规矩：**绘制用的坐标与半径，和命中判定用的，是同一份数据**。
 * 也就是本结构里的 x / y / r —— 只有 balloonUpdate 会写它们，渲染层与
 * shot.cpp 都只读。这样"看着打中了却没中"这类问题在结构上就不可能出现。
 * ==========================================================================*/
#ifndef BALLOON_H
#define BALLOON_H

#include "core.h"
#include "config.h"
#include "scene.h"

typedef enum {
    BSLOT_WAIT = 0,   /* 补位等待（只在 refillDelaySec > 0 时才会停留） */
    BSLOT_GROW,       /* 出现动画中 */
    BSLOT_LIVE,       /* 正常存活，可被命中 */
    BSLOT_ESCAPE      /* 超时逃走动画中（不再可被命中） */
} BalloonSlotState;

typedef struct {
    int      state;
    int      type;      /* BalloonType */

    /* ---- 本帧实际生效的绘制/判定数据：只有 balloonUpdate 会写 ---- */
    float    x, y;      /* 墙面局部坐标（x 以墙中轴为 0，y 以地面为 0），单位米 */
    float    r;         /* 当前视觉半径（已含出现动画/逃走动画的缩放） */
    float    zOffset;   /* 离墙面的距离（气球是鼓出来的） */

    /* ---- 内部状态 ---- */
    float    ax, ay;    /* 锚点：漂移/浮动的中心 */
    float    baseR;     /* 参数决定的基准半径（未含动画缩放） */
    float    vx;        /* 横向漂移速度（米/秒，带符号） */
    float    bobPhase;  /* 纵向浮动相位 */
    float    age;       /* LIVE 持续了多久 */
    float    life;      /* 本球的实际存活时限；0 = 不限 */
    float    timer;     /* WAIT 剩余秒数 / GROW 进度 / ESCAPE 进度 */
    float    hue;       /* HSV 色相 */
    int      armed;     /* 出现动画是否已过前半段 —— 没过就不能被命中 */
    unsigned id;

    /* ★ 这个槽位下一次补位时，"刚被打掉的那颗球"留在哪儿。
       「跟踪训练」的邻位生成规则要拿它当禁区（新球不许压在旧位置上）。
       存成槽位自己的字段、而不是在调用链上传一个参数，理由很实在：
       补位有两条路 —— 击破当帧补（balloonKill 里直接 initSlot）和
       延迟补（先进 WAIT，过几帧在 balloonUpdate 里补）。后者隔了好几层，
       旧圆心在那时早就被 initSlot 盖掉了。存下来，两条路的表现才一致。
       它**不参与持久化**（BalloonPool 从来不落盘），换局 memset 就清了。*/
    int      hasDead;   /* 上面三个数是否有效 */
    float    deadX, deadY, deadR;
} Balloon;

typedef struct {
    Balloon  slots[MAX_BALLOONS];
    int      n;          /* 槽位数（= 参数里的气球数量），恒定 */
    float    time;       /* 池自己的时间轴，供正弦运动用 */
    unsigned nextId;
    Rng      rng;
} BalloonPool;

/* 每类气球的倍率表（balloon.cpp 里定义，score.cpp 也要读 scoreMul）。*/
typedef struct {
    const wchar_t *name;
    float radiusMul;
    float speedMul;
    float scoreMul;
} BalloonTypeDef;

extern const BalloonTypeDef g_balloonTypes[BALLOON_TYPE_COUNT];

/* ============================================================ 接口 */

/* 气球之间的最小间距系数：球面之间至少留 6% 的缝。
   **只用在"全场随机"那条路（findSpot）上**，避免随机撒出来的球互相压着。
   「邻位生成」不乘它 —— 那条路的几何前提是"两球正好相切"，
   精确不加缝（见 balloon.cpp 顶部那段说明）。
   定义留在头文件里（统一放在这一处），因为自检要拿同一个数去验
   "随机撒点确实隔着这个下界" —— 另抄一个 1.06 就成了第二份真源。*/
#define BALLOON_MIN_GAP 1.06f

void balloonPoolInit(BalloonPool *bp, const Params *p, unsigned seed,
                     const FieldRect *f);
/* 把墙上填满（开局用）。已经填满时是空操作。elapsed 决定补位球的尺寸
 * （难度爬升会让新球更小，所以这一个参数不能省）。*/
void balloonPoolFill(BalloonPool *bp, const Params *p, const FieldRect *f,
                     double elapsed);

/* 推进一帧。elapsed 是本局已经进行的秒数（用于难度换算），
 * dt 是固定步长。返回本帧**逃走**的气球数（漏球）。*/
int balloonUpdate(BalloonPool *bp, const Params *p, const FieldRect *f,
                  double elapsed, float dt);

/* 击破某个槽位。成功返回 1，并把被击破球的半径/类型交给调用方计分。
 * 对越界下标、非 LIVE 状态、未 armed 的球一律拒绝（返回 0）。*/
int balloonKill(BalloonPool *bp, const Params *p, const FieldRect *f,
                double elapsed, int index, float *outR, int *outType);

/* 射线拾取：返回最近命中的槽位下标，-1 表示没打中。
 * 判定用"当前视觉半径 × 参数里的宽容度"，与渲染完全同源。*/
int balloonPick(const BalloonPool *bp, const Params *p,
                Vec3 rayOrigin, Vec3 rayDir, float *outT);

/* 当前处于 GROW 或 LIVE 的球数 —— "墙上有几个球"就是这个数。*/
int balloonActiveCount(const BalloonPool *bp);

/* 某个槽位的世界坐标。*/
Vec3 balloonWorldPos(const Balloon *b);

/* 颜色：把 hue 与参数里的饱和度/亮度范围合成一个 RGB。纯函数，自检直接怼。*/
Color3 balloonColor(const Balloon *b, const Params *p);

/* 与所有"占着位置的"球保持的最小间距是否满足要求。自检用。*/
int balloonHasMinGap(const BalloonPool *bp, float gap);

#endif /* BALLOON_H */
