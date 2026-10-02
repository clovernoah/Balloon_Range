/* ============================================================================
 * app.h —— 应用状态与主循环
 *
 * App 是"全局状态"的聚合：窗口、GL 上下文、参数、场景、气球池、玩家、统计。
 * 本项目不搞 OO，也不搞单例，App 由 main.cpp 建在栈上往下传。
 * ==========================================================================*/
#ifndef APP_H
#define APP_H

/* ============================================================ 预设方案表
 *
 * 这张表**原先是"一级菜单"**：面板曾分两级，它是上面那一层。
 * 整层一级菜单后来删了（不再分一二级菜单），
 * 但它没有消失 —— 它变成了设置列表顶行「当前预设方案」拉出来的**弹窗**，
 * 行数与构成一字未改：七条预设 + 一条「自定义参数」。
 *
 * 这里原先还挂着"音量 / 静音 / 试听"三行，后来全撤了：
 * 声音只剩一个开关，它属于**参数**，就该待在「用户偏好设置」那一段里，
 * 不该在这张表上单开一行。于是它只剩两类行。
 *
 * 只有两类之后，"光标停在一行、回车确认"这套操作才立得住：
 * 预设行回车 = 套用这一套；「自定义参数」行回车 = 收起弹窗、什么都不改。
 * 那三行动作型条目混在里面，正是"回车到底会干什么"说不清的原因。
 */
typedef enum {
    PMENU_PRESET = 0,      /* 套用第 arg 号预设（arg = 预设下标） */
    PMENU_CUSTOM,          /* 动作：收起弹窗，保留当前取值（「自定义参数」） */
    PMENU_KIND_COUNT
} ParamMenuKind;

typedef struct {
    int kind;
    int arg;               /* PMENU_PRESET 时是预设下标，其余为 -1 */
} ParamMenuRow;

extern const ParamMenuRow g_paramMenu[];
extern const int g_paramMenuCount;
/* 「自定义参数」那一行的下标（= 最后一行）。渲染层用它把那一条和上面八套
   预设用一条横线隔开，输入层用它判断"光标是不是落在它上面" ——
   落在它上面时右栏解释的是"选了会怎样"，而不是某一套预设怎么玩。*/
extern const int g_paramMenuDetailRow;

/* （两个查询函数的声明在文件末尾 —— 它们要 App，而 App 在这一段之后才定义。）*/

#include "gfx.h"
#include "config.h"
#include "scene.h"
#include "balloon.h"
#include "player.h"
#include "input.h"
#include "effect.h"
#include "score.h"
#include "history.h"
#include "hud.h"
#include "audio.h"      /* 合成音效与伪 3D 声像 */

/* 当前停在哪一屏。玩法暂停与否、光标锁不锁、画什么覆盖层，全看它。*/
typedef enum {
    SCREEN_PLAY = 0,    /* 游戏中 */
    SCREEN_PAUSE,       /* 暂停：光标解锁，可以看参数提示 */
    SCREEN_SETTLE,      /* 结算：本局打完，等按键 */
    SCREEN_HISTORY,     /* 历史记录：结算屏按 F 进来，回去也只有 F 一个键 */
    SCREEN_COUNT
} GameScreen;

/* 本局为什么结束。结算屏会照实写出来 —— "怎么死的"比"死了"有用。*/
typedef enum {
    END_NONE = 0,
    END_TIME,           /* 计时挑战的时间到了 */
    END_LIVES,          /* 生命耗尽 */
    END_ESCAPED,        /* 精准挑战里那个球跑了 */
    END_MISSED,         /* 精准挑战里打空了 */
    END_MANUAL,         /* 玩家按 R 自己结算的 */
    END_REASON_COUNT
} EndReason;

typedef struct {
    HWND      hwnd;
    HDC       dc;
    HGLRC     rc;
    int       winW, winH;      /* 客户区尺寸 */
    int       fbW, fbH;        /* 实际渲染分辨率 = 客户区 × renderScale */
    GLuint    blitTex;         /* 分辨率缩放用的中转贴图 */
    int       glReady;

    Params    params;
    Scene     scene;
    BalloonPool pool;
    FieldRect field;
    Player    player;
    unsigned  seed;

    Input     input;
    HudFont   hud;
    FxPool    fx;
    Audio     audio;           /* 音频设备没开成时这一层整体是空操作 */

    /* ---- 本局状态 ---- */
    double    sessionStart;    /* 本局开始的时间戳 */
    double    elapsed;         /* 本局已进行秒数（暂停时不涨） */
    int       running;         /* 主循环还转不转 */
    int       lostFocus;       /* 窗口不在前台 —— 与"玩家自己按的暂停"区分开 */
    int       frame;
    double    fpsAvg;

    Vec3      wish;            /* 移动意图（x = 右，z = 前），由输入层每帧填 */
    int       crouch;          /* 是否蹲下，同上 */

    int       screen;          /* GameScreen */
    int       lives;           /* 剩余生命（infiniteLives 时只用于显示） */
    float     timeLeft;        /* 计时挑战剩余秒数；<= 0 表示本模式不限时 */
    float     fireCooldown;    /* 连射间隔的剩余冷却 */
    float     fireFlash;       /* 枪口火光剩余秒数，画手持装置时用 */
    float     hitFlash;        /* 击破瞬间的屏幕反馈，HUD 用 */
    float     missFlash;       /* 漏球/失手的红色反馈 */
    float     endHold;         /* 达成结束条件后再跑一拍的余量，让最后一个特效放完 */
    int       endReason;       /* EndReason：为什么结束的，结算屏会写出来 */

    /* 曳光：从发射点拉一条线到落点。落点打在墙上、打在地上都是这一条。*/
    float     tracerLife;
    Vec3      tracerFrom, tracerTo;

    RunResult lastResult;
    int       newRecord;       /* 上一次结算有没有破纪录 */
    int       lastRecordScore; /* 用于"距离纪录还差多少" */

    ScoreBook book;
    wchar_t   bookPath[MAX_PATH];
    wchar_t   paramsPath[MAX_PATH];   /* 参数存档路径（--params 或 exe 同目录） */

    /* ---- 历史记录（结算屏按 F 打开） ----
       和 ScoreBook 分开存、分开读，理由见 history.h 顶上的说明。*/
    RunHistory history;
    wchar_t    histPath[MAX_PATH];    /* 历史.dat；空串表示不落盘（出图/自检形态） */
    int        histSel;               /* 光标停在第几次（0 = 最近一次） */
    int        histTop;               /* 滚动窗口第一行 */

    /* ---- 参数面板（Tab 开合） ----
       面板开着的时候**本局暂停推进**（appStep 直接返回）。理由是输入：
       开面板要放开光标，放开了就没法瞄准射击，让对局继续跑等于白送漏球。
       画面照常画，所以调画面类参数时能立刻看到效果 —— 那正是面板的价值。*/
    int       paramOpen;
    /* 不再分一二级：只剩一层"设置菜单"（列表行号见 paramSel 的注释），
       多出来的是列表顶上那一行拉出来的**预设方案弹窗**。用两个独立字段表示
       弹窗，而不是再造一个 paramLevel 的取值 —— "层"这个概念已经没有了，
       留着它只会让后来的人以为还有一层没找着（--panel-level 也一并删了）。*/
    int       paramPopOpen;    /* 预设方案弹窗是否展开 */
    int       paramPopSel;     /* 弹窗光标：0..PRESET_COUNT-1 = 预设，PRESET_COUNT = 「自定义参数」 */

    /* ★ 菜单栏把面板分成两页，于是多了"哪一页"这个前提。
       行号（paramSel / paramTop）从此是**页内行号**，不是全表行号 ——
       预设页 0..29，偏好页 0..11。见 appParamRowCount 的说明。

       两页各自的位置记在 Mem 里，切页时换进换出。**换页不改渲染与滚动的
       任何一行代码**，这是把"页"当成一个视图状态、而不是给每行贴一个页标签
       换来的好处。*/
    int       paramPage;       /* 当前页：0 = 预设方案设置，1 = 用户偏好设置 */
    int       paramOnMenu;     /* 光标停在菜单栏上（1 = 在栏上，列表不画选中行） */
    int       paramSel;        /* ★ 页内行号：预设页 0 = 「当前预设方案」那一行 */
    int       paramTop;        /* 滚动窗口的第一**页内行号** */
    /* ★ 窗口的起点其实落在"格"上，而一行的块可能是 2 格（组标题条 +
       本行）。paramTop 只记到行，被滚掉的那一格记在这儿（0 或 1）——
       也就是"paramTop 那一行的组标题条已经滚出窗口上沿了"。

       有这个量，窗口起点才能落在**任意一格**上，光标离窗口底边的距离才是一个
       与行高无关的常量，按 ↓ 时光标在屏幕上就**一格都不动**。只让窗口停在块头
       上（skip 恒 0）时，末段遇到"上面那一行是带组标题的、它的两格塞不下"就只
       好空出一格，光标当场往上跳 30 像素 —— 测出来的就是这个。

       只有滚动锚定会把它置 1；别处改 paramTop 都是在"整页/整块地跳"，一律
       置回 0。paramTopSkip 越界（≥ 顶行块宽）时渲染层当 0 处理。*/
    int       paramTopSkip;
    int       paramSelMem[2];  /* 两页各自的 paramSel */
    int       paramTopMem[2];  /* 两页各自的 paramTop */
    int       paramDirty;      /* 面板里改过参数 —— 关面板时据此决定要不要落盘 */
    wchar_t   paramMsg[64];    /* 底部提示："已保存"之类，两秒后自己消失 */
    float     paramMsgT;

    /* ---- 统计 ---- */
    int       score;
    int       popped;
    int       missed;
    int       shots;           /* 开火次数 */
    int       hits;            /* 命中次数（用于命中率） */
    int       combo;
    float     comboLeft;       /* 连击窗口剩余秒数 */
    int       bestCombo;
    float     comboPulse;      /* 连击变化时的视觉脉冲，HUD 用 */

    /* ---- 相机基向量缓存（每帧算一次，绘制层直接读） ---- */
    Vec3      camRight, camUp, camFwd, camEye;
} App;

int  appCreateWindow(App *app, int width, int height, int visible);
int  appInitGL(App *app);
void appShutdown(App *app);
void appResize(App *app, int w, int h);

/* 建中文字形图集。必须在 GL 上下文就绪之后调一次。失败返回 -1，
   此时 HUD 会退化成画灰条而不是崩掉。*/
int  appBuildHud(App *app);

/* 开新一局：重置池、玩家、统计。seed 为 0 表示按时间随机。*/
void appResetSession(App *app, unsigned seed);

/* 推进一个固定步长（玩法逻辑）。*/
void appStep(App *app, float dt);

/* 把本局的输入翻译成动作（转视角、走动、开火、暂停、重置）。
   与 appStep 分开是因为输入要按**渲染帧**处理一次，而不是按固定步长 ——
   否则一帧跑两步时鼠标位移会被算两遍，灵敏度直接翻倍。*/
void appHandleInput(App *app);

/* 切到某一屏，顺带处理光标的锁与放。*/
void appSetScreen(App *app, int screen);

/* 参数面板：开、关、以及"关掉时该做的收尾"。分开是为了让自检能
   在没有窗口、没有输入的情况下直接把面板开起来验状态。*/
void appParamOpen(App *app);
void appParamClose(App *app);
int  appParamIsOpen(const App *app);
/* 面板打开期间，一行行的调整动作。按键 → 参数条目由它一个人管，
   渲染层只负责把 g_paramDescs 画出来，两边不各记一份行号。*/
void appParamInput(App *app);
/* 把选中行滚进可视窗口。渲染前调一次即可。*/
void appParamScrollIntoView(App *app);
/* 参数改动之后的收尾：模式联动、需要重建的贴图与气球池。*/
void appParamAfterChange(App *app, int descIndex, const Params *before);

/* 结束本局并结算（写记录、存档）。*/
void appEndSession(App *app);

/* 开一枪：射线拾取 → 击破 → 特效 → 计分。返回 1 表示打中了。*/
int  appFire(App *app);

/* 把视线转到第 k 个活着的气球上（k 会绕回）。出图模式用它摆出"正在打某个球"
   的画面，也顺便当成"射线拾取与渲染同源"的一个端到端验证。
   返回挑中的槽位下标，-1 表示墙上没有活球。*/
int  appAutoAim(App *app, int k);

/* 把离屏画面回读成 RGB8（自上而下）。调用方负责 free。
   返回 NULL 表示失败。*/
unsigned char *appGrabPixels(App *app);

/* 主循环：跑到 running 变 0 为止。*/
void appRun(App *app);

/* 内部：重建与渲染分辨率 / 窗口尺寸相关的缓冲。*/
void appUpdateFramebuffer(App *app);

/* ---- 面板的查询口（给渲染层用） ---- */

/* 一级菜单的第 menuRow 行如果是预设，返回预设下标；不是就返回 -1。
   侧栏要靠它把**光标停在哪条预设上**的说明显示出来 —— 光标上下移动时
   就能连着读七段说明，不用先套用再看。越界返回 -1。*/
int  appParamMenuPreset(int menuRow);

/* 某项参数当前的值是否与该预设的"原值"不同。它已经不再用来画黄字
   （「（已被修改）」整个删了），但**预设匹配**（appParamMatchedPreset）
   用的还是同一套逐项比对，所以这个函数留着 —— 它现在是那块逻辑的底座。*/
int  appParamDiffersFromPreset(const App *app, int descIndex);

/* ★ 当前这 44 项**完全等于**八套预设里的哪一套？返回预设下标，
   一套都不像就返回 -1。列表顶上「当前预设方案： XXX」那一行显示的就是它。

   两条要紧的规矩：
     ① 逐项比的是 `paramCoveredByPreset(P, i)` 为真的项。预设没写过的项
        根本没有参照值，拿它去比就是凭空造标准。
     ② 找的是**八套里任何一套**，不是只看 params.preset 那一套 ——
        属于预设方案中的任何一个都算。多套同时命中时取预设序
        靠前的那一套。
   代价：O(8 × 42) 次取值比较，每帧一次，可以忽略。*/
int  appParamMatchedPreset(const App *app);

/* ---- 面板的行号换算（按页分开）----
   面板被菜单栏分成两页，**行号是页内行号**，所以这三个函数都要一个页号。
   页号是显式参数而不是"读 app 里的当前页"：藏进全局状态的话，调用点上
   就再也看不出"这一行属于哪一页"，而漏传一处正是"光标和选中项错位"
   这类最难查的 bug 的来源。让它编不过，比让它悄悄错要好。*/
int  appParamPageCount(void);                    /* 恒为 2 */
int  appParamPageRowCount(int page);             /* 预设页 33（第 0 行 + 32 项）/ 偏好页 12 */

/* 页内行号 → 参数下标；预设页第 0 行（「当前预设方案」）没有对应参数，返回 -1。
   换算只在这一个函数里做，别处一律调它。*/
int  appParamRowToDesc(int page, int row);

/* 切到某一页。会先把当前页的光标与滚动位置存进 Mem，再把目标页的换进来；
   页号越界或本来就是这一页则什么都不做。**调用方负责把光标留在菜单栏上**
   （规则：切完页光标停在栏上，按 ↓ 才进列表）。*/
void appParamSetPage(App *app, int page);

/* ---- 历史记录屏 ----
   只在结算屏上可用（落点就在结算屏）。historyOpen 从别处调用是空操作，
   historyInput 只在 SCREEN_HISTORY 上有效。*/
void historyOpen(App *app);
void historyInput(App *app);

#endif /* APP_H */
