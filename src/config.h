/* ============================================================================
 * config.h —— 全部可调参数的**唯一真源**
 *
 * 这里只放"数据"：参数结构体、默认值、取值范围、难度预设表、常量。
 * 不放任何行为。改默认值只改这一个文件。
 *
 * 设计取向（重要）：参数表不是一堆散装开关，而是一套可存可读回的配置 ——
 * 每一项都必须在代码里真的被读到。拨动它前后算一次场景指纹，指纹必须变，
 * 这是识别"假滑杆"（接了线却没接到玩法上）的判据。
 * ==========================================================================*/
#ifndef CONFIG_H
#define CONFIG_H

#include "core.h"

/* ============================================================ 全局常量 */

#define APP_TITLE_W     L"FPS 气球训练场"     /* 窗口标题，中文，走宽字符 */
#define WINDOW_W_DEF    1600
#define WINDOW_H_DEF     900
#define FIXED_DT        (1.0 / 120.0)         /* 玩法固定步长，与渲染帧率解耦 */

#define MAX_BALLOONS      32                  /* 对象池容量；"数量"这一项的参数上限与它相同 */
#define MAX_EFFECTS       64
#define MAX_SHARDS        18                  /* 单个气球爆散的最大碎片数 */
#define MAX_POPUPS        24                  /* 飘分同时存在的条数 */
#define MAX_DECALS       200                  /* 墙上残留的彩色斑（球贴在墙前炸，残迹留在墙面） */

/* ---------------------------------------------------- 固定下来的观感常量
 *
 * 下面这几个值原先都是参数面板上的滑杆，后来从面板上删掉了（设置项过多
 * 没有必要）。删掉的是**滑杆**，不是行为：它们各自最合理的那个值固化在这里，
 * 代码照旧读，只是不再开放调节。将来需要时随时可以加回面板。*/
#define RENDER_SCALE_DEF     1.00f   /* 渲染分辨率倍数，1.0 = 与窗口 1:1 */
#define LIGHT_INTENSITY_DEF  1.00f   /* 主光强度倍数 */
#define FOG_DENSITY_DEF      0.020f  /* 远处变灰的浓度 */
#define FLOOR_TINT_DEF       0.35f   /* 地面与天花板的基色明度 */
#define MAX_PARTICLES_DEF    220     /* 爆散碎片的总预算 */
/* 音量。面板上只留"声音"开关，值固化成这个 ——
   audio 层照旧按 0..1 用，只是不再开放调节。*/
#define VOLUME_DEF           0.70f

/* ============================================================ 场景尺寸（米） */

#define ROOM_HALF_W      5.5f      /* 房间半宽 */
#define ROOM_H           5.0f      /* 房间高 */
#define ROOM_BACK_Z      5.0f      /* 后墙在 +z */
#define WALL_Z          -6.0f      /* 气球墙在 -z，距玩家出生点 4.8 米 */
/* 气球墙必须**横跨整个房间**。早先房间 14 米宽、气球墙 11 米宽，两者之间
   在左右各留了一条 1.5 米的缝；玩家往前走时视线会从缝里穿出去看到虚空，
   画面上就是左右各一条刺眼的黑边。房间收窄到与墙同宽，缝就没了 ——
   自检里有一条断言专门盯着这个不变量。*/
#define WALL_HALF_W      ROOM_HALF_W
#define PLAY_LINE_Z     -3.2f      /* 警戒线：玩家不能越过这条线（z > 这条线） */

/* 出球区域在墙面上的默认占比（相对墙面可视范围），四面留边。*/
#define FIELD_MARGIN_X   0.10f
#define FIELD_MARGIN_Y   0.12f

/* 玩家的碰撞半径与身高，用于房间边界夹取。*/
#define PLAYER_RADIUS    0.34f
#define EYE_HEIGHT_DEF   1.66f

/* ============================================================ 枚举 */

typedef enum {
    MODE_RANGE = 0,     /* 恒量靶场：N 恒定、打一补一、无时限无生命（默认） */
    MODE_TIME,          /* 计时挑战：限时内尽可能多打 */
    MODE_PROGRESS,      /* 渐进训练：难度随时间爬升 + 生命 */
    MODE_PRECISION,     /* 精准挑战：墙上恒为 1 个球，脱靶即结束 */
    MODE_COUNT
} GameMode;

typedef enum {
    MOVE_STILL = 0,     /* 静止不动 */
    MOVE_LINEAR,        /* 匀速横向漂移，撞边界反弹 */
    MOVE_SINE,          /* 正弦横向摆动 */
    MOVE_JUMP,          /* 每隔一段时间瞬移到出球区内另一处 */
    MOVE_COUNT
} BalloonMotion;

typedef enum {
    WALL_PLASTER = 0,   /* 抹灰墙（默认，最干净） */
    WALL_BRICK,         /* 砖墙 */
    WALL_TILE,          /* 瓷砖 */
    WALL_CONCRETE,      /* 毛坯混凝土 */
    WALL_STYLE_COUNT
} WallStyle;

typedef enum {
    CROSS_CROSS = 0,    /* 十字 */
    CROSS_DOT,          /* 十字 + 中心点 */
    CROSS_CIRCLE,       /* 圆环 + 中心点 */
    CROSS_DOTONLY,      /* 只有一个点 */
    CROSS_COUNT
} CrossStyle;

typedef enum {
    CROSSCOL_WHITE = 0,     /* 准星颜色。原先用三个滑杆（红/绿/蓝）调，
                               面板上就是三个看不懂的名词；改成一个调色板，
                               名词从三个变一个。 */
    CROSSCOL_GREEN,
    CROSSCOL_RED,
    CROSSCOL_YELLOW,
    CROSSCOL_CYAN,
    CROSSCOL_COUNT
} CrossColorIdx;

typedef enum {
    BALLOON_NORMAL = 0, /* 普通球：基准尺寸与速度 */
    BALLOON_SMALL,      /* 小快球：小、漂得快、分高 */
    BALLOON_GOLD,       /* 金球：稀有，分数翻倍 */
    BALLOON_SLOW,       /* 慢球：大、慢、分低，容易打但拉低平均分 */
    BALLOON_TYPE_COUNT
} BalloonType;

/* 新气球生成在哪（「跟踪训练」用得上）。
   这不是"模式"，是一个普通的玩法参数 —— 面板上能调、预设能覆盖，
   所以不需要往 GameMode 里加东西。*/
typedef enum {
    SPAWN_RANDOM = 0,   /* 出球区内均匀随机（其余七套预设的行为，也是默认） */
    SPAWN_ADJACENT,     /* 在"墙上剩下的那颗球"周围的环带里生成；环带由下面
                           spawnDistMin / spawnDistMax 两项定。范围可调之后，
                           上限取大时它已经不"紧贴"了，所以面板上叫
                           「邻位生成」。*/
    SPAWN_COUNT
} SpawnRule;

/* 邻位生成的距离范围（倍）。**1 倍 = 新球与搭档的半径之和** ——
   等径时 1 倍就是 2R（两球正好相切），2 倍就是 4R。
   下限的地板就是 1.0：比 1.0 小意味着两颗球的圆盘要重叠，那不是"邻位"。
   上限给到 6 倍（等径时 12R）是"面板够用"的取法；出球区只有 9.9 × 3.8 米，
   取到大值的那一段环带会有相当一部分落到墙外，那时实际可用位置由"环带 ∩
   出球区 − 死球圆盘"这块区域决定（见 balloon.cpp 的 ringAllowedArcAt）。
   写成宏而不是在两处各抄一份：paramsClamp 与参数描述表的 lo/hi 必须逐字一致，
   自检里有一条断言专盯这件事。*/
#define SPAWN_DIST_LO   1.0f
#define SPAWN_DIST_HI   6.0f

/* ============================================================ 参数结构体 */

typedef struct {
    /* ---------------------------------------------------------- 气球 */
    int   balloonCount;                 /* 墙上恒定气球数 N（1..MAX_BALLOONS） */
    float radiusMin;                    /* 最小半径（米） */
    float radiusMax;                    /* 最大半径（米） */
    float colorSat;                     /* 颜色饱和度上限（HSV 的 S） */
    float colorVal;                     /* 颜色亮度上限（HSV 的 V） */
    int   seed;                         /* 随机种子；0 = 每次启动随机 */
    int   allowSpecial;                 /* 是否允许出现特殊气球 */
    int   typeWeight[BALLOON_TYPE_COUNT]; /* 四类气球的权重（0 表示该类不出现） */

    /* ---------------------------------------------------------- 运动 */
    int   motion;                       /* BalloonMotion */
    float driftSpeed;                   /* 横向漂移速度（米/秒） */
    float driftRange;                   /* 漂移幅度占出球区半宽的比例（0..1） */
    float bobAmp;                       /* 纵向浮动幅度（米） */
    float bobFreq;                      /* 纵向浮动频率（Hz） */

    /* ---------------------------------------------------------- 节奏 */
    float lifetimeSec;                  /* 单个气球的存活时限（秒）；0 = 不限 */
    int   missCostsLife;                /* 超时逃走是否扣命 */
    float refillDelaySec;               /* 击破后多久补位（秒）。**默认 0：立刻补** */
    float spawnAnimSec;                 /* 出现动画时长（秒） */
    float autoFireInterval;             /* 按住左键的连射间隔（秒）；0 = 只响应单次点击 */

    /* ---------------------------------------------------------- 命中与计分 */
    float hitForgive;                   /* 判定半径倍数；1.0 = 严格贴着球面 */
    float comboWindowSec;               /* 连击窗口（秒），每次击破刷新 */
    float scoreBase;                    /* 计分基准：基础分 = 520 / 半径（像素） */
    int   missBreaksCombo;              /* 漏球是否打断连击 */

    /* ---------------------------------------------------------- 玩家 */
    float mouseSens;                    /* 鼠标灵敏度（度/像素） */
    int   invertY;                      /* 是否反转 Y 轴 */
    float moveSpeed;                    /* 移动速度（米/秒） */
    float fovDeg;                       /* 垂直视野角（度） */
    int   headBob;                      /* 是否开启头部晃动 */

    /* ---------------------------------------------------------- 画面 */
    int   wallStyle;                    /* WallStyle */
    int   showWeapon;                   /* 是否画手中的发射装置 */
    int   crossStyle;                   /* CrossStyle */
    float crossScale;                   /* 准星大小倍数 */
    int   crossColorIdx;                /* CrossColorIdx（准星颜色，调色板取色） */
    int   showTracer;                   /* 是否画曳光 */

    /* ---------------------------------------------------------- 声音
       「音量」滑杆已从面板上删掉（音量相关设置只保留开关即可），
       「静音」反过来写成「声音」：1 = 出声，0 = 静音。语义反过来是有意的
       —— 面板上那一项要问的是"要不要声音"，而不是"要不要静音"
       （后者选"开"反而是没声，一个开关两种读法，最容易读反）。
       音量本身固化成 VOLUME_DEF，不再开放调节；audio 层照旧收 0..1。*/
    int   soundOn;                      /* 1 = 出声（默认），0 = 静音 */

    /* ---------------------------------------------------------- 训练 */
    int   preset;                       /* GamePreset 下标 —— 面板一级菜单上的那一项 */
    int   mode;                         /* GameMode（**内部规则**，由预设决定，不进面板） */
    float ramp;                         /* 每 30 秒的难度涨幅（由预设决定，不进面板） */
    int   lives;                        /* 初始生命数 */
    float timeLimitSec;                 /* 计时挑战的时限（秒） */
    int   infiniteLives;                /* 无限生命（自由练习用） */
    int   showStats;                    /* 是否显示统计面板 */

    /* ★ 后加的字段。**刻意声明在整个结构体的最后** —— 新字段追加在
       末尾不会挪动任何已有字段的偏移，于是旧的「参数.dat」正好是新布局
       的一个前缀，paramsLoad 放宽一句 size 判据就能无损读回来（旧档读进来
       spawnRule 自然是 0 = SPAWN_RANDOM = 原有行为）。若把它插进气球组
       那一段里，全表偏移整体移位，旧档就只能整份作废。
       面板上它排在气球组（group 决定位置，与声明位置无关）。*/
    int   spawnRule;                    /* SpawnRule：新气球生成在哪 */

    /* ★ 同样**追加在最末尾**（理由与上面那段一字不差：
       旧档仍然是新布局的前缀，SETTINGS_VERSION 因此不用动）。
       两项只在 spawnRule == SPAWN_ADJACENT 时生效：新球圆心到搭档圆心的
       距离落在 [下限, 上限] × (R_新 + R_搭档) 这个环带里，并且不许与"刚被
       打掉那颗"的圆盘重叠、不许越出出球区。
       默认 1.0 / 1.0 = 下限等于上限 = 两球正好相切 ——
       这就是「紧贴相邻」那套几何，一字不差地成了默认值。*/
    float spawnDistMin;                 /* 邻位生成的圆心距下限（倍） */
    float spawnDistMax;                 /* 邻位生成的圆心距上限（倍） */
} Params;

/* ============================================================ 预设方案
 *
 * **「模式」和「难度」其实是同一个概念** —— 都是"一组提前写好的可玩参数"。
 * 所以它们合并成一张表，面板上只剩「预设方案」这一个名词。
 *
 * 一套预设 = 名字 + 一段说明 + 规则(mode) + 涨幅(ramp) + 一串参数覆盖。
 * 参数覆盖用 **offsetof 偏移** 表达，和参数描述表同一套路数：
 * 字段名写错编译不过，也不可能出现"表里写了、代码里没这个字段"。
 *
 * 边界（有意为之）：预设只覆盖**玩法类**参数（气球 / 运动 / 节奏 / 判定 /
 * 训练），不碰玩家手感与画面声音 —— 换一套玩法不该顺手改掉玩家的鼠标灵敏度。
 */
typedef struct {
    int   offset;      /* 字段在 Params 里的字节偏移，用 offsetof 写 */
    float value;       /* 套用后的值 */
} PresetOverride;

typedef struct {
    const wchar_t *name;             /* 一级菜单上的名字 */
    const wchar_t *desc;             /* 侧栏里的一段说明，必填 */
    int   mode;                      /* GameMode（内部规则） */
    float ramp;                      /* 每 30 秒的涨幅，0 = 不爬升 */
    const PresetOverride *ov;        /* 参数覆盖表，可为 NULL */
    int   ovCount;
} GamePreset;

/* 预设套数。写成宏而不是只留 g_presetCount 变量：ScoreBook 里要按预设
   分档存最好成绩（rec[PRESET_COUNT]），数组长度必须是编译期常量。
   另有一条自检断言盯着 g_presetCount == PRESET_COUNT，两边不会走散。*/
#define PRESET_COUNT 8

extern const GamePreset g_presets[PRESET_COUNT];
extern const int g_presetCount;

/* 预设下标在**旧档里的意思**，换算成它在今天这张表里的意思。
 *
 * 为什么需要它：三个存档里都存着"当时用的是第几套预设"这个下标 ——
 * `参数.dat` 存 `Params.preset`、`记录.dat` 按预设分档（`rec[PRESET_COUNT]`）、
 * `历史.dat` 每一条记着 `preset`。「跟踪训练」从第 8 位挪到第 4 位之后，
 * **旧档里的 3 今天是 4、旧档里的 7 今天是 3**。不换算的话，
 * 用户以前的「计时挑战最高分」会挂到「跟踪训练」名下 —— 数据一个字没丢，
 * 但名字全错位了，而且看不出错。
 *
 * 越界的下标**原样返回**（不猜、不夹取）：宁可在面板上看到一个对不上的名字，
 * 也不要把一条记录搬到某个"看起来差不多"的档里。 */
int presetIndexMigrateLegacyOrder(int oldIndex);

/* 各枚举的中文名。HUD 与参数面板都从这里取，不在别处另写一份，
   也就不存在"面板改了、HUD 没改"这种对不上的情况。
   注意：这些字必须同时出现在 hud.cpp 的 g_hudStrings[] 里 ——
   字形图集是照着那张表收集的，自检里有一条断言逐字复核。*/
extern const wchar_t *const g_modeNames[MODE_COUNT];
extern const wchar_t *const g_motionNames[MOVE_COUNT];
extern const wchar_t *const g_spawnNames[SPAWN_COUNT];
extern const wchar_t *const g_wallStyleNames[WALL_STYLE_COUNT];
extern const wchar_t *const g_crossStyleNames[CROSS_COUNT];
extern const wchar_t *const g_crossColorNames[CROSSCOL_COUNT];
/* 预设名不在这里 —— 它是 g_presets[i].name，只有那一份。*/

/* ============================================================ 参数描述表
 *
 * 参数面板要靠它才能"参数加一个、面板自动多一行"。没有这张表的话，面板里
 * 就得把字段名、上下限、步长再手写一遍 —— 而"面板上写的区间"和
 * "paramsClamp 里真正生效的区间"是两份东西，迟早对不上，用户就会遇到
 * "拖到头了还是没变化"这种说不清的问题。
 *
 * 表里的 lo/hi 必须与 paramsClamp 一致，自检有断言逐项比对这一点；
 * 另外还断言"改动描述表里的任何一项，参数指纹必然变化" ——
 * 接了线却没接到玩法上的**假滑杆**过不了这条。
 */
/* 分组顺序 = **面板上的行顺序**，它还有第二层含义：
   前五组是「预设方案设置」，后三组是「用户偏好设置」。所以组号本身
   就是分类的判据（见 paramGroupSection），不再另写一张归类表。

   ★ TRAINING 从末尾挪到了 SCORE 后面。若照 气球/运动/节奏/判定/
     【玩家/画面/声音】/训练 排，个人口味三组夹在中间、训练掉在最后。
     要让"两类"在表里是**连续的两段**，就只能调枚举顺序，否则行顺序
     会变成 玩法4组 + 偏好3组 + 训练。 */
typedef enum {
    PARAM_GROUP_BALLOON = 0,   /* 气球      ┐ */
    PARAM_GROUP_MOTION,        /* 运动      │ */
    PARAM_GROUP_RHYTHM,        /* 节奏      ├ 预设方案设置 */
    PARAM_GROUP_SCORE,         /* 判定与计分 │ */
    PARAM_GROUP_TRAINING,      /* 训练      ┘ */
    PARAM_GROUP_PLAYER,        /* 玩家      ┐ */
    PARAM_GROUP_GRAPHICS,      /* 画面      ├ 用户偏好设置 */
    PARAM_GROUP_AUDIO,         /* 声音      ┘ */
    PARAM_GROUP_COUNT
} ParamGroup;

/* 预设方案设置 = [0, PARAM_GROUP_SECTION_SPLIT)，用户偏好设置 = 后半段。*/
#define PARAM_GROUP_SECTION_SPLIT 5
#define PARAM_SECTION_COUNT       2

/* 组 → 大类（0 = 预设方案设置，1 = 用户偏好设置）。越界返回 1。*/
int paramGroupSection(int group);
/* 大类的中文名，长度 PARAM_SECTION_COUNT。参数面板的两个大标题用它。*/
extern const wchar_t *const g_paramSectionNames[PARAM_SECTION_COUNT];

typedef enum {
    PK_BOOL = 0,    /* 开关，取值 0/1 */
    PK_INT,         /* 整数 */
    PK_FLOAT,       /* 浮点 */
    PK_ENUM         /* 枚举，名字表从 enumNames 取 */
} ParamKind;

typedef struct {
    const wchar_t *name;        /* 面板上显示的中文名 */
    int            group;       /* ParamGroup */
    int            kind;        /* ParamKind */
    int            offset;      /* 字段在 Params 里的字节偏移，用 offsetof 写 */
    float          lo, hi;      /* 可调范围，必须与 paramsClamp 逐项一致 */
    float          step;        /* 一次 ←/→ 的步长 */
    const wchar_t *const *enumNames;   /* PK_ENUM 专用，别的类型填 NULL */
    int            enumCount;
    const wchar_t *unit;        /* 单位后缀，只给面板看；可以为 NULL */
    /* 侧栏里的一句话说明。**允许为 NULL** —— 简单名词可以无需说明，
       而写一句"数量：气球的数量"这种废话比留空更糟。
       非 NULL 时必须是非空字符串（自检里有一条盯着这个）。*/
    const wchar_t *help;
} ParamDesc;

extern const wchar_t *const g_paramGroupNames[PARAM_GROUP_COUNT];
extern const int g_paramGroupCount;
extern const ParamDesc g_paramDescs[];
extern const int g_paramDescCount;
/* 每个分组的第一行在 g_paramDescs 里的下标，长度 = g_paramGroupCount + 1，
   最后一项是 g_paramDescCount（哨兵），方便算"本组有几行"。*/
extern const int g_paramGroupFirst[];

/* 读写一项。写法上刻意只给"读一个 float"和"写一个 float"两个口子，
   调用方（面板、自检）不需要知道字段是什么类型。*/
float paramDescGet(const Params *p, const ParamDesc *d);
void  paramDescSet(Params *p, const ParamDesc *d, float v);   /* 内含 paramsClamp */
/* 整数项（PK_INT / PK_BOOL / PK_ENUM）专用的加法：**整个计算在 int 里做**，
   一步都不经过 float。为什么非要有这么一个口子：float 只有 24 位有效位，
   2^24 = 16777216 以上每加 1 都会被舍进相邻的可表示值。而「随机种子」的
   量程是 0..99999999、步长 1 —— 它的**默认值 20260930 就已经在 2^24 以上**，
   在那一带按一次 → 要么完全没反应、要么一下跳 2。这是调键位处理时撞出来的。
   返回 0 = 该项不是整数类，调用方应改走 paramDescSet。*/
int   paramDescAddInt(Params *p, const ParamDesc *d, int delta);
/* 开关（PK_BOOL）与枚举（PK_ENUM）专用的**绕圈**加法：到两头就绕到
   另一头。面板上"除了有滑动条的所有项都可循环"说的就是它 ——
   有滑动条的那两类（PK_FLOAT / PK_INT）是一条连续区间，不绕。
   返回 1 = 处理了（调用方不必再试别的口子）；0 = 不是这两类。*/
int   paramDescAddWrap(Params *p, const ParamDesc *d, int delta);
/* 恢复该项的默认值（从 paramsDefault 的结果里取，不另存一份默认值）。*/
void  paramDescReset(Params *p, const ParamDesc *d);
/* 把该项退回**当前预设的值**。内部走的是 paramsApplyPreset 的结果，
   不另存一份"预设快照" —— 预设表改了它就跟着改，不会走散。
   入口是 descIndex（不是 ParamDesc*），因为要先拿预设套一份副本。
   返回 0 = 退回了预设值，1 = 该预设没覆盖这一项、退回的是出厂默认值 ——
   调用方据此换提示语，两条路径在界面上得说得不一样。*/
int   paramDescResetToPreset(Params *p, int descIndex);
/* 第 preset 套预设的覆盖表里，有没有显式写这一项（按字节偏移查）。
   面板按 R 要用它决定"退回预设值"还是"退回默认值"，并给不同的提示。*/
int   paramCoveredByPreset(int preset, int descIndex);

/* 这一项属于「用户偏好设置」那一段吗（玩家 / 画面 / 声音三组，共 12 项）。
   R 键只管这一段，所以判据要单独有一个名字，不要在调用处各写各的。
   实现走 paramGroupSection（组号 → 大类），**不写死下标** —— 以后加组、
   或者挪 PARAM_GROUP_SECTION_SPLIT，这里跟着走，不会偷偷指错半张表。*/
int   paramIsPreference(int descIndex);
/* 该项当前值的显示文本，写进 buf。PK_BOOL 出"开/关"，PK_ENUM 出中文名。*/
void  paramDescText(const Params *p, const ParamDesc *d, wchar_t *buf, int n);

/* 枚举第 v 个取值的名字，越界或没有名字时返回 L"?"。*/
const wchar_t *paramEnumName(const ParamDesc *d, int v);

/* 按字节偏移反查描述表里是哪一项，找不到返回 NULL。
 * 预设覆盖表要靠它把"字段偏移"翻译成"该按 int 写还是按 float 写" ——
 * 值本身分不出类型（3.0 两者都合法），猜错就是把 float 的位模式当 int 读。*/
const ParamDesc *paramDescByOffset(int offset);

#define DIFF_FACTOR_MAX      4.0f   /* 难度倍率上限 */
#define DIFF_RAMP_REF_SEC   30.0f   /* 每 30 秒涨一档 */

/* 半径"大小一致"的判据：上下限相差小于这个值就算同半径。
   预设里的"固定靶 / 大小一致"靠它判定，不用做精确相等比较（浮点）。*/
#define RADIUS_SAME_EPS      0.001f

/* 计分按"基础分 = 520 / 半径像素"来算。3D 里半径以米为单位，
   所以先把半径线性映射到 14..42 的像素区间，再套同一个公式，
   分值的量级才稳定。*/
#define SCORE_REF_R_MIN     14.0f
#define SCORE_REF_R_MAX     42.0f
/* 单球基础分的上下限。
   上限 40 是针对"极小半径"的：520/14 = 37 本来就够不着，留着是防御性的。*/
#define SCORE_MIN           10
#define SCORE_MAX           40

/* 连击 */
#define COMBO_MAX           20
#define COMBO_MULT_STEP     0.05f
#define COMBO_MULT_MAX      2.0f

/* 出现动画的前半段不参与命中判定，避免"球还没长出来就被打中"。*/
#define SPAWN_ARM_RATIO     0.5f

/* 气球生成时的拒绝采样：最多试这么多次，还不行就退而求其次。*/
#define SPAWN_TRIES         40

/* ============================================================ 接口 */

/* 套用一套预设：把该预设的规则（mode / ramp）与全部参数覆盖写进 p，
 * 不覆盖的字段保持原样 —— 于是"个人口味"（鼠标灵敏度、音量、墙面）不会被
 * 换玩法顺手改掉。
 *
 * 幂等：对同一个 p 连套两次，结果完全相同（自检里有断言盯着）。
 * 越界下标会被夹到 [0, g_presetCount)，绝不越界读表。*/
void        paramsApplyPreset(Params *p, int preset);

void        paramsDefault(Params *p);            /* 写入一份默认值 */
void        paramsClamp(Params *p);              /* 把所有字段夹回合法区间 */
int         paramsEqual(const Params *a, const Params *b);
unsigned    paramsHash(const Params *p);         /* 参数指纹（自检用） */

int         paramsLoad(Params *p, const wchar_t *path);   /* 0 = 成功 */
int         paramsSave(const Params *p, const wchar_t *path);

/* 默认存档路径：exe 同目录下的"参数.dat"。path 至少要 MAX_PATH 个 wchar_t。
   放在这里而不是 main.cpp，是为了让"面板里的保存/载入"和"命令行的
   --params"落到同一处 —— 两处各写一遍迟早对不上。*/
void        paramsDefaultPath(wchar_t *path, int count);

/* 由"涨幅 ramp"与已进行时长算出当前难度倍率（1.0 .. DIFF_FACTOR_MAX）。
 * 这里早先接的是「难度档」这个枚举，后来换成预设自带的 ramp 浮点数：
 * 名词少了一个，而且预设想涨多快就写多快，不必挤进低/中/高三个整数档。
 * ramp = 0 表示整局不爬升，倍率恒为 1.0。纯函数，自检直接怼单调性与上下限。*/
float       difficultyFactor(float ramp, double elapsedSec);

/* 把"用户基准值"与"时间爬升"合成出**本帧实际生效的值**。
 * 面板上显示的、玩法层用的，都是这三个函数的返回值 —— 不存在第二处算法。*/
float diffSpeedScale(float ramp, double elapsedSec);     /* 乘在漂移速度上 */
float diffLifetimeScale(float ramp, double elapsedSec);  /* 乘在存活时限上 */
float diffRadiusScale(float ramp, double elapsedSec);    /* 乘在气球半径上 */

/* 便捷封装：直接把参数结构体算成生效值。*/
float effectiveDriftSpeed(const Params *p, double elapsedSec);
float effectiveLifetime(const Params *p, double elapsedSec);   /* 返回 0 表示不限 */
float effectiveRadiusScale(const Params *p, double elapsedSec);

/* 由"3D 半径（米）"反推像素等价半径，供计分用。*/
float       radiusToRefPixels(float radiusMeters, const Params *p);

/* 准星调色板：把 CrossColorIdx 换算成 RGB。
 * 三个分量 **只在这里写一次** —— 面板只认下标，HUD 只认下标，
 * 于是"面板上写的颜色名"和"实际画出来的颜色"不可能对不上。
 * 下标越界返回白色。*/
Color3      crossColorOf(int idx);

#endif /* CONFIG_H */
