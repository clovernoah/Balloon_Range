/* ============================================================================
 * render.h —— 绘制入口
 *
 * 分成三块：3D 场景（renderFrame）、2D 覆盖层（renderHud）、以及分辨率缩放
 * 需要的"离屏转贴图"这一步（renderBlit）。顺序不能颠倒。
 * ==========================================================================*/
#ifndef RENDER_H
#define RENDER_H

#include "app.h"

/* 把相机的基向量算好缓存进 app —— 画气球高光、画准星都要用，
   每帧算一次比在绘制循环里反复算三角函数划算。*/
void renderUpdateCameraBasis(App *app);

/* 画 3D 场景（房间 + 地面残斑 + 气球 + 特效）。不含 HUD。*/
void renderFrame(App *app);

/* 拉开镜头之外的最后一层：把 OSD/HUD 画在窗口分辨率上。*/
void renderHud(App *app);

/* 参数面板一屏能显示多少行。排版在 render.cpp、滚动窗口在 app.cpp，
   两边必须是同一个数，所以由这里出，谁都不许自己另算一份。

   口径是"当前**实际**显示几行"：它按传进来的 top 走一步算一步
   （组标题跟着行滚，占的高度会变），而不是原先那个把本页所有组标题都预扣掉的
   保守值。原先那套保守值偏小，画出来的行比窗口认得多，于是光标还没到底
   列表就先滚了一格 —— 就是那个毛病。*/
int  paramPanelRows(const App *app);

/* 同一件事的宽松版：指定 top，并把"上/下还有更多"两个箭头要不要画一并带出来。
   两个箭头**各占一个真实选项位**：︿ 在列表顶上一格（窗口起点 > 0 时出现），
   ﹀ 在列表最后一格（下面还有内容时出现）。所以它们一露面，可见行数就少 1。
   两个出参都可以传 NULL。*/
int  paramPanelFit(const App *app, int top, int *outArrowTop, int *outArrowBot);

/* 历史记录屏同理。它和参数面板的侧栏是同一个 drawPanelSide 画的，
   行高与列表区高度也是同一套常量，所以这个数自然也是同源的一个。
   单独出一个函数而不是直接复用 —— 将来历史记录要加"每行两栏"之类的
   改动时，不至于牵动参数面板。*/
int  historyVisibleRows(const App *app);

/* 某一行上面要不要顶着一条组标题（组标题独占一格，跟着行一起滚）。
   绘制、行数预算、自检三处共用这一个判据。*/
int  paramRowHasGroupBar(const App *app, int row);

/* 本局为什么结束的中文说法（结算屏与历史屏都用它）。
 * ★ 把它从 render.cpp 的 static 放出来，只为一件事：自检要能
 * 逐条拿它的返回值去 g_hudStrings[] 里找 —— 图集照那张表收字，"表里有"
 * 才是"屏幕上画得出来"的保证。这两个字串曾经是**两处各写一份**的
 * （render.cpp 一份、hud.cpp 的文案表一份，就是空心方块那个教训），
 * 放出来之后，走散就会被断言逮住。越界或 END_NONE 返回空串。*/
const wchar_t *endReasonText(int reason);

/* ---- 列表的"格"-----------------------------------------------------------
 * 一格 = 一个固定高度的选项位。参数行占一格，带组标题的行占两格（组标题条
 * 自己占一格）。滚动稳定全靠"容量按格算是常量"这一点。
 * 这四个都由绘制层出，滚动（app.cpp）与自检共用，谁都不许自己另算一份。*/
int   paramRowSpans(const App *app, int row);       /* 这一行占几格 */
int   paramItemIndexOfRow(const App *app, int row); /* 这一行从第几格起 */
int   paramItemCount(const App *app);               /* 本页一共几格 */
int   paramRowAtItem(const App *app, int item);     /* 第 item 格属于哪一行 */
int   paramPanelItemCap(const App *app, int top);   /* 窗口装得下几格 */
int   paramPanelCapAtStart(const App *app, int start); /* 同上，但起点直接给"格"——锚定试算用 */
int   paramPanelSlotCap(const App *app);            /* 锚定用容量：恒扣 ﹀ 一格 */
int   paramTopSkipOf(const App *app, int top);      /* 顶行被滚掉了几格（0/1） */
int   paramWindowStartItem(const App *app);         /* 窗口起点落在第几格 */
int   paramArrowBotOffset(const App *app);          /* ﹀ 画在第几格；不画时 -1 */
int   paramRowBarClipped(const App *app, int row);  /* 这一行的组标题条被底边挡在窗外了吗 */

/* 第 0 行右边显示的那个名字（从绘制代码里提出来）。
   匹配到某套预设 = 那套的名字；一套都不像 = g_panelCustomName。
   提出来是为了让自检量得到**屏幕上那句话**，而不只是"匹配到哪一套"这个状态。*/
const wchar_t *paramRow0Name(const App *app);

#endif /* RENDER_H */
