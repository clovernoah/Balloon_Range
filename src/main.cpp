/* ============================================================================
 * main.cpp —— 入口与命令行
 *
 * 两条运行形态共用同一个 exe：
 *
 *   1) 游戏形态（不带参数，或带 --show）：开窗口、进主循环、玩。
 *      双击 exe 走的就是这条。
 *
 *   2) 工具形态（--selftest / --shot / --help）：跑完就退，不需要人看着。
 *      自检与出图都是这条，是整个验证体系能被自动化驱动的入口。
 *
 * 为什么做成"一个 exe 两种形态"而不是两个程序：出图的代码路径必须与
 * 真正玩的时候**完全同一条**。分成两个 exe 就必然出现"截图里好好的、
 * 跑起来不对"这种最难查的问题。
 *
 * 子系统选的是 CONSOLE（这样 Git Bash / cmd 里的管道能直接抓到输出）；
 * 进游戏形态时如果发现控制台是这个进程独占的，就把它藏起来，
 * 免得双击运行时旁边挂着一个黑框。
 * ==========================================================================*/
#include "app.h"
#include "render.h"
#include "png.h"
#include "selftest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <time.h>       /* _time64 —— --hist-fill 要按"现在"往前推时间戳 */

/* 出图模式的默认种子：定死的，保证同一份命令永远出同一张图。*/
#define SHOT_DEFAULT_SEED 20260930u

/* ============================================================ 命令行工具 */

/* 找 --name=value 或 --name value。找不到返回 NULL。*/
static const wchar_t *optValue(int argc, wchar_t **argv, const wchar_t *name) {
    size_t n = wcslen(name);
    int i;
    for (i = 1; i < argc; ++i) {
        if (wcsncmp(argv[i], name, n) != 0) continue;
        if (argv[i][n] == L'=') return argv[i] + n + 1;
        if (argv[i][n] == L'\0') return (i + 1 < argc) ? argv[i + 1] : L"";
    }
    return NULL;
}

static int optInt(int argc, wchar_t **argv, const wchar_t *name, int def) {
    const wchar_t *v = optValue(argc, argv, name);
    unsigned parsed = 0;
    if (!v || !*v) return def;
    if (swscanf(v, L"%u", &parsed) != 1) return def;
    return (int)parsed;
}

static float optFloat(int argc, wchar_t **argv, const wchar_t *name, float def) {
    const wchar_t *v = optValue(argc, argv, name);
    float parsed = 0.0f;
    if (!v || !*v) return def;
    if (swscanf(v, L"%f", &parsed) != 1) return def;
    return parsed;
}

/* ============================================================ 帮助 */

static void printHelp(void) {
    printUtf8(L"FPS 气球训练场 —— 墙面气球靶场\n");
    printUtf8(L"\n");
    printUtf8(L"用法：BalloonRange.exe [选项]\n");
    printUtf8(L"\n");
    printUtf8(L"【游戏】\n");
    printUtf8(L"  --show                开窗口进游戏（不带任何选项时也是这个）\n");
    printUtf8(L"  --w=N --h=N           窗口客户区尺寸（默认 1600x900）\n");
    printUtf8(L"  --params=<文件>       载入参数存档（参数.dat）\n");
    printUtf8(L"\n");
    printUtf8(L"【离屏出图】—— 不开窗口也能跑，用来产生可复现的证据\n");
    printUtf8(L"  --shot=<png>          渲染一帧写成 PNG 之后退出\n");
    printUtf8(L"  --frames=N            出图之前先推进 N 个固定步长（默认 0）\n");
    printUtf8(L"  --pose=x,y,z,yaw,pitch  指定站位与朝向（米 / 度）\n");
    printUtf8(L"  --fire=N              前 N 步里每步自动瞄准一个球开一枪（默认 0）\n");
    printUtf8(L"                        出图专用：让截图里真的走过一遍拾取与计分\n");
    printUtf8(L"\n");
    printUtf8(L"【本局参数】\n");
    printUtf8(L"  --seed=S              随机种子；出图模式默认 20260930（可复现）\n");
    printUtf8(L"  --preset=N            套用第 N 套预设方案（0 起，默认 0 固定靶）\n");
    printUtf8(L"                        0 固定靶 / 1 移动靶 / 2 小快靶 / 3 跟踪训练\n");
    printUtf8(L"                        4 计时挑战 / 5 渐进训练 / 6 混合靶场 / 7 精准挑战\n");
    printUtf8(L"                        预设定玩法（气球、运动、节奏、判定、训练），\n");
    printUtf8(L"                        不碰鼠标灵敏度/画面/音量这些个人口味\n");
    printUtf8(L"  --n=N                 墙上恒定气球数（默认 8）\n");
    printUtf8(L"  --spawndist=lo,hi     「邻位生成」的圆心距范围，单位 = 两球半径和\n");
    printUtf8(L"                        （1.0 = 相切，2.0 = 等径时的 4R；区间 1.0..6.0，\n");
    printUtf8(L"                        默认 1.0,1.0。只在这一项 = 邻位生成时起作用）\n");
    printUtf8(L"  --wall=N              0 抹灰（默认） / 1 砖 / 2 瓷砖 / 3 混凝土\n");
    printUtf8(L"  --fov=D               垂直视野角，45..110（默认 75）\n");
    printUtf8(L"  --lifetime=S          气球存活时限秒数；0 = 不限（默认 0）\n");
    printUtf8(L"  --move=N              0 静止（默认） / 1 漂移 / 2 正弦 / 3 跳跃\n");
    printUtf8(L"  --hitforgive=F        判定半径倍数，0.5..2.0（默认 1.06）\n");
    printUtf8(L"\n");
    printUtf8(L"【出图专用】\n");
    printUtf8(L"  --panel               出图前把参数面板打开\n");
    printUtf8(L"  --panelsel=N          光标停在第 N **行**（0 起，默认 0；按行数夹取）。\n");
    printUtf8(L"                        行号是**页内行号**：预设页第 0 行是「当前预设方案」，\n");
    printUtf8(L"                        第 r 行 = 第 r-1 项参数；偏好页第 0 行就是第 1 项参数\n");
    printUtf8(L"                        ★ 写了这个开关，光标就**离开菜单栏、进到列表里**\n");
    printUtf8(L"                        （不写才是默认的那一态：光标停在菜单栏上）\n");
    printUtf8(L"  --panel-page=0|1      面板停在预设方案设置页（0，默认）/ 用户偏好设置页（1）\n");
    printUtf8(L"  --panel-menu          光标停在**菜单栏**上（与不写 --panelsel 等价；\n");
    printUtf8(L"                        两个都给时以本开关为准）\n");
    printUtf8(L"  --panel-keys=13,40    打开面板后依次注入这几个虚拟键码，每个键走\n");
    printUtf8(L"                        一次真实的 appParamInput（36=Home 35=End 38=↑ 40=↓\n");
    printUtf8(L"                        37=← 39=→ 13=回车 82=R 27=ESC）\n");
    printUtf8(L"                        用来拍「按下去之后才出现」的界面 —— 比如预设弹窗\n");
    printUtf8(L"                        要用 --panel-keys=13 真的按一下回车才拉得出来\n");
    printUtf8(L"  --dump-sfx=<目录>     把合成的音效导出成 wav（用于试听，不开声卡）\n");
    printUtf8(L"  --screen=N            出图前切到某一屏：0 游戏中（默认）/ 1 暂停 / 2 结算 / 3 历史记录\n");
    printUtf8(L"  --hist-fill=N         出图前先垫 N 条历史记录（只给出图用，不落盘）\n");
    printUtf8(L"  --no-audio            强制按设备被别的程序独占处理（验没声卡时的界面分支）\n");
    printUtf8(L"  --verify-frame        出图后再复核一次字形图集（排障用）\n");
    printUtf8(L"  --atlas=<png>         导出字形图集并自校验（排障用）\n");
    printUtf8(L"  --atlas-late=<png>    同上，但放在场景建完之后（对照看图集有没有被动过）\n");
    printUtf8(L"\n");
    printUtf8(L"【自检】—— 不开窗口、不碰 GL，纯逻辑断言\n");
    printUtf8(L"  --selftest            跑全部断言；全部通过返回 0，有失败返回 1\n");
    printUtf8(L"  --report=<文件>       自检明细报告写到哪里（默认 bin\\selftest_report.txt）\n");
    printUtf8(L"\n");
    printUtf8(L"  --help                显示这份说明\n");
    printUtf8(L"\n");
    printUtf8(L"示例：\n");
    printUtf8(L"  BalloonRange.exe --selftest\n");
    printUtf8(L"  BalloonRange.exe --shot=shots\\a.png --preset=1 --seed=7\n");
    printUtf8(L"  BalloonRange.exe --show --preset=7\n");
    printUtf8(L"  BalloonRange.exe --shot=panel.png --panel --panelsel=3\n");
    printUtf8(L"  BalloonRange.exe --shot=popup.png --panel --panel-keys=13\n");
    printUtf8(L"  BalloonRange.exe --shot=bar.png --panel --panel-menu\n");
}

/* ============================================================ 选项校验
 * 认识的选项清单。**加新选项时这里和 --help 都要改**（两个清单分头维护是
 * 有意的：各自写错的地方不一样，遗漏时更容易被发现）。
 *
 * 为什么需要它：选项解析对不存在的名字只会返回 NULL，于是 `--headless`
 * 这种拼错的/根本没有的开关会被**静默忽略** —— 程序照样跑完，命令行上
 * 一句提示都没有。早先真这么骗过自己：一直以为在跑"无头模式"，
 * 其实那个开关压根不存在，它只是被无视了。凡是会让人"以为某件事发生了"
 * 的静默行为，都得报出来。
 */
static const wchar_t *const kKnownOptions[] = {
    L"--help", L"-h",
    L"--show", L"--w", L"--h", L"--params",
    L"--shot", L"--frames", L"--pose", L"--fire", L"--seed",
    L"--n", L"--preset", L"--wall", L"--fov", L"--lifetime",
    L"--move", L"--hitforgive", L"--spawndist",
    L"--panel", L"--panelsel", L"--panel-keys", L"--panel-page", L"--panel-menu",
    L"--screen", L"--hist-fill", L"--no-audio",
    L"--dump-sfx", L"--verify-frame", L"--atlas", L"--atlas-late",
    L"--selftest", L"--report"
};

static int isKnownOption(const wchar_t *name) {
    int i;
    for (i = 0; i < (int)ARRAY_COUNT(kKnownOptions); ++i)
        if (wcscmp(name, kKnownOptions[i]) == 0) return 1;
    return 0;
}

/* 逐个检查 argv 里以 - 开头的项（取 '=' 之前的部分）。不认识的报一行。*/
static void warnUnknownOptions(int argc, wchar_t **argv) {
    int i;
    for (i = 1; i < argc; ++i) {
        wchar_t name[64];
        const wchar_t *a = argv[i];
        size_t k = 0;
        if (a[0] != L'-') continue;
        while (a[k] && a[k] != L'=' && k < ARRAY_COUNT(name) - 1) {
            name[k] = a[k];
            ++k;
        }
        name[k] = L'\0';
        if (!isKnownOption(name))
            printUtf8f(L"[警告] 不认识的选项 %ls —— 它会被忽略，什么也不会发生\n", name);
    }
}

/* ============================================================ 出图 */

static int doShot(App *app, const wchar_t *path, int frames, int fireCount, int screen) {
    unsigned char *px;
    int i, fired = 0, rc;

    if (frames < 0) frames = 0;
    if (fireCount < 0) fireCount = 0;

    /* 自动开火，为的是让截图里出现"正在炸开"的那一瞬。
       两条讲究：
         · **打中才算数**，所以出图时开火次数就等于击破次数，不会拍了张
           空枪的图还以为拍到了击破；
         · 只在**最后 FIRESHOT_WINDOW 步**里尝试。开局头 0.18 秒气球还在
           "长出来"的动画中，按设计不可被命中；而且炸开特效只有 0.4 秒，
           太早打中，等渲染出来早就散没了。*/
    {
        const int FIRESHOT_WINDOW = 30;      /* 0.25 秒 */
        int start = frames - FIRESHOT_WINDOW;
        if (start < 0) start = 0;

        for (i = 0; i < frames; ++i) {
            if (fired < fireCount && i >= start &&
                appAutoAim(app, fired * 5 + 1) >= 0) {
                if (appFire(app)) ++fired;
            }
            appStep(app, (float)FIXED_DT);
        }
    }
    if (fireCount > 0)
        printUtf8f(L"[出图] 自动开火：击破 %d 个（目标 %d），墙上仍有 %d 个\n",
                   app->popped, fireCount, balloonActiveCount(&app->pool));

    /* 摆屏这一步必须放在**模拟之后**：暂停屏与结算屏要的是"打到一半"
       的那一帧，先摆屏再模拟的话，覆盖层会被后面继续推进的玩法盖过去
       （暂停屏上会显示一串不停变化的数字）。*/
    if (screen == SCREEN_SETTLE || screen == SCREEN_HISTORY) {
        appEndSession(app);              /* 它会顺手把成绩单填好、写历史记录 */
        if (screen == SCREEN_HISTORY) {
            /* 历史记录屏是"从结算屏按 F 进去"的，所以必须真的走这条路径：
               先结算（上面那一步已经把这一局记进去了），再从结算屏打开。
               直接 appSetScreen(SCREEN_HISTORY) 的话，historyOpen 里那条
               "只能在结算屏打开"的守卫会被绕过 —— 出图拍到的就不是
               真实操作能走到的路径了。*/
            appSetScreen(app, SCREEN_SETTLE);
            historyOpen(app);
        }
    } else if (screen != SCREEN_PLAY) {
        appSetScreen(app, screen);
    }

    renderFrame(app);
    renderHud(app);
    glFinish();

    px = appGrabPixels(app);
    if (!px) {
        printUtf8(L"[错误] 回读画面失败（glReadPixels 没拿到数据）\n");
        return -1;
    }
    rc = pngWriteRGB(path, app->winW, app->winH, px);
    free(px);

    if (rc != 0) {
        printUtf8f(L"[错误] 写 PNG 失败：%ls\n", path);
        return -1;
    }
    printUtf8f(L"[出图] %ls  %dx%d  第 %d 帧\n", path, app->winW, app->winH, frames);
    /* 顺带把跑完之后的状态报出来：截图里那些数字（生命、漏球）本来就是
       给人看的，命令行上也报一份，省得为了读一个数去数像素。
       屏幕上停在哪一屏也算状态 —— 出图拍的是覆盖层，不看这个数会以为
       "结算屏怎么没出来"。*/
    printUtf8f(L"[出图] 状态：得分 %d，漏球 %d，生命 %d，墙上 %d，连击 %d，用时 %.1fs，屏幕 %d%ls\n",
               app->score, app->missed, app->lives,
               balloonActiveCount(&app->pool), app->combo, (double)app->elapsed,
               app->screen,
               (app->screen == SCREEN_PLAY)    ? L"（游戏中）" :
               (app->screen == SCREEN_PAUSE)   ? L"（暂停）" :
               (app->screen == SCREEN_HISTORY) ? L"（历史记录）" : L"（结算）");
    return 0;
}

/* ============================================================ 图集出图
 *
 * 把字形图集本身抠出来存成 PNG。HUD 上出现"错字/空心方块"这类问题时，
 * 盯着渲染结果猜是没用的 —— 直接看格子里装的是什么，一眼就定位：
 * 是某个字没进图集，还是 UV 取错了格子。
 * 只做诊断用，正常玩的时候不碰。
 */
/* 图集诊断的两个入口共用这一段：复核 + 导出 PNG。
   分成"建完图集立刻查"和"场景也建完之后再查"两处调用，是因为这两处的
   结果**可能不一样** —— 图集建好之后还有一堆纹理要创建，万一哪个 GL
   调用串了号，症状就是"HUD 上全是错字"而图集本身看着好好的。*/
static int doAtlasDump(App *app, const wchar_t *path);

static int runAtlasCheck(App *app, const wchar_t *pngPath) {
    int bad = hudVerifyAtlas(&app->hud);
    int rc  = doAtlasDump(app, pngPath) == 0 ? 0 : 3;
    if (bad > 0) rc = 4;
    return rc;
}

static int doAtlasDump(App *app, const wchar_t *path) {
    int w, h, i, n, rc;
    unsigned char *rgba, *rgb;

    if (!app->hud.ok) {
        printUtf8(L"[错误] 字形图集没建起来，没有可导出的内容\n");
        return -1;
    }
    w = app->hud.cols * app->hud.cell;
    h = app->hud.rows * app->hud.cell;
    n = w * h;

    rgba = (unsigned char *)malloc((size_t)n * 4);
    rgb  = (unsigned char *)malloc((size_t)n * 3);
    if (!rgba || !rgb) { free(rgba); free(rgb); return -1; }

    glBindTexture(GL_TEXTURE_2D, app->hud.tex);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);

    /* 字是白的、背景透明，直接存 PNG 会是一片白。翻成"白底黑字"才看得清。*/
    for (i = 0; i < n; ++i) {
        unsigned char a = rgba[i * 4 + 3];
        unsigned char v = (unsigned char)(255 - a);
        rgb[i * 3 + 0] = v;
        rgb[i * 3 + 1] = v;
        rgb[i * 3 + 2] = v;
    }

    rc = pngWriteRGB(path, w, h, rgb);
    free(rgba);
    free(rgb);
    if (rc != 0) {
        printUtf8f(L"[错误] 写图集 PNG 失败：%ls\n", path);
        return -1;
    }
    printUtf8f(L"[图集] %ls  %dx%d（%d 格 x %d 格，每格 %d 像素）\n",
               path, w, h, app->hud.cols, app->hud.rows, app->hud.cell);
    return 0;
}

/* ============================================================ 应用参数
 * 把命令行里给的几项覆盖到参数表上。只是"改数据"，不碰玩法逻辑。
 */
static void applyCommandLine(App *app, int argc, wchar_t **argv) {
    const wchar_t *v;

    /* 先预设、后逐项：预设会写掉一大片玩法参数（包括气球数、运动方式、
       存活时限），反过来的话 `--n=1 --preset=1` 里的 1 会被预设顶掉 ——
       命令行上明确写出来的东西应当压过预设。*/
    v = optValue(argc, argv, L"--preset");
    if (v)
        paramsApplyPreset(&app->params, optInt(argc, argv, L"--preset", app->params.preset));

    v = optValue(argc, argv, L"--n");
    if (v) app->params.balloonCount = optInt(argc, argv, L"--n", app->params.balloonCount);

    v = optValue(argc, argv, L"--wall");
    if (v) app->params.wallStyle = optInt(argc, argv, L"--wall", app->params.wallStyle);

    v = optValue(argc, argv, L"--move");
    if (v) app->params.motion = optInt(argc, argv, L"--move", app->params.motion);

    v = optValue(argc, argv, L"--fov");
    if (v) app->params.fovDeg = optFloat(argc, argv, L"--fov", app->params.fovDeg);

    v = optValue(argc, argv, L"--lifetime");
    if (v) app->params.lifetimeSec = optFloat(argc, argv, L"--lifetime", app->params.lifetimeSec);

    v = optValue(argc, argv, L"--hitforgive");
    if (v) app->params.hitForgive = optFloat(argc, argv, L"--hitforgive", app->params.hitForgive);

    /* 「邻位生成」的圆心距范围，写成 "下限,上限"（单位 = 两球半径和）。
       跟踪训练以外的预设不看这两项，这个开关存在的理由是出图要拍"环带拉宽 /
       下限 = 上限"这类确定性的样子。两个数都要给，只给一个就整条忽略。*/
    v = optValue(argc, argv, L"--spawndist");
    if (v) {
        float lo = app->params.spawnDistMin, hi = app->params.spawnDistMax;
        if (swscanf(v, L"%f,%f", &lo, &hi) == 2) {
            app->params.spawnDistMin = lo;
            app->params.spawnDistMax = hi;
        } else {
            printUtf8(L"[警告] --spawndist 要写成「下限,上限」，本项按原值处理\n");
        }
    }

    paramsClamp(&app->params);
}

static int applyPose(App *app, const wchar_t *s) {
    float x, y, z, yaw, pitch;
    if (swscanf(s, L"%f,%f,%f,%f,%f", &x, &y, &z, &yaw, &pitch) != 5) return -1;
    app->player.pos = v3(x, y, z);
    app->player.yaw = yaw * DEG2RAD_F;
    app->player.pitch = clampf(pitch * DEG2RAD_F, -1.5533f, 1.5533f);
    playerClampToRoom(&app->player);
    return 0;
}

/* ============================================================ 自检形态 */

static int runSelftest(int argc, wchar_t **argv) {
    const wchar_t *rep = optValue(argc, argv, L"--report");
    wchar_t defRep[MAX_PATH];
    int fails;

    if (!rep) {
        GetModuleFileNameW(NULL, defRep, MAX_PATH);
        /* 去掉 exe 文件名，换成同目录下的报告文件 —— 双击运行时也能找到。*/
        {
            wchar_t *slash = wcsrchr(defRep, L'\\');
            if (slash) slash[1] = L'\0'; else defRep[0] = L'\0';
        }
        wcscat(defRep, L"selftest_report.txt");
        rep = defRep;
    }

    fails = selftestRun(rep);
    if (fails == 0) {
        printUtf8(L"[自检] 全部通过。\n");
        printUtf8f(L"[自检] 明细报告：%ls\n", rep);
        return 0;
    }
    printUtf8f(L"[自检] 有 %d 项失败，详见：%ls\n", fails, rep);
    return 1;
}

/* ============================================================ 入口 */

int wmain(int argc, wchar_t **argv) {
    App app;
    int wantShow, wantShot, wantSelf, rc = 0;
    int width, height, frames;
    unsigned seed;
    const wchar_t *shotPath, *paramsPath;

    consoleUseUtf8();

    warnUnknownOptions(argc, argv);

    if (argc > 1 && (optValue(argc, argv, L"--help") || optValue(argc, argv, L"-h"))) {
        printHelp();
        return 0;
    }

    wantSelf = optValue(argc, argv, L"--selftest") != NULL;
    if (wantSelf) {
        /* 自检不建窗口、不碰 GL，所以在最前面就分流出去了。*/
        return runSelftest(argc, argv);
    }

    shotPath   = optValue(argc, argv, L"--shot");
    wantShot   = (shotPath != NULL);
    wantShow   = (optValue(argc, argv, L"--show") != NULL) || !wantShot;
    paramsPath = optValue(argc, argv, L"--params");

    width  = optInt(argc, argv, L"--w", WINDOW_W_DEF);
    height = optInt(argc, argv, L"--h", WINDOW_H_DEF);
    if (width  < 320) width  = 320;
    if (height < 240) height = 240;
    if (width  > 7680) width  = 7680;
    if (height > 4320) height = 4320;

    frames = optInt(argc, argv, L"--frames", 0);
    if (frames < 0) frames = 0;
    if (frames > 60 * 600) frames = 60 * 600;   /* 上限 5 分钟（120Hz 步长），防手滑 */

    seed = wantShot ? SHOT_DEFAULT_SEED : 0u;
    {
        const wchar_t *sv = optValue(argc, argv, L"--seed");
        if (sv) {
            unsigned parsed = 0;
            if (swscanf(sv, L"%u", &parsed) == 1) seed = parsed;
        }
    }

    memset(&app, 0, sizeof(app));
    paramsDefault(&app.params);
    if (paramsPath) {
        if (paramsLoad(&app.params, paramsPath) == 0)
            printUtf8f(L"[参数] 已载入 %ls\n", paramsPath);
        else
            printUtf8f(L"[参数] 载入失败，改用默认值：%ls\n", paramsPath);
    }
    applyCommandLine(&app, argc, argv);

    /* 命令行给的 --seed 写回参数结构体，此后"这一局的种子"只有 params.seed
       这一个真源：参数面板改的是它，存档存的是它，重开一局读的也是它。
       以前命令行种子是单独一个局部变量，面板上那个"随机种子"就成了没人读的
       摆设 —— 拧了完全没用，还看不出来。*/
    if (seed) app.params.seed = (int)seed;

    /* 参数存档路径也定在这里：命令行的 --params 优先，没给就用 exe 同目录。
       面板里的 S / L 读写的就是这一条路径 —— 与命令行落到同一处。*/
    if (paramsPath) {
        _snwprintf(app.paramsPath, ARRAY_COUNT(app.paramsPath) - 1, L"%ls", paramsPath);
    } else {
        paramsDefaultPath(app.paramsPath, (int)ARRAY_COUNT(app.paramsPath));
    }
    app.paramsPath[ARRAY_COUNT(app.paramsPath) - 1] = L'\0';

    /* ---- 建窗口与 GL 上下文。出图模式下窗口不显示，但仍然要建：
           需要一个真实的 drawable 才能有后备缓冲。 ---- */
    if (appCreateWindow(&app, width, height, wantShow ? 1 : 0) != 0) {
        printUtf8(L"[错误] 创建窗口失败\n");
        return 2;
    }
    /* 输入要在窗口有了句柄之后立刻接上：Raw Input 是注册到具体窗口的。*/
    inputInit(&app.input, app.hwnd);
    if (appInitGL(&app) != 0) {
        printUtf8(L"[错误] 创建 OpenGL 上下文失败（显卡驱动不支持？）\n");
        appShutdown(&app);
        return 2;
    }
    {
        const char *ver = (const char *)glGetString(GL_VERSION);
        printUtf8f(L"[GL] %hs\n", ver ? ver : "(未知)");
    }

    /* 中文字形图集要在 GL 就绪之后、第一次画 HUD 之前建好。*/
    appBuildHud(&app);

    /* 受控实验出口：--no-audio 让音频设备"打不开"。这里就真的调用一次
       audioOpen（受开关影响必然失败），而不是只设个标志 —— 因为"设备不可用"
       那行文案是照着 audio.tried && !audio.ready 出的，不真走一遍就出不来，
       出图形态也就拍不到这一屏。放在所有模式分支之前，出图/自检同样吃得到。*/
    if (optValue(argc, argv, L"--no-audio")) {
        wchar_t amsg[128];
        audioForceFail(MMSYSERR_ALLOCATED);
        if (audioOpen(&app.audio, app.hwnd, amsg, (int)ARRAY_COUNT(amsg)) == 0)
            printUtf8f(L"[音频] %ls\n", amsg);
        else
            printUtf8f(L"[音频] %ls —— 静音运行，不影响玩法\n", amsg);
    }

    /* 诊断出口：把图集导出来看看格子里到底装的什么字，顺带做一次自校验
       （贴图里每一格 vs 单独重画的那个字，逐像素比）。*/
    if (optValue(argc, argv, L"--dump-sfx")) {
        const wchar_t *dir = optValue(argc, argv, L"--dump-sfx");
        int dumped = 0;
        if (audioDumpClips(dir, &dumped) == 0)
            printUtf8f(L"[音效] 导出 %d 个 wav 到 %ls\n", dumped, dir);
        else
            printUtf8(L"[错误] 音效导出失败（目录不存在或写不进去）\n");
        appShutdown(&app);
        return (dumped > 0) ? 0 : 4;
    }

    if (optValue(argc, argv, L"--atlas")) {
        rc = runAtlasCheck(&app, optValue(argc, argv, L"--atlas"));
        appShutdown(&app);
        return rc;
    }

    /* 历史最好成绩放在 exe 同目录。读不到（第一次跑）就当作没有记录。
       ★ 出图形态两条存档**都不读**：一是保证同一组参数拍出来的图永远一样
       （读进来的条数取决于这台机器上跑过几局），二是别把本机的成绩
       拍进成品截图里。*/
    scoreBookDefaultPath(app.bookPath, MAX_PATH);
    historyDefaultPath(app.histPath, MAX_PATH);

    if (!wantShot) {
        if (scoreBookLoad(&app.book, app.bookPath))
            printUtf8f(L"[记录] 已载入 %ls\n", app.bookPath);

        /* 最近 20 局的流水。和上面那份最好成绩是两个文件、两件事。*/
        if (historyLoad(&app.history, app.histPath) == 0)
            printUtf8f(L"[历史] 已载入 %d 局（%ls）\n",
                       app.history.count, app.histPath);
    }

    /* 贴图种子也取同一处；为 0（每局随机）时给个固定的 1，保证墙面纹理
       本身可复现 —— 气球的位置才是"随机"的那部分。*/
    sceneBuild(&app.scene, &app.params,
               app.params.seed ? (unsigned)app.params.seed : 1u);
    /* 传 0：让 appResetSession 自己按"参数里的种子 → 时间"的顺序定夺。*/
    appResetSession(&app, 0);

    /* 同样的复核，但放在场景也建完之后。和上面那次结果对照着看：
       两次一样 → 图集从头到尾没被动过；不一样 → 中间有 GL 调用串号了。*/
    if (optValue(argc, argv, L"--atlas-late")) {
        rc = runAtlasCheck(&app, optValue(argc, argv, L"--atlas-late"));
        appShutdown(&app);
        return rc;
    }


    /* 出图专用：把参数面板摆出来。面板平时只能靠 Tab 打开，而 --shot
       模式没有输入循环，所以这里直接调 appParamOpen —— 顺带也就验证了
       "不依赖窗口与输入，面板状态能独立建立"这条设计。

       ★ `--panel-level` 已删掉：面板不再分两级，只剩一层，
       那个开关已经无从选起。`--panelsel` 现在一律是**列表行号**
       （0 = 「当前预设方案」那一行，1..42 = 参数），不再是"某一层里的第几项"。
       要拍预设弹窗展开的样子，用 `--panel-keys=13`（回车）真的按一下 ——
       弹窗是浮层，摆造型摆不出来，只能走真实输入路径。

       ★ 菜单栏又带来两个开关：
         --panel-page=0|1  停在预设页 / 偏好页（默认 0）
         --panel-menu      光标停在**菜单栏**上
       ★ 开面板时光标**默认就停在菜单栏上**，所以
       --panel-menu 从"改默认值"变成"把默认值说一遍"；真正会改变位置的是
       --panelsel —— 指定了行号就说明要拍列表里那一行，于是它把光标从
       菜单栏放回列表（两者同时给时以 --panel-menu 为准）。
       这几个都只摆"打开面板那一刻"的初始状态，之后的按键一律走
       --panel-keys 的真实输入路径 —— 出图必须和真实按键按出来的完全一致。*/
    if (optValue(argc, argv, L"--panel")) {
        const wchar_t *sel = optValue(argc, argv, L"--panelsel");
        int page = 0;
        appParamOpen(&app);
        if (optValue(argc, argv, L"--panel-page"))
            page = optInt(argc, argv, L"--panel-page", 0);
        appParamSetPage(&app, page);
        if (sel) {
            int n = appParamPageRowCount(app.paramPage);
            int k = optInt(argc, argv, L"--panelsel", 0);
            if (k < 0) k = 0;
            if (k >= n) k = n - 1;
            app.paramSel = k;
            app.paramOnMenu = 0;      /* 点了行号 = 光标在列表里 */
        }
        if (optValue(argc, argv, L"--panel-menu")) app.paramOnMenu = 1;
        appParamScrollIntoView(&app);
    }

    /* 出图专用：往面板里**真的按几个键**。
       走的是 appParamInput 这条真实输入路径（先塞进 Input，再调它），不是
       绕过去直接改状态 —— 所以拍出来的画面和真实按键按出来的完全一致。
       "试听"那条底部提示就是非这么拍不可的例子：它只在真的按了之后才存在，
       摆造型摆不出来（要么就得在渲染层开个只为出图存在的假状态，那更糟）。
       键码用十进制写：38=↑ 40=↓ 37=← 39=→ 13=回车 27=ESC 36=Home 35=End。*/
    if (optValue(argc, argv, L"--panel-keys")) {
        const wchar_t *q = optValue(argc, argv, L"--panel-keys");
        /* ★ 上限从 32 提到 256。32 是一开始随手写的数，而面板预设页
           有 30 行 —— "一路按到底再往回滚几步"这种状态需要五十几个键，
           32 个键会在第 33 个上**悄悄截断**，
           拍出来的图看着像成功，其实是另一个状态。能拍到什么状态不该由一个
           随手写的上限决定。 */
        int n = 0, max = 256;
        while (q && *q && n < max) {
            wchar_t *end = NULL;
            long vk = wcstol(q, &end, 10);
            if (end == q) break;             /* 解析不动了就停，不空转 */
            if (vk > 0 && vk < 256) {
                memset(&app.input, 0, sizeof(app.input));
                app.input.pressed[vk] = 1;
                app.input.down[vk] = 1;      /* 方向键走的是长按重复那条路 */
                appParamInput(&app);
                memset(&app.input, 0, sizeof(app.input));
                ++n;
            }
            q = end;
            while (*q == L',' || *q == L' ' || *q == L'\t') ++q;
        }
        printf("[panel-keys] injected %d key(s)\n", n);
        if (n == 0) printUtf8(L"[警告] --panel-keys 没解析出任何键码\n");
    }

    if (optValue(argc, argv, L"--pose")) {
        if (applyPose(&app, optValue(argc, argv, L"--pose")) != 0)
            printUtf8(L"[警告] --pose 格式不对，应为 x,y,z,yaw,pitch\n");
    }

    /* 出图形态**不写任何存档**。补这一条的理由：appEndSession 会顺手存成绩单
       和历史记录，而出图为了走到结算屏/历史屏必须调它 —— 于是拍几张图就把
       真实的 记录.dat / 历史.dat 覆盖了。出图是只读操作，这一条不能靠
       "记得别在出图时结算"来保证，得在源头把路径清掉。

       ★ paramsPath 也在清除之列。以前参数只能在面板里手动按 S 落盘，
       而出图不会去按 S，所以这一条不必列；现在**关面板就自动落盘**，
       而 `--panel --panel-keys=39` 这种出图命令会真的按键、真的改参数 ——
       不清路径的话，拍一张图就把 参数.dat 覆盖了，正是之前栽过的那个跟头，
       只是换了个文件。*/
    if (wantShot) {
        app.bookPath[0] = L'\0';
        app.histPath[0] = L'\0';
        app.paramsPath[0] = L'\0';
    }

    /* 出图用的垫场数据：历史记录屏一局都没有的时候只有一行"还没有记录"，
       拍出来看不出这个界面长什么样。垫 N 条假的进去，屏幕上才有东西可看。
       ★ 只改内存里的 app.history，**不写 历史.dat** —— 出图不该碰真实的存档。
       条数、分数由下标推出来 —— 同一组参数拍出来永远一样。**时间不是**：
       下面那个 when 从"现在"往前推，所以这一张（25_历史记录）里右栏那列
       时间戳会随重出的时刻变（分钟级）。实测：同一分钟内连出两张
       逐像素相同，隔了十几分钟再出就不同。这是出图垫场数据的性质，
       不是程序行为，写在这里免得下次又把 25 的 md5 当成"有人动了画面"。
       ★ 垫之前先把读进来的清掉：不清的话拍出来的图是"存档 + 合成数据"
       的混合体，既不可复现（换台机器条数就变），第 1 次那条还可能是早先
       真实跑出来的 0 分。要的是"看这个界面长什么样"，不是看一份存档。*/
    {
        int fill = optInt(argc, argv, L"--hist-fill", 0);
        int k;
        if (fill > HISTORY_CAP) fill = HISTORY_CAP;
        if (fill > 0) historyInit(&app.history);
        /* historyPush 把新的一条塞进槽位 0、其余整体后移，所以**倒着垫**：
           k 最大的那条（分最低、时间最早）先进，k = 0 那条最后进，
           最后正好落在第 1 次的位置上。*/
        for (k = fill - 1; k >= 0; --k) {
            HistoryEntry he;
            memset(&he, 0, sizeof(he));
            he.score       = 1200 - k * 137;
            if (he.score < 40) he.score = 40 + k * 11;
            he.popped      = 40 + (k * 7) % 60;
            he.missed      = (k * 3) % 9;
            he.shots       = 120 + (k * 11) % 90;
            he.hits        = he.shots - 20 - (k * 5) % 30;
            he.bestCombo   = 6 + (k * 5) % 18;
            he.accuracyPct = (he.shots > 0) ? (he.hits * 100 / he.shots) : 0;
            he.durationMs  = 60000 + k * 3200;
            he.preset      = (k + 1) % PRESET_COUNT;
            he.mode        = g_presets[he.preset].mode;
            he.endReason   = (k % 3 == 0) ? END_TIME :
                             (k % 3 == 1) ? END_LIVES : END_MANUAL;
            he.seed        = 100000u + (unsigned)k * 1723u;
            /* 时间戳从"现在"往前推，一条比一条早 —— 第 1 次最新，
               往下越来越旧，和界面上那句"往下越来越早"对得上。*/
            he.when        = (long long)_time64(NULL) - (long long)k * 90000;
            historyPush(&app.history, &he);
        }
        if (fill > 0)
            printUtf8f(L"[出图] 垫了 %d 条历史记录（不落盘；第 1 次 = 分最高的那条）\n",
                       fill);
    }

    if (wantShot) {
        rc = doShot(&app, shotPath, frames, optInt(argc, argv, L"--fire", 0),
                    optInt(argc, argv, L"--screen", SCREEN_PLAY)) == 0 ? 0 : 3;
        /* 再来一次复核，这回是在**整帧渲染跑完之后**。前两次复核能看到
           "场景建完有没有动过图集"，看不到"渲染过程中有没有动过" —— 而
           HUD 上的错字正是那一瞬间的产物。*/
        if (optValue(argc, argv, L"--verify-frame"))
            hudVerifyAtlas(&app.hud);
        appShutdown(&app);
        return rc;
    }

    /* ---- 游戏形态 ---- */
    {
        DWORD pids[4];
        if (GetConsoleProcessList(pids, 4) <= 1) {
            HWND con = GetConsoleWindow();
            if (con) ShowWindow(con, SW_HIDE);
        }
    }
    /* 音频设备只在"真的进游戏"这一形态下才打开：出图与自检要能在没有
       声卡的机器上跑到底，所以那两条路根本不碰设备。开不出来不算错误 ——
       打印原因后静音继续跑。*/
    {
        wchar_t amsg[128];
        if (audioOpen(&app.audio, app.hwnd, amsg, (int)ARRAY_COUNT(amsg)) == 0)
            printUtf8f(L"[音频] %ls\n", amsg);
        else
            printUtf8f(L"[音频] %ls —— 静音运行，不影响玩法\n", amsg);
    }

    printUtf8(L"[启动] FPS 气球训练场 —— 关闭窗口即退出\n");
    app.running = 1;
    appRun(&app);
    appShutdown(&app);
    return 0;
}
