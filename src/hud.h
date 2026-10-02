/* ============================================================================
 * hud.h —— 中文 HUD
 *
 * 字形来源：启动时用 GDI 把**用得到的那些汉字**逐个画进一张内存位图，
 * 提成一张 GL 贴图（字形图集），之后每帧只是画带贴图的四边形。
 *
 * 为什么不用 GLUT 的 glutBitmapCharacter：那个只有 ASCII，画不出中文；
 * 为什么不用 FreeType：工程约定零第三方依赖，而 GDI 是系统自带的，
 * 一个字节的库都不用带。
 *
 * 图集里放哪些字，**不是手写的清单**，而是从下面的字符串表里自动收集的。
 * 手写清单一定会漏字（漏了就是画个空心方块，很难一眼看出来），
 * 自动收集则从构造上不可能漏；自检里还有一条断言逐字复核这件事。
 * ==========================================================================*/
#ifndef HUD_H
#define HUD_H

#include "gfx.h"
#include "config.h"

/* 图集容量。注意它要装得下**未去重**的字符总数：收集阶段是先把表里每个
   字都塞进来再去重，所以这个上限对着的是"文案总字数"，不是"不同字数"。
   曾经的 700 就踩过这个坑 —— 表尾的排版符号被静默截掉，HUD 上只剩空心
   方块，而诊断输出报的是去重后的 301，看着像是绰绰有余。

   侧栏说明（42 条）与预设说明（7 段）也纳入收集之后，总字数从
   约 1100 涨到约 2900，2048 已经不够 —— 撞上限的后果是**表尾的整段文案
   静默变方块**，所以上限按实测值留了一倍余量。真的撞上限会置 overflow
   并在启动时打印警告（见 appBuildHud）。*/
#define HUD_MAX_CHARS   6144
#define HUD_ATLAS_COLS   16
#define HUD_MISSING_MAX  16     /* 记多少个"没进图集"的字就够了，够报警用 */

typedef struct {
    GLuint  tex;
    int     ok;
    int     cols, rows;
    int     cell;               /* 每个格子的边长（像素） */
    int     fontPx;             /* 图集里的字号 */
    float   ascent, descent;    /* 基线上下高度，用来算行高与对齐 */

    wchar_t chars[HUD_MAX_CHARS];
    float   adv[HUD_MAX_CHARS]; /* 每个字的步进宽度（图集字号下的像素） */
    int     count;              /* 去重之后的不同字数 —— 也就是图集里的格子数 */
    int     total;              /* 收集阶段一共收了多少个（未去重） */
    int     overflow;           /* 撞到 HUD_MAX_CHARS 被截断了：必须报警，不能静默 */

    /* 画的时候碰到图集里没有的字，就记在这儿；画成空心方块而不是悄悄吞掉。
       正式跑起来如果这里不为 0，控制台会逐个报出来。*/
    wchar_t missing[HUD_MISSING_MAX];
    int     missingCount;
    int     missingTotal;
    int     reportedTo;         /* 报到第几个了 —— 每个字只报一次，不刷屏 */
} HudFont;

/* ---- 文案表：HUD 上会出现的所有文字都在这里 ---- */
extern const wchar_t *const g_hudStrings[];
extern const int g_hudStringCount;

/* ---- 参数面板的成段提示语 ------------------------------------------------
 *
 * 单独拎出来给 render.cpp 按名字取用，**不是**让渲染层能自带文案 ——
 * 恰恰相反：渲染层只要自己写一句不在表里的字，屏幕上就是一个空心方块，
 * 而且只有肉眼能发现。保存/载入曾经改用 Ctrl 组合键时就踩过这个坑
 * （render.cpp 里那份老的 "S：保存" 没跟着改）。
 * 现在定义只有一处，两边不会再走散。详见 hud.cpp 里那段的说明。*/
/* 面板只剩一层，所以这套常量从两套（L1/L2）并成了一套，名字里的 L1/L2 也
   一并去掉了 —— 留着"L2"而界面上并没有 L1，只会让后来的人去找一个不存在的东西。
   三行页脚的分工：第一行说按键，第二行说面板级的动作，第三行说怎么退出去。

   ★ 页脚按光标位置取词，四条规矩：
     · 第一行有**四种**：普通参数行 / 第 0 行 / 菜单栏上 / 弹窗展开；
     · 第二行不再固定 —— **只有偏好页上才写 R**，预设页上它是空的；
     · 弹窗那一态第二、三行都不写（能按的只有上下、回车、ESC）；
     · 「关闭时自动保存」整句删除 —— 那是写盘时机，不是按键。*/
extern const wchar_t *const g_panelHintMenu;    /* 页脚第一行：光标在普通参数行上 */
extern const wchar_t *const g_panelHintPref;    /* 页脚第二行：R，只在偏好页上写 */
extern const wchar_t *const g_panelCloseMenu;   /* 页脚第三行：怎么退出去 */
extern const wchar_t *const g_panelHintRow0;    /* 页脚第一行：光标在第 0 行「当前预设方案」上 */
extern const wchar_t *const g_panelHintBar;     /* 页脚第一行：光标在菜单栏上 */
extern const wchar_t *const g_panelHintPop;     /* 页脚第一行：预设弹窗展开着 */
extern const wchar_t *const g_panelNoHelp;      /* 侧栏：这一项没有说明 */
extern const wchar_t *const g_panelDetailHelp;  /* 第 0 行「当前预设方案」的说明 */
extern const wchar_t *const g_panelCustomHelp;  /* 弹窗里「自定义参数」的说明 */
extern const wchar_t *const g_panelCustomName;
                                                /* 弹窗里那一条的**名字**（从
                                                   render.cpp 挪到这里：名字
                                                   只留一处，自检才量得到它） */
extern const wchar_t *const g_panelSectionHelp[PARAM_SECTION_COUNT];
                                                /* 光标在菜单栏上时，右栏解释这一页 */

/* 把文案表里的字符收进字形集合（纯计算，不碰 GDI、不碰 GL）。
   自检直接调它来复核"没有漏字"。*/
void hudCollectCharset(HudFont *h);

/* 建图集：GDI 画字 + 传成 GL 贴图。需要已经有一个当前的 GL 上下文。
   返回 0 表示成功；失败时 hudDrawText 会退化成画占位方块而不是崩。*/
int  hudBuildAtlas(HudFont *h, int fontPx);

/* 图集自校验，三道检查各报各的：
     1) 贴图真实尺寸 vs 格数×格边长；
     2) 把贴图每一格与"单独重画一次 chars[i]"逐像素比对（内存下标）；
     3) 走 hudText 真正的绘制路径画一遍再读回来比对（UV 采样）。
   其中第 2 条的参考图是**画在不裁剪的大位图上再裁中间一格**的，否则
   "参考图和被测对象一起被裁、互相印证着报通过"，整行错位那类错法会
   完全躲过去 —— 详见 hud.cpp 里 glyphCoverage 的注释。
   返回出错的格数（0 = 全对）。排查"HUD 上出现错字"时先用它 —— 比盯着
   渲染结果看可靠得多。需要 GL 上下文。*/
int  hudVerifyAtlas(HudFont *h);

void hudDestroy(HudFont *h);

/* 查一个字符在不在图集里。不在就返回 -1。*/
int  hudCharIndex(const HudFont *h, wchar_t c);

/* 把"图集里漏了哪些字"打到控制台，每个字只报一次。
   没有漏字就什么都不打 —— 静默即正常。*/
void hudReportMissing(HudFont *h);

/* ---- 屏幕坐标系 ----
 * 约定：原点在左上角，x 向右、y 向下，单位是窗口像素。这与 Win32 的
 * 习惯一致，也和"排版时脑子里想的那套坐标"一致。*/
void hudBeginScreen(int winW, int winH);
void hudEndScreen(void);

/* ---- 绘制 ---- */

/* 文本宽度（像素），用于居中与右对齐。*/
float hudTextWidth(const HudFont *h, const wchar_t *s, float px);

typedef enum {
    HUD_LEFT = 0, HUD_CENTER, HUD_RIGHT
} HudAlign;

/* 画一行字。x,y 是**左上角**（HUD_LEFT）/ 顶端中点（HUD_CENTER）/
   顶端右点（HUD_RIGHT）。px 是字号（像素）。*/
void hudText(HudFont *h, float x, float y, float px, HudAlign align,
             Color4 color, const wchar_t *s);

/* 半透明底板（带一圈描边），HUD 的各个信息块都铺在它上面。*/
void hudPanel(float x, float y, float w, float h, Color4 fill, Color4 edge);

/* 一根进度条（连击窗口、生命之类）。t 是 0..1 的填充比例。*/
void hudBar(float x, float y, float w, float h,
            Color4 back, Color4 fill, float t);

#endif /* HUD_H */
