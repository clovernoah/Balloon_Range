/* ============================================================================
 * app.cpp —— 窗口、GL 上下文、主循环、离屏回读
 *
 * 窗口是**原生 Win32**（不是控制台宿主、不是任何框架）。做成原生窗口的
 * 理由之一：网页有很多快捷键容易发生冲突。所有按键先到程序自己的消息
 * 处理，谁也抢不走。
 * ==========================================================================*/
#include "app.h"
#include "render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>       /* _time64 —— 历史记录的时间戳 */

static const wchar_t *kClassName = L"BalloonRangeWindow";

/* ---------------------------------------------------------- 按键绑定
 * 全部集中在这里，改键位不用翻别处。用 Win32 的虚拟键码。*/
#define KEY_FORWARD     'W'
#define KEY_BACK        'S'
#define KEY_LEFT        'A'
#define KEY_RIGHT       'D'
#define KEY_CROUCH      VK_CONTROL
#define KEY_PAUSE       VK_ESCAPE
#define KEY_RESTART     'R'     /* 玩游戏时：结算并开始新游戏；结算屏：开新局 */
#define KEY_HISTORY     'F'     /* 只在结算屏：翻看最近 20 局 */
/* KEY_FINISH（'Q'）已删除：它和 KEY_RESTART 在玩家眼里是同一件事
   ——"这局不想要了"，区别只在要不要先看一眼成绩。留两个键只会让人犹豫按哪个。*/
/* 设置面板：Tab 开合（左下角提示写作"Tab 设置"，与面板标题一致）。
   面板里面没有"保存/载入"这两个键 —— 它们已删除，改成关面板时
   自动落盘（见 appParamClose）。所以这里也没有 KEY_SAVE / KEY_LOAD；
   面板里的 'S' / 'L' 是空键，跟 W / A / D 一样什么都不做。*/
#define KEY_PARAM       VK_TAB

/* 手上没有连射时，点一下之后也要有个冷却，防止"按一下出一堆子弹"。*/
#define FIRE_CLICK_COOLDOWN 0.12f

/* ============================================================ 窗口过程 */

static LRESULT CALLBACK appWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    App *app = (App *)GetWindowLongPtrW(h, GWLP_USERDATA);

    /* 键鼠消息先给输入层。它吃掉返回 1，就不再往下传 ——
       WM_INPUT 必须这样，否则系统会白跑一趟。*/
    if (app && inputOnMessage(&app->input, msg, wp, lp)) return 0;

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;                       /* 自己全屏重绘，别让系统刷背景（会闪） */
    case WM_SIZE:
        if (app && wp != SIZE_MINIMIZED)
            appResize(app, LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_CLOSE:
        if (app) app->running = 0;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_GETDLGCODE:
        /* 告诉系统"键盘消息全部要"。少了这一条，Tab 会被系统拿去切焦点，
           而且切不动的时候还会"叮"一声 —— 参数面板要用 Tab。*/
        return DLGC_WANTALLKEYS;
    case WM_ACTIVATEAPP:
        /* 失去前台：光标还给系统并暂停。切出去的时候游戏不该继续跑，
           而且光标被锁着的话，切换到别的窗口后根本点不到东西。*/
        if (app && wp == FALSE) {
            app->lostFocus = 1;
            if (app->screen == SCREEN_PLAY) appSetScreen(app, SCREEN_PAUSE);
        } else if (app) {
            app->lostFocus = 0;
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* ============================================================ 创建窗口 */

int appCreateWindow(App *app, int width, int height, int visible) {
    static int registered = 0;
    WNDCLASSEXW wc;
    RECT r;
    DWORD style;

    if (!app) return -1;

    if (!registered) {
        memset(&wc, 0, sizeof(wc));
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = appWndProc;
        wc.hInstance     = GetModuleHandleW(NULL);
        wc.hCursor       = NULL;        /* 光标由程序自己画/隐藏 */
        wc.lpszClassName = kClassName;
        if (!RegisterClassExW(&wc)) return -1;
        registered = 1;
    }

    style = WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_THICKFRAME);
    r.left = 0; r.top = 0; r.right = width; r.bottom = height;
    AdjustWindowRect(&r, style, FALSE);

    app->hwnd = CreateWindowExW(0, kClassName, APP_TITLE_W, style,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                r.right - r.left, r.bottom - r.top,
                                NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!app->hwnd) return -1;

    SetWindowLongPtrW(app->hwnd, GWLP_USERDATA, (LONG_PTR)app);
    app->dc = GetDC(app->hwnd);
    if (!app->dc) return -1;

    app->winW = width;
    app->winH = height;
    /* 立刻算一次渲染分辨率。少了这一步，fbW/fbH 会一直是 0，
       离屏截图那条路径会拿着 0 去算宽高比。*/
    appUpdateFramebuffer(app);

    if (visible) {
        ShowWindow(app->hwnd, SW_SHOW);
        UpdateWindow(app->hwnd);
    }
    return 0;
}

int appInitGL(App *app) {
    PIXELFORMATDESCRIPTOR pfd;
    int pf;

    if (!app || !app->dc) return -1;
    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize      = sizeof(pfd);
    pfd.nVersion   = 1;
    pfd.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;

    pf = ChoosePixelFormat(app->dc, &pfd);
    if (!pf) return -1;
    if (!SetPixelFormat(app->dc, pf, &pfd)) return -1;

    app->rc = wglCreateContext(app->dc);
    if (!app->rc) return -1;
    if (!wglMakeCurrent(app->dc, app->rc)) return -1;
    app->glReady = 1;

    /* ---- 一次设好的全局状态 ---- */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    /* 不开背面剔除：房间每一面只画一层四边形，剔除省不下什么，
       却要求每一处的绕序都恰好正确 —— 那是个很容易反复踩的坑。
       打开双面光照配合它，背面不会黑。*/
    glDisable(GL_CULL_FACE);
    glEnable(GL_LIGHTING);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, 1);
    glEnable(GL_NORMALIZE);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glShadeModel(GL_SMOOTH);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glClearColor(0.055f, 0.062f, 0.082f, 1.0f);

    glGenTextures(1, &app->blitTex);
    return 0;
}

/* ============================================================ 中文 HUD

 * 图集要 GDI 画字 + 传 GL 贴图，所以只能在 GL 上下文就绪之后建。
 * 失败不是致命的：hud.cpp 里会退化成画灰条，游戏照样能玩。
 */
int appBuildHud(App *app) {
    if (!app) return -1;
    /* 字号按窗口高度缩放：1600x900 用 40 像素，2560x1440 就该用 64。
       否则在大屏上 HUD 会小得像蚂蚁。上限 64 是防止窗口被拉得很大之后
       图集尺寸失控。*/
    int px = (int)((float)app->winH * 0.0444f + 0.5f);   /* 900 → 40 */
    if (px < 20) px = 20;
    if (px > 64) px = 64;
    if (hudBuildAtlas(&app->hud, px) != 0) {
        printUtf8(L"[错误] 中文字形图集建立失败（HUD 会退化成色块）\n");
        return -1;
    }
    printUtf8f(L"[HUD] 字形图集 %d 个字（收进 %d 个，去重后），%dx%d 格，字号 %d 像素\n",
               app->hud.count, app->hud.total, app->hud.cols, app->hud.rows,
               app->hud.fontPx);
    /* 收集阶段被容量截断过就必须喊出来。之前这里是静默 truncate，
       表现是 HUD 上冒出几个空心方块，而统计数字看着还挺富余。*/
    if (app->hud.overflow)
        printUtf8f(L"[HUD] 警告：文案字符总数超过 HUD_MAX_CHARS(%d)，"
                   L"表尾的字被截掉了 —— 调大 hud.h 里的上限\n", HUD_MAX_CHARS);
    return 0;
}

void appUpdateFramebuffer(App *app) {
    int w, h;
    if (!app) return;
    /* 渲染缩放已从面板上删掉（性能排障用的旋钮，正常玩不需要），
       固定成 RENDER_SCALE_DEF。下面那四行夹取留着 —— 常量一旦被改小
       或改大，靠它们兜住"缓冲比窗口还大""小到没法看"这两种情况。*/
    w = (int)((float)app->winW * RENDER_SCALE_DEF + 0.5f);
    h = (int)((float)app->winH * RENDER_SCALE_DEF + 0.5f);
    if (w < 64) w = 64;
    if (h < 64) h = 64;
    if (w > app->winW) w = app->winW;
    if (h > app->winH) h = app->winH;
    app->fbW = w;
    app->fbH = h;
}

void appResize(App *app, int w, int h) {
    if (!app || w <= 0 || h <= 0) return;
    app->winW = w;
    app->winH = h;
    appUpdateFramebuffer(app);
}

/* ============================================================ 一局 */

/* 结束本局的统一入口：记下原因，再等 endHold 秒结算。重复调用不会覆盖
   第一次的原因（先到的那个才是真正的死因）。*/
static void requestEnd(App *app, int reason) {
    if (!app || app->endHold > 0.0f) return;
    app->endReason = reason;
    app->endHold = 0.9f;
}

void appSetScreen(App *app, int screen) {
    if (!app) return;
    /* 用 SCREEN_COUNT 而不是写死的 2 —— 新增 SCREEN_HISTORY 后，
       写死的上界会把新屏幕悄悄夹回结算屏（症状是"按 F 没反应"）。*/
    app->screen = clampi(screen, 0, SCREEN_COUNT - 1);
    /* 光标只在真正玩的时候锁。暂停/结算时必须还回去 —— 否则鼠标被
       锁住、屏幕上找不到，那是最容易被骂的一类问题。
       这里**不做"相等就早退"**：开局时 screen 本来就是 PLAY，
       早退的话光标会一直不锁。inputSetCapture 自己会判重，代价可忽略。

       还要绑一个条件：窗口得是可见的。--shot 出图时窗口是隐藏的，
       那种情况下绝不该去夹光标。*/
    inputSetCapture(&app->input,
                    app->screen == SCREEN_PLAY && !app->paramOpen &&
                    app->hwnd && IsWindowVisible(app->hwnd));
}

void appResetSession(App *app, unsigned seed) {
    if (!app) return;

    /* 种子的三级来源，优先级从高到低：
         1) 调用方显式给的 seed（命令行 --seed、出图模式）；
         2) 参数面板里的"随机种子"，非 0 时用它 —— 玩家想复现某一局就靠这个；
         3) 高精度计数器的低位，同一秒内连开两局也不会撞。
       第 2 级是后来补的：最初面板上那个种子滑杆根本没接到这里，
       拧了完全没用 —— 是自检的"参数活性"断言把它揪出来的。*/
    if (seed == 0) seed = (unsigned)app->params.seed;
    if (seed == 0)
        seed = (unsigned)(nowSeconds() * 1000.0) ^ (unsigned)(uintptr_t)app;
    app->seed = seed;

    /* 参数可能在上一局之后被改过（面板、命令行），这里重算一次是必须的：
       出球区、池容量、难度基准全都从参数派生。*/
    paramsClamp(&app->params);
    sceneComputeField(&app->params, &app->field);
    balloonPoolInit(&app->pool, &app->params, seed, &app->field);
    playerInit(&app->player);
    fxReset(&app->fx, seed ^ 0x9E3779B9u);

    app->sessionStart = nowSeconds();
    app->elapsed      = 0.0;
    app->frame        = 0;
    app->fpsAvg       = 0.0;

    app->wish = v3(0.0f, 0.0f, 0.0f);
    app->crouch = 0;

    app->lives = app->params.lives;
    /* 计时挑战才有倒计时；其它模式 0 表示"不限时"，HUD 据此决定画不画。*/
    app->timeLeft = (app->params.mode == MODE_TIME) ? app->params.timeLimitSec : 0.0f;
    app->fireCooldown = 0.0f;
    app->fireFlash = 0.0f;
    app->hitFlash = 0.0f;
    app->missFlash = 0.0f;
    app->endHold = 0.0f;
    app->endReason = END_NONE;
    app->tracerLife = 0.0f;
    app->tracerFrom = v3(0.0f, 0.0f, 0.0f);
    app->tracerTo   = v3(0.0f, 0.0f, 0.0f);
    memset(&app->lastResult, 0, sizeof(app->lastResult));
    app->newRecord = 0;

    app->score = 0;
    app->popped = 0;
    app->missed = 0;
    app->shots = 0;
    app->hits = 0;
    app->combo = 0;
    app->comboLeft = 0.0f;
    app->bestCombo = 0;
    app->comboPulse = 0.0f;

    appSetScreen(app, SCREEN_PLAY);
}

/* ============================================================ 开枪 */

int appFire(App *app) {
    Vec3 eye, dir;
    Vec3 pos;
    int  idx;
    float r = 0.0f;
    int   type = BALLOON_NORMAL;
    Color3 col;
    float hitT = 0.0f;

    if (!app || app->screen != SCREEN_PLAY) return 0;

    app->shots++;

    playerRay(&app->player, &eye, &dir);

    /* 曳光的终点：打中了就画到球上，没打中就一直画到墙（或 40 米外）。
       射线与墙面的交点用解析法算，不额外做一次场景求交。*/
    app->tracerLife = 0.085f;
    app->tracerFrom = eye;

    idx = balloonPick(&app->pool, &app->params, eye, dir, &hitT);
    if (idx < 0) {
        float t = (fabsf(dir.z) > 1e-5f) ? (WALL_Z - eye.z) / dir.z : 40.0f;
        if (t < 0.5f || t > 40.0f) t = 40.0f;
        app->tracerTo = v3add(eye, v3scale(dir, t));
        app->missFlash = maxf(app->missFlash, 0.45f);
        /* 精准挑战：墙上就那一个球，打空了本局就结束 —— 这个模式的全部
           压力都在这一条上，所以它必须真的生效，不能只是文档里的一句。*/
        if (app->params.mode == MODE_PRECISION) requestEnd(app, END_MISSED);
        return 0;
    }

    /* 世界坐标与颜色都要在击破**之前**取。击破会把槽位状态改掉，
       虽然位置字段还留着旧值，但依赖"改完还留着"是脆的。*/
    pos = balloonWorldPos(&app->pool.slots[idx]);
    col = balloonColor(&app->pool.slots[idx], &app->params);
    app->tracerTo = v3add(eye, v3scale(dir, hitT > 0.0f ? hitT : 0.5f));

    if (!balloonKill(&app->pool, &app->params, &app->field,
                     app->elapsed, idx, &r, &type))
        return 0;

    /* 连击**先加再算分**。
       顺序反了的话，第一球拿不到倍率、第 n 球拿到 n-1 的倍率，
       整局的分都低一档。*/
    app->combo++;
    if (app->combo > app->bestCombo) app->bestCombo = app->combo;
    app->comboLeft = app->params.comboWindowSec;
    app->comboPulse = 1.0f;

    {
        int gain = scoreGainOf(r, &app->params, app->combo);
        /* 再乘气球类型的倍率（金球 2 倍、小快球 1.6 倍…）。*/
        gain = (int)((float)gain * g_balloonTypes[type].scoreMul + 0.5f);
        app->score += gain;
        app->popped++;
        app->hits++;
        app->hitFlash = 1.0f;
        fxSpawnPop(&app->fx, &app->params, pos, r, col, gain, app->combo);

        /* 击破音：左右声像由球在相机右向量上的投影决定，响度按距离衰减，
           音高随连击上升（最多一个八度）。方位自己算而不是读 render 层缓存的
           相机基向量 —— appStep 是固定步长，渲染帧可能还没跑过，
           读那份缓存会拿到上一帧甚至未初始化的值。*/
        {
            float yaw = app->player.yaw;
            Vec3 rel = v3sub(pos, playerEyePos(&app->player));
            /* right = (cos yaw, 0, -sin yaw)：与 playerRay 用的那套朝向同一约定。*/
            float along = rel.x * cosf(yaw) - rel.z * sinf(yaw);
            float pan = clampf(along / 4.0f, -1.0f, 1.0f);
            audioPlay(&app->audio, SFX_POP, pan, v3len(rel), app->combo - 1);
        }
    }
    return 1;
}

int appAutoAim(App *app, int k) {
    int live[MAX_BALLOONS];
    int n = 0, i;
    Vec3 eye, d, target;
    float cp;

    if (!app) return -1;
    for (i = 0; i < app->pool.n && i < MAX_BALLOONS; ++i) {
        int st = app->pool.slots[i].state;
        if (st == BSLOT_GROW || st == BSLOT_LIVE) live[n++] = i;
    }
    if (n == 0) return -1;

    k = ((k % n) + n) % n;
    i = live[k];

    eye    = playerEyePos(&app->player);
    target = balloonWorldPos(&app->pool.slots[i]);
    d = v3norm(v3sub(target, eye));

    /* 由方向反解 yaw/pitch。forward = (-sin(yaw)·cp, sin(pitch), -cos(yaw)·cp)，
       所以 pitch = asin(dy)、yaw = atan2(-dx, -dz)。这里不用 playerLook，
       是因为它只吃增量，而这里要的是"直接摆到某个角度"。*/
    cp = sqrtf(maxf(0.0f, 1.0f - d.y * d.y));
    app->player.pitch = asinf(clampf(d.y, -1.0f, 1.0f));
    app->player.yaw   = (cp > 1e-4f) ? atan2f(-d.x, -d.z) : app->player.yaw;
    return i;
}

/* ============================================================ 单步 */

/* 一个"每帧减一点，减到 0 就停"的量 —— 命中闪、脱靶闪、开火闪、曳光余辉、
   连击脉冲都是同一种东西，只是速率不同。

   原先这五处各写各的（其中四处还挤成一行 if 套 if），改一个速率要改五处，
   加第六个反馈量时又得照抄一遍。收成一个函数之后，"衰减到零"这件事只有
   一种写法，速率是参数。半隐式欧拉在这里退化成线性衰减，不影响数值。*/
static void fadeOut(float *v, float rate, float dt) {
    if (*v <= 0.0f) return;
    *v -= rate * dt;
    if (*v < 0.0f) *v = 0.0f;
}

void appStep(App *app, float dt) {
    const Params *p;
    int escaped;

    if (!app || app->screen != SCREEN_PLAY) return;
    /* 面板开着时本局冻结：开面板要放开光标，放开了就没法瞄准射击，
       让对局继续跑等于白送漏球。画面照常画，所以调画面类参数时
       能立刻看到效果 —— 那正是面板存在的意义。*/
    if (app->paramOpen) return;
    p = &app->params;

    app->elapsed += dt;

    /* ---- 移动 ---- */
    playerMove(&app->player, p, app->wish, dt, app->crouch);

    /* ---- 连射 ----
       点一下在 appHandleInput 里处理（那是对"刚按下"的直接响应），
       这里只管按住不放的按间隔连发。两条路都写 fireCooldown，
       所以同一帧不会被算两次。*/
    if (app->fireCooldown > 0.0f) app->fireCooldown -= dt;
    if (p->autoFireInterval > 0.0f &&
        inputMouseDown(&app->input, 0) && app->fireCooldown <= 0.0f) {
        appFire(app);
        app->fireCooldown = p->autoFireInterval;
    }

    /* ---- 气球 ---- */
    escaped = balloonUpdate(&app->pool, p, &app->field, app->elapsed, dt);
    if (escaped > 0) {
        app->missed += escaped;
        app->missFlash = 1.0f;
        /* 漏球的负反馈音放正中、不加声像：球是从墙上某处逃的，玩家看到的
           是一闪而过的变化，硬给它安一个方位反而会误导（"声音在右边"
           而逃掉的其实是左边那个）。距离按墙面到玩家的典型距离给。*/
        audioPlay(&app->audio, SFX_MISS, 0.0f, 5.0f, 0);
        /* 漏球断连击 —— 唯一的负反馈杠杆。
           注意：**打空枪不断连击**，只有球逃走才算。*/
        if (p->missBreaksCombo) {
            app->combo = 0;
            app->comboLeft = 0.0f;
        }
        if (p->missCostsLife && !p->infiniteLives) {
            app->lives -= escaped;
            if (app->lives <= 0) { app->lives = 0; requestEnd(app, END_LIVES); }
        }
        /* 精准挑战：墙上只有那一个球，它跑了本局就结束。*/
        if (p->mode == MODE_PRECISION) requestEnd(app, END_ESCAPED);
    }

    /* ---- 连击窗口 ---- */
    if (app->combo > 0) {
        app->comboLeft -= dt;
        if (app->comboLeft <= 0.0f) {
            app->combo = 0;
            app->comboLeft = 0.0f;
        }
    }

    /* ---- 视觉反馈的衰减（速率各不相同，写法只有 fadeOut 一种）---- */
    fadeOut(&app->comboPulse, 3.2f, dt);
    fadeOut(&app->hitFlash,   4.0f, dt);
    fadeOut(&app->missFlash,  3.0f, dt);
    fadeOut(&app->fireFlash, 14.0f, dt);
    fadeOut(&app->tracerLife, 1.0f, dt);

    fxUpdate(&app->fx, p, dt);

    /* ---- 计时挑战 ---- */
    if (app->timeLeft > 0.0f) {
        app->timeLeft -= dt;
        if (app->timeLeft <= 0.0f) {
            app->timeLeft = 0.0f;
            requestEnd(app, END_TIME);
        }
    }

    /* ---- 结束 ----
       不立刻结算，先留 endHold 秒让最后一个特效放完、让玩家看清打中的
       是哪一球。这段时间还能继续打 —— 归属最后这一拍的成绩。*/
    if (app->endHold > 0.0f) {
        app->endHold -= dt;
        if (app->endHold <= 0.0f) appEndSession(app);
    }
}

void appEndSession(App *app) {
    if (!app) return;

    scoreFillResult(&app->lastResult, app->score, app->popped, app->missed,
                    app->shots, app->hits, app->bestCombo,
                    app->params.mode, app->params.preset,
                    app->elapsed, app->seed);

    /* 记录按**预设**分档（早先按模式分四档）。*/
    app->lastRecordScore = app->book.rec[app->params.preset].score;
    app->newRecord = scoreBookSubmit(&app->book, app->params.preset,
                                     &app->lastResult);
    if (app->bookPath[0])
        scoreBookSave(&app->book, app->bookPath);

    /* ---- 往流水里记一笔 ----
       和上面那份"最好成绩"是两件事：这份不看成绩高低，每局都留一条。*/
    {
        HistoryEntry he;
        memset(&he, 0, sizeof(he));
        he.score       = app->lastResult.score;
        he.popped      = app->lastResult.popped;
        he.missed      = app->lastResult.missed;
        he.shots       = app->lastResult.shots;
        he.hits        = app->lastResult.hits;
        he.bestCombo   = app->lastResult.bestCombo;
        he.accuracyPct = (int)(app->lastResult.accuracy * 100.0f + 0.5f);
        he.durationMs  = (int)(app->lastResult.durationSec * 1000.0 + 0.5);
        he.preset      = app->lastResult.preset;
        he.mode        = app->lastResult.mode;
        he.endReason   = app->endReason;
        he.seed        = app->lastResult.seed;
        he.when        = (long long)_time64(NULL);
        historyPush(&app->history, &he);
        if (app->histPath[0])
            historySave(&app->history, app->histPath);
    }

    /* 收尾音前先把还在响的击破声掐掉：三音收尾要听得清，
       不然会和最后一声"啪"糊在一起。*/
    audioStopAll(&app->audio);
    audioPlay(&app->audio, SFX_END, 0.0f, 0.0f, 0);

    appSetScreen(app, SCREEN_SETTLE);
}

/* ============================================================ 历史记录

   和参数面板一样是"盖在某一屏上的一层界面"，所以做法也一样：
   自己的状态（histSel / histTop）、自己的输入函数、自己的绘制函数。

   和参数面板**共用一套排版**（历史记录屏的显示格式与设置页面用同一个模板）：
   左边一列可选的行，右边一栏「选项说明」。左边选第几次，右边就是那一次
   的细账。用同一个 drawPanelSide 画右栏，是最硬的那种"一样"。*/

void historyOpen(App *app) {
    if (!app) return;
    if (app->screen != SCREEN_SETTLE) return;   /* F 只在结算屏有意义 */
    app->histSel = 0;      /* 光标停在"最近一次"上 —— 刚打完，最想看的就是它 */
    app->histTop = 0;
    appSetScreen(app, SCREEN_HISTORY);
}

/* 历史记录屏的按键。ESC / F / Tab 都是"看完，回去"。*/
void historyInput(App *app) {
    Input *in;
    int n, rows;

    if (!app || app->screen != SCREEN_HISTORY) return;
    in = &app->input;

    /* 一局都没有时，上下键没有必要响应（也就没有 n-1 这个边界要防）。*/
    n = app->history.count;
    if (n > HISTORY_CAP) n = HISTORY_CAP;
    if (n > 0) {
        rows = historyVisibleRows(app);
        if (rows < 1) rows = 1;

        /* ↑↓ 走行，并且接长按重复 —— 20 行按住不放要能连续走。
           Home/End 跳首尾：与设置面板逐键一致，两处之间不需要换手感。

           ★ **PgUp / PgDn 已删除**。翻页的步长按**行数**算，而一屏能显示几行
           是随窗口高度变的，跳完那一下常常落在屏幕外一半，格式看着就是乱的。

           W/S 也从这个界面摘掉了：三个界面统一采用上下左右键选择，不留半拉子。*/
        if (inputKeyRepeat(in, VK_UP))   --app->histSel;
        if (inputKeyRepeat(in, VK_DOWN)) ++app->histSel;
        if (inputKeyPressed(in, VK_HOME))  app->histSel = 0;
        if (inputKeyPressed(in, VK_END))   app->histSel = n - 1;

        /* 到头就停住，不绕圈：这份列表是**有先后顺序**的（第 1 次永远是最近
           一次），绕回另一头会让人一瞬间搞不清自己在哪一头。设置面板那边
           绕圈是因为那八套预设本来就没有先后。*/
        if (app->histSel < 0) app->histSel = 0;
        if (app->histSel >= n) app->histSel = n - 1;

        if (app->histSel < app->histTop) app->histTop = app->histSel;
        if (app->histSel >= app->histTop + rows) app->histTop = app->histSel - rows + 1;
        if (app->histTop < 0) app->histTop = 0;
        if (app->histTop > n - rows) app->histTop = (n > rows) ? n - rows : 0;
    } else {
        app->histSel = 0;
        app->histTop = 0;
    }

    /* ★ **只有 F 能关这一屏**：ESC 与 Tab 都不关它 —— ESC 只当暂停用，
       Tab 只当设置面板的开合用；在这里按这两个键没有任何副作用，屏幕一动不动。*/
    if (inputKeyPressed(in, KEY_HISTORY)) {    /* F：同一个键再按一次就关 */
        appSetScreen(app, SCREEN_SETTLE);
        audioPlay(&app->audio, SFX_UI, 0.0f, 0.0f, 0);
    }
}

/* ============================================================ 输入 → 动作 */

void appHandleInput(App *app) {
    Params *p;

    if (!app) return;
    p = &app->params;

    /* ---- 参数面板 ----
       Tab 开合。这一次按键必须在这里用掉就返回：否则下面的面板输入会
       再看到同一个"刚按下"，刚打开就被同一次按键关掉。
       结算屏与历史记录屏上不认这个键：面板里调的是"下一局怎么玩"，
       而屏幕上那一刻要回答的是"这局打得怎么样"，两件事不该叠在一起。
       历史记录屏更是有自己的一层覆盖界面，在那里 Tab **什么都不做**。*/
    if (inputKeyPressed(&app->input, KEY_PARAM) &&
        (app->screen == SCREEN_PLAY || app->screen == SCREEN_PAUSE)) {
        if (app->paramOpen) appParamClose(app);
        else                appParamOpen(app);
        app->wish = v3(0.0f, 0.0f, 0.0f);
        app->crouch = 0;
        return;
    }
    /* 面板开着的时候，键位全归面板：WASD 变成选行与调整，R 变成恢复默认，
       R/Q 那种"重置本局 / 结束本局"一律不响应 —— 正在调参数时被重置一局
       是最让人恼火的事。*/
    if (app->paramOpen) {
        appParamInput(app);
        app->wish = v3(0.0f, 0.0f, 0.0f);
        app->crouch = 0;
        return;
    }

    /* ---- 历史记录屏 ----
       先于其它键处理：它是一层"盖在结算屏上的"界面，在它上面按什么
       都只对它有影响。**只有 F 关掉它**（ESC 与 Tab 都收掉，见 historyInput 末尾）。*/
    if (app->screen == SCREEN_HISTORY) {
        historyInput(app);
        app->wish = v3(0.0f, 0.0f, 0.0f);
        app->crouch = 0;
        return;
    }

    /* ---- 任何一屏都要响应的键 ----
     *
     * 这里重排过：R 和 Q 原本功能太冗余。原先三个键各管一摊：
     *     R = 重置本局（分数清零、球重摆，但不结算）
     *     Q = 结束本局并结算
     *     ESC = 暂停；结算屏上还兼着"返回训练场"
     * 问题是 R 和 Q 在玩家眼里做的是同一件事 ——"这一局不想要了，重来"，
     * 区别只在"先看一眼成绩"还是"不看"。那就只留一个键，让它一步做到位：
     *
     *     R = 结算并开始新游戏（游戏中、暂停中都一样）
     *     R = 开始新游戏（结算屏：结算过了，直接开局）
     *     ESC = 只做暂停/继续，在结算屏上**什么都不做**
     *
     * ESC 只作暂停用 —— 所以结算屏上的"按 ESC 返回训练场"
     * 整条撤销：结算屏上要回到训练场，走 R（开新局）就对了。*/
    if (app->screen == SCREEN_SETTLE) {
        if (inputKeyPressed(&app->input, KEY_RESTART)) {
            appResetSession(app, 0);    /* 内含 appSetScreen(PLAY) */
        }
        if (inputKeyPressed(&app->input, KEY_HISTORY)) {
            historyOpen(app);
        }
        app->wish = v3(0.0f, 0.0f, 0.0f);
        app->crouch = 0;
        return;
    }

    if (inputKeyPressed(&app->input, KEY_PAUSE)) {
        if (app->screen == SCREEN_PLAY) appSetScreen(app, SCREEN_PAUSE);
        else                           appSetScreen(app, SCREEN_PLAY);
    }
    if (inputKeyPressed(&app->input, KEY_RESTART)) {
        /* 游戏中、暂停中都是"结算并开始新游戏"：先把当前这局按 END_MANUAL
           结算掉（成绩进记录），结算屏再按一次 R 才真的开新局。
           这正是这个两段式的用意 —— 手一滑按了 R 也有个看成绩的机会。*/
        app->endReason = END_MANUAL;
        appEndSession(app);             /* 手动结算不等 endHold，立刻出成绩 */
    }

    /* ---- 只有游戏中才吃的输入 ---- */
    if (app->screen != SCREEN_PLAY) {
        app->wish = v3(0.0f, 0.0f, 0.0f);
        app->crouch = 0;
        return;
    }

    /* 鼠标转视角。Raw Input 给的位移是**未经系统加速、也没被屏幕边界夹过**的，
       这正是"转到哪都不会卡住"的原因。dx 向右为正，向右看是 yaw 减小
       （yaw=0 面向 -z，yaw 减小则 forward.x = -sin(yaw) 变正，也就是转向 +x）。*/
    if (app->input.dx != 0.0f || app->input.dy != 0.0f) {
        playerLook(&app->player,
                   -app->input.dx * p->mouseSens,
                   -app->input.dy * p->mouseSens, p);
    }

    /* 滚轮微调视野角：不占键位，且改完立刻见效，属于顺手给的调节。*/
    if (app->input.wheel != 0.0f) {
        p->fovDeg = clampf(p->fovDeg - app->input.wheel * 2.0f, 45.0f, 110.0f);
    }

    /* 移动意图：先按前后左右合成，真正的位移在 appStep 里按固定步长做，
       这样移动速度与帧率无关，自检里也能一步步驱动。*/
    {
        float f = 0.0f, r = 0.0f;
        if (inputKeyDown(&app->input, KEY_FORWARD)) f += 1.0f;
        if (inputKeyDown(&app->input, KEY_BACK))    f -= 1.0f;
        if (inputKeyDown(&app->input, KEY_RIGHT))   r += 1.0f;
        if (inputKeyDown(&app->input, KEY_LEFT))    r -= 1.0f;
        app->wish = v3(r, 0.0f, f);
        app->crouch = inputKeyDown(&app->input, KEY_CROUCH) ? 1 : 0;
    }

    /* 开火：单击直接响应。这个必须在这里做而不是等 appStep，
       因为 appStep 是按固定步长跑的，一帧里可能一步都不跑（帧很短的时候），
       那样点一下就会丢。*/
    if (inputMousePressed(&app->input, 0) && app->fireCooldown <= 0.0f) {
        appFire(app);
        app->fireFlash = 1.0f;
        app->fireCooldown = (p->autoFireInterval > 0.0f)
                          ? p->autoFireInterval : FIRE_CLICK_COOLDOWN;
    }
}

/* ============================================================ 参数面板
 *
 * 面板本身就是"描述表 + 一个选中行号"。行内容、上下限、步长全部从
 * g_paramDescs 读 —— render.cpp 里没有第二份参数清单，所以加了参数
 * 不会出现"玩法有了、面板上没有"这种脱节。
 *
 * 为什么要在乎这个：参数表和面板如果各写一份，加参数时很容易只改一处，
 * 界面上那排滑杆就和实际能调的东西成了两回事。这次把"面板 = 表的视图"
 * 定成了结构上的约束，两者共用同一张表，漏改编译不过。
 */

/* ---------------------------------------------------------- 一级菜单表
 *
 * 八条预设 + 「自定义参数」，一共 9 行 —— 一屏绝对放得下，所以一级不滚动。
 * 预设那几行是照着 g_presets 一行一条摆出来的，自检里有一条断言盯着
 * "预设行数 == PRESET_COUNT 且 arg 依次为 0..N-1"，将来加一套预设忘了
 * 在这里补一行，当场就报出来。
 *
 * "音量 / 静音 / 试听"三行已删掉：声音开关是**参数**，归到
 * "用户偏好设置"里去了。一级菜单上只剩"选预设"和"进二级"两件事，
 * 回车键的含义才唯一。
 */
const ParamMenuRow g_paramMenu[] = {
    { PMENU_PRESET, 0 }, { PMENU_PRESET, 1 }, { PMENU_PRESET, 2 },
    { PMENU_PRESET, 3 }, { PMENU_PRESET, 4 }, { PMENU_PRESET, 5 },
    { PMENU_PRESET, 6 }, { PMENU_PRESET, 7 },
    { PMENU_CUSTOM, -1 }
};
const int g_paramMenuCount = (int)(sizeof(g_paramMenu) / sizeof(g_paramMenu[0]));
const int g_paramMenuDetailRow = (int)(sizeof(g_paramMenu) / sizeof(g_paramMenu[0])) - 1;

int appParamMenuPreset(int menuRow) {
    if (menuRow < 0 || menuRow >= g_paramMenuCount) return -1;
    if (g_paramMenu[menuRow].kind != PMENU_PRESET) return -1;
    return clampi(g_paramMenu[menuRow].arg, 0, PRESET_COUNT - 1);
}

int appParamDiffersFromPreset(const App *app, int descIndex) {
    Params ref;
    const ParamDesc *d;
    if (!app || descIndex < 0 || descIndex >= g_paramDescCount) return 0;
    /* 拿"当前这份参数"再套一次当前预设，得到的就是"这一套预设的原值"。
       不另存一份预设快照：那份快照迟早会和 g_presets 走散。*/
    ref = app->params;
    paramsApplyPreset(&ref, ref.preset);
    d = &g_paramDescs[descIndex];
    return paramDescGet(&ref, d) != paramDescGet(&app->params, d);
}

/* ---------------------------------------------------------- 页内行号 <-> 参数下标
 *
 * 列表最前面插了一行「当前预设方案」，于是行号和参数下标差 1。
 * 菜单栏又让面板分成两页，**行号是页内行号**。
 *
 *   预设页（page 0）：行 0 = 「当前预设方案」，行 1..32 = 参数下标 0..31
 *   偏好页（page 1）：行 0..11 = 参数下标 32..43
 *
 * （核准过这两行：全表 44 项，`PARAM_GROUP_SECTION_SPLIT` 切在
 *   第 5 组，前五组 14+5+5+4+4 = 32 项归预设页，后三组 5+6+1 = 12 项归偏好页。
 *   这两行以前写的是 0..29 / 30..41 —— 那是参数只有 42 项时的数，早过期了。）
 *
 * 分界点不另起一个常量，直接问 config.cpp 的 `paramIsPreference` ——
 * "哪些参数属于偏好页"这件事只该有**一个**定义，页的边界与 `R` 的作用范围
 * 本来就该是同一批参数（R 只恢复"用户偏好设置"）。
 *
 * 换算只在这几个函数里做：别处一律调它们。两个函数各写一遍 `row - 1`，
 * 早晚会有一处忘了改 —— 那种错还不是崩溃，是"光标和它选中的项对不上"，
 * 最难查的一类；而带上页号之后，"漏写页号"至少还会编不过。*/
int appParamPageCount(void) {
    return PARAM_SECTION_COUNT;      /* 2：预设方案设置 / 用户偏好设置 */
}

/* 某一页的第一个参数在 g_paramDescs 里的下标。
   依赖一条不变量：**偏好参数在表里是连续的一段**（组在表里是连续的，
   而"是不是偏好"按组判定）。自检里有一条断言专门盯这条不变量 ——
   哪天有人把一项偏好参数插到玩法段中间，这条换算就会静默地错位。*/
static int paramPageFirstDesc(int page) {
    int i;
    if (page <= 0) return 0;
    for (i = 0; i < g_paramDescCount; ++i)
        if (paramIsPreference(i)) return i;
    return g_paramDescCount;         /* 一个偏好项都没有：空页，不是崩溃 */
}

/* 某一页里有几**项参数**（不含预设页第 0 行那条）。*/
static int paramPageParamCount(int page) {
    int first, last;
    if (page <= 0) { first = 0; last = paramPageFirstDesc(1); }
    else           { first = paramPageFirstDesc(1); last = g_paramDescCount; }
    if (last < first) last = first;
    return last - first;
}

int appParamPageRowCount(int page) {
    if (page < 0) page = 0;
    if (page >= appParamPageCount()) page = appParamPageCount() - 1;
    /* 预设页多一条第 0 行；偏好页顶上不挂它（它只在预设方案设置页出现）。*/
    return paramPageParamCount(page) + ((page <= 0) ? 1 : 0);
}

int appParamRowToDesc(int page, int row) {
    int first;
    if (page < 0 || page >= appParamPageCount()) return -1;
    if (row < 0 || row >= appParamPageRowCount(page)) return -1;
    if (page <= 0) {
        if (row == 0) return -1;     /* 「当前预设方案」那一行：没有对应参数 */
        return row - 1;
    }
    first = paramPageFirstDesc(1);
    if (row >= paramPageParamCount(1)) return -1;
    return first + row;
}

void appParamSetPage(App *app, int page) {
    int i;
    if (!app) return;
    if (page < 0) page = 0;
    if (page >= appParamPageCount()) page = appParamPageCount() - 1;
    if (page == app->paramPage) return;

    app->paramSelMem[app->paramPage] = app->paramSel;
    app->paramTopMem[app->paramPage] = app->paramTop;
    app->paramPage = page;
    app->paramSel = app->paramSelMem[page];
    app->paramTop = app->paramTopMem[page];
    app->paramTopSkip = 0;           /* 换页就是整页换过，窗口从块头起 */

    /* 记忆值本来就该是合法的（同一页存、同一页取），但 App 也可能被
       自检或出图直接从外面摆出来，夹一次比信任调用方便宜。*/
    i = appParamPageRowCount(page);
    if (app->paramSel < 0) app->paramSel = 0;
    if (app->paramSel >= i) app->paramSel = (i > 0) ? i - 1 : 0;
    if (app->paramTop < 0) app->paramTop = 0;
    app->paramTopSkip = 0;

    /* 记忆下来的 top 是按"块头"记的，恢复出来可能正好把光标挤到窗口外面
       （两个窗口的可见行数不同，见 paramPanelFit）。这里补一次归位。
       归位只在光标看不见时才动手，看得见时一格不挪。*/
    appParamScrollIntoView(app);
}

/* ★ 当前这 44 项完全等于八套预设里的哪一套？返回预设下标，都不像就 -1。
 *
 * 列表顶上那行「当前预设方案： XXX」显示的就是它，它是**唯一**
 * 一处"现在和预设像不像"的判据 —— 旧的「（已被修改）」黄字已经整个删掉，
 * 不再有第二个真相来源。
 *
 * 比法是"拿当前参数套一遍第 P 套预设，再逐项比" —— 与 appParamDiffersFromPreset
 * 同一套算法，只是把"当前那一套"换成了"试一试第 P 套"。**只比该预设覆盖到的项**：
 * 预设没写过的项没有参照值，拿它去比就是凭空造标准（那 12 项个人偏好
 * 也会因此把每一套预设都判成"不像"）。
 *
 * 多套同时命中取预设序靠前的那一套。八套里真出现"覆盖项完全一样"的两套
 * 理论上做得到，实际上没有 —— 自检里有一条断言盯着八套两两互不相同。*/
int appParamMatchedPreset(const App *app) {
    int p, i;
    if (!app) return -1;
    for (p = 0; p < PRESET_COUNT; ++p) {
        Params ref = app->params;
        int same = 1;
        paramsApplyPreset(&ref, p);
        for (i = 0; i < g_paramDescCount && same; ++i) {
            const ParamDesc *d = &g_paramDescs[i];
            if (!paramCoveredByPreset(p, i)) continue;
            if (paramDescGet(&ref, d) != paramDescGet(&app->params, d)) same = 0;
        }
        if (same) return p;
    }
    return -1;
}

/* 面板一屏放不下所有行，用滚动窗口。可见行数由渲染层给（排版在那边），
   但滚动必须在输入层做 —— 否则调到最后一行时看不见自己在调什么。

   ★ 这个函数早先做的是"把光标按到窗口底边"，后改成"窗口大小按当前 paramTop
   现算 + 一格一格往前挪，挪到光标刚进窗口为止"。规则是：不要提前滚动，
   只有光标到底部时，再往下选择才滚动一项。

   ★★ 现在改成**按格锚定**。早先那套"一格一格挪"在实测里留下了第二个
   毛病：窗口每推一行，消耗的格数可能是 1 也可能是 2（撞上带组标题的那一行），
   于是"挪到光标刚进窗口"停下时，光标相对窗口的位置会在最后两格之间晃 ——
   按 ↓ 时光标从 570 跳到 540，**往下按反而往上跑**。根因是"按行推窗口"与
   "按格量容量"两把尺子混用（详见 render.cpp 里 paramItemCapZero 上面那段）。

   现在只做两件事：

     往下：光标掉出窗口下沿时，把窗口起点定成"光标那一块（含组标题条）的尾巴 -
           容量"。光标于是永远落在窗口的第 容量-1 格上，离底边的距离只由容量
           决定，而锚定容量是个常量 —— 于是每一格都稳。光标没越界时一格都不动，
           这正是"到底了才滚"。
     往上：光标掉出窗口上沿时，把**光标那一块的头**对齐到窗口上沿 —— 光标
           的内容于是落在第 1 格上（它的组标题条若有，正好占第 0 格）。
           ★ 别写成"top 收成光标那一行"：那样光标的内容会紧贴第 0 格，而它
           头顶的组标题条一进窗口就得往下挤一格 —— 按 ↑ 走进一个新组时，光标
           在屏幕上**往下跳 30 像素**。断在自检「滚动稳定性」那一组里。

   ★ 窗口起点记的是**格**，不是行：于是它允许停在"某一行块的中间"，也就是
   顶行的组标题条正好滚出窗口上沿。这一格之差不许省 —— 省掉（起点只能落在
   块头上）就意味着末段遇到"上面那行是带组标题的、两格塞不下"时必须空出一格，
   光标当场往上跳 30 像素。实测过，见 render.cpp 里 paramTopSkipOf 的说明。

   ★ 旧代码末尾那句"把窗口底边压实"（top = total - rows）仍然不能要 ——
   它会在末段把窗口整体往上抬，与"贴着底边滚"是相反方向的力。*/
void appParamScrollIntoView(App *app) {
    const int total = appParamPageRowCount(app ? app->paramPage : 0);

    if (!app || total <= 0) return;
    if (app->paramSel < 0) app->paramSel = 0;
    if (app->paramSel >= total) app->paramSel = total - 1;
    if (app->paramTop < 0) app->paramTop = 0;
    app->paramTopSkip = paramTopSkipOf(app, app->paramTop);
    /* ★ 别在这里写"窗口跑到光标上面了就把 top 收成 sel"—— 那条捷径让光标
       落在窗口第 0 格上，而"光标那一行的组标题条"要不要占第 0 格是随行而异的：
       普通行第 0 格就是光标自己，带组标题的行第 0 格是那条标题、光标在第 1 格。
       于是往上走撞进一个新组时，光标在屏幕上往下跳一格。交给下面那条
       掉出窗口上方的分支，它是按"格"对齐的，两种行落点一样。*/
    {
        const int fit = paramPanelFit(app, app->paramTop, NULL, NULL);
        const int blockEnd = paramItemIndexOfRow(app, app->paramSel)
                             + paramRowSpans(app, app->paramSel);
        const int cur = blockEnd - 1;      /* 光标那一行**内容**所在的那一格 */
        int start;

        if (app->paramSel >= app->paramTop && app->paramSel < app->paramTop + fit)
            return;                                        /* 光标已经在窗口里 */

        if (cur < paramWindowStartItem(app)) {
            /* 掉出窗口上方：起点直接落到光标内容那一格 —— 光标于是永远贴在
               窗口的第 0 格上，按 ↑ 时它在屏幕上一格都不动（那条组标题条如果
               在它头上，就跟着滚出窗口上沿，不占位置）。

               ★ 别改成"把光标那一块的头对齐到窗口上沿"（起点 = cur - 1）：
               那样光标落在第 1 格，往上走到第 0 格再走一步就又得重新对齐，
               于是一路按 ↑ 时光标在屏幕上 **1 格、2 格、1 格、2 格**地打摆子。
               自检「滚动稳定性」量到的就是这个摆。*/
            start = cur;
        } else {
            /* 掉出窗口下方：**让光标块的尾巴贴在窗口底边上**。
               窗口起点直接按"格"算出来 —— 光标那一块（组标题条也算它的，若有）
               的最后一格，往回退整整一个容量。光标于是永远落在窗口的第 容量-1
               格上，离底边的距离是个常量，按 ↓ 时它在屏幕上**一格都不动**。

               容量取 paramPanelSlotCap（把 ﹀ 那格永远预扣掉的保守值）：末段 ﹀
               消失、容量多出一格时，光标不会跟着往下跳，多出来的那格正好用来
               "把最后一项显示出来"。*/
            start = blockEnd - paramPanelSlotCap(app);
        }
        if (start < 0) start = 0;
        /* ★ 这里还要把窗口底边压实。
         *
         * 末段锚定算出来的 start 可能让窗口**伸到表尾外面去**（起点 17 + 容量 19
         * = 36 > 表尾 35），那一格伸出表尾的地方就是列表底下空出的一行。
         * 先前容量里有一条"看光标"的例外恰好把它挡住了；例外一拆，洞就露出来了
         * —— 这才是"拆一条规则要顺手看看它替谁挡着事"。
         *
         * 退回来的量就是超出量，按**格**算。它只会让窗口**上移**，也就是光标在
         * 屏幕上**往下走** —— 与"按 ↑ 光标不许往上跳"不冲突。这不是早先删掉的
         * 那句 `top = 总行数 - 可见行数`：那句按行算、会把光标整个顶上去，
         * 这里按格算、最多退几格，而且退到"窗口末尾正好压住表尾"就停 ——
         * 那时最后一行就在窗口最底格上，正是"滚到底"该有的样子。
         *
         * 退完之后容量可能因为"起点是不是 0""还要不要给 ﹀ 让格"再变一次，
         * 所以走两遍：第二遍必然收敛（第二遍的超出量只会更小）。*/
        {
            int pass;
            for (pass = 0; pass < 2; ++pass) {
                const int over = start + paramPanelCapAtStart(app, start)
                                 - paramItemCount(app);
                if (over <= 0) break;
                start -= over;
                if (start < 0) { start = 0; break; }
            }
        }
        /* 起点落在第几格上就记到那一格 —— 命中组标题条那一格时 skip 自然是 1
           （条已经滚出窗口上沿，不占窗口里的位置），命中本行内容那一格时是 0。
           早先写成"换算回行号"的版本在这里白丢一格，光标末段就会往上跳 30 像素。*/
        app->paramTop = paramRowAtItem(app, start);
        app->paramTopSkip = start - paramItemIndexOfRow(app, app->paramTop);
    }
}

static void paramMsg(App *app, const wchar_t *text) {
    if (!app || !text) return;
    _snwprintf(app->paramMsg, ARRAY_COUNT(app->paramMsg) - 1, L"%ls", text);
    app->paramMsg[ARRAY_COUNT(app->paramMsg) - 1] = L'\0';
    /* 停留时长。从 2.4 秒加到 4.4 秒 —— 保存/载入那两句提示比"已恢复预设值"
       长得多，2.4 秒读不完，而且在面板里很容易被下一行操作抢走注意力。*/
    app->paramMsgT = 4.4f;
}

void appParamOpen(App *app) {
    if (!app || app->paramOpen) return;
    app->paramOpen = 1;
    app->paramDirty = 0;
    app->paramPopOpen = 0;
    app->paramPopSel = 0;
    app->paramMsg[0] = L'\0';
    app->paramMsgT = 0.0f;
    /* 光标落在**第一行**（「当前预设方案」）上 —— 但它现在站在菜单栏上，
       列表里的位置（0）只是"等会儿按 ↓ 从哪一行开始看"的记分牌（见下面
       paramSel 的说明）。早先这里停在"当前那套预设"上，那时列表是预设清单、
       那样停最省事；现在第一行本身就是"当前是哪一套"。
       弹窗不自动弹出 —— 打开面板就弹一个浮层盖住半张表，太吵。

       ★ 光标默认停在菜单栏上（paramOnMenu = 1）。菜单栏是这一屏的最顶端，
       一进来先看见"当前在哪一类里"比直接落进列表更符合"从外往里"的层次。*/
    app->paramPage = 0;
    app->paramOnMenu = 1;            /* 开面板时光标停在菜单栏上 */
    app->paramSelMem[0] = app->paramSelMem[1] = 0;
    app->paramTopMem[0] = app->paramTopMem[1] = 0;
    app->paramSel = 0;
    app->paramTop = 0;
    app->paramTopSkip = 0;
    /* 放开光标。screen 仍是 PLAY，所以这里得让 appSetScreen 重算一次
       捕获条件 —— 面板开着的时候绝不该夹住光标。*/
    appSetScreen(app, app->screen);
}

void appParamClose(App *app) {
    if (!app || !app->paramOpen) return;
    app->paramOpen = 0;
    app->paramPopOpen = 0;
    app->paramMsg[0] = L'\0';
    app->paramMsgT = 0.0f;
    /* 参数可能是从存档载入的，也可能改过，收回控制权之前夹一次，
       免得存档里那种越界值一直带到玩法里。*/
    paramsClamp(&app->params);
    appSetScreen(app, app->screen);

    /* ★ **自动落盘就挂在这里**：关面板时保存，效果上就是即写即存，
       省去频繁保存。
       paramDirty 是"这一轮面板里有东西被改过"，没改过就一个字节都不写 ——
       打开看一眼再关掉，不该让存档文件的时间戳动一下。

       这里也顺手把"存档"和"玩"彻底解耦：以前是手动按 S，忘按就丢；
       现在是走出这道门就存，没有"忘"这个可能。代价是**强杀进程会丢**
       （任务管理器结束进程不经过这里），README 里写明了。*/
    if (app->paramDirty && app->paramsPath[0]) {
        paramsSave(&app->params, app->paramsPath);
        app->paramDirty = 0;
    }
}

int appParamIsOpen(const App *app) { return app && app->paramOpen; }

/* 改完一项之后的收尾。before 是改动前的那一份，用它比较"到底变了什么"。
   为什么要比而不是直接看是哪一项：半径上下限会互相交换，预设一次会改掉
   二十来项 —— 只看"改动落在哪一项"会漏掉这些连带改动。

   descIndex 允许传 -1，表示"这次改的不是表里某一项"（一级菜单上的音量、
   静音，以及套用预设）。这时一切判断都退化成"拿 before 和现在逐字段比"，
   照样能把该重建的重建起来。*/
void appParamAfterChange(App *app, int descIndex, const Params *before) {
    const ParamDesc *d = NULL;
    const Params *now;
    int poolDirty = 0;

    if (!app || !before) return;
    if (descIndex >= g_paramDescCount) return;
    if (descIndex >= 0) d = &g_paramDescs[descIndex];
    now = &app->params;

    /* 墙面风格换贴图。这张贴图是按风格生成的，不重建的话改了没反应。*/
    if (now->wallStyle != before->wallStyle)
        sceneRefreshStyle(&app->scene, &app->params);

    /* 气球池那两组的参数直接决定"墙上有哪些球"，得重建池。
       判据用的是描述表里的分组，不是硬编码的字段名清单 ——
       以后往这两组里加参数，不用回来改这里。*/
    if (d && (d->group == PARAM_GROUP_BALLOON || d->group == PARAM_GROUP_MOTION)) {
        poolDirty = 1;
    } else if (!d) {
        /* 没指名是哪一项（套预设、调音量、切静音）：把这两组**整体比一遍**。
           套预设会一口气改掉二十来项，要是按"每一项各调一次"来做，池子会
           被重建二十来次 —— 每次都重新抽一遍气球位置，白干活；而只调一次
           又漏掉了那些没被显式指名的改动。整体比一遍两个毛病都没有。*/
        int i;
        for (i = 0; i < g_paramDescCount; ++i) {
            const ParamDesc *dd = &g_paramDescs[i];
            if (dd->group != PARAM_GROUP_BALLOON && dd->group != PARAM_GROUP_MOTION)
                continue;
            if (paramDescGet(now, dd) != paramDescGet(before, dd)) { poolDirty = 1; break; }
        }
    }
    /* 规则变了（比如从"恒量靶场"换成"计时挑战"）也要重建：判定与计分、
       节奏那几组会改掉球的行为。早先这里判的是 mode，后来规则改由预设
       决定，但**仍可以绕过预设直接改规则**（预设只是一组初始值），
       所以还得盯着 mode 本身。*/
    if (now->mode != before->mode) poolDirty = 1;
    if (now->balloonCount != before->balloonCount) poolDirty = 1;

    if (poolDirty) {
        paramsClamp(&app->params);
        sceneComputeField(&app->params, &app->field);
        balloonPoolInit(&app->pool, &app->params, app->seed, &app->field);
        balloonPoolFill(&app->pool, &app->params, &app->field, app->elapsed);
    }

    /* 声音开关在这里**立刻**生效，而不是等这一帧末尾那次统一同步。
       调用方紧接着就要放界面音（"拨到开就响一声"），而统一同步在输入处理
       之后才跑 —— 晚一帧，那一声就被旧的静音状态吃掉了。*/
    audioSetVolume(&app->audio, VOLUME_DEF);
    audioSetMuted(&app->audio, !app->params.soundOn);

    app->paramDirty = 1;
    /* 界面音**不在这里放**。这个函数会被套预设那条路径连着调四十来次
       （每项一次），在这里放音会叠成一片噪声。改由各个调用点自己放一声：
       手改一项、撤销修改、套预设、转动声音开关、进二级，各一处。*/
}

/* 套用一套预设。和"手改某一项"走的是同一条收尾路径（appParamAfterChange），
   所以预设改了哪些东西都会被正确地重建 —— 墙面贴图、气球池、判定。
   逐项走一遍表，随便哪一项变了都能照顾到，不必维护一张"预设影响了什么"的清单。*/
static void paramApplyPreset(App *app, int preset) {
    Params before;
    if (!app || !app->paramOpen) return;
    if (preset < 0 || preset >= PRESET_COUNT) return;

    before = app->params;
    paramsApplyPreset(&app->params, preset);
    /* 收尾只调一次，不逐项调 —— 套预设是一个原子动作，池子重建一次就够。*/
    appParamAfterChange(app, -1, &before);
    paramMsg(app, g_presets[preset].name);
    /* 界面音只放这一声：appParamAfterChange 自己不出声（它会被连着调很多次，
       在那里出声会叠成一片噪声）。*/
    audioPlay(&app->audio, SFX_UI, 0.0f, 0.0f, 0);
}

/* 「R：恢复用户偏好设置」。
 *
 * 这个键**只动「用户偏好设置」那一段**（玩家 / 画面 / 声音三组，共 12 项，
 * 下标 32..43），玩法参数一项都不碰 —— 同一组键在其他地方含义可能相反，
 * 写在这里免得以后有人照着一份过期的说明来对代码。
 *
 * 目标值是**出厂默认**，不是"某一套预设的值"：预设本来就不管这三组
 * （paramCoveredByPreset 对它们恒为假），根本没有"预设值"可退。
 * 这 12 项恢复之后不挂黄字、看不出来，所以由列表顶上那一行"当前预设方案"
 * 负责说明；而那一行只比玩法参数，所以恢复偏好**不会**让它的显示发生变化
 * （这正是想要的效果）。
 *
 * 判据走 paramIsPreference（组号 → 大类），不写死下标。
 *
 * appParamAfterChange 只调一次、传 -1：恢复是一**个原子动作**，逐项调会让
 * 气球池重建十来次（声音那几项还会触发重灌），球的位置白跳一下。*/
static void appParamRestorePreferences(App *app) {
    Params before;
    int i;

    if (!app || !app->paramOpen) return;

    before = app->params;
    for (i = 0; i < g_paramDescCount; ++i) {
        if (!paramIsPreference(i)) continue;
        paramDescReset(&app->params, &g_paramDescs[i]);
    }
    /* 一项都没变也有可能：本来就是默认值（刚打开面板就按 R 很常见）。
       照样给提示 —— 提示语说的是"这件事做完了"，不是"有东西变了"。*/
    appParamAfterChange(app, -1, &before);

    paramMsg(app, L"已恢复用户偏好设置");
    audioPlay(&app->audio, SFX_UI, 0.0f, 0.0f, 0);
}

/* ---------------------------------------------------------- 预设方案弹窗
 *
 * 列表第 0 行「当前预设方案： XXX ▶」按回车把它拉出来的浮层，
 * **光标直接进弹窗**。
 *
 * 内容是**原封不动复用 g_paramMenu 那张表** —— 它就是"八套预设 + 一条入口"，
 * 与弹窗一模一样，只是入口那条改名叫「自定义参数」。不新造一张表，
 * 自检里"预设行数 == PRESET_COUNT 且 arg 依次为 0..N-1"那条断言就还能继续管着它。
 *
 * 三条要紧的规则：
 *   ① 光标一进来就落在**当前那套**上（appParamMatchedPreset 的结果，
 *      一套都不像时落在「自定义参数」上）—— 不然每次都要从头拨。
 *   ② 回车 = 选定，**选定后自动收起**（选好了再关闭这一层）。
 *   ③ ESC 收起来但什么都不改；在弹窗里按 ESC 不该把整个面板关掉 ——
 *      预期是"退一层"，这是那一层的最后一层了。
 *
 * ←/→ 在这里什么都不做（面板里只有 ↑↓ 挪光标、←→ 调数值，
 * 而弹窗里没有"数值"可调）。*/
static void appParamInputPopup(App *app) {
    Input *in = &app->input;
    const int n = g_paramMenuCount;      /* 8 套预设 + 「自定义参数」 */
    int toFirstParam = 0;                /* 选的是「自定义参数」→ 光标往下一行跳 */

    /* ★ 到两端就停住，不绕圈（早先绕到另一端）。列表那一层本来就是夹取的，
       这里跟着一致。*/
    if (inputKeyRepeat(in, VK_UP))   --app->paramPopSel;
    if (inputKeyRepeat(in, VK_DOWN)) ++app->paramPopSel;
    if (inputKeyPressed(in, VK_HOME)) app->paramPopSel = 0;
    if (inputKeyPressed(in, VK_END))  app->paramPopSel = n - 1;
    if (app->paramPopSel < 0) app->paramPopSel = 0;
    if (app->paramPopSel >= n) app->paramPopSel = n - 1;

    if (inputKeyPressed(in, KEY_PAUSE)) {                  /* ESC = 收起来，不改任何东西 */
        app->paramPopOpen = 0;
        audioPlay(&app->audio, SFX_UI, 0.0f, 0.0f, 0);
        return;
    }

    if (!inputKeyPressed(in, VK_RETURN)) return;

    {
        const ParamMenuRow *row = &g_paramMenu[app->paramPopSel];
        if (row->kind == PMENU_PRESET) {
            int np = clampi(row->arg, 0, PRESET_COUNT - 1);
            /* 判据从"和 params.preset 是不是同一套"改成"现在是不是**就已经
               长成这样**"（appParamMatchedPreset）。这一改修掉一个会挨骂的坑：
               改了十几项、第 0 行已经显示「自定义参数」之后，回弹窗点
               **原来那一套** 想退回去 —— 照老判据 np == params.preset 会被
               当成"重复套用"跳过，屏幕上什么都不变，看着就是"点了没反应"。
               现在只有真的已经一模一样时才跳过（那种情况跳过是对的：
               重复套用会把气球池整个重建，球的位置白跳一下）。*/
            if (appParamMatchedPreset(app) != np) paramApplyPreset(app, np);
            else {
                paramMsg(app, g_presets[np].name);
                audioPlay(&app->audio, SFX_UI, 0.0f, 0.0f, 0);
            }
        } else {
            /* 「自定义参数」= "就这样吧，自己调"。它不做任何改动，
               只把弹窗收起来 —— 参数本来就是自己调的那些。
               收起之后第 0 行会如实显示「自定义参数」（除非他调出来的这套
               恰好等于某一套预设，那就显示那一套，那也是对的）。

               ★ 选「自定义参数」走完把光标放到**列表第 1 行**（第 0 行下面
               那一行，也就是第一项参数）。语义上也顺：选「自定义参数」的
               意思就是"要开始自己调"，光标落到第一项参数上，接着就是调。*/
            toFirstParam = 1;
            audioPlay(&app->audio, SFX_UI, 0.0f, 0.0f, 0);
        }
    }
    app->paramPopOpen = 0;
    /* 收起之后光标回第 0 行 —— 刚才做的就是"改当前预设方案"这件事，
       停在那一行才能立刻看见它变成了什么。（选「自定义参数」的那条路
       落到第 1 行，见上。）*/
    app->paramSel = toFirstParam ? 1 : 0;
    app->paramTop = 0;
    app->paramTopSkip = 0;
}

/* 当前页是不是「用户偏好设置」。`R` 的生效范围、页脚的 R 提示、页面的划法
   都问这一个函数 —— 三处各写一遍 `paramPage == 1` 的话，早晚有一处跟着改漏。*/
static int paramIsPreferencePage(const App *app) {
    return app && app->paramPage == 1;
}

/* ---------------------------------------------------------- 菜单栏按键
 *
 * 光标停在两页分类上时走这条路。规则：
 *   ←→ 切分类，↓ 回到列表，↑ 什么都没有（上面没东西了）。
 *   **回车在这儿不做事** —— 回车只用于打开预设方案弹窗，不用于进入列表，
 *   所以这里不认回车，页脚也不写它。
 *   R 只在偏好页上有效（见 appParamInputMenu 末尾的说明）。*/
static void appParamInputMenuBar(App *app) {
    Input *in = &app->input;

    /* 切页用 inputKeyRepeat：按住 ← / → 要能连切，和列表里 ↑↓ 的待遇一致。
       appParamSetPage 自己会夹页号，所以到头再按就是没反应，不绕圈。*/
    if (inputKeyRepeat(in, VK_LEFT))  appParamSetPage(app, app->paramPage - 1);
    if (inputKeyRepeat(in, VK_RIGHT)) appParamSetPage(app, app->paramPage + 1);

    /* ↓ = 回到列表首行。切页时光标落在菜单栏上，按下键进入列表。
       回到的**行号取自该页记忆** —— 从这页下去看一眼再上来，回来时还在原处。*/
    if (inputKeyRepeat(in, VK_DOWN)) {
        app->paramOnMenu = 0;
        appParamScrollIntoView(app);
        audioPlay(&app->audio, SFX_UI, 0.0f, 0.0f, 0);
    }

    /* ---- End = 一跳到列表最底下 ----
       它和列表里的 End 是同一件事，只是入口在栏上；也与 Home 正好对称 ——
       Home 从列表跳进栏（这个面板的最顶），End 从栏跳进列表的最底。

       光标要**回到列表里**（paramOnMenu = 0）：End 的语义是"到最底下去"，
       把光标留在栏上、只有列表滚到底，那不是"移动到底部"。
       ↓ 也是出栏，但它是"回到列表、接着上次看的那一行"（见上一条），
       与这里"不管从哪儿来，落到最末一行"是两件事，所以分开写。*/
    if (inputKeyPressed(in, VK_END)) {
        app->paramOnMenu = 0;
        app->paramSel = appParamPageRowCount(app->paramPage) - 1;
        appParamScrollIntoView(app);       /* 落到末行，窗口跟着滚到底 */
        audioPlay(&app->audio, SFX_UI, 0.0f, 0.0f, 0);
        return;
    }

    /* R 与列表里同一个条件：当前页是偏好页。两处判据各写一遍的话，
       早晚有一处会被改漏 —— 所以两边都调 paramIsPreferencePage。*/
    if (inputKeyPressed(in, KEY_RESTART) && paramIsPreferencePage(app))
        appParamRestorePreferences(app);
}

/* ---------------------------------------------------------- 设置列表按键
 * 就是参数表，按菜单栏分成两页：预设页 = 第 0 行 + 32 项玩法
 * 参数，偏好页 = 12 项个人口味。行号一律是 appParamPageRowCount 那套。*/
static void appParamInputMenu(App *app) {
    Input *in = &app->input;
    const int total = appParamPageRowCount(app->paramPage);
    int descIdx;

    /* 键位分工：
       ↑↓ 选行，←→ 调数值，Home 进菜单栏、End 到尾行，R 恢复用户偏好设置
       （**仅在偏好页**），回车在第 0 行拉出方案弹窗（**只在这一行**）。
       ★ 面板里不认 WASD：统一采用上下左右键选择。
       S / L 是空键 —— 保存和载入已整个删掉，改为关面板时自动落盘。

       这里不再算 `rows`：可见行数只在渲染层按当时的 paramTop 现算
       （组标题跟着行滚，行高会变），收键盘这边一个数都不存。*/

    /* ---- 顶部 = 菜单栏 ----
       ① 光标停在第 0 行再按 ↑，进栏。
       ② Home 直接进栏、并把列表行号归零。
          菜单栏本来就是这个面板的**最顶端**，Home = 一跳到顶。

       判据用 **inputKeyRepeat**：输入层把系统的连发串
       丢掉了，长按是自己排的节拍（见 input.cpp），所以长按上键到顶之后只有
       repeat 通道还会出键、pressed 那条边沿早就用完了 —— 这正是
       "长按到顶进不去菜单栏"的原因。（输入层确实是自己排节拍，
       按下后停 0.32 秒，然后每秒约 22 次，与打字的系统连发无关。）

       必须排在下面那行 --paramSel 之前，并且**进去就 return**：同一个 ↑
       会让 repeat 为真，不拦住的话这一帧会又进栏又减行号。*/
    if (app->paramSel == 0 && inputKeyRepeat(in, VK_UP)) {
        app->paramOnMenu = 1;
        audioPlay(&app->audio, SFX_UI, 0.0f, 0.0f, 0);
        return;
    }
    /* Home 单独一支，不和上面合并：↑ 是"接着光标往上走一格到顶"，
       Home 是"不管从哪儿来，回到最顶上" —— 它顺手把列表里的记分牌也
       归零。不归零的话，从最后一行按 Home、再按 ↓ 会落回最后一行，
       那就不是"回到顶上"了。*/
    if (inputKeyPressed(in, VK_HOME)) {
        app->paramOnMenu = 1;
        app->paramSel = 0;
        app->paramTop = 0;
        app->paramTopSkip = 0;
        audioPlay(&app->audio, SFX_UI, 0.0f, 0.0f, 0);
        return;
    }

    /* ---- 选行 ----
       ↑↓ 走 inputKeyRepeat 而不是 inputKeyPressed：按住不放要能连续走。
       Home 在上面那条里被菜单栏接走了，所以这里不再有它的分支。

       ★ **PgUp / PgDn 已删掉**。它当年解决的那个真 bug（"PgUp / PgDn 不能
       翻页"——appParamScrollIntoView 写好了却从没被调用过）早已由"所有改
       paramSel 的路径都过滚动同步"这一条根治，翻页本身不再是必需的；
       而"整页跳"的步长是**行数**，撞上"组标题条跟着行滚"之后，跳完那一下
       常常正好停在块的腰上，格式看着就是乱的 —— "打乱格式"是这个。
       列表里现在只剩 ↑↓ 一行一行走（外加 Home / End 跳到两头）。*/
    if (inputKeyRepeat(in, VK_UP))   --app->paramSel;
    if (inputKeyRepeat(in, VK_DOWN)) ++app->paramSel;
    if (inputKeyPressed(in, VK_END))   { app->paramSel = total - 1; }
    /* 所有能改 paramSel 的路径最后都要过这一关：选中行必须在可见窗口内。
       放在这一处而不是每个分支各写一遍，是为了"以后再加一种移动方式时
       不会忘记同步窗口"—— 曾经吃过一次亏（漏了同步）。

       ★ 它**不再收"可见行数"这个参数**：窗口大小现在按当时的
       paramTop 现算（组标题跟着行滚，行高会变），由渲染层同一个函数给出。
       End 也不再自己指定 paramTop —— 交给它"滚到刚好看得见最后一行"，
       末段位置就只有一个来源了。*/
    appParamScrollIntoView(app);

    descIdx = appParamRowToDesc(app->paramPage, app->paramSel);

    /* ---- 第 0 行：拉出预设方案弹窗 ---- */
    if (descIdx < 0) {
        /* 这一行没有"数值"可调，←/→ 在它上面什么都不做。清空弹窗状态是为了
           "打开过一次、ESC 收起、再按回车"时下标不会停在上一次的位置。*/
        if (inputKeyPressed(in, VK_RETURN)) {
            int m = appParamMatchedPreset(app);
            app->paramPopSel = (m >= 0) ? m : (g_paramMenuCount - 1);  /* 都不像 → 「自定义参数」 */
            app->paramPopOpen = 1;
            audioPlay(&app->audio, SFX_UI, 0.0f, 0.0f, 0);
        }
        /* ★ 第 0 行**只在预设页**存在（偏好页的 descIdx<0 走不到这里，
           因为偏好页每一行都有对应参数），所以这一行上的 R 也不必再管 ——
           预设方案菜单里不再有 R 提示。*/
        return;
    }

    /* ---- 调整选中项 ---- */
    {
        const ParamDesc *d = &g_paramDescs[descIdx];
        Params before = app->params;
        float delta = 0.0f;

        /* 只有左右键能调（A/D 别名已摘掉 —— 面板里既然统一用方向键，
           留着 A/D 就不叫统一）。
           步长从表里取 —— 整数项步长是 1 或 5，浮点项是小步长，
           这样"按一下"的手感与参数本身量级相称。
           同样接长按重复：一个 0..2000 的滑杆要按上千次才能到头，太笨。

           ★ **没有滑动条的项（开关与枚举）两头绕圈**（一直按向左和向右会在
           几个枚举中循环）。绕圈在 paramDescAddWrap
           里做，判据是"类"而不是"哪一项" —— 界面画不画那根细条，用的也是
           同一个判据（isNum = PK_FLOAT | PK_INT），两处不会走散。*/
        if (inputKeyRepeat(in, VK_RIGHT)) delta = +d->step;
        if (inputKeyRepeat(in, VK_LEFT))  delta = -d->step;

        /* Shift 按住时用大步长（10 倍），整数项至少跨 5。
           注意这里有意的例外：**"存活时限"那种带 0 边界的项**大步长也
           跨不过 0（paramDescSet 会夹回 lo），所以 0 = "不限"这个状态永远
           按得到，不会因为步子太大被跳过去。*/
        if (delta != 0.0f && (GetKeyState(VK_SHIFT) & 0x8000)) {
            delta *= 10.0f;
            if (d->kind != PK_FLOAT && fabsf(delta) < 5.0f) delta = (delta < 0 ? -5.0f : 5.0f);
        }
        if (delta != 0.0f) {
            /* 三层口子，从最特殊到最一般：
               ① 开关与枚举走**绕圈** —— 到两头绕回另一头。
                  它先问，是因为"到头就夹住"和"到头绕回去"是两种相反的手感，
                  只有开关与枚举要后者。
               ② 其余整数项走整数加法（见 config.h 里 paramDescAddInt 的说明）：
                  直接写 paramDescGet(...) + delta 的话，「随机种子」在 2^24 以上
                  那一段按一下是没反应的 —— float 只有 24 位有效位，+1 会被舍掉。
               ③ 浮点项（也就是滑杆）走浮点加法 —— 它**不绕圈**，到头就停。*/
            if (!paramDescAddWrap(&app->params, d, (int)delta) &&
                !paramDescAddInt(&app->params, d, (int)delta))
                paramDescSet(&app->params, d, paramDescGet(&app->params, d) + delta);
            appParamAfterChange(app, descIdx, &before);
            audioPlay(&app->audio, SFX_UI, 0.0f, 0.0f, 0);
        }
    }

    /* ---- R：恢复用户偏好设置 ----
       只动「用户偏好设置」那一段（玩家 / 画面 / 声音，12 项），
       32 项玩法参数一项都不碰。

       ★ **生效范围收紧了**：从"光标停在哪一行都管用"改成"当前页是
       偏好页才管用"。预设方案菜单里不再有 R 提示 —— 提示只留一份，
       那按键本身也只该在一处生效，否则就是"预设页上按了 R，界面一声不吭
       却把偏好改了"。判据与页的划法共用 paramIsPreferencePage 一处定义。*/
    if (inputKeyPressed(in, KEY_RESTART) && paramIsPreferencePage(app))
        appParamRestorePreferences(app);
}

/* 面板里的按键。这里**只认面板自己的键**：WASD 一个都不认，
   R 是"恢复用户偏好设置"。面板开着时玩法输入一概不吃，
   这一点在 appHandleInput 里已经用提前 return 保证了。

   ★ **面板不认 ESC**：ESC 只用于暂停游戏和继续游戏，不用于关闭设置页面
   和关闭历史记录页面。面板只有一个开关 —— Tab，从哪一页进来就从哪一页
   出去，不存在"退一层"这个概念了，所以也没必要再留一条"没有浮层时 ESC
   管关面板"的老路。
   唯一的例外是**预设弹窗**：那是压在面板上的浮层，弹出时按 ESC 把它
   收起来 —— 见 appParamInputPopup。*/
void appParamInput(App *app) {
    Input *in;

    if (!app || !app->paramOpen) return;
    in = &app->input;

    /* ---- 底部提示的倒计时 ---- */
    if (app->paramMsgT > 0.0f) {
        app->paramMsgT -= 1.0f / 60.0f;     /* 一帧一次，按 60 帧估 */
        if (app->paramMsgT <= 0.0f) { app->paramMsgT = 0.0f; app->paramMsg[0] = L'\0'; }
    }

    /* ★ 这里原先有一段"没有浮层时 ESC = 关面板"，已删。
       面板的开合只归 Tab（见 appHandleInput）。ESC 唯一还管的浮层是
       预设弹窗，那一条在 appParamInputPopup 里，**在下面的分派里**，
       所以这里不能提前把 ESC 拦掉 —— 拦掉就等于连弹窗的"收起"一起废了
       （早先这么写过一次，tPanelMenu 里那几条弹窗断言当场变红）。

       ESC 剩下两条去路，都是"什么都不做"：弹窗没开时它掉进下面的列表/
       菜单栏分支，而那两边一个字都没读 ESC；面板开着时 appHandleInput
       早就 return 了，也走不到末尾那条"ESC = 暂停/继续"。*/

    /* ★ 分派：弹窗压在最上面 → 它先吃键；其次是菜单栏（光标在栏上时
       列表按键一概不认，否则 ←→ 会一边切分类一边把参数值也调了）。*/
    if (app->paramPopOpen)      appParamInputPopup(app);
    else if (app->paramOnMenu)  appParamInputMenuBar(app);
    else                        appParamInputMenu(app);
}

/* ============================================================ 离屏回读 */

unsigned char *appGrabPixels(App *app) {
    int w, h, y;
    unsigned char *buf, *flip;
    if (!app || !app->glReady) return NULL;
    w = app->winW; h = app->winH;
    if (w <= 0 || h <= 0) return NULL;

    buf  = (unsigned char *)malloc((size_t)w * h * 3);
    flip = (unsigned char *)malloc((size_t)w * h * 3);
    if (!buf || !flip) { free(buf); free(flip); return NULL; }

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glFinish();
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, buf);

    /* GL 的原点在左下角，PNG 的在左上角，翻一下。*/
    for (y = 0; y < h; ++y)
        memcpy(flip + (size_t)y * w * 3,
               buf + (size_t)(h - 1 - y) * w * 3,
               (size_t)w * 3);

    free(buf);
    return flip;
}

/* ============================================================ 主循环 */

void appRun(App *app) {
    MSG msg;
    double last, acc = 0.0;
    if (!app) return;

    last = nowSeconds();
    while (app->running) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { app->running = 0; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!app->running) break;
        if (!IsWindowVisible(app->hwnd)) { sleepMs(8); last = nowSeconds(); continue; }

        {
            double now = nowSeconds();
            double fdt = now - last;
            last = now;
            /* 拖动窗口、切出去再回来会产生一个很大的 dt。夹住它，
               否则一帧里会补上百步固定步长，看起来像"瞬移"。*/
            if (fdt > 0.20) fdt = 0.20;
            if (fdt < 0.0)  fdt = 0.0;
            acc += fdt;

            /* 长按计时要按真实时间推进，所以用渲染帧的 dt 而不是固定步长 ——
               否则卡帧的时候"按住多久"会被算成固定步长数，与手感对不上。*/
            inputTick(&app->input, (float)fdt);

            /* 输入每**渲染帧**处理一次，而不是每个固定步长。
               鼠标位移是按消息累积的，若按固定步长处理，一帧跑两步时
               同一个位移会被算两遍，灵敏度直接翻倍。*/
            appHandleInput(app);

            /* 声音状态每帧灌一次。它是"随时可调"的参数，面板上拨一下
               就应该立刻听到变化，不值得为它单开一条通知链路。
               注意 appParamAfterChange 里**也**灌一次，不是为了双保险：
               拨到"开"的那一下，界面音是在同一次输入处理里放出去的，
               而这里是输入处理**之后**才跑 —— 光靠这一处，那一声会被
               尚未解除的静音吃掉，听到的就是"拨到开却没响"。*/
            audioSetVolume(&app->audio, VOLUME_DEF);
            audioSetMuted(&app->audio, !app->params.soundOn);

            while (acc >= FIXED_DT) {
                appStep(app, (float)FIXED_DT);
                acc -= FIXED_DT;
            }
            /* 上面可能一步都没跑（帧极短），那样累积的位移会留到下一帧，
               变成"延迟一帧"，所以无论跑没跑都清掉。*/
            inputEndFrame(&app->input);

            renderFrame(app);
            renderHud(app);
            SwapBuffers(app->dc);
            ++app->frame;

            if (fdt > 1e-6) {
                double fps = 1.0 / fdt;
                app->fpsAvg = (app->fpsAvg <= 0.0) ? fps : app->fpsAvg * 0.92 + fps * 0.08;
            }
        }
    }
}

/* ============================================================ 清理 */

void appShutdown(App *app) {
    if (!app) return;

    /* ★ 面板还开着就退出（直接点窗口右上角的叉），
       最后一次改动会跟着一起丢 —— 补一次。走的是与 appParamClose
       同一条路径（同一个判据、同一个函数），不是另写一段。
       放在最前面：后面几步会拆窗口、拆 GL，越往后越不该再碰状态。*/
    if (app->paramOpen) appParamClose(app);

    /* 音频先关：混音线程还在跑的时候拆窗口，线程有可能在窗口没了之后
       再往设备里写 —— 先让它停下来，后面拆什么都安全。*/
    audioClose(&app->audio);

    inputShutdown(&app->input);         /* 先把光标还给系统，再拆窗口 */
    sceneDestroy(&app->scene);
    if (app->glReady) {
        hudDestroy(&app->hud);
        if (app->blitTex) { glDeleteTextures(1, &app->blitTex); app->blitTex = 0; }
        wglMakeCurrent(NULL, NULL);
    }
    if (app->rc) { wglDeleteContext(app->rc); app->rc = NULL; }
    if (app->dc && app->hwnd) { ReleaseDC(app->hwnd, app->dc); app->dc = NULL; }
    if (app->hwnd) { DestroyWindow(app->hwnd); app->hwnd = NULL; }
}
