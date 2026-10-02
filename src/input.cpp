/* ============================================================================
 * input.cpp —— 输入实现
 * ==========================================================================*/
#include "input.h"
#include <string.h>

/* ---------------------------------------------------------- 光标锁定 */

static void updateClip(Input *in) {
    if (!in || !in->hwnd) return;

    if (in->captured) {
        RECT r;
        POINT tl, br;
        GetClientRect(in->hwnd, &r);
        tl.x = r.left;  tl.y = r.top;
        br.x = r.right; br.y = r.bottom;
        ClientToScreen(in->hwnd, &tl);
        ClientToScreen(in->hwnd, &br);
        r.left = tl.x; r.top = tl.y; r.right = br.x; r.bottom = br.y;
        ClipCursor(&r);
    } else {
        ClipCursor(NULL);
    }

    /* ShowCursor 维护的是一个**计数器**，不是布尔量：多调一次 FALSE
       就得多调一次 TRUE 才能还回来。用循环把它压到确定的一侧，
       这样"锁几次"都不会让光标消失不见（那是很难查的一类 bug）。*/
    if (in->captured && !in->cursorHidden) {
        while (ShowCursor(FALSE) >= 0) { }
        in->cursorHidden = 1;
    } else if (!in->captured && in->cursorHidden) {
        while (ShowCursor(TRUE) < 0) { }
        in->cursorHidden = 0;
    }
}

void inputSetCapture(Input *in, int on) {
    if (!in) return;
    on = on ? 1 : 0;
    if (on == in->captured) { updateClip(in); return; }
    in->captured = on;
    updateClip(in);
    /* 刚锁上时把本帧累积的位移清掉：点击窗口那一瞬间光标会跳一下，
       不清的话视角会跟着猛甩一下。*/
    in->dx = in->dy = 0.0f;
}

/* ---------------------------------------------------------- 生命周期 */

void inputInit(Input *in, HWND hwnd) {
    RAWINPUTDEVICE rid;
    if (!in) return;
    memset(in, 0, sizeof(*in));
    in->hwnd = hwnd;

    rid.usUsagePage = 0x01;          /* Generic Desktop */
    rid.usUsage     = 0x02;          /* Mouse */
    rid.dwFlags     = 0;             /* 只在前台收：切出去就自动停，正合适 */
    rid.hwndTarget  = hwnd;
    in->rawReady = RegisterRawInputDevices(&rid, 1, sizeof(rid)) ? 1 : 0;

    updateClip(in);
}

void inputShutdown(Input *in) {
    if (!in) return;
    in->captured = 1;                /* 先假装锁着，再解锁，强制走一次还原分支 */
    inputSetCapture(in, 0);
}

/* ---------------------------------------------------------- 消息 */

int inputOnMessage(Input *in, UINT msg, WPARAM wp, LPARAM lp) {
    if (!in) return 0;

    switch (msg) {
    case WM_INPUT: {
        /* 只有锁定了光标才需要处理原始鼠标 —— 菜单里让系统光标正常工作。*/
        RAWINPUT ri;
        UINT size = sizeof(ri);
        if (!in->captured) break;
        if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &ri, &size,
                            sizeof(RAWINPUTHEADER)) == (UINT)-1) break;
        if (ri.header.dwType != RIM_TYPEMOUSE) break;
        if (ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) {
            /* 绝对坐标设备（数位板、远程桌面）。按"两点之差"处理也可以，
               但这里没有上一次的位置，索性忽略 —— 这种情况用键鼠以外的
               设备玩也不合理。*/
            break;
        }
        in->dx += (float)ri.data.mouse.lLastX;
        in->dy += (float)ri.data.mouse.lLastY;
        return 1;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        int vk = (int)(wp & 0xFF);
        if (vk < INPUT_MAX_KEYS) {
            /* bit 30 是"这一键之前就按着"：长按的自动重复不该算作"刚按下"，
               否则菜单里按着不放会疯狂翻页。*/
            if (!(lp & (1 << 30))) in->pressed[vk] = 1;
            in->down[vk] = 1;
        }
        /* Alt 会走 WM_SYSKEYDOWN，默认行为是弹系统菜单，吃掉它。*/
        if (msg == WM_SYSKEYDOWN) return 1;
        break;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        int vk = (int)(wp & 0xFF);
        if (vk < INPUT_MAX_KEYS) { in->down[vk] = 0; in->released[vk] = 1; }
        if (msg == WM_SYSKEYUP) return 1;
        break;
    }

    case WM_LBUTTONDOWN: in->mdown[0] = 1; in->mpressed[0] = 1; return 1;
    case WM_LBUTTONUP:   in->mdown[0] = 0; in->mreleased[0] = 1; return 1;
    case WM_RBUTTONDOWN: in->mdown[1] = 1; in->mpressed[1] = 1; return 1;
    case WM_RBUTTONUP:   in->mdown[1] = 0; in->mreleased[1] = 1; return 1;
    case WM_MBUTTONDOWN: in->mdown[2] = 1; in->mpressed[2] = 1; return 1;
    case WM_MBUTTONUP:   in->mdown[2] = 0; in->mreleased[2] = 1; return 1;

    case WM_MOUSEWHEEL:
        in->wheel += (float)GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA;
        return 1;

    case WM_ACTIVATEAPP:
        /* 切到别的程序：立刻还回光标，否则用户在别的窗口里找不到鼠标。*/
        if (wp == FALSE) inputSetCapture(in, 0);
        break;

    case WM_SETCURSOR:
        /* 锁定期间自己管光标，不让系统改回去。*/
        if (in->captured) return 1;
        break;
    }
    return 0;
}

/* ============================================================ 长按重复
 *
 * 系统在按住一个键时会自己发一串 WM_KEYDOWN（bit 30 置位）。那一串的节奏
 * 由控制面板里的"键盘重复速度"决定，各人机器上不一样，而且第一次的延迟
 * 通常是 500 ms 上下 —— 在菜单里按着 ↓ 要等半秒才动第二格，手感很钝。
 *
 * 所以：**系统那串一律丢掉**（不让外部设置影响面板手感），自己排节拍。
 * 三档常量都写在这里，改手感只改这一处。
 */
#define REPEAT_DELAY_SEC    0.32f   /* 按住多久开始重复 */
#define REPEAT_FAST_SEC     1.20f   /* 再按住多久开始加速 */
#define REPEAT_SLOW_STEP    0.045f  /* 慢速档的间隔（约 22 次/秒） */
#define REPEAT_FAST_STEP    0.018f  /* 加速档的间隔（约 55 次/秒） */

void inputTick(Input *in, float dt) {
    int i;
    if (!in) return;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.25f) dt = 0.25f;      /* 卡了一下也只当 0.25 秒，别让重复暴冲 */

    for (i = 0; i < INPUT_MAX_KEYS; ++i) {
        if (!in->down[i]) {
            in->holdSec[i] = 0.0f;
            in->repeatSec[i] = 0.0f;
            in->repeatCount[i] = 0;
            in->repeatConsumed[i] = 0;
            in->pressDelivered[i] = 0;
            continue;
        }
        in->holdSec[i] += dt;
        if (in->holdSec[i] < REPEAT_DELAY_SEC) continue;

        in->repeatSec[i] += dt;
        /* 用 while 而不是 if：一帧里可能跨过好几个重复周期（帧率低的时候），
           那样这一帧就该多走几格，而不是补不上就一直慢半拍。*/
        while (in->repeatSec[i] >=
               (in->holdSec[i] >= REPEAT_FAST_SEC ? REPEAT_FAST_STEP : REPEAT_SLOW_STEP)) {
            in->repeatSec[i] -= (in->holdSec[i] >= REPEAT_FAST_SEC
                                 ? REPEAT_FAST_STEP : REPEAT_SLOW_STEP);
            in->repeatCount[i] += 1;
        }
    }
}

int inputKeyRepeat(Input *in, int vk) {
    if (!in || vk < 0 || vk >= INPUT_MAX_KEYS) return 0;
    if (!in->down[vk]) return 0;

    /* 两条路各记各的账，互不干扰：
         · "刚按下"这一次按下只算一次（pressDelivered 记着发过没有）；
         · 之后每一次重复由 repeatCount 与 repeatConsumed 的差量决定。
       为什么要分开：两者合成一个计数器的话，"按下"那次会把账
       记走，紧接着的第一次重复反而被吞掉（差一格）。两个计数器各自
       直白，就没有这种互相踩脚的地方。*/
    if (in->pressed[vk] && !in->pressDelivered[vk]) {
        in->pressDelivered[vk] = 1;
        return 1;
    }
    if (in->repeatCount[vk] > in->repeatConsumed[vk]) {
        in->repeatConsumed[vk] = in->repeatCount[vk];
        return 1;
    }
    return 0;
}

void inputEndFrame(Input *in) {
    if (!in) return;
    memset(in->pressed, 0, sizeof(in->pressed));
    memset(in->released, 0, sizeof(in->released));
    memset(in->mpressed, 0, sizeof(in->mpressed));
    memset(in->mreleased, 0, sizeof(in->mreleased));
    in->dx = 0.0f;
    in->dy = 0.0f;
    in->wheel = 0.0f;
}

int inputKeyDown(const Input *in, int vk) {
    return (in && vk >= 0 && vk < INPUT_MAX_KEYS) ? in->down[vk] : 0;
}

int inputKeyPressed(const Input *in, int vk) {
    return (in && vk >= 0 && vk < INPUT_MAX_KEYS) ? in->pressed[vk] : 0;
}

int inputMouseDown(const Input *in, int btn) {
    return (in && btn >= 0 && btn < INPUT_MOUSE_BUTTONS) ? in->mdown[btn] : 0;
}

int inputMousePressed(const Input *in, int btn) {
    return (in && btn >= 0 && btn < INPUT_MOUSE_BUTTONS) ? in->mpressed[btn] : 0;
}
