/* ============================================================================
 * input.h —— 键鼠输入
 *
 * 用 Win32 的 **Raw Input** 读鼠标位移，而不是 WM_MOUSEMOVE。
 * 理由有二：
 *   1. 网页里的快捷键容易和浏览器、输入法抢键，冲突多。原生窗口 + Raw Input
 *      是这条诉求的正面回答 —— 按键先到程序自己的消息循环，
 *      浏览器、输入法、系统手势都插不进来。
 *   2. WM_MOUSEMOVE 给的是光标位置，会被屏幕边界截断：转视角转到一半，
 *      光标顶到屏幕边上就再也转不动了。Raw Input 给的是**未加速、未夹取**
 *      的原始位移，转到哪都不会卡。
 *
 * 键状态分三份：down（当前按着）、pressed（本帧刚按下）、released（本帧刚抬起）。
 * 后两份由 inputEndFrame 在每个**渲染帧**结束时清掉，而不是每个固定步长，
 * 所以一帧里跑多个固定步长时，"刚按下"会被每个步长都看到，不会漏。
 * ==========================================================================*/
#ifndef INPUT_H
#define INPUT_H

#include "core.h"

#define INPUT_MAX_KEYS     256
#define INPUT_MOUSE_BUTTONS  3      /* 左 / 右 / 中 */

typedef struct {
    HWND          hwnd;
    int           rawReady;     /* RegisterRawInputDevices 成功了没有 */
    int           captured;     /* 是否正在锁光标（游戏进行中才锁） */
    int           cursorHidden;

    unsigned char down[INPUT_MAX_KEYS];
    unsigned char pressed[INPUT_MAX_KEYS];
    unsigned char released[INPUT_MAX_KEYS];

    unsigned char mdown[INPUT_MOUSE_BUTTONS];
    unsigned char mpressed[INPUT_MOUSE_BUTTONS];
    unsigned char mreleased[INPUT_MOUSE_BUTTONS];

    float dx, dy;               /* 本帧累积的原始位移（像素，未乘灵敏度） */
    float wheel;                /* 本帧滚轮格数（正 = 向前滚） */

    /* ---- 长按重复（菜单用） ----
       系统自己的键盘自动重复被丢掉了（见 input.cpp 里 bit 30 那段），
       理由是"按着不放会疯狂翻页"，但没有重复又变成"按住没反应"。
       两头都不对，所以在这里自己排节拍：按住够久才开始重复，之后按固定
       间隔重发，再按久一点就加速。参数见 input.cpp 顶部的四个常量。*/
    float holdSec[INPUT_MAX_KEYS];      /* 这一键按住了多久（松开清零） */
    float repeatSec[INPUT_MAX_KEYS];    /* 距离上一次"重复触发"过了多久 */
    int   repeatCount[INPUT_MAX_KEYS];    /* 已经重复触发过几次（0 = 还没开始重复） */
    int   repeatConsumed[INPUT_MAX_KEYS]; /* 其中已经被调用方取走过几次 */
    int   pressDelivered[INPUT_MAX_KEYS]; /* "刚按下"那一次发过没有 */
} Input;

void inputInit(Input *in, HWND hwnd);
void inputShutdown(Input *in);

/* 从窗口过程里调。返回 1 表示这条消息已经被吃掉，调用方不用再传给
   DefWindowProc（WM_INPUT 必须这么处理，否则系统会白跑一趟）。*/
int  inputOnMessage(Input *in, UINT msg, WPARAM wp, LPARAM lp);

/* 每帧末调用：清掉"刚按下/刚抬起"、清掉累积的鼠标位移与滚轮。
   不清 down 数组 —— 那个靠消息维护。*/
void inputEndFrame(Input *in);

/* 开关光标锁定。锁定时光标被夹在客户区内并隐藏；离开游戏（暂停、结算、
   切到别的窗口）必须解锁，否则用户在菜单里点不到东西。*/
void inputSetCapture(Input *in, int on);

/* 每帧调一次（在 inputEndFrame 之前），推进长按计时。dt 是这一帧的秒数。*/
void inputTick(Input *in, float dt);

/* 长按重复的询问口。**只给菜单用**：玩法输入（移动、开火）必须继续走
   inputKeyPressed / inputKeyDown，否则按住 W 走路会变成一顿一顿的。
   返回 1 的时机 = 刚按下那一刻，以及之后按节拍重发的每一次。*/
int  inputKeyRepeat(Input *in, int vk);

int  inputKeyDown(const Input *in, int vk);
int  inputKeyPressed(const Input *in, int vk);
int  inputMouseDown(const Input *in, int btn);      /* 0 左 / 1 右 / 2 中 */
int  inputMousePressed(const Input *in, int btn);   /* 本帧刚按下 */

#endif /* INPUT_H */
