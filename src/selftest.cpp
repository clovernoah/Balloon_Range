/* ============================================================================
 * selftest.cpp —— 断言实现
 *
 * 输出策略：控制台只打**一行 ASCII 摘要**（便于脚本抓），明细中文报告写成
 * UTF-8 文本文件。失败时同时往控制台吐 UTF-8 字节 —— Git Bash 直接显示，
 * 不会被 936 代码页搅乱。
 *
 * 全部断言不建窗口、不碰 GL。能在没有桌面的环境下跑，这是整套验证的地基。
 * ==========================================================================*/
#include "selftest.h"
#include "png.h"
#include "app.h"     /* 参数活性探针要建一个无窗口的 App 直接跑 appStep */
#include "render.h"  /* 面板滚动断言要用 paramPanelRows 算可见行数 */
#include "scene.h"
#include "balloon.h"
#include "player.h"
#include "hud.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <io.h>      /* _chsize / _fileno —— 历史记录那组要砍一个文件的尾巴 */

/* ============================================================ 记账 */

static int      g_pass = 0;
static int      g_fail = 0;
static FILE    *g_rep  = NULL;
static int      g_groupFail = 0;

/* 宽字符 → UTF-8 字节。报告与失败输出都走这一条路，
   保证中文到哪儿都是同一份字节。*/
static void toUtf8(const wchar_t *w, char *out, int outCount) {
    int n;
    if (!out || outCount <= 0) return;
    out[0] = '\0';
    if (!w) return;
    n = WideCharToMultiByte(CP_UTF8, 0, w, -1, out, outCount, NULL, NULL);
    if (n <= 0) out[0] = '\0';
    else out[outCount - 1] = '\0';
}

static void repW(const wchar_t *s) {
    char buf[1024];
    if (!g_rep || !s) return;
    toUtf8(s, buf, (int)sizeof(buf));
    fputs(buf, g_rep);
}

static void repA(const char *s) {
    if (g_rep && s) fputs(s, g_rep);
}

static void repFmt(const wchar_t *fmt, ...) {
    wchar_t buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, ARRAY_COUNT(buf) - 1, fmt, ap);
    va_end(ap);
    buf[ARRAY_COUNT(buf) - 1] = L'\0';
    repW(buf);
}

static void group(const wchar_t *name) {
    if (g_rep) {
        if (g_groupFail > 0) repW(L"  ← 本组有失败\n");
        g_groupFail = 0;
        repW(L"\n## ");
        repW(name);
        repW(L"\n");
    }
}

static void checkImpl(int ok, const wchar_t *what) {
    if (ok) {
        ++g_pass;
        if (g_rep) { repW(L"  ok   "); repW(what); repW(L"\n"); }
    } else {
        ++g_fail;
        ++g_groupFail;
        if (g_rep) { repW(L"  FAIL "); repW(what); repW(L"\n"); }
        if (g_fail <= 40) {           /* 失败必须打到控制台，但也别刷屏 */
            wchar_t line[1100];
            _snwprintf(line, ARRAY_COUNT(line) - 1, L"[FAIL] %ls", what);
            line[ARRAY_COUNT(line) - 1] = L'\0';
            printUtf8(line);
            printUtf8(L"\n");
        }
    }
}

#define CHECK(cond, what) checkImpl((cond) ? 1 : 0, (what))

/* 带名字的断言：失败时把"是哪一项"一起打出来。参数表那些断言都是逐项跑的，
   不指明是哪一项就只看到"有 3 项不对"，还得自己二分去找。*/
static void checkNamed(int ok, const wchar_t *what, const wchar_t *who) {
    wchar_t line[1100];
    if (ok) { checkImpl(1, what); return; }
    _snwprintf(line, ARRAY_COUNT(line) - 1, L"%ls —— 出在「%ls」", what, who);
    line[ARRAY_COUNT(line) - 1] = L'\0';
    checkImpl(0, line);
}
#define CHECK_NAMED(cond, what, who) checkNamed((cond) ? 1 : 0, (what), (who))

/* 浮点比较：绝对误差 1e-4。本项目里都是个位数量级，够严。*/
static int feq(float a, float b) { return fabsf(a - b) < 1e-4f; }

/* 占着墙上一块地方的槽位数（出现中 / 活着 / 正在逃走）。
   这才是"墙上恒为 N"的精确表述 —— 逃走动画期间球确实不在墙上。*/
static int occupiedCount(const BalloonPool *bp) {
    int i, c = 0;
    for (i = 0; i < bp->n; ++i)
        if (bp->slots[i].state != BSLOT_WAIT) ++c;
    return c;
}

static void advancePool(BalloonPool *bp, const Params *p, const FieldRect *f,
                        double *elapsed, float seconds, int *outEscaped) {
    float dt = (float)FIXED_DT;
    float t = 0.0f;
    while (t < seconds) {
        int e;
        *elapsed += dt;
        e = balloonUpdate(bp, p, f, *elapsed, dt);
        if (outEscaped) *outEscaped += e;
        t += dt;
    }
}

/* ============================================================ 标量与向量 */

static void tScalar(void) {
    group(L"标量与向量");
    CHECK(feq(clampf(5.0f, 0.0f, 1.0f), 1.0f), L"clampf 上夹");
    CHECK(feq(clampf(-3.0f, 0.0f, 1.0f), 0.0f), L"clampf 下夹");
    CHECK(feq(clampf(0.5f, 0.0f, 1.0f), 0.5f), L"clampf 区间内不动");
    CHECK(clampi(99, 0, 10) == 10, L"clampi 上夹");
    CHECK(clampi(-1, 0, 10) == 0, L"clampi 下夹");
    CHECK(feq(easeOutQuad(0.0f), 0.0f), L"easeOutQuad(0) = 0");
    CHECK(feq(easeOutQuad(1.0f), 1.0f), L"easeOutQuad(1) = 1");
    CHECK(easeOutQuad(0.5f) > 0.5f, L"easeOutQuad 先快后慢");
    CHECK(easeOutCubic(0.5f) > easeOutQuad(0.5f), L"easeOutCubic 比 Quad 更急");
    CHECK(feq(easeInQuad(0.5f), 0.25f), L"easeInQuad(0.5) = 0.25");
    CHECK(feq(smoothstep01(-5.0f), 0.0f), L"smoothstep 下夹");
    CHECK(feq(smoothstep01(5.0f), 1.0f), L"smoothstep 上夹");
    CHECK(feq(smoothstep01(0.5f), 0.5f), L"smoothstep 中点 = 0.5");
    CHECK(feq(lerpf(2.0f, 4.0f, 0.25f), 2.5f), L"lerpf");

    CHECK(feq(v3len(v3(3.0f, 4.0f, 0.0f)), 5.0f), L"v3len 3-4-5");
    CHECK(feq(v3dot(v3(1.0f, 2.0f, 3.0f), v3(4.0f, -5.0f, 6.0f)), 12.0f), L"v3dot");
    {
        Vec3 c = v3cross(v3(1.0f, 0.0f, 0.0f), v3(0.0f, 1.0f, 0.0f));
        CHECK(feq(c.x, 0.0f) && feq(c.y, 0.0f) && feq(c.z, 1.0f), L"v3cross x×y = z");
    }
    {
        Vec3 z = v3norm(v3zero());
        CHECK(feq(z.x, 0.0f) && feq(z.y, 0.0f) && feq(z.z, 0.0f),
              L"v3norm 零向量不除零，返回零向量");
    }
    CHECK(feq(v3len(v3norm(v3(7.0f, -2.0f, 0.5f))), 1.0f), L"v3norm 结果长度为 1");
    CHECK(feq(v3dist(v3(1.0f, 1.0f, 1.0f), v3(1.0f, 1.0f, 1.0f)), 0.0f), L"v3dist 同点为 0");
}

/* ============================================================ 矩阵 */

static void tMatrix(void) {
    group(L"矩阵");
    {
        Mat4 M = mat4Mul(mat4Translate(v3(1.0f, 2.0f, 3.0f)), mat4Identity());
        Vec3 p = mat4XformPoint(M, v3(0.0f, 0.0f, 0.0f));
        CHECK(feq(p.x, 1.0f) && feq(p.y, 2.0f) && feq(p.z, 3.0f), L"平移矩阵 × 单位阵");
    }
    {
        /* 方向变换不带平移 —— 这是 XformPoint 与 XformDir 的分野。*/
        Vec3 d = mat4XformDir(mat4Translate(v3(9.0f, 9.0f, 9.0f)), v3(1.0f, 0.0f, 0.0f));
        CHECK(feq(d.x, 1.0f) && feq(d.y, 0.0f), L"方向变换忽略平移分量");
    }
    {
        Vec3 q = mat4XformDir(mat4RotateY(PI_F * 0.5f), v3(1.0f, 0.0f, 0.0f));
        CHECK(feq(q.x, 0.0f) && feq(q.z, -1.0f), L"绕 Y 转 90°：+x → -z");
    }
    {
        Vec3 q = mat4XformDir(mat4RotateX(PI_F * 0.5f), v3(0.0f, 1.0f, 0.0f));
        CHECK(feq(q.y, 0.0f) && feq(q.z, 1.0f), L"绕 X 转 90°：+y → +z");
    }
    {
        Mat4 V = mat4LookAt(v3(0.0f, 0.0f, 0.0f), v3(0.0f, 0.0f, -1.0f),
                            v3(0.0f, 1.0f, 0.0f));
        Vec3 q = mat4XformPoint(V, v3(0.0f, 0.0f, -5.0f));
        CHECK(feq(q.x, 0.0f) && feq(q.y, 0.0f) && feq(q.z, -5.0f),
              L"lookAt：正前方的点留在正前方");
        CHECK(q.z < 0.0f, L"lookAt：眼空间里前方是 -z");
    }
    {
        /* 用一个不对齐的机位再验一次，防止"只对轴对齐情形成立"。*/
        Mat4 V = mat4LookAt(v3(2.0f, 1.0f, 3.0f), v3(2.0f, 1.0f, 0.0f),
                            v3(0.0f, 1.0f, 0.0f));
        Vec3 q = mat4XformPoint(V, v3(2.0f, 1.0f, 3.0f));
        CHECK(feq(v3len(q), 0.0f), L"lookAt：眼点被变换到眼空间原点");
    }
    {
        Mat4 P = mat4Perspective(90.0f, 1.0f, 1.0f, 100.0f);
        /* 注意要自己做透视除法：mat4XformPoint 返回的是齐次坐标
           (x, y, z, w)，裁剪空间里 z 还没除以 w。*/
        Vec3 q = mat4XformPoint(P, v3(0.0f, 0.0f, -1.0f));
        CHECK(feq(q.z, -1.0f), L"透视：近平面映射到 NDC -1");
        q = mat4XformPoint(P, v3(0.0f, 0.0f, -100.0f));
        CHECK(feq(q.z, 100.0f), L"透视：远平面的裁剪空间 z = zfar");
        CHECK(feq(q.z / 100.0f, 1.0f), L"透视：远平面除以 w 后映射到 NDC +1");
    }
    {
        /* 宽高比必须真的进矩阵，否则画面会被拉伸。*/
        Mat4 A = mat4Perspective(90.0f, 1.0f, 1.0f, 100.0f);
        Mat4 B = mat4Perspective(90.0f, 2.0f, 1.0f, 100.0f);
        CHECK(feq(B.m[0], A.m[0] * 0.5f), L"透视：宽高比只缩放 x，不改 y");
        CHECK(feq(B.m[5], A.m[5]), L"透视：宽高比不影响 y 方向");
    }
    {
        Mat4 V = mat4LookAt(v3(0.0f, 0.0f, 0.0f), v3(0.0f, 5.0f, 0.0f),
                            v3(0.0f, 1.0f, 0.0f));
        Vec3 q = mat4XformPoint(V, v3(1.0f, 0.0f, 0.0f));
        CHECK(v3len(q) > 0.5f, L"lookAt 退化（视线与 up 平行）时不塌成零矩阵");
    }
    {
        /* 基向量矩阵：把局部坐标 (1,0,0) 送成 right 方向。*/
        Mat4 B = mat4Basis(v3(0.0f, 0.0f, 1.0f), v3(0.0f, 1.0f, 0.0f),
                           v3(-1.0f, 0.0f, 0.0f));
        Vec3 q = mat4XformDir(B, v3(1.0f, 0.0f, 0.0f));
        CHECK(feq(q.z, 1.0f), L"mat4Basis：局部 x 轴映射到 right 方向");
    }
}

/* ============================================================ 射线与球 */

static void tRaySphere(void) {
    group(L"射线与球求交");
    {
        float t = -1.0f;
        CHECK(raySphere(v3(0.0f, 0.0f, 5.0f), v3(0.0f, 0.0f, -1.0f),
                        v3(0.0f, 0.0f, 0.0f), 1.0f, &t) == 1, L"正对球心命中");
        CHECK(feq(t, 4.0f), L"命中距离 = 5 - 1");
    }
    {
        /* 擦边：横向偏移正好等于半径，判别式 = 0。判定用 >=，所以算命中 ——
           这是沿用的一种手感设计（圆周算命中），不是笔误。*/
        float t = -1.0f;
        CHECK(raySphere(v3(1.0f, 0.0f, 5.0f), v3(0.0f, 0.0f, -1.0f),
                        v3(0.0f, 0.0f, 0.0f), 1.0f, &t) == 1, L"擦边（判别式 = 0）算命中");
        CHECK(feq(t, 5.0f), L"擦边的 t 落在切点上");
    }
    {
        float t = -1.0f;
        CHECK(raySphere(v3(1.5f, 0.0f, 5.0f), v3(0.0f, 0.0f, -1.0f),
                        v3(0.0f, 0.0f, 0.0f), 1.0f, &t) == 0, L"偏离半径之外不命中");
    }
    {
        float t = -1.0f;
        CHECK(raySphere(v3(0.0f, 0.0f, 0.0f), v3(0.0f, 0.0f, 1.0f),
                        v3(0.0f, 0.0f, -5.0f), 1.0f, &t) == 0, L"球整颗在身后不命中");
    }
    {
        float t = -1.0f;
        CHECK(raySphere(v3(0.0f, 0.0f, 0.0f), v3(0.0f, 0.0f, -1.0f),
                        v3(0.0f, 0.0f, 0.0f), 1.0f, &t) == 1, L"起点在球内算命中");
        CHECK(feq(t, 0.0f), L"起点在球内时 t = 0");
    }
    {
        /* 半径 0 是退化球。射线擦着它过时判别式为 0，按"圆周算命中"的规则
           返回 1；但只要偏开一点点就不该命中。两条都要钉住，
           免得以后有人以为半径 0 会算出 NaN。*/
        float t = -9.0f;
        CHECK(raySphere(v3(0.0f, 0.0f, 5.0f), v3(0.0f, 0.0f, -1.0f),
                        v3(0.0f, 0.1f, 0.0f), 0.0f, &t) == 0,
              L"半径 0 且射线不穿过球心时不命中");
        CHECK(t == -9.0f, L"不命中时不写 outT（调用方的初值原样保留）");
        t = 0.0f;
        raySphere(v3(0.0f, 0.0f, 5.0f), v3(0.0f, 0.0f, -1.0f),
                  v3(0.0f, 0.0f, 0.0f), 0.0f, &t);
        CHECK(t == t && t < 1e30f, L"半径 0 不产生 NaN / inf");
    }
    {
        /* outT 传 NULL 也不能崩 —— 调用方有只关心"中没中"的场合。*/
        CHECK(raySphere(v3(0.0f, 0.0f, 5.0f), v3(0.0f, 0.0f, -1.0f),
                        v3(0.0f, 0.0f, 0.0f), 1.0f, NULL) == 1,
              L"outT 传 NULL 照常工作");
    }

    CHECK(spheresOverlap2D(0.0f, 0.0f, 1.0f, 1.5f, 0.0f, 1.0f, 1.0f) == 1,
          L"间距不足判定为重叠");
    CHECK(spheresOverlap2D(0.0f, 0.0f, 1.0f, 2.5f, 0.0f, 1.0f, 1.0f) == 0,
          L"间距足够判定为不重叠");
    CHECK(spheresOverlap2D(0.0f, 0.0f, 1.0f, 2.05f, 0.0f, 1.0f, 1.06f) == 1,
          L"1.06 倍缝：2.0 的距离仍算重叠");
    CHECK(spheresOverlap2D(0.0f, 0.0f, 1.0f, 2.2f, 0.0f, 1.0f, 1.06f) == 0,
          L"1.06 倍缝：超过 2.12 就算分开了");
}

/* ============================================================ 随机数 */

static void tRng(void) {
    Rng a, b;
    int i, same = 1, ok = 1, distinct = 0;
    unsigned prev;
    group(L"随机数");

    rngSeed(&a, 12345u);
    rngSeed(&b, 12345u);
    for (i = 0; i < 200; ++i)
        if (rngNext(&a) != rngNext(&b)) same = 0;
    CHECK(same, L"同种子完全可复现（出图与自检都靠这条）");

    rngSeed(&a, 999u);
    for (i = 0; i < 5000; ++i) {
        float f = rngFloat(&a);
        if (!(f >= 0.0f && f < 1.0f)) { ok = 0; break; }
    }
    CHECK(ok, L"rngFloat 恒落在 [0,1)");

    ok = 1;
    rngSeed(&a, 7u);
    for (i = 0; i < 5000; ++i) {
        int v = rngInt(&a, 3, 7);
        if (v < 3 || v > 7) { ok = 0; break; }
    }
    CHECK(ok, L"rngInt 恒落在闭区间内");
    CHECK(rngInt(&a, 5, 5) == 5, L"rngInt 上下界相同时返回该值");

    /* 覆盖度：1..6 六个数都该被抽到过。*/
    {
        int seen[7] = { 0, 0, 0, 0, 0, 0, 0 };
        rngSeed(&a, 88u);
        for (i = 0; i < 3000; ++i) seen[rngInt(&a, 1, 6)] = 1;
        for (i = 1; i <= 6; ++i) if (!seen[i]) ok = 0;
        CHECK(ok, L"rngInt 取值覆盖整个区间");
    }

    /* 种子为 0 时 xorshift 会退化成恒零，必须被兜住。*/
    rngSeed(&a, 0u);
    CHECK(rngNext(&a) != 0u, L"种子 0 不会退化成恒零");
    prev = rngNext(&a);
    for (i = 0; i < 50; ++i) {
        unsigned v = rngNext(&a);
        if (v != prev) ++distinct;
        prev = v;
    }
    CHECK(distinct > 45, L"连续取值不重复");

    {
        int hits = 0;
        rngSeed(&a, 31337u);
        for (i = 0; i < 20000; ++i) hits += rngChance(&a, 0.25f);
        CHECK(hits > 4500 && hits < 5500, L"rngChance(0.25) 的频率接近 1/4");
    }
}

/* ============================================================ 颜色与校验和 */

static void tColor(void) {
    group(L"颜色与校验和");
    {
        Color3 c = hsvToRgb(0.0f, 1.0f, 1.0f);
        CHECK(feq(c.r, 1.0f) && feq(c.g, 0.0f) && feq(c.b, 0.0f), L"HSV 0° = 纯红");
    }
    {
        Color3 c = hsvToRgb(120.0f, 1.0f, 1.0f);
        CHECK(feq(c.r, 0.0f) && feq(c.g, 1.0f) && feq(c.b, 0.0f), L"HSV 120° = 纯绿");
    }
    {
        Color3 c = hsvToRgb(240.0f, 1.0f, 1.0f);
        CHECK(feq(c.r, 0.0f) && feq(c.g, 0.0f) && feq(c.b, 1.0f), L"HSV 240° = 纯蓝");
    }
    {
        Color3 c = hsvToRgb(360.0f, 1.0f, 1.0f);
        CHECK(feq(c.r, 1.0f) && feq(c.b, 0.0f), L"HSV 360° 绕回纯红");
    }
    {
        /* 330° 是红与品红的中间：红满、绿零、蓝半。*/
        Color3 c = hsvToRgb(-30.0f, 1.0f, 1.0f);
        CHECK(feq(c.r, 1.0f) && feq(c.g, 0.0f) && feq(c.b, 0.5f),
              L"HSV 负角度绕回 330°（红满、绿零、蓝半）");
    }
    {
        Color3 c = hsvToRgb(0.0f, 0.0f, 0.5f);
        CHECK(feq(c.r, 0.5f) && feq(c.g, 0.5f) && feq(c.b, 0.5f), L"饱和度 0 = 无彩灰");
    }
    {
        Color3 c = hsvToRgb(0.0f, 5.0f, 5.0f);
        CHECK(c.r <= 1.0f && c.g >= 0.0f && c.b >= 0.0f, L"越界的 s/v 被夹住，不会溢出");
    }
    CHECK(feq(colorScale(color3(0.5f, 0.5f, 0.5f), 1.0f).r, 1.0f), L"colorScale(+1) = 全白");
    CHECK(feq(colorScale(color3(0.5f, 0.5f, 0.5f), -1.0f).r, 0.0f), L"colorScale(-1) = 全黑");
    CHECK(feq(colorLerp(color3(0.0f, 0.0f, 0.0f), color3(1.0f, 1.0f, 1.0f), 0.25f).g, 0.25f),
          L"colorLerp 线性插值");
    CHECK(feq(colorFromRGB8(255, 0, 0).r, 1.0f), L"colorFromRGB8 归一化");

    /* CRC32 的标准测试向量："123456789" → 0xCBF43926。
       这一条能同时查出多项式写错、表建错、初值/终值取反漏掉这一整类错误。*/
    CHECK(crc32Buf("123456789", 9) == 0xCBF43926u, L"CRC32 标准测试向量");
    CHECK(crc32Buf("", 0) == 0x00000000u, L"CRC32 空输入为 0");
    CHECK(crc32Buf("a", 1) == 0xE8B7BE43u, L"CRC32 单字节向量");
    {
        /* 改一个字节，校验和必须变 —— 这是篡改检测的地基。*/
        const char *s1 = "BalloonRange";
        char s2[13];
        memcpy(s2, s1, 13);
        s2[5] = 'X';
        CHECK(crc32Buf(s1, 12) != crc32Buf(s2, 12), L"内容变动后 CRC 必变");
    }
}

/* ============================================================ PNG 编码器 */

static void tPng(void) {
    int w = 37, h = 23;     /* 故意用奇数尺寸：行对齐写错会立刻暴露 */
    unsigned char *buf = (unsigned char *)malloc((size_t)w * h * 3);
    size_t bad = 0;
    int i;
    group(L"PNG 编码器");

    if (!buf) { CHECK(0, L"测试缓冲分配成功"); return; }

    for (i = 0; i < w * h * 3; ++i) buf[i] = (unsigned char)((i * 37 + 11) & 0xFF);
    CHECK(pngRoundTripRGB(w, h, buf, &bad) == 0,
          L"编码后自己能解回来、逐像素一致（含渐变与噪声）");

    memset(buf, 0, (size_t)w * h * 3);
    CHECK(pngRoundTripRGB(w, h, buf, &bad) == 0, L"全黑图往返一致");

    memset(buf, 255, (size_t)w * h * 3);
    CHECK(pngRoundTripRGB(w, h, buf, &bad) == 0, L"全白图往返一致");

    /* 1x1：最容易被行填充与循环边界写错的地方。*/
    buf[0] = 12; buf[1] = 34; buf[2] = 56;
    CHECK(pngRoundTripRGB(1, 1, buf, &bad) == 0, L"1x1 图往返一致");

    /* 宽度不是 4 的倍数：行填充出错的经典触发点。*/
    {
        unsigned char row[15];
        for (i = 0; i < 15; ++i) row[i] = (unsigned char)(i * 17);
        CHECK(pngRoundTripRGB(5, 1, row, &bad) == 0, L"5x1 图往返一致（单行不对齐）");
    }

    /* ---- 独立于解码器的结构检查：直接看编码出来的字节流 ----
       这一段不依赖被测代码里的解码器，验的是 PNG 规范里最硬的那几个字段。*/
    {
        unsigned char *png;
        size_t len = 0;
        static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

        for (i = 0; i < w * h * 3; ++i) buf[i] = (unsigned char)(i & 0xFF);
        png = pngEncodeRGB(w, h, buf, &len);

        CHECK(png != NULL && len > 8 + 25 + 12 + 12, L"编码输出的字节流非空且长度合理");
        if (png && len > 8 + 25 + 12 + 12) {
            CHECK(memcmp(png, sig, 8) == 0, L"PNG 八字节签名正确");
            CHECK(memcmp(png + 12, "IHDR", 4) == 0, L"IHDR 块类型正确");
            CHECK((((unsigned)png[16] << 24 | (unsigned)png[17] << 16 |
                    (unsigned)png[18] << 8 | png[19]) == (unsigned)w) &&
                  (((unsigned)png[20] << 24 | (unsigned)png[21] << 16 |
                    (unsigned)png[22] << 8 | png[23]) == (unsigned)h),
                  L"IHDR 里的宽高按大端写入且与输入一致");
            CHECK(png[24] == 8, L"IHDR 位深 = 8");
            CHECK(png[25] == 2, L"IHDR 颜色类型 = 2（真彩色 RGB）");
            CHECK(memcmp(png + len - 8, "IEND", 4) == 0 &&
                  png[len - 12] == 0 && png[len - 11] == 0 &&
                  png[len - 10] == 0 && png[len - 9] == 0,
                  L"以长度 0 的 IEND 块收尾");
            free(png);
        }
    }

    free(buf);
}

/* ============================================================ 参数表 */

/* ============================================================ 预设方案
 *
 * 早先把"模式"与"难度"分成两张表，后来合并成一张 g_presets[] ——
 * 这三者是同一个概念，就是一组提前写好的可玩的参数。
 *
 * 这一组断言盯三件事：
 *   a) 表本身没写坏（覆盖项的偏移都指得着、名字说明都齐、规格对得上）；
 *   b) 每套预设单独拿到手，**参数之间的配合**是合理的 —— 单看"8 个球"
 *      "4.5 秒""5 条命"都合理，凑在一起却是"开局第一波全漏就必死"，这个
 *      bug 真的发生过（墙上 8 个球同时出生、同时到时，一口气扣光 5 条命），
 *      而它不是任何一项单独能看出来的；
 *   c) 八套预设**真的不一样**（指纹两两不同），以及它们合起来覆盖了
 *      足够多的参数项 —— 要让玩家能玩遍这些设置参数中的大部分，
 *      这一条就是那句话的可判定版本。*/
static void tPresets(void) {
    int i, j;
    group(L"预设方案");

    CHECK(g_presetCount == PRESET_COUNT, L"预设条数与 PRESET_COUNT 一致");
    CHECK(PRESET_COUNT >= 6, L"至少提供六套预设（对应几档难度模式）");

    /* ---- ★ 顺序钉死 ----
       预设的**下标是会写进存档的**：`记录.dat` 的八档最高分按预设分档，
       `历史.dat` 每一条也记着当时用的是哪一套。所以"顺序"不是界面上的小事 ——
       「跟踪训练」固定在「小快靶」与「计时挑战」之间，这里把顺序逐位钉死：
       以后谁再动一下，这几条当场红，而不是等到玩家发现自己的最高分挂到了
       别的预设名下。 */
    {
        static const wchar_t *order[PRESET_COUNT] = {
            L"固定靶", L"移动靶", L"小快靶", L"跟踪训练",
            L"计时挑战", L"渐进训练", L"混合靶场", L"精准挑战"
        };
        for (i = 0; i < PRESET_COUNT; ++i)
            CHECK_NAMED(wcscmp(g_presets[i].name, order[i]) == 0,
                        L"这一位就是该在的那一套预设", order[i]);
        /* 逐套各套一遍：点第 i 行必须真的落到第 i 套上 ——
           面板取预设靠的就是"菜单行的 arg"，而存档记的就是这个下标。*/
        for (i = 0; i < PRESET_COUNT; ++i) {
            Params q;
            paramsDefault(&q);
            paramsApplyPreset(&q, i);
            CHECK_NAMED(q.preset == i, L"套用第 i 套之后 params.preset == i", order[i]);
            CHECK_NAMED(appParamMenuPreset(i) == i,
                        L"一级菜单第 i 行的 arg 就是下标 i", order[i]);
        }
    }

    /* ---- a) 表本身的规格 ---- */
    for (i = 0; i < PRESET_COUNT; ++i) {
        const GamePreset *g = &g_presets[i];
        const wchar_t *nm = g->name;
        CHECK_NAMED(g->name && g->name[0], L"预设名非空", nm);
        /* 每个预设方案都要有说明 —— 空说明过不了这一条。*/
        CHECK_NAMED(g->desc && g->desc[0], L"预设说明非空", nm);
        CHECK_NAMED(g->ovCount > 0, L"覆盖项非空（否则这套预设什么也没定）", nm);
        CHECK_NAMED(g->mode >= 0 && g->mode < MODE_COUNT, L"规则下标合法", nm);
        CHECK_NAMED(g->ramp >= 0.0f && g->ramp <= DIFF_FACTOR_MAX, L"涨幅在合法区间", nm);

        /* 每一条覆盖项的偏移都必须在描述表里指得着。
           指不着 = 预设定了一个面板上看不见、存档也存不下的值 —— 静默失效，
           正是最难发现的那一类。paramsApplyPreset 里遇到这种项是 continue,
           所以这里必须自己盯住。*/
        for (j = 0; j < g->ovCount; ++j) {
            CHECK_NAMED(paramDescByOffset(g->ov[j].offset) != NULL,
                        L"覆盖项的字段在参数描述表里存在", nm);
        }
        /* 预设的覆盖值必须落在这一项自己的可调区间里 —— 越界的值会被
           paramsClamp 默默夹回来，于是"预设写的是 A，实际生效的是 B"。*/
        for (j = 0; j < g->ovCount; ++j) {
            const ParamDesc *d = paramDescByOffset(g->ov[j].offset);
            if (!d) continue;
            CHECK_NAMED(g->ov[j].value >= d->lo - 1e-4f && g->ov[j].value <= d->hi + 1e-4f,
                        L"覆盖值在该项的可调区间内（不会被夹走）", d->name);
        }
    }

    /* 名字互不相同 —— 八套里有两套叫一个名，用户在菜单上就分不出来了。*/
    for (i = 0; i < PRESET_COUNT; ++i)
        for (j = i + 1; j < PRESET_COUNT; ++j)
            CHECK_NAMED(wcscmp(g_presets[i].name, g_presets[j].name) != 0,
                        L"预设名两两不同", g_presets[i].name);

    /* ---- b) 每套预设单独看是否成立 ---- */
    for (i = 0; i < PRESET_COUNT; ++i) {
        Params p;
        const wchar_t *nm = g_presets[i].name;

        paramsDefault(&p);
        paramsApplyPreset(&p, i);
        paramsClamp(&p);

        CHECK_NAMED(p.preset == i, L"preset 字段被正确写入", nm);
        CHECK_NAMED(p.mode == g_presets[i].mode, L"mode 字段被正确写入", nm);
        CHECK_NAMED(feq(p.ramp, g_presets[i].ramp), L"ramp 字段被正确写入", nm);
        CHECK_NAMED(p.balloonCount >= 1 && p.balloonCount <= MAX_BALLOONS,
                    L"气球数在合法区间内", nm);
        CHECK_NAMED(p.radiusMin <= p.radiusMax, L"半径上下限没有写反", nm);

        /* 会掉命 + 有存活时限：这两条同时成立时，墙上的球是**同时出生、
           同时到时**的，全漏一波就是一次同时扣命。要求它小于初始生命 ——
           玩家最坏情况下也有第二次机会，而不是开局即死。*/
        if (p.missCostsLife && !p.infiniteLives && p.lifetimeSec > 0.0f) {
            CHECK_NAMED((float)p.balloonCount < (float)p.lives,
                        L"第一波全漏也不会直接送命（球数 < 生命）", nm);
            CHECK_NAMED(p.lives >= 2, L"默认生命留得下第二次机会", nm);
        } else {
            CHECK_NAMED(p.infiniteLives || !p.missCostsLife,
                        L"不掉命的预设确实设了无限生命或不扣命", nm);
        }

        /* 套用两次结果一样（幂等）。否则用户在菜单上把光标在同一项上
           来回拨，参数会一档档漂走。*/
        {
            Params r = p;
            paramsApplyPreset(&r, i);
            paramsClamp(&r);
            CHECK_NAMED(paramsEqual(&p, &r), L"重复套用同一套预设结果不变", nm);
        }
    }

    /* ---- c) 八套真的不一样，且合起来盖住大部分可玩参数 ---- */
    for (i = 0; i < PRESET_COUNT; ++i) {
        Params a;
        paramsDefault(&a);
        paramsApplyPreset(&a, i);
        for (j = i + 1; j < PRESET_COUNT; ++j) {
            Params b;
            paramsDefault(&b);
            paramsApplyPreset(&b, j);
            /* 指纹不同 = 拨过去之后场上真的会变。差一项也算差。*/
            CHECK_NAMED(paramsHash(&a) != paramsHash(&b),
                        L"两两预设的参数指纹互不相同", g_presets[j].name);
        }
    }

    {
        /* 覆盖度 = 有多少个参数项被**至少一套**预设指定过。
           这条判据盯的是"预设开箱即用"：预设没碰过的项，玩家只能自己在二级菜单
           里一项项摸出来，而愿意逐项手调参数的人本来就是少数。

           ★ 分母只算**玩法那五组**（气球/运动/节奏/判定/训练）。
           玩家、画面、声音那三组是**有意不覆盖**的 —— 预设管的是"这一局
           怎么玩"，鼠标灵敏度、准星颜色、音量是个人口味，换一套预设就
           把用户的音量改掉是越界。侧栏页脚也是这么写给用户看的
           （"预设只定玩法；鼠标灵敏度、画面、音量属于个人口味，不会被换掉"）。*/
        static const int kCoveredFloorPct = 75;
        char covered[256];
        int n = 0, c = 0, gidx;
        int total = 0, totalCovered = 0;
        CHECK(g_paramDescCount <= (int)sizeof(covered), L"描述表项数在可统计范围内");
        memset(covered, 0, sizeof(covered));
        for (i = 0; i < PRESET_COUNT; ++i) {
            for (j = 0; j < g_presets[i].ovCount; ++j) {
                int k;
                for (k = 0; k < g_paramDescCount; ++k)
                    if (g_paramDescs[k].offset == g_presets[i].ov[j].offset) covered[k] = 1;
            }
        }
        for (i = 0; i < g_paramDescCount; ++i) {
            int gp = g_paramDescs[i].group;
            if (gp == PARAM_GROUP_PLAYER || gp == PARAM_GROUP_GRAPHICS ||
                gp == PARAM_GROUP_AUDIO) continue;
            ++n;
            if (covered[i]) { ++c; }
        }
        for (i = 0; i < g_paramDescCount; ++i) {
            ++total;
            if (covered[i]) ++totalCovered;
        }
        printf("      [预设] 玩法参数覆盖度：%d / %d 项 = %d%%（门槛 %d%%）；"
               "含个人口味项共 %d / %d\n",
               c, n, (n > 0) ? (c * 100 / n) : 0, kCoveredFloorPct,
               totalCovered, total);
        CHECK(n > 0, L"玩法参数组非空");
        CHECK(c * 100 >= kCoveredFloorPct * n, L"预设合起来覆盖了大部分玩法参数");

        /* 玩法五组，**每一组**至少被碰过一项 —— 某一整组没有任何预设涉及，
           意味着用户永远摸不到它，那组的滑杆就成了摆设。

           ★ 注意这里**不能**写成 for (gidx = BALLOON; gidx <= TRAINING; ++gidx)：
           ParamGroup 的枚举顺序是 气球 / 运动 / 节奏 / 判定 / 【玩家 / 画面 /
           声音】 / 训练 —— 个人口味那三组正夹在中间，「从气球到训练」这个
           区间顺手就把它们圈进来了。
           这一条就踩过这个坑 —— 报出来的是「玩家」「画面」「声音」，而按本意
           那三组根本不该被检查。所以按"跳过这三组"来筛，而不是按区间。*/
        for (gidx = 0; gidx < PARAM_GROUP_COUNT; ++gidx) {
            int hit = 0;
            if (gidx == PARAM_GROUP_PLAYER || gidx == PARAM_GROUP_GRAPHICS ||
                gidx == PARAM_GROUP_AUDIO) continue;
            for (i = 0; i < g_paramDescCount; ++i)
                if (covered[i] && g_paramDescs[i].group == gidx) { hit = 1; break; }
            CHECK_NAMED(hit, L"该玩法组至少有一项被某套预设指定过",
                        g_paramGroupNames[gidx]);
        }

        /* 反过来盯一下**个人口味那三组**：一项都不该被预设碰。
           这不是"漏了"，是有意为之；哪天有人顺手把音量塞进某套预设，
           这条立刻会红，提醒他这是越界。*/
        for (i = 0; i < g_paramDescCount; ++i) {
            int gp = g_paramDescs[i].group;
            if (gp != PARAM_GROUP_PLAYER && gp != PARAM_GROUP_GRAPHICS &&
                gp != PARAM_GROUP_AUDIO) continue;
            CHECK_NAMED(!covered[i], L"个人口味项不被任何预设改动（换预设不改音量）",
                        g_paramDescs[i].name);
        }
    }

    /* ---- 四项**故意**不覆盖的玩法参数，逐条钉住理由 ----
       没法在代码里断言"理由成立"，所以写成断言的是"它们确实没被覆盖"，
       理由写在这里供将来的人复核。*/
    {
        struct { int offset; const wchar_t *why; } kIntentional[] = {
            { (int)offsetof(Params, seed),       L"种子：预设钉死种子 = 每次开局都长一样" },
            { (int)offsetof(Params, colorSat),   L"气球饱和度：观感，不是玩法" },
            { (int)offsetof(Params, colorVal),   L"气球亮度：观感，不是玩法" },
            { (int)offsetof(Params, showStats),  L"统计面板开关：界面偏好" }
        };
        for (i = 0; i < (int)ARRAY_COUNT(kIntentional); ++i) {
            int hit = 0;
            for (j = 0; j < PRESET_COUNT; ++j) {
                int k;
                for (k = 0; k < g_presets[j].ovCount; ++k)
                    if (g_presets[j].ov[k].offset == kIntentional[i].offset) hit = 1;
            }
            CHECK_NAMED(!hit, L"这项确实没有出现在任何预设里", kIntentional[i].why);
        }
    }

    /* ---- 三条默认值 ---- */

    /* "气球速度默认设置为 0，也就是固定靶" —— 默认那套必须是静止靶。*/
    {
        Params p;
        paramsDefault(&p);
        CHECK(p.preset == 0, L"默认落在第 0 套预设上");
        CHECK(p.motion == MOVE_STILL, L"默认预设是静止靶（速度 0）");
        CHECK(feq(p.driftSpeed, 0.0f), L"默认预设的漂移速度确实是 0");
        /* "大小设置为全部相同" —— 上下限相等。*/
        CHECK(feq(p.radiusMin, p.radiusMax), L"默认预设的气球大小全部相同");
        CHECK(p.radiusMin > 0.0f, L"相同的大小是个正数");
        /* 固定靶再随机出个会跑会缩的金球就没意义了 —— 关掉特殊球。*/
        CHECK(!p.allowSpecial, L"默认预设不出特殊气球");
        CHECK(p.lifetimeSec == 0.0f, L"默认预设不限时（球不会自己跑）");
        CHECK(p.refillDelaySec == 0.0f, L"默认预设打掉立刻补位");
        CHECK(p.autoFireInterval == 0.0f, L"默认预设不自动开火");
        /* 静止靶配"漏球扣命"会立刻变成"站着不动也会死"。*/
        CHECK(!p.missCostsLife, L"默认预设漏球不扣命");
    }

    /* "移动靶这一套：气球大小随机、可以当作一个难度模式选"
       —— 移动靶那套必须真的是在动的、而且大小是随机的。*/
    {
        Params p;
        paramsDefault(&p);
        paramsApplyPreset(&p, 1);
        CHECK(p.motion != MOVE_STILL, L"第 1 套（移动靶）真的在动");
        CHECK(p.driftSpeed > 0.0f || p.bobAmp > 0.0f, L"移动靶有非零的速度或幅度");
        CHECK(p.radiusMax > p.radiusMin, L"移动靶的气球大小是随机的（上下限不同）");
    }

    /* 渐进训练那套：既然是"球会跑"的预设，时限就不能是 0 —— 否则那是个假模式。*/
    {
        Params p;
        int prog = -1;
        for (i = 0; i < PRESET_COUNT; ++i)
            if (g_presets[i].mode == MODE_PROGRESS) { prog = i; break; }
        CHECK(prog >= 0, L"八套里有一套是渐进训练（规则 = MODE_PROGRESS）");
        if (prog >= 0) {
            paramsDefault(&p);
            paramsApplyPreset(&p, prog);
            CHECK(p.ramp > 0.0f, L"渐进训练的涨幅大于 0（难度真的会爬）");
            CHECK(p.lifetimeSec > 0.0f, L"渐进训练有存活时限（球真的会跑）");
            CHECK(!p.infiniteLives, L"渐进训练不是无限生命（漏球要扣命）");
            CHECK(p.missBreaksCombo, L"渐进训练漏球断连击");
        }
        /* 其余六套的 ramp 必须是 0：预设不该在没人要求的时候偷偷加难度。*/
        for (i = 0; i < PRESET_COUNT; ++i) {
            if (i == prog) continue;
            CHECK_NAMED(feq(g_presets[i].ramp, 0.0f), L"非渐进预设的涨幅为 0",
                        g_presets[i].name);
        }
    }

    /* 精准挑战那套：墙上恒为 1（paramsClamp 强制），脱靶即结束。*/
    {
        int prec = -1;
        Params p;
        for (i = 0; i < PRESET_COUNT; ++i)
            if (g_presets[i].mode == MODE_PRECISION) { prec = i; break; }
        CHECK(prec >= 0, L"八套里有一套是精准挑战（规则 = MODE_PRECISION）");
        if (prec >= 0) {
            paramsDefault(&p);
            p.balloonCount = 12;
            paramsApplyPreset(&p, prec);
            paramsClamp(&p);
            CHECK(p.balloonCount == 1, L"精准挑战把气球数强制成 1");
        }
    }

    /* 从"小快靶"（球小）切回"固定靶"（球大）时，半径必须真的涨回去 ——
       这是覆盖表顺序那个坑的回归测试：paramsClamp 里有"上限低于下限就
       互换"的规矩，而每写一项都过一遍它，所以 radiusMax 必须写在
       radiusMin 前面。写反了的话 0.40 会被夹成"下限"、0.18 变成上限，
       结果是从小快靶切回固定靶后球还是小的。*/
    {
        Params p0, small;
        int k = -1, kk;
        float smallest = 0.0f;
        paramsDefault(&p0);                       /* 预设 0 就是固定靶 */
        for (i = 1; i < PRESET_COUNT; ++i) {
            paramsDefault(&small);
            paramsApplyPreset(&small, i);
            /* 按 radiusMin 挑而不是 radiusMax：精准挑战的下限是 0.14（最小）
               但上限 0.18 之外还有别的预设上限也不大，用下限才真的挑得出
               "球最小的那套"。*/
            if (k < 0 || small.radiusMin < smallest) { k = i; smallest = small.radiusMin; }
        }
        CHECK(k >= 0, L"除第 0 套之外还有别的预设可选");
        CHECK(smallest < p0.radiusMin, L"存在一套球明显更小的预设（切过去才看得出差别）");
        if (k >= 0) {
            /* 一路从「球最小的那套」切回固定靶 —— 中途不留任何残值。*/
            paramsDefault(&small);
            paramsApplyPreset(&small, k);
            paramsClamp(&small);
            for (kk = 0; kk < 3; ++kk) {          /* 多切几次，夹取不幂等也会露馅 */
                paramsApplyPreset(&small, 0);
                paramsClamp(&small);
            }
            CHECK(feq(small.radiusMin, p0.radiusMin) && feq(small.radiusMax, p0.radiusMax),
                  L"从球最小的预设切回固定靶，半径真的涨了回去");
            CHECK(paramsEqual(&small, &p0),
                  L"切回固定靶之后参数与「刚进游戏时」逐字节相同");
        }
    }
}

static void tConfig(void) {
    Params p, q;
    group(L"参数表");
    paramsDefault(&p);

    CHECK(p.balloonCount == 8, L"默认气球数 = 8");
    CHECK(p.refillDelaySec == 0.0f, L"默认补位延迟 = 0（打掉立刻补）");
    CHECK(p.lifetimeSec == 0.0f, L"默认不限时（默认模式是恒量靶场）");
    CHECK(p.mode == MODE_RANGE, L"默认模式 = 恒量靶场");
    CHECK(p.hitForgive >= 1.0f, L"默认判定半径不严于视觉半径");
    CHECK(p.comboWindowSec > 0.0f, L"默认连击窗口为正");
    CHECK(p.showStats == 1, L"默认显示统计面板");
    /* 准星颜色从三个分量滑杆合并成一个枚举（滑杆太多调不明白）。
       默认仍是绿 —— 与整套界面同色系，也是准星在浅灰墙上最清楚的颜色。*/
    CHECK(p.crossColorIdx == CROSSCOL_GREEN, L"默认准星是绿色（与整套界面同色系）");
    {
        Color3 cc = crossColorOf(p.crossColorIdx);
        CHECK(cc.g > cc.r, L"默认准星颜色真的偏绿");
    }
    CHECK(p.typeWeight[BALLOON_NORMAL] > 0, L"默认普通球权重非零");

    q = p;
    CHECK(paramsEqual(&p, &q), L"同参数结构体逐字节相等");
    CHECK(paramsHash(&p) == paramsHash(&q), L"同参数指纹相同");

    q.balloonCount = 9;
    CHECK(paramsHash(&p) != paramsHash(&q), L"改动参数后指纹必变");
    CHECK(!paramsEqual(&p, &q), L"改动后 paramsEqual 为假");
    q = p; q.mouseSens += 0.0001f;
    CHECK(paramsHash(&p) != paramsHash(&q), L"极小的浮点改动也逃不过指纹");

    /* ---------------------------------------------------------- 夹取 */
    q = p;
    q.balloonCount = 9999;
    q.radiusMin = 9.0f;
    q.radiusMax = -1.0f;
    q.fovDeg = 500.0f;
    q.mouseSens = -5.0f;
    q.crossColorIdx = 999;
    q.preset = -7;
    q.ramp = 1e9f;
    q.lives = 0;
    q.timeLimitSec = 1.0f;
    paramsClamp(&q);
    CHECK(q.balloonCount <= MAX_BALLOONS, L"气球数被夹到池容量以内");
    CHECK(q.balloonCount >= 1, L"气球数至少为 1");
    CHECK(q.radiusMin <= q.radiusMax, L"半径上下限写反时被交换而不是报错");
    CHECK(q.fovDeg <= 110.0f && q.fovDeg >= 45.0f, L"FOV 被夹到 [45,110]");
    CHECK(q.mouseSens >= 0.02f, L"灵敏度不会变成 0 或负数");
    CHECK(q.crossColorIdx >= 0 && q.crossColorIdx < CROSSCOL_COUNT,
          L"准星颜色下标被夹进合法区间");
    CHECK(q.preset >= 0 && q.preset < PRESET_COUNT, L"预设下标被夹进合法区间");
    CHECK(q.ramp >= 0.0f && q.ramp <= DIFF_FACTOR_MAX, L"难度涨幅被夹进合法区间");
    CHECK(q.lives >= 1, L"生命数至少为 1");
    CHECK(q.timeLimitSec >= 10.0f, L"时限不低于 10 秒");

    /* 夹取必须是幂等的，否则读存档会越读越偏。*/
    {
        Params r = q;
        paramsClamp(&r);
        CHECK(paramsEqual(&q, &r), L"paramsClamp 是幂等的");
    }

    /* 精准挑战模式下"墙上恒为 1 个"是模式定义，不给用户改。*/
    q = p;
    q.mode = MODE_PRECISION;
    q.balloonCount = 17;
    paramsClamp(&q);
    CHECK(q.balloonCount == 1, L"精准挑战模式强制气球数 = 1");

    /* ---------------------------------------------------------- 难度曲线
       难度从一个三档枚举改成了每套预设自带的**涨幅 ramp**。
       下面这一组断言只盯曲线本身（与哪套预设无关），所以直接给 ramp 值。*/
    {
        const float kSlow = 0.25f;    /* 一个偏慢的涨幅 */
        const float kFast = 0.80f;    /* 一个偏快的涨幅 */

        CHECK(feq(difficultyFactor(kSlow, 0.0), 1.0f), L"t = 0 时难度倍率 = 1");
        CHECK(difficultyFactor(kSlow, 30.0) > difficultyFactor(kSlow, 0.0),
              L"难度随时间单调上升");
        CHECK(difficultyFactor(kFast, 30.0) > difficultyFactor(kSlow, 30.0),
              L"涨幅大的爬升比涨幅小的快");
        CHECK(feq(difficultyFactor(kSlow, 1e6), DIFF_FACTOR_MAX), L"难度倍率有上限");
        CHECK(difficultyFactor(kSlow, -100.0) >= 1.0f, L"负时间不会让难度低于 1");
        /* ramp = 0 就是"永远不涨" —— 八套预设有七套是这一档，它必须严格恒为 1。*/
        CHECK(feq(difficultyFactor(0.0f, 0.0), 1.0f), L"涨幅为 0 时起始倍率 = 1");
        CHECK(feq(difficultyFactor(0.0f, 9999.0), 1.0f), L"涨幅为 0 时永远不涨");
        {
            int i, mono = 1;
            float prev = -1.0f;
            for (i = 0; i <= 400; ++i) {
                float f = difficultyFactor(kFast, (double)i * 0.9);
                if (f < prev - 1e-5f) { mono = 0; break; }
                prev = f;
            }
            CHECK(mono, L"难度曲线在 0..360 秒上单调不减");
        }
        /* 三个派生量的方向要一致：更快、更短命、更小。*/
        CHECK(diffSpeedScale(kSlow, 60.0) > diffSpeedScale(kSlow, 0.0),
              L"爬升后漂移更快");
        CHECK(diffLifetimeScale(kSlow, 60.0) < diffLifetimeScale(kSlow, 0.0),
              L"爬升后气球更短命");
        CHECK(diffRadiusScale(kSlow, 60.0) < diffRadiusScale(kSlow, 0.0),
              L"爬升后气球更小");
        /* 起始时刻两者都是 1.0（还没开始爬），要比就得比爬过一段之后的。*/
        CHECK(feq(diffRadiusScale(kSlow, 0.0), diffRadiusScale(kFast, 0.0)),
              L"起始时刻：涨幅大小还不影响球的尺寸");
        CHECK(diffRadiusScale(kSlow, 60.0) > diffRadiusScale(kFast, 60.0),
              L"爬过一分钟之后，涨幅小的那套球更大");
    }
    /* 不限时的预设，难度怎么爬都不该爬出一个时限来。*/
    {
        Params t2;
        paramsDefault(&t2);
        t2.lifetimeSec = 0.0f;
        t2.ramp = 0.80f;
        CHECK(effectiveLifetime(&t2, 300.0) == 0.0f,
              L"不限时预设下难度爬升不会凭空造出时限");
    }
    {
        Params t2;
        paramsDefault(&t2);
        t2.lifetimeSec = 5.0f;
        t2.ramp = 0.80f;
        CHECK(effectiveLifetime(&t2, 300.0) < 5.0f &&
              effectiveLifetime(&t2, 300.0) > 0.0f,
              L"限时预设下爬升把时限压短，但不会压到 0 或负数");
    }

    /* ---------------------------------------------------------- 计分换算
       默认预设是"大小全部相同"，也就是 radiusMin == radiusMax ——
       这个退化情形以前返回区间下端（14 像素 = 基础分满分 40），于是固定靶
       白送最高分；改成了返回区间中点。下面第一条就是那个改动的回归测试。*/
    {
        Params s;
        const float kMid = 0.5f * (SCORE_REF_R_MIN + SCORE_REF_R_MAX);

        paramsDefault(&s);
        CHECK(feq(s.radiusMin, s.radiusMax), L"默认预设确实是同一个半径（大小相同）");
        CHECK(feq(radiusToRefPixels(s.radiusMin, &s), kMid),
              L"半径上下限相同时取区间中点，而不是白送上限");

        /* 非退化的情形照旧。*/
        paramsDefault(&s);
        paramsApplyPreset(&s, 1);                 /* 移动靶：大小随机 */
        CHECK(s.radiusMax > s.radiusMin, L"第 1 套预设的半径是区间");
        CHECK(feq(radiusToRefPixels(s.radiusMin, &s), SCORE_REF_R_MIN),
              L"最小半径映射到 14 像素");
        CHECK(feq(radiusToRefPixels(s.radiusMax, &s), SCORE_REF_R_MAX),
              L"最大半径映射到 42 像素");
        CHECK(feq(radiusToRefPixels(0.0f, &s), SCORE_REF_R_MIN),
              L"过小的半径被夹到区间下端");
        CHECK(feq(radiusToRefPixels(99.0f, &s), SCORE_REF_R_MAX),
              L"过大的半径被夹到区间上端");
        CHECK(radiusToRefPixels(0.5f, &s) > radiusToRefPixels(0.4f, &s),
              L"半径越大，映射出的像素半径越大");

        /* 抓一个"差一丁点"的退化情形：上下限之差小于 RADIUS_SAME_EPS 时
           也走中点分支，不能除零、不能翻出区间外。*/
        s.radiusMin = 0.60f;
        s.radiusMax = 0.60f + RADIUS_SAME_EPS * 0.5f;
        {
            float px = radiusToRefPixels(0.6f, &s);
            CHECK(px >= SCORE_REF_R_MIN - 1e-3f && px <= SCORE_REF_R_MAX + 1e-3f,
                  L"半径几乎相同时的结果仍在 14..42 之间");
        }
    }

    /* ---------------------------------------------------------- 持久化 */
    {
        wchar_t path[MAX_PATH];
        Params r;
        GetTempPathW(MAX_PATH, path);
        wcscat(path, L"balloon_range_selftest.dat");

        q = p;
        q.balloonCount = 13;
        q.mouseSens = 0.33f;
        q.mode = MODE_PROGRESS;
        q.wallStyle = WALL_BRICK;
        paramsClamp(&q);

        CHECK(paramsSave(&q, path) == 0, L"存档写入成功");
        paramsDefault(&r);
        CHECK(paramsLoad(&r, path) == 0, L"存档读回成功");
        CHECK(paramsEqual(&q, &r), L"存档往返后参数逐字节一致");

        /* 篡改文件：校验和必须拦住它，而不是读进一堆脏参数。*/
        {
            FILE *f = _wfopen(path, L"r+b");
            CHECK(f != NULL, L"能打开存档以便做篡改测试");
            if (f) {
                long pos = (long)(sizeof(uint32_t) * 4);   /* payload 第一个字节 */
                fseek(f, pos, SEEK_SET);
                fputc(0x5A, f);
                fclose(f);
            }
        }
        CHECK(paramsLoad(&r, path) != 0, L"被篡改的存档被拒绝（CRC 拦下）");

        /* 截断的文件也不能崩。*/
        {
            FILE *f = _wfopen(path, L"wb");
            if (f) { fwrite("BRS", 1, 3, f); fclose(f); }
        }
        CHECK(paramsLoad(&r, path) != 0, L"过短的存档被拒绝");

        _wremove(path);
        CHECK(paramsLoad(&r, path) != 0, L"文件不存在时返回失败而不是崩溃");
        CHECK(paramsSave(&q, L"Z:\\__no_such_dir__\\x.dat") != 0,
              L"写到不存在的目录时返回失败而不是崩溃");
    }
}

/* ============================================================ 参数描述表 */

/* 把一段字节揉进指纹。*/
static unsigned fpMix(unsigned h, const void *data, size_t n) {
    const unsigned char *d = (const unsigned char *)data;
    size_t i;
    for (i = 0; i < n; ++i) { h ^= d[i]; h *= 16777619u; }
    return h;
}

/* 气球池的指纹：把每个槽位的状态、类型、位置、半径、色相都揉进去。
   它就是"这个参数到底有没有接到气球系统上"这个问题的答案。*/
static unsigned poolFingerprint(const BalloonPool *bp, const Params *p) {
    unsigned h = 2166136261u;
    int i;
    for (i = 0; i < bp->n; ++i) {
        const Balloon *b = &bp->slots[i];
        Color3 c = balloonColor(b, p);
        float v[8];
        int   iv[2];
        v[0] = b->x;    v[1] = b->y;     v[2] = b->r;     v[3] = b->hue;
        v[4] = b->vx;   v[5] = b->life;  v[6] = b->timer; v[7] = b->baseR;
        iv[0] = b->state; iv[1] = b->type;
        h = fpMix(h, v,  sizeof(v));
        h = fpMix(h, iv, sizeof(iv));
        h = fpMix(h, &c, sizeof(c));
    }
    { int n = bp->n; h = fpMix(h, &n, sizeof(n)); }
    return h;
}

/* 池探针：固定种子建池、跑满一秒，**沿途一直采样**，把整段时间的指纹串起来。
   只取"刚填满"和"跑完"两个端点是不够的：出现动画（spawnAnimSec）在这两个
   时刻都已经结束了，端点相等而中间不同 —— 那样一个真参数会被误判成死的。
   每 4 帧采一次（约 30 次），够密又几乎不花时间。*/
/* 参数活性探针的**共同基线**：一套"每一项都有机会表现自己"的参数。
 *
 * 直接拿 paramsDefault()（也就是"固定靶"那套预设）当基线是错的 ——
 * 固定靶刻意把球钉死（motion = 静止、漂移速度 0、浮动幅度 0）、只出一种
 * 普通球（allowSpecial = 0），于是"漂移幅度""浮动频率""四种球权重"这些项
 * **本来就该不起作用**。拿它当基线，断言会把这一整批好端端的参数判成死参数。
 *
 * 探针要回答的问题是"这一项接在系统上没有"，与它默认值是多少无关，所以
 * 基线必须是每一项都能发挥作用的那一档。这个基线只在自检里用，不进游戏。*/
static void paramsProbeBase(Params *p) {
    paramsDefault(p);
    p->motion       = MOVE_LINEAR;    /* 让漂移速度/幅度有地方使劲 */
    p->driftSpeed   = 1.20f;
    p->driftRange   = 1.00f;
    p->bobAmp       = 0.05f;          /* 让浮动幅度/频率有地方使劲 */
    p->bobFreq      = 0.60f;
    p->allowSpecial = 1;              /* 让四种球权重有地方使劲 */
    p->typeWeight[BALLOON_NORMAL] = 40;
    p->typeWeight[BALLOON_SMALL]  = 20;
    p->typeWeight[BALLOON_GOLD]   = 20;
    p->typeWeight[BALLOON_SLOW]   = 20;
    p->lifetimeSec  = 4.0f;           /* 让存活时限在池里看得出来 */
    p->radiusMin    = 0.20f;          /* 半径上下限分开，两项才各自可测 */
    p->radiusMax    = 0.60f;
    p->balloonCount = 8;
    paramsClamp(p);
}

static unsigned poolProbe(const Params *p) {
    BalloonPool pool;
    FieldRect f;
    unsigned h;
    int i;
    sceneComputeField(p, &f);
    balloonPoolInit(&pool, p, 20260930u, &f);
    balloonPoolFill(&pool, p, &f, 0.0);
    h = poolFingerprint(&pool, p);
    for (i = 0; i < (int)(1.0f / FIXED_DT); ++i) {
        balloonUpdate(&pool, p, &f, 1.0, (float)FIXED_DT);
        if (i % 4 == 3) h = h * 31u + poolFingerprint(&pool, p);
    }
    h = h * 31u + poolFingerprint(&pool, p);
    return h;
}

/* 击破探针：只有"打掉一个"之后才看得出来的参数（补位延迟）走这条。
   返回补位延迟有没有让"墙上短暂少一个"这件事真的发生。*/
static int refillProbe(const Params *p) {
    BalloonPool pool;
    FieldRect f;
    int i, idx = -1, sawGap = 0;
    float r = 0.0f;
    int t = 0;
    sceneComputeField(p, &f);
    balloonPoolInit(&pool, p, 20260930u, &f);
    balloonPoolFill(&pool, p, &f, 0.0);
    /* 先让球长出来（出现动画没过半是不给打的）。*/
    for (i = 0; i < 60; ++i) balloonUpdate(&pool, p, &f, 0.0, (float)FIXED_DT);
    for (i = 0; i < pool.n; ++i)
        if (pool.slots[i].state == BSLOT_LIVE) { idx = i; break; }
    if (idx < 0) return 0;
    if (!balloonKill(&pool, p, &f, 0.0, idx, &r, &t)) return 0;
    /* 击破后的头几帧里，"墙上占着位置的球数"应当少一个。*/
    for (i = 0; i < 8; ++i) {
        balloonUpdate(&pool, p, &f, 0.0, (float)FIXED_DT);
        if (balloonActiveCount(&pool) == pool.n - 1) sawGap = 1;
    }
    /* 再往后必须补回来 —— 这是硬要求。*/
    for (i = 0; i < (int)(1.5f / FIXED_DT); ++i)
        balloonUpdate(&pool, p, &f, 0.0, (float)FIXED_DT);
    return sawGap && balloonActiveCount(&pool) == pool.n;
}

/* 给一项挑一个"确实与默认值不同"的新值，写进 dst。
   注意不能拿中段值直接跟默认值比：布尔与枚举要四舍五入（0.5 会变成 1），
   中段看着不同、写进去却跟默认值一样。所以直接试三次，看哪个真把结构体
   改动了。三次都改不动说明这一项被别处的规则锁死了，返回 0。*/
static int paramNudge(Params *dst, const Params *base, const ParamDesc *d) {
    float tries[3];
    int i;
    tries[0] = (d->lo + d->hi) * 0.5f;
    tries[1] = d->lo;
    tries[2] = d->hi;
    for (i = 0; i < 3; ++i) {
        *dst = *base;
        paramDescSet(dst, d, tries[i]);
        if (!paramsEqual(dst, base)) return 1;
    }
    return 0;
}

/* 模拟"打开面板 → 套一套预设 → 面板收尾"这一串动作。预设一套上就得把
   整套玩法参数一起换掉，否则切到渐进训练却仍然"球不会跑"，那是个假预设。
   面板上改完其它项之后也是这么收尾的，所以探针必须走同一条路。

   早先这里调的是 paramsApplyModeDefaults(p, p->mode) —— 那时
   "模式"是一张只写规矩的默认值表，现在是每套预设自带的一整串覆盖项。*/
static void paramPrepare(Params *p) {
    paramsApplyPreset(p, p->preset);
    paramsClamp(p);
}

/* app 层探针：由 app.cpp 消费、池里看不出来的参数（种子、漏球扣命、
 * 连射间隔、模式、难度）走这条。appStep 不碰 GL，所以这里可以不开窗口
 * 直接跑。传进来的参数里 seed 必须非 0，否则每局种子取的是时间，比不了；
 * 而且**模式默认值要由调用方先调 paramPrepare 落定**，这样"先定模式、
 * 再覆盖单项"的顺序与面板一致。*/
static unsigned appProbe(Params p, int frames, int holdTrigger) {
    static App a;
    unsigned h;
    int i;
    memset(&a, 0, sizeof(a));
    a.params = p;
    appResetSession(&a, 0);          /* 0 → 用参数里的种子，保证可复现 */
    if (holdTrigger) a.input.mdown[0] = 1;
    h = 2166136261u;
    for (i = 0; i < frames; ++i) {
        appStep(&a, (float)FIXED_DT);
        if (i % 8 == 7) h = h * 31u + poolFingerprint(&a.pool, &a.params);
    }
    h = h * 31u + poolFingerprint(&a.pool, &a.params);
    {
        float v[6];
        v[0] = (float)a.lives;   v[1] = (float)a.combo;
        v[2] = (float)a.missed;  v[3] = (float)a.shots;
        v[4] = a.timeLeft;       v[5] = (float)a.elapsed;
        h = fpMix(h, v, sizeof(v));
    }
    return h;
}

/* 这一组是冲着"假滑杆"来的 —— 以前踩过的坑：面板上一个滑杆拖得动、
   数字也在变，但那个参数根本没接到任何东西上，拖到哪儿都一样。
   光读代码看不出来，只有"拨一下、算指纹、看变不变"才能逮住。

   探针分三档，覆盖哪一档是按"这个参数在哪一层被消费"定的：
     ① 池探针   —— 气球 / 运动 / 节奏里直接影响气球状态的那些；
     ② 击破探针 —— 只在击破之后才显形的那一个（补位延迟）；
     ③ app 探针 —— 由 app.cpp 消费的（种子、漏球扣命、连射间隔、模式）。
   三档的条数加起来必须等于描述表总行数，另有一张"渲染层"名单是
   靠截图矩阵验的。少配一个探针，下面的账目断言会报出来。*/
/* ============================================================ 面板滚动
 *
 * 这一组是**为了一个真发生过的 bug 补的**：PgUp / PgDn 一度不能翻页，
 * 查下来根因是 appParamScrollIntoView() 写好了却从没在输入路径上调用过 ——
 * 于是滚动窗口恒为 0，选中行走出第一屏之后屏幕上什么都不动了。
 * 当时已有近千项断言，没有一条覆盖这里，所以全绿也拦不住它。
 *
 * ★ 那个键（PgUp / PgDn）本身已删除，
 * 但这一组**留着**，而且②③两条整个反写成"按下去必须什么都不动"。
 * 理由是这一组真正盯着的东西从来不是那两个键，而是"**选中行走出视野时
 * 窗口会不会跟着走**"——那条纪律现在由 ↑↓（①⑤ 两条）守着，翻页只是当年
 * 撞出这个 bug 的那个动作。把键删掉就顺手把这段删掉，等于把这块地重新
 * 交还给沉默。
 *
 * 断言直接驱动**真正的输入处理函数** appParamInput，而不是另写一份
 * 模拟逻辑 —— 那样只会测到一份照猫画虎的假货。
 */
static void tPanelScroll(void) {
    static App a;
    Input *in;
    int rows, i, h;
    /* 四种窗口高度：很矮（可见行数被压到下限）、默认、高、非常高。*/
    const int heights[4] = { 560, 900, 1440, 2160 };

    group(L"参数面板：滚动");

    for (h = 0; h < 4; ++h) {
        const int rowCount = appParamPageRowCount(0);
        memset(&a, 0, sizeof(a));
        a.winH = heights[h];
        a.winW = 1600;
        a.paramOpen = 1;
        a.paramPage = 0;            /* ★ 行号是"页内行号"，这一组测预设页 */
        in = &a.input;
        rows = paramPanelRows(&a);
        CHECK(rows >= 3, L"可见行数至少 3 行");
        /* 列表没有"级"了，所以这里也不用再显式进二级。
           但要**断言一下列表确实不止参数那么多行**，否则下面那些拿
           rowCount 当上限的循环会和旧写法一样、把第 0 行漏在外面测不到。
           ★ 加了菜单栏、面板分成两页之后，行数也得分页算：
           "多出来的那一行"只有预设页才有（偏好页上没有「当前预设方案」）。*/
        {
            int nPlay = 0;
            for (i = 0; i < g_paramDescCount; ++i)
                if (!paramIsPreference(i)) ++nPlay;
            CHECK(rowCount == nPlay + 1,
                  L"预设页列表行数 = 玩法参数项数 + 1（多出来的是顶上「当前预设方案」那一行）");
        }

        /* ---- ① 无论选中哪一行，它都必须落在可见窗口里 ----
           这是那个 bug 的判据本身。旧代码下 paramSel >= rows 时全都会失败。
           ★ 上限从 g_paramDescCount 改成了 rowCount：第 0 行也是
           一行，它同样得能被选中、同样得滚进视野。
           ★ 可见行数**随 paramTop 变**（组标题跟着行滚，占的高度
           不一样），所以判据里的行数必须按**当时的** paramTop 现算，
           不能沿用循环外面那个"top=0 时"的 rows —— 那正是老毛病
           "窗口认的行数和画出来的行数不是一个数"在断言里的翻版。*/
        {
            int bad = 0;
            for (i = 0; i < rowCount; ++i) {
                int fitNow;
                a.paramSel = i;
                a.paramTop = 0;             /* 故意把窗口摆错，看它会不会自己纠正 */
                appParamInput(&a);
                fitNow = paramPanelFit(&a, a.paramTop, NULL, NULL);
                if (a.paramTop > a.paramSel || a.paramSel >= a.paramTop + fitNow) ++bad;
            }
            CHECK_NAMED(bad == 0, L"任意一行被选中时，它都在可见窗口内",
                        (h == 1) ? L"默认 900" : L"其它高度");
        }

        /* ---- ② PgUp / PgDn 已经**删掉**：按下去必须一帧都不动 ----
           ★ PgUp / PgDn 这两个键已删除（它们会打乱格式），
           于是这里从"它真的会翻页"整个反写成"它什么都不做"。

           为什么反写而不是删掉这一段：这一组当年是给一个**真发生过的** bug
           补的（appParamScrollIntoView 写好了却从没被调用），反着留一条，
           谁哪天把翻页接回来、或只删了一半（比如历史屏那条分支没删），
           这里就会红。删掉这段等于把这块地重新交还给沉默。

           两个起始位置各按一遍，不然"没动"可以被"本来就在边上、没得可动"
           蒙混过去；PgUp 与 PgDn 都要走一遍，只删一个的漏法才拦得住。
           三个状态量一起比（选中行、窗口起点、窗口起点在块内的偏移）——
           只比 selected 的话，"行没动但窗口偷偷跳了"照样绿。*/
        {
            const int pgs[2] = { VK_NEXT, VK_PRIOR };
            const int starts[2] = { 0, rowCount / 2 };
            int pi, pk, bad = 0;
            for (pi = 0; pi < 2; ++pi) {
                for (pk = 0; pk < 2; ++pk) {
                    int sel0, top0, skip0;
                    a.paramOnMenu = 0;
                    a.paramSel = starts[pi];
                    a.paramTop = 0;
                    a.paramTopSkip = 0;
                    appParamScrollIntoView(&a);
                    sel0 = a.paramSel; top0 = a.paramTop; skip0 = a.paramTopSkip;

                    memset(in, 0, sizeof(*in));
                    in->pressed[pgs[pk]] = 1;
                    appParamInput(&a);
                    if (a.paramSel != sel0 || a.paramTop != top0 ||
                        a.paramTopSkip != skip0) ++bad;
                }
            }
            CHECK_NAMED(bad == 0,
                        L"★ 按 PgUp / PgDn 什么都不动（这两个键已删除）",
                        (h == 1) ? L"默认 900" : L"其它高度");
        }

        /* ---- ③ 别处也不能有副作用 ----
           上一条只比了三个"位置"量。"删了一半"还有一种漏法：PgUp / PgDn
           在别处还挂在**别的动作**上（打开弹窗、翻页、写一句底部提示、
           甚至是当初"翻页"整段留下的残枝）。所以这一条把面板里所有会变的
           状态一起快照前后比对 —— 按下去之后，整个面板必须**逐字节**一致。*/
        {
            Params p0;
            int pop0, msg0, menu0, page0, sel0, top0, skip0;
            int bad;

            a.paramOnMenu = 0;
            a.paramSel = rowCount / 2;
            a.paramTop = 0;
            a.paramTopSkip = 0;
            appParamScrollIntoView(&a);
            p0    = a.params;
            pop0  = a.paramPopOpen;
            msg0  = (a.paramMsgT > 0.0f);
            menu0 = a.paramOnMenu;
            page0 = a.paramPage;
            sel0  = a.paramSel;
            top0  = a.paramTop;
            skip0 = a.paramTopSkip;

            memset(in, 0, sizeof(*in));
            in->pressed[VK_NEXT] = 1;   appParamInput(&a);
            memset(in, 0, sizeof(*in));
            in->pressed[VK_PRIOR] = 1;  appParamInput(&a);

            bad = !paramsEqual(&a.params, &p0) ||
                  a.paramPopOpen != pop0 ||
                  ((a.paramMsgT > 0.0f) != msg0) ||
                  a.paramOnMenu != menu0 ||
                  a.paramPage != page0 ||
                  a.paramSel != sel0 ||
                  a.paramTop != top0 ||
                  a.paramTopSkip != skip0;
            CHECK_NAMED(!bad, L"★ PgUp / PgDn 不带任何副作用（参数没被改、没开弹窗、没写提示）",
                        (h == 1) ? L"默认 900" : L"其它高度");
        }

        /* ---- ④ End / Home ----
           ★ Home 的落点改过：现在要能进入菜单栏，
           所以 Home 是"一跳到面板最顶端"，而菜单栏就在列表之上。
           它不再落在第 0 行上 —— 落在第 0 行的路子是"从菜单栏按 ↓"
           （也是打开面板之后的默认路径）。*/
        memset(in, 0, sizeof(*in));
        in->pressed[VK_END] = 1;
        appParamInput(&a);
        CHECK(a.paramSel == rowCount - 1, L"End 跳到最后一行的**选中**");
        CHECK(a.paramTop <= a.paramSel &&
              a.paramSel < a.paramTop + paramPanelFit(&a, a.paramTop, NULL, NULL),
              L"End 之后最后一行可见（窗口必须滚到底）");
        memset(in, 0, sizeof(*in));
        a.paramOnMenu = 0;
        in->down[VK_HOME] = 1;
        in->pressed[VK_HOME] = 1;
        appParamInput(&a);
        CHECK(a.paramOnMenu, L"★ Home 直接进菜单栏");
        CHECK(a.paramSel == 0, L"Home 顺手把列表行号也归零（「回到最顶上」的字面含义）");
        memset(in, 0, sizeof(*in));
        /* down 必须一起给：菜单栏里的 ↓ 走 inputKeyRepeat，而它第一句就是
           if (!in->down[vk]) return 0; —— 真实按键永远不会只有 pressed。*/
        in->down[VK_DOWN] = 1;
        in->pressed[VK_DOWN] = 1;           /* 从菜单栏回列表 */
        appParamInput(&a);
        CHECK(!a.paramOnMenu && a.paramSel == 0,
              L"↓ 回到列表，落在第 0 行（而不是 Home 之前那一行）");

        /* ---- ⑤ 一格格往下走：光标始终可见，窗口底边不越过表尾 ----
           推出去了就会看见一片空白，而用户会以为"面板坏了"。

           ★ 这里换过写法。旧写法是"直接往 paramTop 里塞越界值，看它会不会
           自己收回来"；现在滚动窗口是"光标走到哪儿、窗口跟到哪儿"（不许提前
           滚动），硬塞 paramTop 会被函数当成"窗口跑到了光标上面"
           而立刻拉回去，等于什么都没测。改成**真的驱动光标一行一行走到底**，
           这才是真实的用法，也才测得到新的滚动口径。*/
        {
            int bad = 0, moved = 0, prevTop;
            memset(in, 0, sizeof(*in));
            a.paramOnMenu = 0;
            a.paramSel = 0;
            a.paramTop = 0;
            appParamScrollIntoView(&a);
            prevTop = a.paramTop;
            for (i = 0; i < rowCount + 5; ++i) {
                int fitNow;
                memset(in, 0, sizeof(*in));
                in->pressed[VK_DOWN] = 1;
                in->down[VK_DOWN] = 1;
                appParamInput(&a);
                fitNow = paramPanelFit(&a, a.paramTop, NULL, NULL);
                if (a.paramTop > a.paramSel || a.paramSel >= a.paramTop + fitNow) ++bad;
                if (a.paramTop + fitNow > rowCount) ++bad;
                if (a.paramTop != prevTop) { ++moved; prevTop = a.paramTop; }
            }
            CHECK(bad == 0,
                  L"一行一行走到底：光标始终在窗口里，窗口底边也没越过表尾");
            CHECK(a.paramSel == rowCount - 1, L"一直按 ↓ 会停在最后一行（不绕圈到顶）");
            CHECK(moved > 0 || rows >= rowCount,
                  L"（前置）列表放不下时这一路真的滚动过（否则上面两条是空断言）");
        }

        /* ---- ⑥ 可见行数与窗口高度无关 ----
           这不是巧合，是构造出来的：可见行数的判据
             listH = H − (2×40 + 64 + 40 + 104)·u、格高 30u
           里每一项都正比于 u = H/900，比值一约就掉 —— 所以四种高度的
           "可见几格"本来就应该一模一样（行是格拼的，格数一样行数就一样）。
           真出现差异，说明有人往几何里混进了不随 u 缩放的常数，那正是小
           窗口下"最后一行被页脚盖住"这类错的老配方。
           ★ 格高**统一**成行高了（组标题条不再另占一个 20u 的常量），
           这条断言因此比早先更硬：那时组标题与行高不等，格与行的换算
           还要按组数分别算，现在一格就是一行高，数格与数行是一回事。*/
        {
            static App b;
            int base;
            memset(&b, 0, sizeof(b));
            b.winW = 1600;
            b.winH = 900;
            b.paramPage = 0;
            base = paramPanelFit(&b, 0, NULL, NULL);
            CHECK_NAMED(base == paramPanelFit(&a, 0, NULL, NULL),
                        L"可见行数与窗口高度无关（几何全按 u 缩放）",
                        (h == 1) ? L"默认 900" : L"其它高度");
            CHECK_NAMED(base == rows,
                        L"（对照）它就是循环开头那个 rows", L"");
        }

        /* "滚动稳不稳"那一笔账单独成组：tPanelScrollStable()。
           原来这里摊着一块 printf 台账，是量它用的；量完了把它变成断言，
           那条断言才是**本该拦住这个 bug** 的东西 —— 当时自检近 3000 条
           全绿，光标却在屏幕上上下乱跳。*/
    }
}

/* 光标行的**内容**落在窗口的第几格上（相对窗口起点）。与 render.cpp 的绘制
   同源：窗口起点 > 0 时顶上那一格是 ︿ 的位子，起点为 0 时那一格归第一行。*/
static float cursorSlotY(const App *app, int sel) {
    const int cur  = paramItemIndexOfRow(app, sel) + paramRowSpans(app, sel) - 1;
    const int w0   = paramWindowStartItem(app);
    const int off  = (w0 > 0) ? 1 : 0;
    return (float)(off + cur - w0);
}

/* 把窗口暂时摆到 (top, skip) 上，问一句"光标行 sel 看得见吗"，问完还原。
   —— 用来判"这一步到底该不该滚"：不该滚而滚了，就是提前滚动。*/
static int cursorVisibleAt(App *app, int sel, int top, int skip) {
    const int saveT = app->paramTop, saveS = app->paramTopSkip;
    int fit, ok;

    app->paramTop = top;
    app->paramTopSkip = skip;
    fit = paramPanelFit(app, app->paramTop, NULL, NULL);
    ok = (sel >= app->paramTop) && (sel < app->paramTop + fit);
    app->paramTop = saveT;
    app->paramTopSkip = saveS;
    return ok;
}

/* ================================================= 面板滚动：光标不许上下乱动
 *
 * ★★ 滚动时栏目一度不稳定，光标会上下乱动。
 * 这一组就是"光标选项不许上下乱动"的可判定版本。
 *
 * 为什么值得单独成组：此前近 3000 条断言**全绿**，光标照样在屏幕上跳 ——
 * 因为它们查的全是"状态对不对"（光标在窗口里吗、窗口越界了吗），没有一条查
 * "同一个光标，按一下 ↓，它在屏幕上的 y 会不会往上走"。屏幕坐标必须自己算出来。
 *
 * 屏幕 y 的口径与 render.cpp 的绘制**同源**，都以"格"为单位（一格 = 一行高）：
 *     y = （窗口起点在第 0 格 ? 0 : 1） + 光标内容那一格 − 窗口起点格
 * 顶上那一格：窗口有滚动时（起点 > 0）它是 ︿ 的位子；起点为 0 时那一格归第一行。
 *
 * 四条：
 *   ① 按 ↓ 一格，光标的 y 绝不许往上；按 ↑ 一格，绝不许往下；
 *   ② 每步只许走整一格或原地不动 —— 出现"半格"就说明有人把像素和格混着算；
 *   ③ 窗口只在"不滚就看不见光标"时才许滚。光标本来就看得见时窗口动了，
 *      就是不该出现的"提前滚动"；
 *   ④ 每一步光标都在窗口里（含它头顶那条组标题条所占的那一格）。
 */
static void tPanelScrollStable(void) {
    static App a;
    const int pages[2]   = { 0, 1 };
    const int heights[4] = { 560, 900, 1440, 2160 };
    int p, h, sel;
    int badUp = 0, badStep = 0, badEarly = 0, hidden = 0;

    group(L"参数面板：滚动稳定性（光标不许上下乱动）");

    for (p = 0; p < 2; ++p) {
        for (h = 0; h < 4; ++h) {
            const int total = appParamPageRowCount(pages[p]);
            float prev;

            if (total <= 1) continue;
            memset(&a, 0, sizeof(a));
            a.winW = 1600;
            a.winH = heights[h];
            a.paramOpen = 1;
            a.paramPage = pages[p];
            a.paramOnMenu = 0;          /* 光标在列表里，不是停在菜单栏上 */
            a.paramSel = 0;
            a.paramTop = 0;
            a.paramTopSkip = 0;
            appParamScrollIntoView(&a);

            /* ---- 从第一项一路按 ↓ 到底 ---- */
            prev = cursorSlotY(&a, a.paramSel);
            for (sel = 1; sel < total; ++sel) {
                const int topBefore = a.paramTop, skipBefore = a.paramTopSkip;
                const int seenBefore = cursorVisibleAt(&a, sel, topBefore, skipBefore);
                float now, step;

                a.paramSel = sel;
                appParamScrollIntoView(&a);
                now  = cursorSlotY(&a, a.paramSel);
                step = now - prev;

                if (step < -0.001f)                  ++badUp;   /* 往下按却往上跑 */
                if (step > 2.001f)                   ++badStep; /* 一步跨了不止两格 */
                if (step > 0.001f && step < 0.999f)  ++badStep; /* 半格：像素与格混算了 */
                /* 上限是 2 格不是 1 格：光标下面一行如果正好是新一组的头，那条
                   组标题条会跟着挤进窗口，光标连同它一起往下走两格。这是**对的**
                   （组标题条占一个真实选项位），不算抖。*/
                if (seenBefore &&
                    (a.paramTop != topBefore || a.paramTopSkip != skipBefore))
                    ++badEarly;                                  /* 看得见还滚 */
                if (!cursorVisibleAt(&a, a.paramSel, a.paramTop, a.paramTopSkip))
                    ++hidden;
                prev = now;
            }

            /* ---- 再从最后一项一路按 ↑ 回到头 ---- */
            for (sel = total - 2; sel >= 0; --sel) {
                float now, step;

                a.paramSel = sel;
                appParamScrollIntoView(&a);
                now  = cursorSlotY(&a, a.paramSel);
                step = now - prev;

                if (step > 0.001f)                    ++badUp;   /* 往上按却往下跑 */
                if (step < -2.001f)                   ++badStep;
                if (step < -0.001f && step > -0.999f) ++badStep;
                if (!cursorVisibleAt(&a, a.paramSel, a.paramTop, a.paramTopSkip))
                    ++hidden;
                prev = now;
            }
        }
    }

    CHECK(badUp == 0, L"★ 一路按 ↓ 光标不许往上跑、一路按 ↑ 不许往下跑（两页、四种窗口高度）");
    CHECK(badStep == 0, L"★ 每按一下就整整齐齐走一格（最多两格，跨组时带上组标题条），不许走半格");
    CHECK(badEarly == 0, L"★ 光标本来就看得见时窗口一格都不动 —— 不提前滚动");
    CHECK(hidden == 0, L"★ 每一步光标都在窗口里（含它头顶那条组标题条那一格）");
}

/* ============================================================ 长按重复
 *
 * 长按上下键要能快速切换选项。原先的代码只做了丢弃系统自动重复
 * 那一半（那一半是对的：按着不放不该疯狂翻页），没做自己排节拍这一半，
 * 于是按住 = 一点反应都没有。这一组盯的就是新加的那一半。
 */
/* 往面板里按一个键，然后清掉输入 —— 一条断言一次按键，互不干扰。
 *
 * ★ 这里曾经有个第三参数 down（"要不要按住"），后来把它删掉了。
 *   原因：WM_KEYDOWN 在真实输入里**必然同时**置上 down 与 pressed，所以
 *   "只有 pressed、没有 down"这种输入在真实世界中不存在。而
 *   inputKeyRepeat 第一句就是 if (!in->down[vk]) return 0;，于是传
 *   down=0 去按方向键根本不是"轻敲一下"，而是**什么都没按**。
 *   这里栽过跟头：菜单栏里的 ↓ 走 inputKeyRepeat，用例传了 0，
 *   于是"从菜单栏回列表"这一条静悄悄没生效，紧接着它后面那一整串
 *   （回车拉弹窗、弹窗里选预设…）全跟着失败 —— 一次调用点写错，
 *   报出来的是 24 项失败。参数删掉，这条路就没有第二种走法了。*/
static void panelKey(App *a, int vk) {
    memset(&a->input, 0, sizeof(a->input));
    a->input.down[vk] = 1;
    a->input.pressed[vk] = 1;
    appParamInput(a);
    memset(&a->input, 0, sizeof(a->input));
}

/* ================================================= 箭头的中段与末段
 *
 * 向下的那个箭头被分成了**两段**：
 *
 *   中段  "先预留一个选项位给箭头，当光标一直选到箭头上面一个选项时，
 *          此时再按下键就整体滚动一项，箭头保留"
 *   末段  "直到光标到达倒数第二项时，此时再按下键，不滚动，然后箭头
 *          替换成最后一项显示"
 *
 * ★ 这两条起初一条断言都没有（只有一张截图拿眼睛看过）。补成
 *   可判定的断言时，**末段那半条被推翻了**：
 *
 *     光标拉到底后，此时再按上键向上选择时，
 *     向下的箭头不要出现，只有向上到页面开始滚动时，再出现向下的箭头。
 *
 *   "滚到底之后箭头不许出现"与"光标到倒数第二项时箭头还在"在同一个状态上
 *   要求相反（﹀ 在场时容量 18 格：要让最后一项露面需要 start ≥ 17，要让 ﹀
 *   露面需要 start ≤ 15，同一个 start 到不了两处）。以新规则为准 ——
 *   旧断言整个删掉，换成新的，不留一条恒绿的空壳。
 *
 * 中段那条原样保留：它没有被推翻，而且它是**抓真 bug 的那条**
 * （"按 ↓ 光标却在屏幕上往上跑"）。
 */
static void tPanelArrowEnds(void) {
    static App a;
    const int pages[2]   = { 0, 1 };
    const int heights[4] = { 560, 900, 1440, 2160 };
    int p, h;
    int rollCase = 0, badRoll = 0, badRollSel = 0;            /* 中段 */
    int endCase  = 0, badEndScroll = 0, badEndArrow = 0;      /* 末段：到底那一刻 */
    int upCase   = 0, badUpStill = 0, badUpMove = 0;          /* 末段：从末行往回按 */

    group(L"参数面板：末段两段箭头规则");

    for (p = 0; p < 2; ++p) {
        for (h = 0; h < 4; ++h) {
            const int total = appParamPageRowCount(pages[p]);
            int sel;

            /* ---- 中段：光标停在 ﹀ 上面那一格，再按 ↓ 必须**整体滚动一格** ----
               "整体滚动"的可判定含义：列表在光标底下走，**光标的屏幕格位不动**。
               量它用的是与绘制同源的两个数（窗口起点格、光标内容所在格），
               不是这里另算的一份几何。

               ★ 别把它写成"窗口起点恰好 +1"：跨组时下一行的块是 2 格
               （组标题条 + 行），窗口就得走 2 格才跟得上光标。写成 +1 会
               把正确的行为判成错的 —— 第一次跑这一条就是这么红的，
               红的是断言不是代码。

               ★ 末段那一步（光标从倒数第二项走到最后一项）**不在这条里测**：
               那一步按规则就该"窗口不动、光标往下走进 ﹀ 那一格"，
               与这里的"整体滚动"是两条相反的规矩。第一次跑时两条
               混在一起，中段这条正是被末段那一步弄红的 —— 分开测才说得清。*/
            for (sel = 0; sel + 2 < total; ++sel) {
                int w0, cap, arrow, cur;
                float slot0;

                memset(&a, 0, sizeof(a));
                a.winW = 1600; a.winH = heights[h];
                a.paramOpen = 1; a.paramPage = pages[p];
                a.paramOnMenu = 0;
                a.paramSel = sel;
                a.paramTop = 0; a.paramTopSkip = 0;
                appParamScrollIntoView(&a);
                if (a.paramSel != sel) continue;        /* 摆不进去的行不参与 */
                if (!cursorVisibleAt(&a, sel, a.paramTop, a.paramTopSkip)) continue;

                w0   = paramWindowStartItem(&a);
                cap  = paramPanelItemCap(&a, a.paramTop);   /* ★ 格容量，不是行数 */
                arrow = 0;
                paramPanelFit(&a, a.paramTop, NULL, &arrow);
                cur  = paramItemIndexOfRow(&a, sel) + paramRowSpans(&a, sel) - 1;
                if (!arrow) continue;                   /* 下面没内容：没有 ﹀ 可谈 */
                if (cur - w0 != cap - 1) continue;      /* 光标不在 ﹀ 上面那一格 */

                slot0 = cursorSlotY(&a, sel);
                panelKey(&a, VK_DOWN);
                ++rollCase;
                if (a.paramSel != sel + 1) { ++badRollSel; continue; }
                if (paramWindowStartItem(&a) == w0) ++badRoll;      /* 一格都没动 */
                if (cursorSlotY(&a, a.paramSel) != slot0) ++badRoll;/* 光标在屏幕上跳了 */
            }

            /* ---- 末段：先一路按 ↓ 到底，再从最后一项
               一路按 ↑ 回头，量这两件事：

                 ① 光标到底之后，**只要窗口还没动，﹀ 就不许出现**；
                    窗口一动（真的向上滚了），﹀ 才回来。
                 ② 光标在最后一项时，窗口末尾正好压住表尾：既没有 ﹀，
                    底下也不留空行。

               ★ 这里原来是另一条规矩：「光标到倒数第二项时 ﹀ 还在，再按一下
               不滚动、箭头换成最后一项显示」。它作废了 ——
               不是没实现，是**与新规则数学上冲突**：
               ﹀ 在场时容量 18 格，要让最后一项（第 34 格）露面需要 start ≥ 17，
               要让 ﹀ 露面需要 start ≤ 15，同一个 start 到不了两处。
               以新规则为准，旧断言整个删掉，不留一条恒绿的空壳。*/
            {
                int arrowSeen = 0;
                memset(&a, 0, sizeof(a));
                a.winW = 1600; a.winH = heights[h];
                a.paramOpen = 1; a.paramPage = pages[p];
                a.paramOnMenu = 0;
                a.paramSel = 0;
                a.paramTop = 0; a.paramTopSkip = 0;
                appParamScrollIntoView(&a);
                for (sel = 1; sel < total; ++sel) {   /* 一路按 ↓ 到底 */
                    a.paramSel = sel;
                    appParamScrollIntoView(&a);
                }
                if (a.paramSel == total - 1) {
                    int arrow = 1, cap = 0, w0 = 0;

                    ++endCase;
                    paramPanelFit(&a, a.paramTop, NULL, &arrow);
                    if (arrow) ++badEndArrow;         /* 到底了还有 ﹀ → 红 */
                    w0 = paramWindowStartItem(&a);
                    cap = paramPanelItemCap(&a, a.paramTop);
                    /* 窗口末尾不许越过表尾 —— 越过去列表底下就空一行。
                       起点在顶部（w0 = 0）时不算数：偏好页只有 12 行、整个装
                       得下，那时"窗口比本页还高"是正常的（没有可越过的表尾）。*/
                    if (w0 > 0 && w0 + cap > paramItemCount(&a)) ++badEndScroll;

                    /* 再一路按 ↑ 回头，按新的规则盯住那个转折点。*/
                    for (sel = total - 2; sel >= 0; --sel) {
                        const int t0 = a.paramTop, s0 = a.paramTopSkip;
                        int arrow2 = 0;

                        a.paramSel = sel;
                        appParamScrollIntoView(&a);
                        paramPanelFit(&a, a.paramTop, NULL, &arrow2);
                        ++upCase;
                        if (arrow2) arrowSeen = 1;
                        /* 窗口没动（还没开始滚）却有 ﹀ → 红；
                           窗口动了之后 ﹀ 就该回来（本步或更早都算，记 arrowSeen）。*/
                        if (a.paramTop == t0 && a.paramTopSkip == s0) {
                            if (arrow2) ++badUpStill;
                        } else if (!arrowSeen && !arrow2) {
                            ++badUpMove;                  /* 滚了却还没有 ﹀ */
                        }
                    }
                }
            }
        }
    }

    /* 前提：这几段都得**真跑到过**，否则底下几条断言是空转。*/
    CHECK(rollCase > 0, L"（前提）中段确实出现过「光标停在 ﹀ 上面那一格」的情形");
    CHECK(endCase > 0, L"（前提）末段确实跑到过「光标在最后一项」那一刻");
    CHECK(upCase > 0, L"（前提）末段确实跑到过从末行往回按的每一步");

    CHECK(badRoll == 0,
          L"★ 中段：光标停在 ﹀ 上面一格时再按 ↓，列表整体滚动、光标的屏幕格位不动");
    CHECK(badRollSel == 0, L"★ 中段：那一下按键光标正好落到下一行");
    CHECK(badEndArrow == 0,
          L"★ 末段：光标拉到最后一项时 ﹀ **不画**（「向下的箭头不要出现」）");
    CHECK(badEndScroll == 0,
          L"★ 末段：那一刻窗口末尾正好压住表尾（既没有 ﹀，底下也不留空行）");
    CHECK(badUpStill == 0,
          L"★ 末段：从末行往回按，**窗口还没动**的那些步里 ﹀ 一步都不许出现");
    CHECK(badUpMove == 0,
          L"★ 末段：窗口一开始向上滚，﹀ 就回来了（「向上到页面开始滚动时，再出现」）");
}

/* 站在当前 (paramSel, paramTop) 上量一次，四种计数累加进去。
   ★ 三种走法（一路向下 / 一路回头 / 每行都从顶部跳过去）的量法必须一字不差，
   复制三份迟早走散，所以量法只留这一处。*/
static void arrowPinProbe(App *a, int *arrowCase, int *badGap,
                          int *tailCase, int *badTail) {
    int fit, arrow = 0, w0, cap, off, used, r;

    appParamScrollIntoView(a);
    fit = paramPanelFit(a, a->paramTop, NULL, &arrow);
    w0  = paramWindowStartItem(a);
    cap = paramPanelItemCap(a, a->paramTop);
    off = (w0 > 0) ? 1 : 0;

    /* 内容实占几格：与绘制同源的那几个函数加出来，
       顶行扣掉滚出去的格、末行扣掉被底边挡住的组标题条。*/
    used = off;
    for (r = 0; r < fit; ++r) {
        const int row = a->paramTop + r;
        int span = paramRowSpans(a, row);
        if (r == 0) span -= paramTopSkipOf(a, a->paramTop);
        if (paramRowBarClipped(a, row)) span -= 1;
        used += span;
    }

    if (arrow) {
        ++*arrowCase;
        /* ① 箭头紧贴内容底、且落在列表区最后一格 */
        if (paramArrowBotOffset(a) != used) ++*badGap;
    } else if (w0 > 0) {
        /* ② 没箭头又不在顶部：窗口末尾必须正好压住表尾 */
        ++*tailCase;
        if (w0 + cap != paramItemCount(a)) ++*badTail;
    }
}

/* ================================================= ﹀ 钉在底格
 *
 * 这两条规则说的都是**那个向下箭头的格位**。它们可以写成两句与绘制同源的
 * 话，而且是这块 UI 真正的判据 —— 这两句话各自对应一个出现过的毛病：
 *
 *   ①「画 ﹀ 时，内容正好排到它头上」—— 反例是：
 *      ﹀ 曾画在"最后一行内容的正下方"，而内容撞上"组标题条 + 它的行"
 *      这种两格块塞不进窗口时会提前停一格，箭头就跟着上移一格。实测那两处
 *      正是「特殊气球」与「数量」（四个窗口高度都中）。
 *      把 ﹀ 钉在"窗口起点格 + 容量"那一格之后，内容**必须**填满容量，
 *      两句合起来才成立 —— 所以这条断言同时盯着"箭头别动"和"内容别留缝"，
 *      少了任何一半它都会红。
 *
 *   ②「不画 ﹀ 时，窗口末尾不许短在表尾前面」—— 反例是旧的容量判据
 *      （`<=` 让最后一屏少一格）与拆掉例外后新露出来的那个洞：锚定算出的
 *      起点让窗口伸出表尾，列表底下就空出一行。窗口末尾要么压在表尾上，
 *      要么这一页整个装得下（起点就在顶部）。
 *
 * 量的是三条走法**全程**：
 *   0. 一路 ↓ 到底；
 *   1. 先一路 ↓ 到底、再一路 ↑ 回头 —— 那两个毛病都是在往回走的路上看见的，
 *      只扫"一路向下"扫不到；
 *   2. **逐行都从顶部直接跳过去**：End、选预设、改窗口高度走的都是这条路，
 *      窗口起点由"锚定"一次算出来，而不是跟着光标一格一格挪。
 *
 * ★ 第 2 条是"故意破坏"试出来的：把 app.cpp 里那段"窗口底边压实"整个
 *   拿掉，编译成功、当时全部断言**全绿** —— 一条断言都动不了它。查到原因是
 *   前两条走法都是一格一格挪：光标没掉出窗口，锚定分支根本不执行，那段代码
 *   等于没人管。加上第 2 条走法之后，一注入破坏就当场变红。
 *   这正是这种破坏性验证要办的事：把纪律破坏掉看它红不红，红了才算有人守。
 */
static void tArrowPinned(void) {
    static App a;
    const int pages[2]   = { 0, 1 };
    const int heights[4] = { 560, 900, 1440, 2160 };
    int p, h, mode, sel;
    int arrowCase = 0, badGap = 0, tailCase = 0, badTail = 0;

    group(L"参数面板：﹀ 钉在底格");

    for (p = 0; p < 2; ++p) {
        for (h = 0; h < 4; ++h) {
            const int total = appParamPageRowCount(pages[p]);
            for (mode = 0; mode < 3; ++mode) {
                memset(&a, 0, sizeof(a));
                a.winW = 1600; a.winH = heights[h];
                a.paramOpen = 1; a.paramPage = pages[p];
                a.paramOnMenu = 0;
                a.paramSel = 0; a.paramTop = 0; a.paramTopSkip = 0;
                appParamScrollIntoView(&a);
                if (mode == 1) {                /* 先一路按到底，建立末段的窗口 */
                    for (sel = 1; sel < total; ++sel) {
                        a.paramSel = sel;
                        appParamScrollIntoView(&a);
                    }
                }
                if (mode == 2) {
                    for (sel = 0; sel < total; ++sel) {
                        a.paramSel = 0; a.paramTop = 0; a.paramTopSkip = 0;
                        appParamScrollIntoView(&a);
                        a.paramSel = sel;
                        arrowPinProbe(&a, &arrowCase, &badGap, &tailCase, &badTail);
                    }
                    continue;
                }
                for (sel = (mode ? total - 2 : 0); sel >= 0 && sel < total;
                     sel += (mode ? -1 : 1)) {
                    a.paramSel = sel;
                    arrowPinProbe(&a, &arrowCase, &badGap, &tailCase, &badTail);
                }
            }
        }
    }

    CHECK(arrowCase > 0, L"（前提）扫到过画 ﹀ 的位置");
    CHECK(tailCase > 0, L"（前提）扫到过「不画 ﹀ 且窗口不在顶部」的位置");
    CHECK(badGap == 0,
          L"★ 画 ﹀ 时内容正好排到它头上：中间不留缝，箭头也就不会上下乱动"
          L"（特殊气球 / 数量那两处「向上顶了一栏」）");
    CHECK(badTail == 0,
          L"★ 不画 ﹀ 时窗口末尾正好压住表尾：列表底下不留空行");
}

/* ============================================================ 设置列表与预设弹窗
 *
 * "两级菜单"整个删了（不再分一二级，原来的二级菜单就是设置菜单）。
 * 现在的结构只有一层，分为两页
 * （加菜单栏时分出来的；行号是**页内行号**）：
 *
 *   预设方案设置页                        用户偏好设置页
 *     ├─ 第 0 行 「当前预设方案： XXX ▶」     └─ 第 0..11 行  12 项偏好参数
 *     └─ 第 1..32 行  32 项玩法参数
 *          ┌─────────────────┐
 *          │ 固定靶 … 移动靶 │   ← 浮层：向下展开，直接盖住参数行
 *          │ 自定义参数      │
 *          └─────────────────┘
 *
 * （核准：全表 44 项，32 项玩法 + 12 项偏好。这段注释以前写的是
 *   "第 1..42 行 四十二项参数" —— 那是分页之前的旧样子，早已过期。）
 *
 * 这一组断言盯的是**状态机**：光标在哪一行、浮层开着没有、回车之后
 * 谁变了谁没变。排版（"浮层确实盖住了下面的行"）出图看，那是人眼的事。
 *
 * 早先那套"在第几层"的断言（paramLevel）在这里整个作废 —— 层没有了，
 * 取而代之的是两个新字段：paramPopOpen / paramPopSel。*/
static void tPanelMenu(void) {
    static App a;
    int i;

    group(L"参数面板：设置列表与预设弹窗");

    /* ---- 打开时落在第 0 行「当前预设方案」，弹窗是收着的 ---- */
    memset(&a, 0, sizeof(a));
    a.winW = 1600; a.winH = 900;
    paramsDefault(&a.params);
    paramsApplyPreset(&a.params, 2);
    a.seed = 4242u;
    appParamOpen(&a);
    CHECK(appParamIsOpen(&a), L"appParamOpen 之后面板是开着的");
    CHECK(a.paramSel == 0, L"打开时光标落在第 0 行「当前预设方案」上");
    CHECK(a.paramTop == 0, L"打开时窗口在顶部");
    CHECK(!a.paramPopOpen, L"打开面板不会顺手把预设弹窗也拉出来（那太吵）");
    /* ★ 进入设置页面后光标默认停在菜单栏上。
       列表里的行号仍然是 0（上面那条），它是"等会儿按 ↓ 从哪一行开始看"
       的记分牌 —— 光标本身在栏上。 */
    CHECK(a.paramOnMenu, L"★ 打开面板时光标停在菜单栏上");
    /* ★ 行号是**页内行号**，两页加起来正好是整张表再加第 0 行。
       这一条把"分页分丢了/分重了"这类错全兜住 —— 以后往表里加参数、
       或者调整哪一段属于偏好，只要两页没覆盖满，它立刻红。*/
    CHECK(a.paramPage == 0, L"打开面板落在预设页");
    CHECK(appParamPageCount() == PARAM_SECTION_COUNT, L"页数 = 大类数（两个）");
    /* 现数出来的分界点。名字带 panel 前缀，免得和后面文案那一节里的
       nPlay / nPref 撞上（/W4 的 C4456 会报"声明遮住外层的同名变量"）。*/
    int panelPlayCount = 0, panelPrefCount = 0, panelLastPlay = -1;
    CHECK(appParamPageRowCount(0) + appParamPageRowCount(1) == g_paramDescCount + 1,
          L"两页的行数加起来 = 44 项参数 + 第 0 行（不重不漏）");
    /* 行数用**直接数表**算出来对一遍，不拿上面那个函数自己证明自己。
       ★ 这段里的"分界点下标"从写死的 28/29 改成**现数出来**。
       原来那对数字是气球组 11 项时数出来的，往表里插一项「生成位置」
       就整体错位 —— 而"分界点在第几项"本来就不该是断言的内容，这里该管的
       是"偏好参数恰好是**连续的一段尾巴**"（下面那条才是真判据）。
       nPlay / nPref / lastPlay 提到块外来，后面两条换算断言要用。*/
    {
        int k;
        for (k = 0; k < g_paramDescCount; ++k) {
            if (paramIsPreference(k)) ++panelPrefCount;
            else { ++panelPlayCount; panelLastPlay = k; }
        }
        CHECK(appParamPageRowCount(0) == panelPlayCount + 1,
              L"预设页行数 = 玩法项数 + 第 0 行");
        CHECK(appParamPageRowCount(1) == panelPrefCount,
              L"偏好页行数 = 偏好项数（顶上没有第 0 行）");
        CHECK(appParamRowToDesc(0, appParamPageRowCount(0) - 1) == panelLastPlay,
              L"预设页最后一行 = 表里最后一项玩法参数");
    }

    /* 行号与参数下标的换算。预设页第 0 行不是参数行，越界也不许读表。*/
    CHECK(appParamRowToDesc(0, 0) == -1,
          L"预设页第 0 行不是参数行（appParamRowToDesc 报 -1）");
    CHECK(appParamRowToDesc(0, -1) == -1, L"负行号报 -1");
    CHECK(appParamRowToDesc(0, appParamPageRowCount(0)) == -1, L"超尾行号报 -1");
    CHECK(appParamRowToDesc(0, 1) == 0, L"预设页第 1 行 = 第 0 项参数");
    /* 偏好页顶上**没有**第 0 行，第 0 行就是第一项参数。*/
    CHECK(appParamRowToDesc(1, 0) == panelLastPlay + 1,
          L"偏好页第 0 行 = 紧跟玩法段之后的那一项参数");
    CHECK(appParamRowToDesc(1, appParamPageRowCount(1) - 1) == g_paramDescCount - 1,
          L"偏好页最后一行 = 最后一项参数（两端都对得上）");
    CHECK(appParamRowToDesc(-1, 0) == -1 && appParamRowToDesc(2, 0) == -1,
          L"页号越界报 -1");
    /* ★ 这条盯的是 appParamRowToDesc 赖以成立的那条不变量：偏好参数在表里
       必须是**连续的一段**。哪天有人把一项偏好参数插进玩法段中间，
       换算会静默错位 —— 只有这条断言拦得住。*/
    {
        int k, seenPref = 0, bad = 0;
        for (k = 0; k < g_paramDescCount; ++k) {
            if (paramIsPreference(k)) seenPref = 1;
            else if (seenPref) bad = 1;      /* 偏好段后面又冒出非偏好项 */
        }
        CHECK(!bad, L"偏好参数在表里是连续的一段（页内行号换算的前提）");
        CHECK(paramIsPreference(panelLastPlay + 1) && !paramIsPreference(panelLastPlay),
              L"分界点就在玩法段末尾：再往后一项是偏好，往前一项是玩法");
    }

    /* ---- 弹窗那张表：七条预设 + 一条「自定义参数」 ---- */
    CHECK(g_paramMenuCount == PRESET_COUNT + 1,
          L"弹窗是「每套预设一行」+「自定义参数」一行，一行不多一行不少");
    {
        int k, badm = 0;
        for (k = 0; k < PRESET_COUNT; ++k) {
            if (g_paramMenu[k].kind != PMENU_PRESET) ++badm;
            if (g_paramMenu[k].arg != k) ++badm;
        }
        CHECK(!badm, L"前 PRESET_COUNT 行依次是第 0..N-1 套预设，顺序与 g_presets 一致");
        CHECK(g_paramMenu[PRESET_COUNT].kind == PMENU_CUSTOM,
              L"最后一行是「自定义参数」");
        CHECK(g_paramMenuDetailRow == PRESET_COUNT, L"它就是 g_paramMenuDetailRow");
    }

    /* ---- （前置）从菜单栏按 ↓ 进列表 ----
       打开面板时光标就停在栏上，后面这些用例测的都是列表里的
       行为，所以先走一步"进列表"。这一步本身也是正常的操作路径。*/
    panelKey(&a, VK_DOWN);
    CHECK(!a.paramOnMenu, L"（前置）↓ 从菜单栏进列表");
    CHECK(a.paramSel == 0, L"（前置）进列表落在第 0 行「当前预设方案」上");

    /* ---- 第 0 行没有"数值"可调：←/→ 一个字节都不许动 ---- */
    {
        int selBefore = a.paramSel;
        Params beforeParams = a.params;
        panelKey(&a, VK_RIGHT);
        panelKey(&a, VK_LEFT);
        CHECK(a.paramSel == selBefore, L"第 0 行上 ←/→ 不挪光标");
        CHECK(memcmp(&a.params, &beforeParams, sizeof(Params)) == 0,
              L"第 0 行上 ←/→ 之后整个参数结构一个字节都没动");
        CHECK(!a.paramPopOpen, L"第 0 行上 ←/→ 也不会把弹窗拉出来");
    }

    /* ---- ↑↓ 在"第 0 行"与"参数区"之间来回走，两端夹住不绕圈 ---- */
    panelKey(&a, VK_DOWN);
    CHECK(a.paramSel == 1, L"↓ 从第 0 行走进参数区");
    CHECK(appParamRowToDesc(a.paramPage, a.paramSel) == 0,
          L"第 1 行对应的确实是第 0 项参数");
    panelKey(&a, VK_UP);
    CHECK(a.paramSel == 0, L"↑ 从第 1 行走回第 0 行");
    /* ★ 这一条改过：早先"已经在第 0 行，再往上就停住"，
       现在再往上**进菜单栏**（光标向上选到顶时可以选择菜单栏）。
       光标行号本身不动 —— 回来的时候还停在原处。*/
    panelKey(&a, VK_UP);
    CHECK(a.paramSel == 0, L"再往上，光标行号停在第一行不走");
    CHECK(a.paramOnMenu, L"再往上 = 进菜单栏");
    panelKey(&a, VK_DOWN);
    CHECK(!a.paramOnMenu, L"↓ 从菜单栏回到列表");
    CHECK(a.paramSel == 0, L"回到的是原来那一行（第一行）");
    panelKey(&a, VK_END);
    CHECK(a.paramSel == appParamPageRowCount(a.paramPage) - 1,
          L"End 跳到**本页**列表最后一行");
    CHECK(appParamRowToDesc(a.paramPage, a.paramSel) == panelLastPlay,
          L"预设页的最后一行是玩法段的最后一项（再往后就是偏好页了）");
    /* ★ Home 改成了"一跳到面板最顶端"，而最顶端是菜单栏
       （Home 要能够进入菜单栏）。第 0 行不再是它的落点，
       落回第 0 行的路子是"从菜单栏按 ↓"。*/
    panelKey(&a, VK_HOME);
    CHECK(a.paramOnMenu, L"★ Home 进菜单栏（不再直接落在第 0 行上）");
    CHECK(a.paramSel == 0, L"Home 不动列表行号，回来时还停在第 0 行");
    panelKey(&a, VK_DOWN);
    CHECK(!a.paramOnMenu && a.paramSel == 0, L"↓ 回到列表第 0 行");

    /* ---- ★ End 在菜单栏上：一跳到列表最后一行 ----
       光标停留在菜单栏中时，按 End 要能移动到底部。
       所以 End 的落点与"光标现在在哪儿"无关 —— 一律是本页最后
       一行，并且**顺手离开菜单栏**（留在栏上的话，光标在栏、列表却滚到了底，
       接着按 ←→ 调的是页签而不是参数，看着像没反应）。

       ↓ 那条路与它不一样：↓ 是"回到刚才那一行"，End 是"不管从哪儿来都到
       最底下"。两条各自的落点都钉住，才不至于哪天被合成一条。*/
    {
        const int lastRow = appParamPageRowCount(a.paramPage) - 1;
        panelKey(&a, VK_HOME);              /* 先回菜单栏 */
        CHECK(a.paramOnMenu && a.paramSel == 0, L"（前置）Home 回到菜单栏");
        panelKey(&a, VK_END);
        CHECK(a.paramSel == lastRow,
              L"★ 菜单栏上按 End：光标落到**本页最后一行**（从前这一下没反应）");
        CHECK(!a.paramOnMenu, L"★ 菜单栏上按 End：顺带离开菜单栏（不再是栏态）");
        CHECK(a.paramTop <= a.paramSel &&
              a.paramSel < a.paramTop + paramPanelFit(&a, a.paramTop, NULL, NULL),
              L"End 之后最后一行确实滚进了可见窗口");
    }

    /* ---- 回车拉出弹窗，光标落在**当前那一套**上 ---- */
    {
        a.paramSel = 0;
        CHECK(appParamMatchedPreset(&a) == 2, L"刚套完第 2 套，判据也认第 2 套");
        panelKey(&a, VK_RETURN);
        CHECK(a.paramPopOpen, L"第 0 行按回车把预设弹窗拉出来");
        CHECK(a.paramPopSel == 2, L"弹窗光标一进来就落在当前那一套上");
        CHECK(a.paramSel == 0, L"弹窗是浮层：设置列表的光标一动不动");
        CHECK(appParamIsOpen(&a), L"弹窗开着时面板当然还开着");
    }

    /* ---- 弹窗里 ↑↓ 挪光标，两端**夹住不绕圈** ----
       ★ 这一条改过：二级菜单不开启上下循环。早先是
       绕圈的（到顶再往上 = 跳到最后一行），现在和列表那一层一致：到两端
       就停住。绕圈与夹住必须挑一个，混着来才是真难用。*/
    panelKey(&a, VK_DOWN);
    CHECK(a.paramPopSel == 3, L"弹窗里 ↓ 往下走一格");
    panelKey(&a, VK_UP);
    panelKey(&a, VK_UP);
    panelKey(&a, VK_UP);
    CHECK(a.paramPopSel == 0, L"弹窗里 ↑ 往上走");
    panelKey(&a, VK_UP);
    CHECK(a.paramPopSel == 0, L"★ 从第一行再往上就停住（不再绕到最后一行）");
    panelKey(&a, VK_END);
    CHECK(a.paramPopSel == g_paramMenuCount - 1, L"End 到弹窗最后一行");
    panelKey(&a, VK_DOWN);
    CHECK(a.paramPopSel == g_paramMenuCount - 1,
          L"★ 从最后一行再往下就停住（不再绕回第一行）");

    /* ---- 弹窗里 ←/→ 什么都不做 ---- */
    {
        int popBefore = a.paramPopSel;
        Params pBefore = a.params;
        panelKey(&a, VK_RIGHT);
        panelKey(&a, VK_LEFT);
        CHECK(a.paramPopSel == popBefore, L"弹窗里 ←/→ 不挪光标");
        CHECK(memcmp(&a.params, &pBefore, sizeof(Params)) == 0,
              L"弹窗里 ←/→ 也不改任何参数");
    }

    /* ---- 弹窗里 ESC = 收起来，什么都不改，**面板不关** ---- */
    {
        Params beforeParams = a.params;
        a.paramPopSel = 5;
        panelKey(&a, VK_ESCAPE);
        CHECK(!a.paramPopOpen, L"弹窗里按 ESC 把弹窗收起来");
        CHECK(appParamIsOpen(&a),
              L"★ 弹窗里按 ESC **不关面板**（用户按 ESC 的预期是「退一层」）");
        CHECK(memcmp(&a.params, &beforeParams, sizeof(Params)) == 0,
              L"用 ESC 退出弹窗不修改任何参数");
        CHECK(a.params.preset == 2, L"当前预设还是原来那套");
    }

    /* ---- ★ 没有浮层时按 ESC：面板**不关**，屏幕也不许被带走 ----
       ESC 只能暂停游戏和继续游戏，不能用于
       关闭设置页面和关闭历史记录页面。
       这一条要同时钉住两件事：
         · 面板还开着（旧的"没有浮层 → ESC 关面板"那条路已删）；
         · 屏幕没被 ESC 那个"暂停/继续"顺手带走（面板开着时根本走不到那一条，
           但那是靠 appHandleInput 里的提前 return 保证的 —— 这里量的是**结果**，
           将来谁动了那个分派、把暂停漏进面板来，这里就红）。*/
    {
        const int scrBefore = a.screen;
        CHECK(appParamIsOpen(&a), L"（前置）这一步开始时面板是开着的");
        panelKey(&a, VK_ESCAPE);
        CHECK(appParamIsOpen(&a), L"★ 没有浮层时按 ESC：面板照旧开着");
        CHECK(a.screen == scrBefore, L"★ 这一下 ESC 也没把屏幕切到暂停/继续");
        panelKey(&a, VK_ESCAPE);
        CHECK(appParamIsOpen(&a) && a.screen == scrBefore,
              L"★ 连按两次也一样：ESC 在面板里不累积任何效果");
    }

    /* ---- 弹窗里回车 = 选定，且**自动收起**，光标回第 0 行 ---- */
    {
        a.paramSel = 0;
        panelKey(&a, VK_RETURN);          /* 拉出来 */
        CHECK(a.paramPopOpen, L"（前置）弹窗又拉出来了");
        a.paramPopSel = 5;
        panelKey(&a, VK_RETURN);          /* 选定第 5 套 */
        CHECK(!a.paramPopOpen,
              L"选定之后弹窗自己收起来（选好了就关闭二级窗口）");
        CHECK(a.params.preset == 5, L"套用的是光标停着的那一套");
        CHECK(a.params.mode == g_presets[5].mode, L"换预设连规则一起换掉了");
        CHECK(feq(a.params.ramp, g_presets[5].ramp), L"换预设连难度涨幅一起换掉了");
        CHECK(a.paramSel == 0 && a.paramTop == 0,
              L"选定后列表光标回到第 0 行（好让用户立刻看见它变成了什么）");
        CHECK(appParamMatchedPreset(&a) == 5,
              L"★ 第 0 行的判据也跟着变成第 5 套（这就是「实时更新」）");
        CHECK(a.paramMsg[0] != L'\0' && wcscmp(a.paramMsg, g_presets[5].name) == 0,
              L"选定后底部反馈写的是那一套的名字");
        /* ★ 这一条量的是**第 0 行右边会显示的那个字串本身**，
           而不是"匹配到第几套"这个状态。改名这类改动只有这种断言拦得住。*/
        CHECK(wcscmp(paramRow0Name(&a), g_presets[5].name) == 0,
              L"★ 第 0 行右边显示的字就是那一套的名字");
    }

    /* ---- 手改一项：第 0 行必须立刻不再报那一套 ---- */
    {
        const ParamDesc *d = paramDescByOffset((int)offsetof(Params, lifetimeSec));
        CHECK(d != NULL, L"存活时限这一项在描述表里");
        if (d) {
            float v0, nv;
            int idx = -1;
            for (i = 0; i < g_paramDescCount; ++i)
                if (&g_paramDescs[i] == d) idx = i;
            CHECK(idx >= 0 && paramCoveredByPreset(5, idx),
                  L"这一项确实归第 5 套预设管（否则下面这条等于没测）");
            v0 = paramDescGet(&a.params, d);
            nv = (v0 + d->step <= d->hi) ? v0 + d->step : v0 - d->step;
            paramDescSet(&a.params, d, nv);
            CHECK(appParamMatchedPreset(&a) == -1,
                  L"★ 手改一项之后，第 0 行立刻不再报那一套（显示「自定义参数」）");
            /* 同一个时刻的**屏幕文案**：逐字等于「自定义参数」，且与弹窗末条
               共用同一个常量 —— 一处改名而另一处漏改，这条立刻红。*/
            CHECK(wcscmp(paramRow0Name(&a), g_panelCustomName) == 0,
                  L"★ 那一刻第 0 行显示的字逐字是「自定义参数」（与弹窗末条同一个常量）");
            /* 改回原样：判据是"和预设比"，不是"碰过没有"。*/
            paramDescSet(&a.params, d, v0);
            CHECK(appParamMatchedPreset(&a) == 5,
                  L"把值改回原样，第 0 行自己变回那一套（不是碰过就永久变自定义）");
        }
    }

    /* ---- 手改过之后再回弹窗选**原来那一套**，必须真的退得回去 ----
       这是设计时自己撞出来的坑：老的判据是"np == params.preset 就跳过"，
       于是用户改了十几项、想点回原来那套时，程序当成"重复套用"跳过，
       屏幕上什么都不变 —— 看着就是"点了没反应"。现在判据改成"是不是
       已经一模一样"，这条断言把那个坑钉死。*/
    {
        const ParamDesc *d = paramDescByOffset((int)offsetof(Params, lifetimeSec));
        const int beforePreset = a.params.preset;
        if (d) {
            float v0 = paramDescGet(&a.params, d);
            float nv = (v0 + d->step <= d->hi) ? v0 + d->step : v0 - d->step;
            paramDescSet(&a.params, d, nv);
            CHECK(appParamMatchedPreset(&a) == -1, L"（前置）现在确实不是那一套了");
            a.paramSel = 0;
            panelKey(&a, VK_RETURN);
            CHECK(a.paramPopSel == g_paramMenuCount - 1,
                  L"一套都不像时，弹窗光标落在「自定义参数」上");
            a.paramPopSel = beforePreset;    /* 就是原来那一套 */
            panelKey(&a, VK_RETURN);
            CHECK(appParamMatchedPreset(&a) == beforePreset,
                  L"★ 选原来那一套真的把它退回来了（不是「点了没反应」）");
            CHECK(paramDescGet(&a.params, d) == v0,
                  L"被手改的那一项也回到了预设值");
        }
    }

    /* ---- 「自定义参数」那一条：选了不改任何东西，只收起来 ---- */
    {
        Params beforeParams = a.params;
        a.paramSel = 0;
        panelKey(&a, VK_RETURN);
        CHECK(a.paramPopOpen, L"（前置）弹窗拉出来了");
        CHECK(a.paramPopSel == a.params.preset, L"现在正是第 5 套，光标就落在第 5 行上");
        a.paramPopSel = g_paramMenuCount - 1;
        panelKey(&a, VK_RETURN);
        CHECK(!a.paramPopOpen, L"「自定义参数」按回车也把弹窗收起来");
        CHECK(memcmp(&a.params, &beforeParams, sizeof(Params)) == 0,
              L"★ 「自定义参数」一个字节都不改（它的意思就是「就这样吧」）");
        /* ★ 二级菜单中选择自定义参数后，光标跳到下一个选项。
           "下一个选项" = 面板列表的第 1 行（第 0 行「当前预设方案」下面那一行，
           也就是第一项参数）—— 选「自定义参数」的意思本来就是"从这里起自己调"，
           光标落到第一项参数上，接着就能调。
           ★ 另一半同样重要：**选某套预设时不跳**（上面那条断言钉的就是它，
             光标回第 0 行好让人立刻看见它变成了什么）。两条一起才说明
             "跳"这件事是按选择分岔的，不是全局行为。*/
        CHECK(a.paramSel == 1, L"★ 选「自定义参数」后光标跳到列表第 1 行");
        CHECK(a.paramTop == 0, L"窗口还在顶部（第 1 行当然看得见）");
        CHECK(!a.paramOnMenu, L"光标在列表里，不在菜单栏上");
    }

    /* ---- appParamMenuPreset：预设行报下标，其它行与越界都报 -1 ---- */
    {
        int k, badm = 0;
        for (k = 0; k < g_paramMenuCount; ++k) {
            int pr = appParamMenuPreset(k);
            if (g_paramMenu[k].kind == PMENU_PRESET) {
                if (pr != g_paramMenu[k].arg) ++badm;
            } else if (pr != -1) ++badm;
        }
        CHECK(!badm, L"appParamMenuPreset：预设行报下标，其它行报 -1");
        CHECK(appParamMenuPreset(-1) == -1, L"appParamMenuPreset 越界（负）返回 -1");
        CHECK(appParamMenuPreset(g_paramMenuCount) == -1,
              L"appParamMenuPreset 越界（超尾）返回 -1");
    }

    /* ---- R：只恢复「用户偏好设置」，玩法那一半完全不动 ---- */
    {
        Params gameBefore, after;
        int k, prefCount = 0, movedPref = 0, gameMoved = 0, bad = 0;

        for (k = 0; k < g_paramDescCount; ++k)
            if (paramIsPreference(k)) ++prefCount;
        CHECK(prefCount == 12, L"「用户偏好设置」正好 12 项（玩家 / 画面 / 声音三组）");
        CHECK(paramIsPreference(-1) == 0 && paramIsPreference(g_paramDescCount) == 0,
              L"paramIsPreference 越界返回 0，不越界读表");

        /* 把偏好段逐项拨到"和现在不一样"的位置。*/
        for (k = 0; k < g_paramDescCount; ++k) {
            const ParamDesc *d = &g_paramDescs[k];
            float v0, nv;
            if (!paramIsPreference(k)) continue;
            v0 = paramDescGet(&a.params, d);
            nv = (v0 + d->step <= d->hi) ? v0 + d->step : v0 - d->step;
            if (nv == v0) continue;              /* 拨不动的项跳过 */
            paramDescSet(&a.params, d, nv);
            ++movedPref;
        }
        CHECK(movedPref > 0, L"（前置）真的拨动了偏好段（否则下面等于没测）");

        /* ★ 先测反面：此刻面板停在「预设方案设置」页（上面那些弹窗
           测试一路都在这一页上），R 在这一页上是死键 —— 一项都不许动。
           光标同样要先落到**参数行**上：停在第 0 行的话 descIdx 是 -1，
           appParamInputMenu 会在那儿提前 return，测出来的"没生效"是假的。*/
        a.paramSel = 7;
        CHECK(appParamRowToDesc(a.paramPage, a.paramSel) == 6,
              L"（前置）光标停在预设页的参数行上（不是第 0 行）");
        gameBefore = a.params;
        panelKey(&a, 'R');
        {
            int same = 1;
            for (k = 0; k < g_paramDescCount && same; ++k) {
                const ParamDesc *d = &g_paramDescs[k];
                if (paramDescGet(&a.params, d) != paramDescGet(&gameBefore, d)) same = 0;
            }
            CHECK(same, L"★ 预设页上按 R 一项参数都不动（R 只在偏好页生效）");
        }

        /* 切到偏好页再按 —— 这才是它该管用的地方。*/
        appParamSetPage(&a, 1);
        CHECK(a.paramPage == 1, L"（前置）已切到「用户偏好设置」页");
        panelKey(&a, 'R');
        after = a.params;

        for (k = 0; k < g_paramDescCount; ++k) {
            const ParamDesc *d = &g_paramDescs[k];
            if (paramIsPreference(k)) continue;
            if (paramDescGet(&after, d) != paramDescGet(&gameBefore, d)) ++gameMoved;
        }
        CHECK(gameMoved == 0,
              L"★ R 之后 32 项玩法参数一项都没动（「预设方案完全不动」）");

        {
            int prefChanged = 0;
            for (k = 0; k < g_paramDescCount; ++k) {
                const ParamDesc *d = &g_paramDescs[k];
                if (!paramIsPreference(k)) continue;
                if (paramDescGet(&after, d) != paramDescGet(&gameBefore, d)) ++prefChanged;
            }
            CHECK(prefChanged > 0, L"R 确实把偏好段改回去了（不是空动作）");
        }

        /* 幂等：再按一次，结果一模一样。*/
        panelKey(&a, 'R');
        for (k = 0; k < g_paramDescCount && !bad; ++k) {
            const ParamDesc *d = &g_paramDescs[k];
            if (paramDescGet(&a.params, d) != paramDescGet(&after, d)) ++bad;
        }
        CHECK(bad == 0, L"连按两次 R 结果一样（恢复是幂等的）");
        CHECK(wcscmp(a.paramMsg, L"已恢复用户偏好设置") == 0,
              L"R 之后的提示语是「已恢复用户偏好设置」");
        CHECK(a.paramSel == 0, L"R 不动光标");
        /* 收尾：切回预设页，免得后面接着跑的用例以为还在偏好页上。*/
        appParamSetPage(&a, 0);
    }

    /* ---- ★ 长按 ↑ 到顶之后要能进菜单栏 ----
     *
     * 按 Home 键以及长按上键一直到顶要能够进入菜单栏。
     * 根因：进栏那一句原来判的是 inputKeyPressed —— 它只认"物理刚按下"那个
     * 边沿；而输入层把系统的连发串丢掉了、长按是自己排的节拍，所以长按到顶
     * 的那一刻，那个边沿早就用完了，只剩 repeat 通道还在出键。
     *
     * 所以这条用例**不给 pressed 边沿**，只给"按住 + 排队等着的重复计数"，
     * 模拟的正是"长按到顶"那一帧。给了边沿就测不到这个 bug —— 那也正是它
     * 能一直躲过断言的原因：旧断言全走 inputKeyPressed 那条路。*/
    {
        memset(&a, 0, sizeof(a));
        a.winW = 1600; a.winH = 900;
        paramsDefault(&a.params);
        a.paramOpen = 1;
        a.paramPage = 0;
        a.paramSel = 0;
        a.paramTop = 0;
        a.paramOnMenu = 0;

        /* ① 光标在第 0 行：只有重复、没有边沿，照样要进栏。*/
        a.input.repeatCount[VK_UP] = 3;
        a.input.down[VK_UP] = 1;
        appParamInput(&a);
        CHECK(a.paramOnMenu,
              L"★ 长按 ↑ 到顶（只有重复事件、没有按下边沿）照样进菜单栏");
        CHECK(a.paramSel == 0, L"进栏时光标行号不动");

        /* ② 反面：光标不在第 0 行时，长按上键只是往上走，不许跳进栏。*/
        memset(&a.input, 0, sizeof(a.input));
        a.paramOnMenu = 0;
        a.paramSel = 5;
        a.input.repeatCount[VK_UP] = 1;
        a.input.down[VK_UP] = 1;
        appParamInput(&a);
        CHECK(!a.paramOnMenu && a.paramSel == 4,
              L"（反面）光标不在第 0 行时长按 ↑ 只往上走一格，不会直接跳进菜单栏");

        /* ③ 走到第 0 行之后再来一次重复就该进栏 —— 这正是用户按住 ↑ 不放
              一格格走到顶、到顶那一下进栏的真实过程。*/
        memset(&a.input, 0, sizeof(a.input));
        a.paramSel = 0;
        a.input.repeatCount[VK_UP] = 1;
        a.input.down[VK_UP] = 1;
        appParamInput(&a);
        CHECK(a.paramOnMenu, L"★ 走到第 0 行之后再按一下 ↑，进菜单栏");
    }

    appParamClose(&a);
    CHECK(!appParamIsOpen(&a), L"appParamClose 之后面板是关着的");
    CHECK(!a.paramPopOpen, L"appParamClose 会把弹窗状态一起清掉");
    CHECK(!appParamIsOpen(NULL), L"appParamIsOpen(NULL) 不崩，返回 0");
}

/* ================================================= ←/→ 绕圈
 *
 * 按左右键调参数时，除了有滑动条的项都可循环，
 * 一直按向左和向右会在几个枚举中循环。
 *
 * 拆成两条纪律，分别钉住：
 *   · 开关与枚举（PK_BOOL / PK_ENUM）—— **绕圈**：连按 取值个数 次回到原处；
 *   · 整数与浮点（PK_INT / PK_FLOAT，就是有滑动条的）—— **夹住**：
 *     到头再按还是停在头上，绝不绕到另一头。规则是"除了有滑动条的"，
 *     这一半同样是他明确要的，所以也得有断言 —— 只查前半句，
 *     哪天有人把 wrap 摊到所有项上，这里照样全绿。
 *
 * 为什么每条都走 appParamInput 而不是直接调 paramDescAddWrap：
 * 那个函数是**实现**，断言要盯的是"按一下键屏幕上会怎样"。真正容易错的
 * 地方也在这儿 —— 三种加法（wrap / int / set）的**分派顺序**，
 * 写在 app.cpp 里，paramDescAddWrap 单独测再多次也照不到它。
 *
 * 为什么"连按 n 次回到原处"比"第 k 次等于 lo+((v0-lo+k) mod n)"更值：
 * 后者是把公式抄一遍，函数写错、断言跟着错（两边一起错、互相印证，
 * 这个项目已经栽过一回）。前者是**性质**，与公式怎么写的无关。
 */
static void tParamCycle(void) {
    static App a;
    int k, i;
    int firstPref = -1;          /* 第一项偏好参数的下标，现扫出来，不写死 29 */
    int nWrap = 0, nClamp = 0, bad = 0, badStep = 0, badRange = 0, badSlider = 0;

    group(L"参数面板：开关与枚举的 ←/→ 绕圈");

    for (k = 0; k < g_paramDescCount; ++k)
        if (paramIsPreference(k)) { firstPref = k; break; }

    paramsDefault(&a.params);
    a.winW = 1600; a.winH = 900;
    a.paramOpen = 1;
    a.paramOnMenu = 0;

    for (k = 0; k < g_paramDescCount; ++k) {
        const ParamDesc *d = &g_paramDescs[k];
        const int page = paramIsPreference(k) ? 1 : 0;
        int row, lo, hi, n, v0, v;

        /* 参数下标 → 页内行号。预设页顶上多一行「当前预设方案」，
           偏好页没有 —— 与 appParamRowToDesc 是同一套换算的反面。*/
        if (page == 1 && firstPref >= 0) row = k - firstPref;
        else                             row = k + 1;

        lo = (int)d->lo;
        hi = (int)d->hi;
        if (hi < lo) { int t = lo; lo = hi; hi = t; }

        a.paramPage = page;
        a.paramSel = row;
        a.paramTop = 0;
        a.paramTopSkip = 0;
        appParamScrollIntoView(&a);

        /* 面板真的停在这一项上了吗 —— 换算写错了后面全是假的。*/
        if (appParamRowToDesc(a.paramPage, a.paramSel) != k) { ++bad; continue; }

        if (d->kind == PK_BOOL || d->kind == PK_ENUM) {
            n = hi - lo + 1;
            if (d->kind == PK_ENUM && d->enumCount > 0) n = d->enumCount;
            if (n < 2) continue;          /* 只有一个取值的开关没有"循环"可言 */
            ++nWrap;

            paramDescSet(&a.params, d, (float)lo);      /* 从最小的一端开始 */
            v0 = (int)paramDescGet(&a.params, d);

            /* ① 连按 n 次 →，回到出发点 */
            for (i = 0; i < n; ++i) {
                int before = (int)paramDescGet(&a.params, d);
                panelKey(&a, VK_RIGHT);
                v = (int)paramDescGet(&a.params, d);
                if (v == before) ++badStep;                 /* 卡住了 */
                if (v < lo || v > hi) ++badRange;           /* 越界了 */
            }
            if ((int)paramDescGet(&a.params, d) != v0) ++bad;

            /* ② 连按 n 次 ←，同样回到出发点（双向都可绕）*/
            for (i = 0; i < n; ++i) panelKey(&a, VK_LEFT);
            if ((int)paramDescGet(&a.params, d) != v0) ++bad;
        } else if (d->kind == PK_INT || d->kind == PK_FLOAT) {
            float prev;
            int m, movedFwd = 0, movedBack = 0;
            if (d->hi <= d->lo) continue;   /* 没有量程的项，绕不绕无从谈起 */
            ++nClamp;

            /* ③ 有滑动条的项**不绕**：一路按 → 六十次，值只许持平或变大。
               判据必须是"**单调**"，不能写成"值始终落在 lo..hi 里" ——
               后者对绕圈是失明的：绕回另一头之后，值照样在量程内。
               （也不能像这一组最初那样"先把值摆到 hi 再按三下、看它变没变"：
               参数表里有**互相咬合的项**，"最小半径"设到 2.0 会被"最大半径"
               的当前值当场夹回去 —— 那是参数之间的约束，不是绕圈。
               按真实按键一路走，就绕不开这些约束，量的才是用户看到的东西。）*/
            prev = paramDescGet(&a.params, d);
            for (m = 0; m < 60; ++m) {
                float now;
                panelKey(&a, VK_RIGHT);
                now = paramDescGet(&a.params, d);
                if (now < prev) ++badSlider;      /* 掉头 = 绕到另一端去了 */
                if (now != prev) movedFwd = 1;
                prev = now;
            }
            for (m = 0; m < 60; ++m) {
                float now;
                panelKey(&a, VK_LEFT);
                now = paramDescGet(&a.params, d);
                if (now > prev) ++badSlider;
                if (now != prev) movedBack = 1;
                prev = now;
            }
            /* 来回各六十下，两个方向都推不动 —— 那说明这个"滑杆"根本没接上
               （以前踩过的"假滑杆"坑），不是"不绕圈"。*/
            if (!movedFwd && !movedBack) ++badSlider;
        }
    }

    /* 账目：覆盖到的项数必须与表里实际的项数一致 —— 否则"全绿"可能是
       因为一个都没测（比如分派写错、所有项都从 continue 溜走）。*/
    {
        int nBoolEnum = 0, nSlider = 0;
        for (k = 0; k < g_paramDescCount; ++k) {
            const ParamDesc *d = &g_paramDescs[k];
            if (d->kind == PK_BOOL || d->kind == PK_ENUM) {
                /* 与上面那个循环用**同一个**筛选条件（取值个数 >= 2） */
                int n = (int)d->hi - (int)d->lo + 1;
                if (d->kind == PK_ENUM && d->enumCount > 0) n = d->enumCount;
                if (n >= 2) ++nBoolEnum;
            } else if (d->hi > d->lo) {
                ++nSlider;
            }
        }
        CHECK(nWrap == nBoolEnum,
              L"（账目）每一个开关与枚举项都真的按过 ←/→（没有从 continue 溜走的）");
        CHECK(nClamp == nSlider,
              L"（账目）每一个有滑动条的项也都被试过（这一半查的是「不绕」）");
        CHECK(nWrap > 0 && nClamp > 0,
              L"（账目）两类各自都有样本，上面那些断言不是空转");
    }

    CHECK(badStep == 0, L"★ 开关 / 枚举上每按一次 ←/→ 值都真的变了（不是卡住）");
    CHECK(badRange == 0, L"★ 绕圈途中值始终落在该项的取值范围内");
    CHECK(bad == 0,
          L"★ 开关 / 枚举连按一整圈回到原值（一直按会循环）");
    CHECK(badSlider == 0,
          L"★ 有滑动条的项不绕圈：一路按 → 值只许持平或变大，按 ← 只许持平或变小"
          L"（即「除了有滑动条的」）");
}

static void tKeyRepeat(void) {
    static Input in;
    int i, n;

    group(L"长按重复");

    /* ---- ① 刚按下立刻响应一次 ---- */
    memset(&in, 0, sizeof(in));
    in.pressed[VK_DOWN] = 1;
    in.down[VK_DOWN] = 1;
    CHECK(inputKeyRepeat(&in, VK_DOWN) != 0, L"刚按下时立刻响应一次");
    CHECK(inputKeyRepeat(&in, VK_DOWN) == 0,
          L"同一次按下（重复计数还没涨）不会连发两次");
    inputEndFrame(&in);                  /* "刚按下"那一帧过去了 */
    inputTick(&in, 0.0f);
    in.down[VK_DOWN] = 1;
    CHECK(inputKeyRepeat(&in, VK_DOWN) == 0, L"按下之后、延迟未到时不再重复");

    /* ---- ② 按住不放，重复次数随时间单调增长 ---- */
    {
        int prev = 0, mono = 1, total = 0;
        for (i = 0; i < 200; ++i) {          /* 200 帧 x 1/60 秒 ≈ 3.33 秒 */
            inputTick(&in, 1.0f / 60.0f);
            in.down[VK_DOWN] = 1;
            n = in.repeatCount[VK_DOWN];
            if (n < prev) mono = 0;
            prev = n;
            total += inputKeyRepeat(&in, VK_DOWN);
        }
        CHECK(mono, L"重复计数只增不减");
        CHECK(total > 20, L"按住 3.3 秒至少走了 20 格（否则快速切换名不副实）");
        CHECK(total < 200, L"重复不会每帧都触发（那就退回成疯狂翻页了）");
    }

    /* ---- ③ 松手之后一切归零 ---- */
    in.down[VK_DOWN] = 0;
    inputTick(&in, 1.0f / 60.0f);
    CHECK(in.repeatCount[VK_DOWN] == 0 && in.holdSec[VK_DOWN] == 0.0f,
          L"松手后长按状态清零");
    CHECK(inputKeyRepeat(&in, VK_DOWN) == 0, L"松手后不再产生重复事件");

    /* ---- ④ 加速：后半程比前半程密 ----
       按久了还按同一个节奏走，翻到第 40 行时会烦。*/
    {
        int early = 0, lateN = 0;
        memset(&in, 0, sizeof(in));
        in.down[VK_DOWN] = 1;
        for (i = 0; i < 66; ++i) {           /* 前 1.1 秒 */
            inputTick(&in, 1.0f / 60.0f);
            in.down[VK_DOWN] = 1;
            early += inputKeyRepeat(&in, VK_DOWN);
        }
        for (i = 0; i < 66; ++i) {           /* 之后 1.1 秒 */
            inputTick(&in, 1.0f / 60.0f);
            in.down[VK_DOWN] = 1;
            lateN += inputKeyRepeat(&in, VK_DOWN);
        }
        CHECK(lateN > early, L"按住 1.2 秒之后重复变密（加速档生效）");
    }

    /* ---- ⑤ 玩法输入不吃重复 ----
       这是设计边界：走路与开火必须走 inputKeyPressed / inputKeyDown，
       否则按住 W 会变成一顿一顿的。这里用一个"按住的键"验证两个口子
       的行为确实不同 —— 将来谁把玩法改成 inputKeyRepeat 就会被这条拦下。*/
    {
        memset(&in, 0, sizeof(in));
        in.pressed[VK_DOWN] = 1;
        in.down[VK_DOWN] = 1;
        inputKeyRepeat(&in, VK_DOWN);
        inputEndFrame(&in);                  /* "刚按下"这一帧过去了 */
        for (i = 0; i < 200; ++i) {
            inputTick(&in, 1.0f / 60.0f);
            in.down[VK_DOWN] = 1;
        }
        CHECK(inputKeyPressed(&in, VK_DOWN) == 0,
              L"按住不放时 inputKeyPressed 不会重复发（玩法不受影响）");
        CHECK(inputKeyDown(&in, VK_DOWN) != 0,
              L"按住不放时 inputKeyDown 始终为真（持续动作照常）");
    }
}

static void tParamDesc(void) {
    Params base, p, q;
    int i, g, j, ok;

    group(L"参数描述表");

    /* ------------------------------------------------------------ 表结构 */
    CHECK(g_paramDescCount == g_paramGroupFirst[PARAM_GROUP_COUNT],
          L"分组哨兵 = 描述表总行数");
    CHECK(g_paramGroupFirst[0] == 0, L"第一组从第 0 行开始");
    for (i = 0; i < PARAM_GROUP_COUNT; ++i)
        CHECK(g_paramGroupFirst[i] < g_paramGroupFirst[i + 1],
              L"每组至少有一行（分组下标严格递增）");
    CHECK(g_paramGroupCount == PARAM_GROUP_COUNT, L"分组数 = PARAM_GROUP_COUNT");
    CHECK(g_paramDescCount >= 40, L"描述表项数与预期相符");
    /* 上限是给字形图集与面板排版留的余量：行数再涨，二级菜单一屏就更装不下，
       侧栏也得跟着重排。这条不是"不许超过"，而是"涨到这里就该回头看看"。*/
    CHECK(g_paramDescCount <= 48, L"描述表项数没有失控膨胀（面板一屏还放得下）");

    /* 每行都得落在它自己声明的组里。分组边界数错了，面板就会把行
       挂到隔壁组的标题下面 —— 这条断言是那种错误唯一的出口。*/
    ok = 1;
    for (g = 0; g < PARAM_GROUP_COUNT; ++g)
        for (i = g_paramGroupFirst[g]; i < g_paramGroupFirst[g + 1]; ++i)
            if (g_paramDescs[i].group != g) ok = 0;
    CHECK(ok, L"每一行的分组字段与分组边界一致");

    /* 下面三档探针共用这一份基线（为什么不用 paramsDefault 见它的注释）。*/
    paramsProbeBase(&base);

    /* -------------------------------------------------- 字段偏移合法且不重叠 */
    /* 重叠意味着两个滑杆拧的是同一个字节 —— 表现是"拖 A 的时候 B 也跟着动"。
       偏移量本身是 offsetof 算出来的，字段名拼错编译就过不了，所以这里
       真正在防的是"两项写了同一个字段"。*/
    {
        unsigned char used[sizeof(Params)];
        int clashTotal = 0;
        memset(used, 0, sizeof(used));
        for (i = 0; i < g_paramDescCount; ++i) {
            const ParamDesc *d = &g_paramDescs[i];
            int span = (d->kind == PK_FLOAT) ? (int)sizeof(float) : (int)sizeof(int);
            CHECK(d->name && d->name[0], L"每一项都有中文名");
            CHECK(d->offset >= 0 && d->offset + span <= (int)sizeof(Params),
                  L"字段偏移落在 Params 之内");
            if (d->offset < 0 || d->offset + span > (int)sizeof(Params)) continue;
            for (j = 0; j < span; ++j) if (used[d->offset + j]) clashTotal = 1;
            for (j = 0; j < span; ++j) used[d->offset + j] = 1;
            CHECK(d->lo <= d->hi, L"区间下界不高于上界");
            CHECK(d->step > 0.0f, L"步长为正");
            if (d->kind == PK_ENUM) {
                CHECK(d->enumCount > 0, L"枚举项有取值个数");
                CHECK(d->lo == 0.0f && d->hi == (float)(d->enumCount - 1),
                      L"枚举项的区间就是 0..取值个数-1");
            } else {
                CHECK(d->enumNames == NULL, L"非枚举项不挂名字表");
                CHECK(d->enumCount == 0, L"非枚举项不写取值个数");
            }
        }
        CHECK(!clashTotal, L"没有两项指向同一个字段");
    }

    /* ------------------------------------------- 区间与 paramsClamp 一致 */
    /* 描述表写着"能调到 2.0"而 paramsClamp 只让到 1.0，面板上就会拖到一半
       卡住 —— 这条断言把两边的区间钉死在一起。*/
    for (i = 0; i < g_paramDescCount; ++i) {
        const ParamDesc *d = &g_paramDescs[i];
        p = base;
        paramDescSet(&p, d, d->lo - 1000.0f);
        CHECK(paramDescGet(&p, d) >= d->lo - 1e-3f && paramDescGet(&p, d) <= d->hi + 1e-3f,
              L"低于下界的值被夹进区间");
        paramDescSet(&p, d, d->lo - 1.0e-4f);
        CHECK(paramDescGet(&p, d) >= d->lo - 1e-3f, L"刚好低于下界也被夹住");
        paramDescSet(&p, d, d->hi + 1000.0f);
        CHECK(paramDescGet(&p, d) <= d->hi + 1e-3f, L"高于上界的值被夹进区间");
    }

    /* ---------------------------------------- 出图种子必须活过 paramsClamp */
    /* 出图模式不带 --seed 时用 20260930 当种子（作者的日期），图才可复现。
       给 seed 加上界时曾把它定成 999999，20260930 就被悄悄夹掉了 ——
       是参数面板把这个夹取显示出来才发现的。这条断言守着它。*/
    {
        const int shotSeed = 20260930;
        p = base;
        p.seed = shotSeed;
        paramsClamp(&p);
        CHECK(p.seed == shotSeed, L"★ 出图默认种子 20260930 不被 paramsClamp 改掉");
    }

    /* --------------------------------------- 第一档：描述表接到真实字段上 */
    /* 逐项拨一下，参数指纹必须变。指纹是按结构体原始字节算的，所以这条
       证明的是"这一项确实写进了 Params 的某个字节"，它挡的是偏移量写串
       那一类错误 —— 但要证明它接到了玩法上，得看下面两档。*/
    {
        unsigned h0 = paramsHash(&base);
        int moved = 0;
        for (i = 0; i < g_paramDescCount; ++i) {
            const ParamDesc *d = &g_paramDescs[i];
            if (d->hi - d->lo < 1e-6f) continue;
            CHECK_NAMED(paramNudge(&p, &base, d), L"这一项拨得动", d->name);
            if (!paramsEqual(&p, &base) && paramsHash(&p) != h0) ++moved;
        }
        CHECK(moved == g_paramDescCount, L"每一项拨动后参数指纹都变了");
    }

    /* ------------------------------------ 第二档：参数接到气球系统上了 */
    {
        unsigned ref = poolProbe(&base);
        /* 第二档基线：**邻位生成 + 恰好两颗球**。「生成距离下限/上限」那两项
           只在生成位置 = 邻位生成、且墙上只剩一颗
           搭档时才轮得到它们使劲 —— 上面那条基线跑的是全场随机 + 8 颗球，
           那两项当然一动不动。换一档真的会用上它们的玩法再来一遍。
           （两档都是实际能调出来的玩法状态，所以"某一档里改了必变"
           就足以说明这一项接在玩法上，不是假滑杆。）*/
        Params alt = base;
        unsigned refAlt;
        alt.spawnRule   = SPAWN_ADJACENT;
        alt.balloonCount = 2;
        alt.radiusMin = alt.radiusMax = 0.40f;
        paramsClamp(&alt);
        refAlt = poolProbe(&alt);
        CHECK(refAlt != ref, L"（前置）两档基线本身就不一样（否则第二档白搭）");
        int live = 0;
        int skipSeed = 1, skipMiss = 1, skipAuto = 1, skipRefill = 1;
        for (i = g_paramGroupFirst[PARAM_GROUP_BALLOON];
             i < g_paramGroupFirst[PARAM_GROUP_RHYTHM + 1]; ++i) {
            const ParamDesc *d = &g_paramDescs[i];
            /* 这四项的消费点在别处，由下面两条探针负责，池里看不出来。*/
            if (d->offset == (int)offsetof(Params, seed))           { skipSeed   = 0; continue; }
            if (d->offset == (int)offsetof(Params, missCostsLife))  { skipMiss   = 0; continue; }
            if (d->offset == (int)offsetof(Params, autoFireInterval)){ skipAuto  = 0; continue; }
            if (d->offset == (int)offsetof(Params, refillDelaySec)) { skipRefill = 0; continue; }

            /* 一个取值不够就再试另外两个。中段常常"不够狠"：
               比如漂移幅度从 1.0 降到 0.5，球在一秒内还没撞到收窄后的
               边界，位置一模一样 —— 那不是死参数，只是拨得不够远。
               三个取值都试不出来，才算这一项没接到气球系统上。*/
            {
                const float tries[3] = {
                    (d->lo + d->hi) * 0.5f, d->lo, d->hi
                };
                int alive = 0, k;
                for (k = 0; k < 3; ++k) {
                    q = base;
                    paramDescSet(&q, d, tries[k]);
                    if (paramsEqual(&q, &base)) continue;
                    if (poolProbe(&q) != ref) { alive = 1; break; }
                }
                /* 这一档试不出来，换"邻位生成 + 两颗球"那一档再试三遍。*/
                if (!alive) {
                    for (k = 0; k < 3; ++k) {
                        q = alt;
                        paramDescSet(&q, d, tries[k]);
                        if (paramsEqual(&q, &alt)) continue;
                        if (poolProbe(&q) != refAlt) { alive = 1; break; }
                    }
                }
                CHECK_NAMED(alive,
                            L"这一项接在气球系统上（改了它气球状态必变）", d->name);
                live += alive;
            }
        }
        CHECK(live >= 17, L"气球/运动/节奏组里有 17 项能改变气球池状态");

        /* 漂移速度与漂移幅度必须在**每一种会动的运动方式**下都起作用。
           原先"漂移幅度"只喂给了正弦摆动，默认的匀速漂移根本不看它 ——
           面板上那个滑杆在默认设置下是个摆设。这条断言就是冲它来的。*/
        {
            const int moving[3] = { MOVE_LINEAR, MOVE_SINE, MOVE_JUMP };
            int k;
            for (k = 0; k < 3; ++k) {
                Params m;
                paramsProbeBase(&m);      /* 基线必须本身就在动，否则无从谈起 */
                m.motion = moving[k];
                paramsClamp(&m);
                {
                    unsigned r0 = poolProbe(&m);
                    Params q1 = m;
                    q1.driftSpeed = (m.driftSpeed < 1.5f) ? 2.5f : 0.05f;
                    paramsClamp(&q1);
                    CHECK_NAMED(poolProbe(&q1) != r0,
                                L"这种运动方式下漂移速度起作用", g_motionNames[moving[k]]);
                    {
                        Params q2 = m;
                        q2.driftRange = (m.driftRange > 0.5f) ? 0.25f : 1.0f;
                        paramsClamp(&q2);
                        CHECK_NAMED(poolProbe(&q2) != r0,
                                    L"这种运动方式下漂移幅度起作用", g_motionNames[moving[k]]);
                    }
                }
            }
        }
        /* 上面四个 continue 必须真的各命中一次 —— 描述表换了字段名时，
           这里会变成 1，提醒探针名单该跟着改。*/
        CHECK(!skipSeed && !skipMiss && !skipAuto && !skipRefill,
              L"四项豁免参数都确实在表里（探针名单没有过期）");
    }

    /* ------------------------------------ 第三档：击破与 app 层 */
    {
        /* 补位延迟：默认 0 时击破当帧就补，看不出"少一个"；
           调大之后必须能看到墙上短暂少一个，然后补回来。*/
        Params slow;
        paramsDefault(&slow);
        CHECK(!refillProbe(&slow), L"补位延迟 = 0 时不留空隙（打掉立刻补）");
        slow.refillDelaySec = 0.40f;
        paramsClamp(&slow);
        CHECK(refillProbe(&slow), L"补位延迟 > 0 时墙上会短暂少一个再补回来");

        /* 种子：两个不同的种子必须给出不同的气球布局。*/
        {
            Params s1, s2;
            paramsDefault(&s1); paramsDefault(&s2);
            paramPrepare(&s1); paramPrepare(&s2);
            s1.seed = 111; s2.seed = 222;
            CHECK(appProbe(s1, 1, 0) != appProbe(s2, 1, 0),
                  L"随机种子确实决定气球布局（不是死参数）");
        }
        /* 漏球扣命：球逃走 3 秒之后，扣命与不扣命的剩余生命必须不同。*/
        {
            Params a, b;
            int prog = -1, k;
            for (k = 0; k < PRESET_COUNT; ++k)
                if (g_presets[k].mode == MODE_PROGRESS) { prog = k; break; }
            CHECK(prog >= 0, L"存在一套带生命与时限的预设（漏球才有得扣）");
            paramsDefault(&a); paramsDefault(&b);
            /* 只有"球会跑"的预设里才有球可漏，所以这一项得在那套上比。*/
            if (prog >= 0) { a.preset = b.preset = prog; }
            paramPrepare(&a); paramPrepare(&b);
            a.seed = b.seed = 4242;
            a.missCostsLife = 1; b.missCostsLife = 0;
            /* 渐进训练默认存活 4.5 秒，得跑过这个点才算数。*/
            CHECK(appProbe(a, 900, 0) != appProbe(b, 900, 0),
                  L"漏球扣命确实影响剩余生命（不是死参数）");
        }
        /* 连射间隔：按住不放时，间隔不同则开火次数不同。*/
        {
            Params a, b;
            paramsDefault(&a); paramsDefault(&b);
            paramPrepare(&a); paramPrepare(&b);
            a.seed = b.seed = 4242;
            a.autoFireInterval = 0.0f; b.autoFireInterval = 0.25f;
            CHECK(appProbe(a, 240, 1) != appProbe(b, 240, 1),
                  L"连射间隔确实影响按住时开火次数（不是死参数）");
        }
        /* 预设：换一套预设必须连带换掉节奏参数，否则就是"假预设"。
           挑规则不同的两套来比（恒量靶场 vs 计时挑战），只换规则不换参数
           的话探针算出来的指纹会撞在一起。*/
        {
            int ta = -1, tb = -1, k;
            for (k = 0; k < PRESET_COUNT && (ta < 0 || tb < 0); ++k) {
                if (ta < 0 && g_presets[k].mode == MODE_RANGE) ta = k;
                if (tb < 0 && g_presets[k].mode == MODE_TIME) tb = k;
            }
            CHECK(ta >= 0 && tb >= 0, L"存在规则不同的两套预设（恒量靶场 / 计时挑战）");
            if (ta >= 0 && tb >= 0) {
                Params a, b;
                paramsDefault(&a); paramsDefault(&b);
                paramsApplyPreset(&a, ta); paramPrepare(&a);
                paramsApplyPreset(&b, tb); paramPrepare(&b);
                a.seed = b.seed = 4242;
                CHECK(appProbe(a, 60, 0) != appProbe(b, 60, 0),
                      L"换预设确实改变对局规则（不是死预设）");
            }
        }
        /* 难度涨幅：其它项全都一样，只有 ramp 不同，场上状态必须真的不一样。
           探针跑 900 步（7.5 秒）而不是 60 步 —— 涨幅是按秒累积的，半秒钟
           还看不出差别来，跑太短这条会变成"无论接没接上都过"。*/
        {
            int prog = -1, k;
            for (k = 0; k < PRESET_COUNT; ++k)
                if (g_presets[k].ramp > 0.0f) { prog = k; break; }
            CHECK(prog >= 0, L"存在一套涨幅大于 0 的预设");
            if (prog >= 0) {
                Params a, b;
                paramsDefault(&a); paramsDefault(&b);
                paramsApplyPreset(&a, prog);
                paramsApplyPreset(&b, prog);
                paramPrepare(&a); paramPrepare(&b);
                a.ramp = 0.0f;               /* 赋值必须在 paramPrepare 之后 —— */
                b.ramp = 0.80f;              /* paramPrepare 会用预设里的值覆盖掉 */
                a.seed = b.seed = 4242;
                CHECK(appProbe(a, 900, 0) != appProbe(b, 900, 0),
                      L"难度涨幅确实改变气球状态（不是死参数）");
            }
        }
    }

    /* ------------------------------------------- 恢复默认值 */
    {
        int good = 1;
        Params def;
        paramsDefault(&def);
        for (i = 0; i < g_paramDescCount; ++i) {
            const ParamDesc *d = &g_paramDescs[i];
            p = base;
            paramDescSet(&p, d, (d->lo + d->hi) * 0.5f);
            paramDescReset(&p, d);
            /* ★ 参照物是 paramsDefault 那一份，**不是 base**。base 是探针基线
               （有意把运动方式设成匀速漂移、四类球权重全给上），它和默认值
               本来就不一样；拿它当参照，会把"恢复得完全正确"判成失败。
               另外不能拿整个结构体比：半径上下限互为约束，单独恢复一项时
               夹取规则可能会顺手动到另一项。只比被恢复的这一项。*/
            if (fabsf(paramDescGet(&p, d) - paramDescGet(&def, d)) > 1e-3f) good = 0;
        }
        CHECK(good, L"每一项都能单独恢复成默认值");
    }

    /* ------------------------------------------- 显示文本 */
    {
        wchar_t buf[64];
        int nonEmpty = 1, boolOk = 1, enumOk = 1;
        for (i = 0; i < g_paramDescCount; ++i) {
            const ParamDesc *d = &g_paramDescs[i];
            paramDescText(&base, d, buf, (int)ARRAY_COUNT(buf));
            if (!buf[0]) nonEmpty = 0;
            if (d->kind == PK_BOOL && wcscmp(buf, L"开") && wcscmp(buf, L"关")) boolOk = 0;
            if (d->kind == PK_ENUM) {
                const wchar_t *nm = paramEnumName(d, (int)paramDescGet(&base, d));
                if (!nm || nm[0] == L'?' || !wcsstr(buf, nm)) enumOk = 0;
            }
        }
        CHECK(nonEmpty, L"每一项都能渲染出非空文本");
        CHECK(boolOk, L"开关项出「开」或「关」");
        CHECK(enumOk, L"枚举项文本里含它自己的中文名");

        /* 单位后缀要真的接上去，不然"0.55"这种数字读不出是什么意思。*/
        {
            int unitOk = 1, unitCount = 0;
            for (i = 0; i < g_paramDescCount; ++i) {
                const ParamDesc *d = &g_paramDescs[i];
                if (!d->unit) continue;
                ++unitCount;
                paramDescText(&base, d, buf, (int)ARRAY_COUNT(buf));
                if (!wcsstr(buf, d->unit)) unitOk = 0;
            }
            CHECK(unitCount > 0, L"表里有带单位的项");
            CHECK(unitOk, L"所有带单位的项都渲染出了单位");
        }
        /* 越界的枚举取值要返回问号，不能越界读名字表。*/
        CHECK(!wcscmp(paramEnumName(&g_paramDescs[0], -1), L"?"), L"枚举取值越界返回「?」");
        CHECK(!wcscmp(paramEnumName(&g_paramDescs[0], 999), L"?"), L"枚举取值越界返回「?」");
        /* 缓冲区很小时不能溢出写。*/
        {
            wchar_t tiny[4];
            paramDescText(&base, &g_paramDescs[0], tiny, 4);
            CHECK(tiny[3] == L'\0', L"文本缓冲区很小时不越界");
        }
    }

    /* ------------------------------------- 面板一级菜单的完整性
       预设、音量、静音、试听与"详细参数"入口都放在一级菜单上
       （放在显眼位置）。这张表漏一行，玩家
       就够不着那个功能，而菜单本身看着还挺正常 —— 所以要逐项盯。*/
    {
        int seenPreset[PRESET_COUNT];
        int nPreset = 0, hasCustom = 0, customRows = 0;
        memset(seenPreset, 0, sizeof(seenPreset));

        CHECK(g_paramMenuCount == PRESET_COUNT + 1,
              L"一级菜单 = 八套预设 + 一条进二级的入口，正好 9 行");
        CHECK(g_paramMenuDetailRow == g_paramMenuCount - 1,
              L"「自定义参数」是最后一行（入口放末尾，用户的手感就是这样）");

        for (i = 0; i < g_paramMenuCount; ++i) {
            const ParamMenuRow *m = &g_paramMenu[i];
            switch (m->kind) {
            case PMENU_PRESET:
                CHECK(m->arg >= 0 && m->arg < PRESET_COUNT, L"预设行的下标合法");
                if (m->arg >= 0 && m->arg < PRESET_COUNT && !seenPreset[m->arg]) {
                    seenPreset[m->arg] = 1;
                    ++nPreset;
                }
                break;
            case PMENU_CUSTOM:
                hasCustom = 1;
                ++customRows;
                CHECK(m->arg == -1, L"入口行不带 arg（它不是某一套预设）");
                break;
            default:
                CHECK(0, L"一级菜单里出现了未知的行类型");
                break;
            }
        }
        /* 八套预设**一套都不能少** —— 少一套用户就玩不到那组参数。*/
        CHECK(nPreset == PRESET_COUNT, L"八套预设在一级菜单里都有自己的一行");
        CHECK(customRows == 1, L"进二级的入口**只有一条**（两条会让回车有歧义）");
        CHECK(hasCustom, L"一级菜单里有「自定义参数」入口");
        /* ★ 这一条要叫「自定义参数」（原名「自定义设置」）。
           名字与说明一样是具名常量，所以这里能逐字量 —— 改错了会当场红。*/
        CHECK(wcscmp(g_panelCustomName, L"自定义参数") == 0,
              L"★ 弹窗最后一条的名字逐字为「自定义参数」");
        CHECK(wcsstr(g_panelDetailHelp, g_panelCustomName) != NULL,
              L"第 0 行的说明里用的是同一个名字，不出现第二个叫法");
        /* 声音相关的三行撤走了，声音开关是参数，归二级。
           这里反过来钉住"一级菜单上不该再有那些行类型" —— 不然将来有人
           顺手加回去，一级菜单又会变成"回车含义说不清"的样子。*/
        CHECK(PMENU_KIND_COUNT == 2,
              L"一级菜单只有两类行（预设 / 入口）；多一类就要重新想清楚回车干什么");
    }

    /* ------------------------------------- 参数说明（侧栏的文案）
       设置面板上带一个侧边说明面板，简单名词可以无需说明（说明内容为空）。
       所以这里**不能**要求每项都有说明，只能要求写了的
       都写对。*/
    {
        int withHelp = 0, bad = 0;
        for (i = 0; i < g_paramDescCount; ++i) {
            const ParamDesc *d = &g_paramDescs[i];
            size_t hl;
            if (!d->help) continue;
            ++withHelp;
            hl = wcslen(d->help);
            if (hl == 0) bad = 1;                    /* 空串 = 写了等于没写 */
            if (hl > 120) bad = 1;                   /* 太长会撑爆侧栏 */
            /* 说明不能只是把名字重复一遍 —— 那比留空更占地方。*/
            if (wcscmp(d->help, d->name) == 0) bad = 1;
        }
        CHECK(!bad, L"写了的说明都非空、不过长、且不是复述名字");
        /* 门槛：至少覆盖一半的参数项，否则侧栏基本是空的，等于没做。*/
        CHECK(withHelp * 2 >= g_paramDescCount,
              L"至少一半的参数项写了说明（侧栏不能基本是空的）");
        printf("      [面板] 参数说明：%d / %d 项（%.0f%%）\n",
               withHelp, g_paramDescCount,
               (g_paramDescCount > 0) ? (100.0 * withHelp / g_paramDescCount) : 0.0);
    }

    /* ------------------------------------- 预设与参数值是同一批
       paramsApplyPreset 的覆盖项必须能在描述表里找着（tPresets 已盯），
       这里反过来盯另一件事：预设写进去的字段名必须**真的在 Params 里**，
       而不是两条表各写各的。方法是对每一套预设套用之后取指纹，与
       paramsDefault 的结果比对 —— 预设 0 必须等于"出厂默认"。*/
    {
        Params d0, d1;
        paramsDefault(&d0);
        paramsDefault(&d1);
        paramsApplyPreset(&d1, 0);
        CHECK(paramsEqual(&d0, &d1), L"出厂默认就是第 0 套预设（没有第二份默认值）");
    }
}

/* ============================================================ 场景 */

static void tScene(void) {
    Params p;
    FieldRect f;
    group(L"场景与出球区");
    paramsDefault(&p);
    sceneComputeField(&p, &f);

    CHECK(f.x0 < f.x1 && f.y0 < f.y1, L"出球区是有效矩形");
    CHECK(f.x0 >= -WALL_HALF_W && f.x1 <= WALL_HALF_W, L"出球区在墙面横向可视范围内");
    CHECK(f.y0 >= 0.0f && f.y1 <= ROOM_H, L"出球区在墙面纵向范围内");
    CHECK(f.y0 > 0.0f && f.y1 < ROOM_H, L"出球区上下都留了边距");
    CHECK(f.x1 - f.x0 > 2.0f, L"出球区宽度足够摆下气球");
    CHECK(f.y1 - f.y0 > 1.0f, L"出球区高度足够摆下气球");
    CHECK(f.x0 < 0.0f && f.x1 > 0.0f, L"出球区跨过墙面中轴");
    CHECK(PLAY_LINE_Z > WALL_Z, L"警戒线在气球墙的前方（玩家这一侧）");
    CHECK(WALL_Z < PLAY_LINE_Z && PLAY_LINE_Z < ROOM_BACK_Z,
          L"三个 z 平面的前后关系正确：墙 < 警戒线 < 后墙");
    /* 房间必须**密闭**：气球墙要横跨整个房间宽度。气球墙比房间窄的时候，
       墙的两侧会各留一条缝，玩家一往前走视线就能从缝里穿出去看见虚空
       （02_look.png 左右两边的黑边就是这么来的）。这条断言把那个不变量
       钉住：以后谁把房间改宽、又忘了跟着改墙宽，这里立刻会红。*/
    CHECK(WALL_HALF_W >= ROOM_HALF_W - 1e-4f,
          L"★ 气球墙横跨整个房间（不留能看穿到虚空的缝）");
    CHECK(WALL_HALF_W <= ROOM_HALF_W + 1e-4f,
          L"气球墙不超出房间范围（超出去的部分会悬在房间外面）");
    {
        Vec3 w = sceneWallToWorld(1.5f, 2.5f, WALL_Z);
        CHECK(feq(w.x, 1.5f) && feq(w.y, 2.5f) && feq(w.z, WALL_Z),
              L"墙面局部坐标 → 世界坐标");
    }
    /* 出球区不能依赖参数里的气球数量 —— 数量变了区域不该跟着抖。*/
    {
        FieldRect g;
        Params q2 = p;
        q2.balloonCount = 1;
        sceneComputeField(&q2, &g);
        CHECK(feq(g.x0, f.x0) && feq(g.y1, f.y1),
              L"出球区不随气球数量变化（换数量不会让靶区跳舞）");
    }
}

/* ============================================================ 气球池 */

static void tBalloon(void) {
    Params p;
    FieldRect f;
    BalloonPool bp;
    double elapsed = 0.0;
    int i;

    paramsDefault(&p);
    p.ramp = 0.0f;                    /* 半径倍率恒为 1.00，便于精确断言 */
    p.allowSpecial = 0;               /* 全普通球，半径倍率 1.00 */
    p.radiusMin = p.radiusMax = 0.5f;
    p.motion = MOVE_STILL;            /* 位置稳定，便于断言 */
    p.bobAmp = 0.0f;
    p.lifetimeSec = 0.0f;             /* 不限时：先测"恒量" */
    p.balloonCount = 8;
    paramsClamp(&p);
    sceneComputeField(&p, &f);

    /* ---------------------------------------------------- 开局与恒量 */
    group(L"气球池：恒量");
    balloonPoolInit(&bp, &p, 4242u, &f);
    CHECK(bp.n == 8, L"池里正好 8 个槽位");
    CHECK(bp.n == p.balloonCount, L"槽位数 = 参数里的气球数");
    CHECK(balloonActiveCount(&bp) == 8, L"开局墙上就有 8 个（出现动画中）");
    CHECK(occupiedCount(&bp) == 8, L"开局 8 个槽位全部占位");

    advancePool(&bp, &p, &f, &elapsed, 1.0f, NULL);
    CHECK(occupiedCount(&bp) == 8, L"动画走完后仍是 8 个");
    {
        int liveAll = 1, armedAll = 1;
        for (i = 0; i < bp.n; ++i) {
            if (bp.slots[i].state != BSLOT_LIVE) liveAll = 0;
            if (!bp.slots[i].armed) armedAll = 0;
        }
        CHECK(liveAll, L"出现动画结束后全部进入存活态");
        CHECK(armedAll, L"存活态的球全部可被命中（armed）");
        CHECK(balloonActiveCount(&bp) == occupiedCount(&bp),
              L"不限时模式下「活着的球数」与「占位数」一致");
    }

    /* ------------------------------------------- 硬要求 */
    group(L"气球池：打一补一");
    {
        int before = balloonActiveCount(&bp);
        unsigned oldId = bp.slots[3].id;
        float expectR = bp.slots[3].baseR;
        float r0 = -1.0f;
        int t0 = -1;

        CHECK(balloonKill(&bp, &p, &f, elapsed, 3, &r0, &t0) == 1, L"击破第 3 个球成功");
        CHECK(feq(r0, expectR), L"击破回传的半径 = 该球当时的基准半径");
        CHECK(expectR > 0.40f && expectR < 0.55f,
              L"回传半径落在参数设定的 0.5 米附近（已含难度缩放）");
        CHECK(t0 == BALLOON_NORMAL, L"关闭特殊球后击破的是普通球");
        CHECK(balloonActiveCount(&bp) == before,
              L"★ 击破后墙上的球数不变（打掉一个立刻补一个）");
        CHECK(occupiedCount(&bp) == 8, L"★ 击破后 8 个槽位仍然全部占位");
        CHECK(bp.slots[3].state == BSLOT_GROW, L"补位的新球立刻进入出现动画");
        CHECK(bp.slots[3].id != oldId, L"补位的是一颗全新的球（id 变了）");
        CHECK(!bp.slots[3].armed, L"刚补位的球在长出来之前不可被命中");

        CHECK(balloonKill(&bp, &p, &f, elapsed, 3, &r0, &t0) == 0,
              L"同一个槽位不能连击两次（补位动画中拒绝）");
        CHECK(balloonKill(&bp, &p, &f, elapsed, -1, &r0, &t0) == 0, L"负下标被拒");
        CHECK(balloonKill(&bp, &p, &f, elapsed, 999, &r0, &t0) == 0, L"越界下标被拒");
        CHECK(occupiedCount(&bp) == 8, L"被拒绝的击破不会改变墙上的球数");
        CHECK(balloonKill(&bp, &p, &f, elapsed, 3, NULL, NULL) == 0,
              L"outR / outType 传 NULL 时不崩（此时槽位仍在动画中）");
    }

    /* ------------------------------------------- 连续清空 40 轮 */
    {
        int ok = 1, rounds;
        advancePool(&bp, &p, &f, &elapsed, 1.0f, NULL);
        for (rounds = 0; rounds < 40 && ok; ++rounds) {
            for (i = 0; i < bp.n; ++i) {
                if (bp.slots[i].state == BSLOT_LIVE) {
                    balloonKill(&bp, &p, &f, elapsed, i, NULL, NULL);
                    if (balloonActiveCount(&bp) != 8) { ok = 0; break; }
                    if (occupiedCount(&bp) != 8) { ok = 0; break; }
                }
            }
            advancePool(&bp, &p, &f, &elapsed, 0.25f, NULL);
            if (occupiedCount(&bp) != 8) ok = 0;
        }
        CHECK(ok, L"★ 连续 40 轮全墙清空，每一轮之后墙上恒为 8 个");
    }

    /* ------------------------------------------- 间距与极端情况 */
    group(L"气球池：间距与极端输入");
    {
        Params g = p;
        g.balloonCount = 6;
        g.radiusMin = g.radiusMax = 0.35f;
        paramsClamp(&g);
        balloonPoolInit(&bp, &g, 777u, &f);
        CHECK(balloonHasMinGap(&bp, 1.0f), L"6 个球在默认出球区里互不重叠（拒绝采样有效）");
    }
    {
        /* 墙塞不下这么多球：绝不能空转，也绝不能把球丢到墙外面去。*/
        Params e = p;
        FieldRect small;
        double el2 = 0.0;
        e.balloonCount = 30;
        e.radiusMin = e.radiusMax = 0.70f;
        paramsClamp(&e);
        small.x0 = -1.0f; small.x1 = 1.0f; small.y0 = 1.0f; small.y1 = 2.0f;

        balloonPoolInit(&bp, &e, 31337u, &small);
        advancePool(&bp, &e, &small, &el2, 1.0f, NULL);
        CHECK(bp.n == 30 && occupiedCount(&bp) == 30,
              L"★ 出球区塞不下时也不空转：30 个球一个不少地生成出来");
        {
            int inField = 1;
            for (i = 0; i < bp.n; ++i) {
                const Balloon *b = &bp.slots[i];
                if (b->x < small.x0 - 1e-3f || b->x > small.x1 + 1e-3f ||
                    b->y < small.y0 - 1e-3f || b->y > small.y1 + 1e-3f) inField = 0;
            }
            CHECK(inField, L"塞不下时球心仍被夹在出球区内（不会跑到墙外）");
        }
        /* 球心被夹在区内、半径又大，重叠是必然的。如实断言清楚，
           免得以后有人把这里的重叠当成漏了约束。*/
        CHECK(balloonHasMinGap(&bp, 1.0f) == 0,
              L"塞不下时的重叠是预期内的（间距约束无法满足，不是 bug）");
    }
    {
        /* 球比出球区还大：退化成居中，仍不许崩、不许出区。*/
        Params e = p;
        FieldRect tiny;
        double el2 = 0.0;
        e.balloonCount = 2;
        e.radiusMin = e.radiusMax = 2.00f;
        paramsClamp(&e);
        tiny.x0 = -0.3f; tiny.x1 = 0.3f; tiny.y0 = 2.0f; tiny.y1 = 2.6f;

        balloonPoolInit(&bp, &e, 4711u, &tiny);
        advancePool(&bp, &e, &tiny, &el2, 1.0f, NULL);
        CHECK(occupiedCount(&bp) == 2, L"球比出球区还大时也照样生成，不空转");
        {
            int finite = 1;
            for (i = 0; i < bp.n; ++i) {
                const Balloon *b = &bp.slots[i];
                if (!(b->x == b->x) || !(b->y == b->y) || !(b->r == b->r)) finite = 0;
                if (b->r < 0.0f || b->r > 5.0f) finite = 0;
            }
            CHECK(finite, L"退化尺寸下坐标与半径仍是有限值");
        }
    }

    /* ------------------------------------------- 超时逃走与补位 */
    group(L"气球池：超时逃走与补位");
    {
        Params e = p;
        double el2 = 0.0;
        int escaped = 0;
        e.balloonCount = 4;
        e.lifetimeSec = 1.0f;
        paramsClamp(&e);
        balloonPoolInit(&bp, &e, 55u, &f);
        advancePool(&bp, &e, &f, &el2, 3.0f, &escaped);
        CHECK(escaped > 0, L"存活时限到点后气球逃走（漏球被计入）");
        CHECK(balloonActiveCount(&bp) <= 4, L"逃走后墙上的球数不会超过 N");
        CHECK(occupiedCount(&bp) == 4, L"★ 逃走的气球也会被补回来，槽位始终占满");
    }
    {
        /* 补位延迟 > 0 是给玩家观察"墙会短暂缺一个"用的。
           此时槽位进入 WAIT，占位数会真的掉下来 —— 这条也要钉住，
           否则以后有人把 refillDelay 实现成无效参数就没人发现。*/
        Params e = p;
        double el2 = 0.0;
        int minOccupied = 99;
        int step;
        e.balloonCount = 4;
        e.refillDelaySec = 1.0f;
        paramsClamp(&e);
        balloonPoolInit(&bp, &e, 66u, &f);
        for (step = 0; step < 400; ++step) {
            int c;
            advancePool(&bp, &e, &f, &el2, (float)FIXED_DT, NULL);
            balloonKill(&bp, &e, &f, el2, 0, NULL, NULL);
            c = occupiedCount(&bp);
            if (c < minOccupied) minOccupied = c;
        }
        CHECK(minOccupied < 4, L"补位延迟 > 0 时墙上确实会短暂缺一个（参数是活的）");
        advancePool(&bp, &e, &f, &el2, 3.0f, NULL);
        CHECK(occupiedCount(&bp) == 4, L"延迟到点后仍然补满（延迟不会永久丢球）");
    }

    /* ------------------------------------------- 运动 */
    group(L"气球池：运动");
    {
        Params d = p;
        double el3 = 0.0;
        int step, inField = 1, rOk = 1;
        float firstX;
        d.motion = MOVE_LINEAR;
        d.driftSpeed = 1.2f;
        paramsClamp(&d);
        balloonPoolInit(&bp, &d, 909u, &f);
        advancePool(&bp, &d, &f, &el3, 0.5f, NULL);
        firstX = bp.slots[0].x;

        for (step = 0; step < 600; ++step) {
            advancePool(&bp, &d, &f, &el3, 0.05f, NULL);
            for (i = 0; i < bp.n; ++i) {
                const Balloon *b = &bp.slots[i];
                if (b->state == BSLOT_WAIT) continue;
                if (b->x < f.x0 + b->baseR - 1e-3f ||
                    b->x > f.x1 - b->baseR + 1e-3f) inField = 0;
                if (b->y < f.y0 + b->baseR - 1e-3f ||
                    b->y > f.y1 - b->baseR + 1e-3f) inField = 0;
                if (b->state == BSLOT_LIVE && !feq(b->r, b->baseR)) rOk = 0;
            }
        }
        CHECK(inField, L"漂移 30 秒，气球始终完整待在出球区内（撞边反弹正确）");
        CHECK(rOk, L"存活态的绘制半径 == 基准半径（判定与绘制同源）");
        CHECK(fabsf(bp.slots[0].x - firstX) > 1e-3f, L"漂移模式真的在动");
    }
    {
        /* 静止模式必须真的不动 —— 否则"静止"这个选项是假的。*/
        Params d = p;
        double el3 = 0.0;
        float x0;
        d.motion = MOVE_STILL;
        paramsClamp(&d);
        balloonPoolInit(&bp, &d, 4321u, &f);
        advancePool(&bp, &d, &f, &el3, 1.0f, NULL);
        x0 = bp.slots[2].x;
        advancePool(&bp, &d, &f, &el3, 3.0f, NULL);
        CHECK(feq(bp.slots[2].x, x0), L"静止模式下球的横向位置一动不动");
    }
    {
        /* 四种运动模式都不许把球弄出区、不许产生 NaN。*/
        int m, allOk = 1;
        for (m = 0; m < MOVE_COUNT; ++m) {
            Params d = p;
            double el3 = 0.0;
            int step;
            d.motion = m;
            d.driftSpeed = 2.0f;
            d.bobAmp = 0.3f;
            paramsClamp(&d);
            balloonPoolInit(&bp, &d, 2000u + (unsigned)m, &f);
            for (step = 0; step < 400; ++step) {
                advancePool(&bp, &d, &f, &el3, 0.05f, NULL);
                for (i = 0; i < bp.n; ++i) {
                    const Balloon *b = &bp.slots[i];
                    if (b->state == BSLOT_WAIT) continue;
                    if (!(b->x == b->x) || !(b->y == b->y)) allOk = 0;
                    if (b->x < f.x0 + b->baseR - 1e-3f ||
                        b->x > f.x1 - b->baseR + 1e-3f) allOk = 0;
                    if (b->y < f.y0 + b->baseR - 1e-3f ||
                        b->y > f.y1 - b->baseR + 1e-3f) allOk = 0;
                }
            }
        }
        CHECK(allOk, L"四种运动模式各跑 20 秒都不出区、不产生 NaN");
    }

    /* ------------------------------------------- 射线拾取 */
    group(L"气球池：射线拾取");
    {
        Params s = p;
        Vec3 origin, dir, c;
        float t;
        s.balloonCount = 1;               /* 只留一个球，排除"更近的球"干扰 */
        s.hitForgive = 1.0f;
        paramsClamp(&s);
        balloonPoolInit(&bp, &s, 2024u, &f);
        advancePool(&bp, &s, &f, &elapsed, 1.0f, NULL);
        CHECK(occupiedCount(&bp) == 1, L"单球模式下墙上就是 1 个");

        c = balloonWorldPos(&bp.slots[0]);
        CHECK(feq(c.z, WALL_Z + bp.slots[0].zOffset), L"世界坐标 z = 墙面 + 鼓出量");
        CHECK(c.z > WALL_Z, L"气球鼓在墙面之外（不是贴在墙上的贴纸）");

        origin = v3(c.x, c.y, c.z + 4.0f);
        dir = v3(0.0f, 0.0f, -1.0f);
        CHECK(balloonPick(&bp, &s, origin, dir, &t) == 0, L"正对球心的射线命中");
        CHECK(t > 0.0f && t < 4.0f, L"命中点在射线正方向上，且在球之前");

        /* 侧偏 1.6 倍半径：严格判定下打不中 */
        origin = v3(c.x + 1.6f * bp.slots[0].r, c.y, c.z + 4.0f);
        CHECK(balloonPick(&bp, &s, origin, dir, &t) == -1, L"侧偏 1.6r 在严格判定下打不中");
        {
            /* 把宽容度放到 2.0，同一根射线就该中了 ——
               这条同时证明了 hitForgive 是"活的"，不是摆设。*/
            Params s2 = s;
            s2.hitForgive = 2.0f;
            paramsClamp(&s2);
            CHECK(balloonPick(&bp, &s2, origin, dir, &t) == 0,
                  L"宽容度调到 2.0 后同一根射线命中（hitForgive 参数活性）");
        }
        {
            /* 反方向：把宽容度拉到最大也打不中 —— 判定不是"距离阈值"。*/
            Params s3 = s;
            s3.hitForgive = 2.0f;
            paramsClamp(&s3);
            CHECK(balloonPick(&bp, &s3, v3(c.x, c.y, c.z + 4.0f),
                              v3(0.0f, 0.0f, 1.0f), &t) == -1,
                  L"背向射线即使宽容度拉满也打不中");
        }
        CHECK(balloonPick(&bp, &s, v3(c.x, c.y + 3.0f, c.z + 4.0f),
                          v3(0.0f, 0.0f, -1.0f), &t) == -1, L"偏离到球上方打不中");
        CHECK(balloonPick(&bp, &s, v3(c.x, c.y, c.z + 4.0f), dir, NULL) == 0,
              L"outT 传 NULL 时照常返回命中的下标");

        /* 未长出前半段的球不该被打中 —— 否则"球还没冒出来就被打爆"。*/
        {
            Params s4 = s;
            s4.spawnAnimSec = 1.0f;
            paramsClamp(&s4);
            balloonPoolInit(&bp, &s4, 2025u, &f);
            advancePool(&bp, &s4, &f, &elapsed, 0.2f, NULL);   /* 进度 0.2 < SPAWN_ARM_RATIO */
            c = balloonWorldPos(&bp.slots[0]);
            CHECK(balloonPick(&bp, &s4, v3(c.x, c.y, c.z + 4.0f),
                              v3(0.0f, 0.0f, -1.0f), &t) == -1,
                  L"出现动画前半段的球不可被命中（arm 机制生效）");
            advancePool(&bp, &s4, &f, &elapsed, 1.0f, NULL);
            c = balloonWorldPos(&bp.slots[0]);
            CHECK(balloonPick(&bp, &s4, v3(c.x, c.y, c.z + 4.0f),
                              v3(0.0f, 0.0f, -1.0f), &t) == 0,
                  L"动画走完后同一颗球可以被命中");
        }
    }

    /* ------------------------------------------- 颜色 */
    group(L"气球池：颜色");
    {
        Params s = p;
        int k, distinct = 0;
        s.balloonCount = 8;
        paramsClamp(&s);
        balloonPoolInit(&bp, &s, 1212u, &f);
        advancePool(&bp, &s, &f, &elapsed, 1.0f, NULL);
        {
            Color3 c1 = balloonColor(&bp.slots[0], &s);
            Color3 c2 = balloonColor(&bp.slots[0], &s);
            CHECK(feq(c1.r, c2.r) && feq(c1.g, c2.g) && feq(c1.b, c2.b),
                  L"同一颗球每帧颜色一致（用 id 派生，不用随机数）");
        }
        for (k = 1; k < bp.n; ++k) {
            Color3 a = balloonColor(&bp.slots[0], &s);
            Color3 b = balloonColor(&bp.slots[k], &s);
            if (fabsf(a.r - b.r) > 1e-3f || fabsf(a.g - b.g) > 1e-3f) ++distinct;
        }
        CHECK(distinct >= 4, L"一墙气球的颜色确实各不相同");
        {
            /* 同一颗球重新染色要能被参数拨动 —— 饱和度上限是活的。*/
            Params s2 = s;
            Color3 a, b;
            s2.colorSat = 0.20f;
            paramsClamp(&s2);
            a = balloonColor(&bp.slots[0], &s);
            b = balloonColor(&bp.slots[0], &s2);
            CHECK(fabsf(a.r - b.r) > 1e-3f || fabsf(a.g - b.g) > 1e-3f ||
                  fabsf(a.b - b.b) > 1e-3f,
                  L"拨动饱和度上限后颜色确实变了（colorSat 参数活性）");
        }
    }

    /* ------------------------------------------- 特殊气球 */
    group(L"气球池：特殊气球");
    {
        Params s = p;
        int sawOther = 0, k, ok = 1;
        s.balloonCount = 30;
        s.allowSpecial = 1;
        /* ★ 权重也得给上。默认值已经不等于"什么都出"了 ——
           paramsDefault 现在就是"固定靶"那一套，它**有意**只出普通球
           （权重 100/0/0/0），光把 allowSpecial 打开是抽不到特殊球的。
           这里补上四类权重，测的才是"开关与权重都允许时会出特殊球"。*/
        for (k = 0; k < BALLOON_TYPE_COUNT; ++k) s.typeWeight[k] = 40;
        paramsClamp(&s);
        balloonPoolInit(&bp, &s, 606u, &f);
        for (k = 0; k < bp.n; ++k) {
            if (bp.slots[k].type < 0 || bp.slots[k].type >= BALLOON_TYPE_COUNT) ok = 0;
            if (bp.slots[k].type != BALLOON_NORMAL) sawOther = 1;
        }
        CHECK(ok, L"抽出的类型下标始终合法");
        CHECK(sawOther, L"开启特殊球后能抽到非普通球");

        s.allowSpecial = 0;
        balloonPoolInit(&bp, &s, 606u, &f);
        ok = 1;
        for (k = 0; k < bp.n; ++k)
            if (bp.slots[k].type != BALLOON_NORMAL) ok = 0;
        CHECK(ok, L"关闭特殊球后全是普通球（allowSpecial 参数活性）");

        /* 权重全为 0 时不能崩、不能抽到非法类型。*/
        s.allowSpecial = 1;
        s.typeWeight[BALLOON_NORMAL] = 0;
        s.typeWeight[BALLOON_SMALL]  = 0;
        s.typeWeight[BALLOON_GOLD]   = 0;
        s.typeWeight[BALLOON_SLOW]   = 0;
        paramsClamp(&s);
        balloonPoolInit(&bp, &s, 607u, &f);
        ok = 1;
        for (k = 0; k < bp.n; ++k)
            if (bp.slots[k].type != BALLOON_NORMAL) ok = 0;
        CHECK(ok, L"权重全 0 时回落到普通球而不是崩溃");
    }
    {
        CHECK(feq(g_balloonTypes[BALLOON_SMALL].radiusMul, 0.62f),
              L"小快球半径倍率 = 0.62");
        CHECK(g_balloonTypes[BALLOON_SMALL].speedMul > 1.0f, L"小快球速度倍率大于 1");
        CHECK(g_balloonTypes[BALLOON_GOLD].scoreMul > 1.0f, L"金球分数倍率大于 1");
        CHECK(g_balloonTypes[BALLOON_SLOW].speedMul < 1.0f, L"慢球速度倍率小于 1");
        CHECK(g_balloonTypes[BALLOON_SLOW].radiusMul > 1.0f, L"慢球比普通球大");
        CHECK(feq(g_balloonTypes[BALLOON_NORMAL].radiusMul, 1.0f) &&
              feq(g_balloonTypes[BALLOON_NORMAL].scoreMul, 1.0f),
              L"普通球是基准（倍率全为 1）");
    }

    /* ------------------------------------------- 参数活性 */
    group(L"气球池：参数活性（防死参数）");
    {
        /* 气球数量：拨动它，墙上的球数必须真的跟着变。*/
        Params s = p;
        int k, ok = 1;
        for (k = 1; k <= 12; ++k) {
            double e2 = 0.0;
            s.balloonCount = k;
            paramsClamp(&s);
            balloonPoolInit(&bp, &s, 100u + (unsigned)k, &f);
            advancePool(&bp, &s, &f, &e2, 1.0f, NULL);
            if (occupiedCount(&bp) != k) ok = 0;
        }
        CHECK(ok, L"balloonCount 从 1 到 12 逐档生效：墙上就是那么多个");
    }
    {
        /* 半径上下限：拨小它，球必须真的变小。*/
        Params s = p;
        double e2 = 0.0;
        float big, small;
        balloonPoolInit(&bp, &s, 555u, &f);
        advancePool(&bp, &s, &f, &e2, 1.0f, NULL);
        big = bp.slots[0].baseR;

        s.radiusMin = 0.10f;
        s.radiusMax = 0.12f;
        paramsClamp(&s);
        e2 = 0.0;
        balloonPoolInit(&bp, &s, 555u, &f);
        advancePool(&bp, &s, &f, &e2, 1.0f, NULL);
        small = bp.slots[0].baseR;
        CHECK(small < big, L"把半径区间拨小后球确实变小（radiusMin/Max 参数活性）");
        CHECK(small >= 0.10f * 0.9f, L"新半径贴住了设定的下限");
    }
    {
        /* 漂移速度：拨到 0 与拨到 2.0，同样两秒的位移必须不同。*/
        Params s = p;
        float moved0, moved2;
        s.motion = MOVE_LINEAR;

        s.driftSpeed = 0.0f;
        paramsClamp(&s);
        {
            double e2 = 0.0;
            float x;
            balloonPoolInit(&bp, &s, 111u, &f);
            advancePool(&bp, &s, &f, &e2, 1.0f, NULL);
            x = bp.slots[0].x;
            advancePool(&bp, &s, &f, &e2, 2.0f, NULL);
            moved0 = fabsf(bp.slots[0].x - x);
        }
        s.driftSpeed = 2.0f;
        paramsClamp(&s);
        {
            double e2 = 0.0;
            float x;
            balloonPoolInit(&bp, &s, 111u, &f);
            advancePool(&bp, &s, &f, &e2, 1.0f, NULL);
            x = bp.slots[0].x;
            advancePool(&bp, &s, &f, &e2, 2.0f, NULL);
            moved2 = fabsf(bp.slots[0].x - x);
        }
        CHECK(feq(moved0, 0.0f), L"漂移速度拨到 0 后气球一动不动");
        CHECK(moved2 > 0.3f, L"漂移速度拨到 2.0 后两秒内确实挪了位置（driftSpeed 参数活性）");
    }
}

/* ============================================================ 跟踪训练
 *
 * 这一组盯的规则：
 *   「初始有两个相邻的气球，始终保持墙面上有且仅有 2 个气球，且每次打掉一个后，
 *     新生成的气球要和剩的那一个气球在相邻的位置，允许斜着相邻但不允许生成在
 *     打掉的那个位置」
 *
 * "相邻"落地成的几何：
 *   新球圆心与**剩下那颗**的圆心距 d ∈ [下限, 上限] × (R_新 + R_剩)。
 *   跟踪训练预设取 1.0 ~ 2.0 倍，等径时正是 2R ~ 4R；同时新球不许与
 *   **刚被打掉那颗**的圆盘重叠（即"挖掉一个完整的半径为 R 的圆面积"）。
 *
 * 为什么把 d 做成一个区间而不是定值：旧规则把 d 钉死在 (半径和) × 1.06 这**一个**
 * 值上，而这里需要的是一个**范围**、要能自己调，所以取样从"一根圆"改成"一个环带"。
 * 断言也跟着从"恰好等于 L"变成区间断言，并补了一条独立可验的性质：环带里按**面积**均匀。
 */

/* 直接摆一颗球的位置与状态。正常玩法里这些字段只有 balloonUpdate 会写，
   但这里要验的是"给定这个局面，补位结果对不对"，所以由断言把局面搭出来。
   ax/ay 一起摆：死球的旧圆心由 balloonKill 从那儿取，而"围着谁生成"用的是
   当前位置 x/y —— 静止模式下两者本来就是同一个数。*/
static void placeBalloon(BalloonPool *bp, int i, float x, float y, float r) {
    Balloon *b = &bp->slots[i];
    b->state = BSLOT_LIVE;
    b->armed = 1;
    b->x = b->ax = x;
    b->y = b->ay = y;
    b->r = b->baseR = r;
    b->vx = 0.0f;
    b->bobPhase = 0.0f;
    b->age = 0.0f;
    b->life = 0.0f;
    b->hasDead = 0;
}

static float dist2D(const Balloon *a, const Balloon *b) {
    float dx = a->x - b->x, dy = a->y - b->y;
    return sqrtf(dx * dx + dy * dy);
}

static float normTwoPi(float t) {
    t = fmodf(t, 2.0f * PI_F);
    if (t < 0.0f) t += 2.0f * PI_F;
    return t;
}

/* 跟踪训练是第几套预设。★ 它从第 8 位（末尾）挪到了第 4 位，
   凡是「这一套自己」与「其余七套」的断言都从这一个常量取下标 ——
   再挪位置时，"按名字找的那一处"与"按个数数的那几处"不会走散。
   这个名字在 `g_presets` 里的位置另有「预设顺序」那一组断言逐位钉死。*/
#define PRESET_TRACK 3

static void tTrackSpawn(void) {
    FieldRect f;
    BalloonPool bp;
    Params p;
    const float R = 0.20f;               /* 半径摆小一点，好让整圈取位都离墙边很远 */
    float dmin, dmax;                    /* 环带的两个半径（等径时 = 2R × 上下限） */
    double e;
    int i;

    group(L"跟踪训练：邻位生成（环带）");

    /* ---------------------------------------------------- 预设本身 */
    paramsDefault(&p);
    paramsApplyPreset(&p, PRESET_TRACK);
    paramsClamp(&p);

    CHECK(PRESET_COUNT == 8, L"预设加到 8 套");
    CHECK(wcscmp(g_presets[PRESET_TRACK].name, L"跟踪训练") == 0,
          L"第 4 套预设叫「跟踪训练」（从第 8 位移到这里）");
    CHECK(g_presets[PRESET_TRACK].desc &&
          wcsstr(g_presets[PRESET_TRACK].desc, L"2R 至 4R") != NULL,
          L"它的说明里写清了新气球出现在 2R 至 4R 的环带内");
    CHECK(g_presets[PRESET_TRACK].desc &&
          wcsstr(g_presets[PRESET_TRACK].desc, L"重叠") != NULL,
          L"它的说明里写清了不与刚被击破的位置重叠");
    CHECK(p.balloonCount == 2, L"跟踪训练：墙上恒定 2 个");
    CHECK(p.spawnRule == SPAWN_ADJACENT, L"跟踪训练：生成位置 = 邻位生成");
    CHECK(feq(p.spawnDistMin, 1.0f) && feq(p.spawnDistMax, 2.0f),
          L"★ 跟踪训练：圆心距 = 1.0 ~ 2.0 倍半径和（等径时正是 2R ~ 4R）");
    CHECK(wcscmp(g_spawnNames[SPAWN_ADJACENT], L"邻位生成") == 0,
          L"面板上这个值叫「邻位生成」（范围可调之后，旧名与行为对不上）");
    CHECK(p.motion == MOVE_STILL && feq(p.driftSpeed, 0.0f),
          L"跟踪训练：气球不移动（固定靶）");
    CHECK(feq(p.bobAmp, 0.0f),
          L"★ 跟踪训练：浮动幅度为 0 —— 一浮动两颗球就竖直错开，「围着它生成」当场破功");
    CHECK(feq(p.lifetimeSec, 0.0f) && !p.missCostsLife,
          L"跟踪训练：气球不超时消失（不逃走）");
    CHECK(p.allowSpecial == 0 && feq(p.radiusMin, p.radiusMax),
          L"跟踪训练：清一色等径普通球（几何才干净、断言才严密）");

    /* 残值：从"邻位生成"切回别的预设，不许把这一连三项留在那儿。*/
    {
        Params q = p;
        paramsApplyPreset(&q, 0);
        CHECK(q.spawnRule == SPAWN_RANDOM,
              L"★ 从跟踪训练切回固定靶，「生成位置」不会留下「邻位生成」的残值");
        CHECK(feq(q.spawnDistMin, 1.0f) && feq(q.spawnDistMax, 1.0f),
              L"★ 切回固定靶后两项生成距离也回到 1.0 倍（不留 1.0 ~ 2.0 的残值）");
        paramsApplyPreset(&q, PRESET_TRACK);
        CHECK(q.spawnRule == SPAWN_ADJACENT && feq(q.spawnDistMax, 2.0f),
              L"再切回跟踪训练又能切回去");
    }

    /* 8 套预设**每一套**都必须明确写到全部的玩法参数（通用规矩）：
       起初只盯着 spawnRule 一项，后来把它推广成一条通用规矩 ——
       "任何被至少一套预设覆盖过的玩法参数，都必须被 8 套预设全部覆盖"。
       漏写的症状是"从跟踪训练切过去，某一项还留着上一套的值"，而那种错
       在界面上看不出来。四个**有意不覆盖**的参数（种子/色彩饱和度/亮度/
       统计面板）本来就没有任何预设碰它们，所以不受这条约束。*/
    {
        int d, k, bad = 0, checked = 0;
        for (d = 0; d < g_paramDescCount; ++d) {
            int cov = 0;
            if (g_paramDescs[d].group >= PARAM_GROUP_SECTION_SPLIT) continue;
            for (k = 0; k < PRESET_COUNT; ++k)
                if (paramCoveredByPreset(k, d)) ++cov;
            if (cov == 0) continue;                 /* 谁都不碰的，不管 */
            ++checked;
            if (cov != PRESET_COUNT) {
                ++bad;
                CHECK_NAMED(0, L"这一项只被部分预设覆盖（漏写就会留残值）",
                            g_paramDescs[d].name);
            }
        }
        CHECK(checked >= 25,
              L"（前置）被覆盖的玩法参数够多，这条断言不是空转");
        CHECK(bad == 0,
              L"★ 8 套预设覆盖的玩法参数集合完全一致（防残值，防将来漏写）");
    }

    /* ---------------------------------------------------- 开局：两颗球在环带里 */
    sceneComputeField(&p, &f);
    p.radiusMin = p.radiusMax = R;       /* 等径，几何才干净 */
    paramsClamp(&p);
    dmin = 2.0f * R * p.spawnDistMin;    /* 等径时"半径和"就是 2R */
    dmax = 2.0f * R * p.spawnDistMax;

    {
        int bad = 0, k;
        for (k = 0; k < 60; ++k) {        /* 换 60 个种子，开局都得是"环带里的两颗球" */
            double el = 0.0;
            float d;
            balloonPoolInit(&bp, &p, 900u + (unsigned)k, &f);
            if (bp.n != 2 || bp.slots[0].state == BSLOT_WAIT ||
                bp.slots[1].state == BSLOT_WAIT) { ++bad; continue; }
            advancePool(&bp, &p, &f, &el, 1.0f, NULL);
            if (bp.slots[0].state != BSLOT_LIVE || bp.slots[1].state != BSLOT_LIVE) ++bad;
            d = dist2D(&bp.slots[0], &bp.slots[1]);
            if (d < dmin - 1e-3f || d > dmax + 1e-3f) ++bad;
            if (spheresOverlap2D(bp.slots[0].x, bp.slots[0].y, bp.slots[0].baseR,
                                 bp.slots[1].x, bp.slots[1].y, bp.slots[1].baseR,
                                 1.0f)) ++bad;
            for (i = 0; i < 2; ++i)
                if (bp.slots[i].x < f.x0 + R - 1e-3f || bp.slots[i].x > f.x1 - R + 1e-3f ||
                    bp.slots[i].y < f.y0 + R - 1e-3f || bp.slots[i].y > f.y1 - R + 1e-3f) ++bad;
        }
        CHECK(bad == 0,
              L"★ 60 个种子开局都是「第二颗围着第一颗、落在 2R~4R 的环带里」（且不重叠、不出墙）");
    }

    /* ---------------------------------------------------- 补位：几何硬约束
     *
     * 把两颗球摆在出球区正中（等径、死球就摆在正东 2R 处，也就是"刚相切"），
     * 然后反复：打掉 slot 1 → 量新球落在哪儿。三种错法各有各的症状：
     *   · 半径没按环带取（钉死一个值）→ d 出了 [dmin, dmax]；
     *   · 忘了死球禁区（即"挖掉一个圆面积"）→ 新球压在刚打掉的位置上；
     *   · 漏了墙的截取 → 圆心落到出球区外。
     * 内圈那一档还额外量一次"禁区真的挡着"：死球正东，它的 ±60° 里一颗都不许有，
     * 而自由弧两侧必须有 —— 否则"禁区里没有"可能只是"那一圈本来就没样本"。
     */
    {
        const int N = 6000;
        float cx = (f.x0 + f.x1) * 0.5f;
        float cy = (f.y0 + f.y1) * 0.5f;
        float innerTop = dmin + 0.10f * (dmax - dmin);
        float lo = 1e9f, hi = -1e9f;
        int badKill = 0, badRange = 0, badDead = 0, badWall = 0;
        int nearInner = 0, inCone = 0, inFree = 0;
        int k;

        CHECK((f.x1 - f.x0) * 0.5f > dmax + R && (f.y1 - f.y0) * 0.5f > dmax + R,
              L"（前置）出球区够大：整圈取位都碰不到墙边");

        placeBalloon(&bp, 0, cx, cy, R);
        for (k = 0; k < N; ++k) {
            float dx, dy, d;
            /* 死球固定摆在"正东、恰好相切"处；补位后的球是"长出来"的状态，
               下一次循环开头会把它重新摆回死球位置。*/
            placeBalloon(&bp, 1, cx + dmin, cy, R);
            if (!balloonKill(&bp, &p, &f, 0.0, 1, NULL, NULL)) { ++badKill; continue; }

            dx = bp.slots[1].ax - cx;
            dy = bp.slots[1].ay - cy;
            d  = sqrtf(dx * dx + dy * dy);
            if (d < dmin - 1e-3f || d > dmax + 1e-3f) ++badRange;
            if (bp.slots[1].ax < f.x0 + R - 1e-3f || bp.slots[1].ax > f.x1 - R + 1e-3f ||
                bp.slots[1].ay < f.y0 + R - 1e-3f || bp.slots[1].ay > f.y1 - R + 1e-3f) ++badWall;
            {
                float ex = bp.slots[1].ax - (cx + dmin);
                float ey = bp.slots[1].ay - cy;
                if (sqrtf(ex * ex + ey * ey) < 2.0f * R - 1e-3f) ++badDead;
            }
            if (d < lo) lo = d;
            if (d > hi) hi = d;
            if (d <= innerTop) {
                /* 死球在正东 → 死球方位角 φ0 = 0；把新球的方位折到 [0, π] 比。*/
                float a = fabsf(atan2f(dy, dx));
                if (a > PI_F) a = 2.0f * PI_F - a;
                if (a < 50.0f * DEG2RAD_F) ++inCone;
                if (a > 65.0f * DEG2RAD_F && a < 115.0f * DEG2RAD_F) ++inFree;
                ++nearInner;
            }
        }
        CHECK(badKill == 0, L"（前置）6000 次击破全部成功");
        CHECK(badRange == 0, L"★ 每一次新球的圆心距都落在 [2R, 4R] 里（一次都没偏出去）");
        CHECK(badDead == 0, L"★ 每一次新球都没有压在「刚被打掉那颗」的圆盘上");
        CHECK(badWall == 0, L"★ 每一次新球的圆盘都整个留在出球区里");
        CHECK(nearInner > 300, L"（前置）内圈那一档的样本够多，下面两条不是空转");
        CHECK(inCone == 0, L"★ 内圈档：死球方位 ±50° 里一颗都没有（禁区真的挡着）");
        CHECK(inFree > 0, L"★ 内圈档：自由弧两侧照常有球（不是「禁区外也没样本」）");
        CHECK(lo < dmin + 0.01f && hi > dmax - 0.01f,
              L"★ 实测圆心距铺满 [2R, 4R]：近端贴着下界、远端够到上界");
    }

    /* ---------------------------------------------------- 面积均匀：可独立验的预言
     *
     * 把死球摆到 3 米开外 —— 这时它挡住的角**整圈都不存在**（判别式 T > 1），
     * W(d) 恒为 2π，环带上的密度就正比于 d。这正是"面积均匀"的解析结论：
     * 把 [dmin, dmax] 按**等面积**切成四条，每条应当各占 1/4。
     * 若实现改成了"圆心距均匀"，第一条会占到 32.3%、最后一条只剩 19.7% ——
     * 与 1/4 差 7 个百分点，±3% 的容差挡得住（N = 8000 时抽样误差约 0.5%）。
     */
    {
        const int N = 8000;
        float cx = (f.x0 + f.x1) * 0.5f;
        float cy = (f.y0 + f.y1) * 0.5f;
        float edges[5];
        int band[4];
        int k, bad = 0;
        double chi = 0.0;

        memset(band, 0, sizeof(band));
        for (k = 0; k <= 4; ++k)          /* 等面积分割：d_k² 等差 */
            edges[k] = sqrtf(dmin * dmin + (dmax * dmax - dmin * dmin) * (float)k / 4.0f);

        placeBalloon(&bp, 0, cx, cy, R);
        for (k = 0; k < N; ++k) {
            float dx, dy, d;
            int b;
            placeBalloon(&bp, 1, cx + 3.0f, cy, R);
            if (!balloonKill(&bp, &p, &f, 0.0, 1, NULL, NULL)) { ++bad; continue; }
            dx = bp.slots[1].ax - cx;
            dy = bp.slots[1].ay - cy;
            d = sqrtf(dx * dx + dy * dy);
            b = 0;
            while (b < 3 && d > edges[b + 1]) ++b;
            ++band[b];
        }
        CHECK(bad == 0, L"（前置）8000 次补位全部成功");
        for (k = 0; k < 4; ++k) {
            double pct = 100.0 * (double)band[k] / (double)N;
            double e2 = (double)band[k] - 0.25 * (double)N;
            chi += e2 * e2 / (0.25 * (double)N);
            if (pct < 21.0 || pct > 29.0) ++bad;
        }
        CHECK(bad == 0,
              L"★ 等面积四档各占 25%±3%（按面积均匀，不是按圆心距均匀）");
        CHECK(chi < 25.0,
              L"★ 卡方统计量落在自由度为 3 的正常范围内（半径分布确实是面积律）");
    }

    /* ---------------------------------------------------- 下限 = 上限：退化成一根圆
     *
     * 这是用户把滑条拖到底时**真会碰到**的合法设置。旧写法在这里会掉进
     * "尽力而为"兜底，而那条路挑的是"第一个不被扣分的点" —— 角度上几乎钉死，
     * 每颗新球都落在同一个地方。所以除了"半径只有一个值"，还要盯**角度仍然均匀**：
     * 400 次采样按角度排序后，相邻两颗的最大夹角必须远小于一整圈。
     */
    {
        Params q = p;
        const int N = 400;
        const float fixedD = 2.0f * R * 1.5f;      /* 上下限都写 1.5 倍 → 3R */
        float cx = (f.x0 + f.x1) * 0.5f;
        float cy = (f.y0 + f.y1) * 0.5f;
        float ang[400];
        int bad = 0, k, nm = 0;

        q.spawnDistMin = q.spawnDistMax = 1.5f;
        paramsClamp(&q);
        CHECK(feq(q.spawnDistMin, 1.5f) && feq(q.spawnDistMax, 1.5f),
              L"（前置）1.5 倍这一对值活在 [1.0, 6.0] 里，没被夹走");

        placeBalloon(&bp, 0, cx, cy, R);
        for (k = 0; k < N; ++k) {
            float dx, dy, d;
            placeBalloon(&bp, 1, cx + fixedD, cy, R);   /* 死球也摆在 3R 上 */
            if (!balloonKill(&bp, &q, &f, 0.0, 1, NULL, NULL)) { ++bad; continue; }
            dx = bp.slots[1].ax - cx;
            dy = bp.slots[1].ay - cy;
            d  = sqrtf(dx * dx + dy * dy);
            if (fabsf(d - fixedD) > 1e-3f) ++bad;
            ang[nm++] = normTwoPi(atan2f(dy, dx));
        }
        CHECK(bad == 0, L"★ 下限 = 上限 时所有落点都在同一个半径上（环带退化成一根圆）");

        {
            /* 死球与搭档的距离也是 3R，禁区半角 = acos(T)：
               T = (d² + dd² − m²)/(2·d·dd)，d = dd = 3R、m = 2R → T = 0.7778 → 38.9°。*/
            float maxGap = 0.0f;
            int cone = 0;
            for (k = 1; k < nm; ++k) {          /* 插入排序：nm 只有 400 */
                float v = ang[k];
                int j = k - 1;
                while (j >= 0 && ang[j] > v) { ang[j + 1] = ang[j]; --j; }
                ang[j + 1] = v;
            }
            for (k = 0; k < nm; ++k) {
                float a = ang[k];
                if (a > PI_F) a = 2.0f * PI_F - a;
                if (a < 30.0f * DEG2RAD_F) ++cone;
            }
            /* 只在**可用弧内部**量缝隙：把首尾连起来的那一段必然横穿死球禁区
               （那里本来就没有样本，约 78°），量进去就成了拿规则去罚实现。
               "每颗都长在同一个地方"这种错由下一条的跨度兜住。*/
            for (k = 1; k < nm; ++k) {
                float gap = ang[k] - ang[k - 1];
                if (gap > maxGap) maxGap = gap;
            }
            CHECK(cone == 0, L"★ 退化成一个圆时，死球的禁区照样挡着（±30° 里没有样本）");
            CHECK(nm > 380 && maxGap < 30.0f * DEG2RAD_F,
                  L"★ 退化成一个圆时可用弧里没有 30° 以上的空洞（角度照旧按弧长均匀）");
            CHECK(ang[nm - 1] - ang[0] > 200.0f * DEG2RAD_F,
                  L"★ 退化成一个圆时角度铺开了大半圈（不许掉进兜底、每颗都长在同一个地方）");
        }

        /* 拖反了要被交换，而不是变成空区间。*/
        q.spawnDistMin = 4.0f;
        q.spawnDistMax = 1.0f;
        paramsClamp(&q);
        CHECK(feq(q.spawnDistMin, 1.0f) && feq(q.spawnDistMax, 4.0f),
              L"★ 上下限拖反了：交换，而不是变成空区间（与半径那一对同一条规矩）");
    }

    /* ---------------------------------------------------- 贴边与贴角 */
    {
        float midY = (f.y0 + f.y1) * 0.5f;
        float midX = (f.x0 + f.x1) * 0.5f;
        float cases[4][2];
        int c, badKill = 0, badWall = 0;
        int badRange = 0, badDead = 0;
        cases[0][0] = f.x0 + R; cases[0][1] = midY;          /* 贴左墙 */
        cases[1][0] = f.x1 - R; cases[1][1] = midY;          /* 贴右墙 */
        cases[2][0] = midX;     cases[2][1] = f.y0 + R;      /* 贴地面 */
        cases[3][0] = midX;     cases[3][1] = f.y1 - R;      /* 贴天花板 */
        for (c = 0; c < 4; ++c) {
            int k;
            for (k = 0; k < 300; ++k) {
                float dx, dy, d;
                placeBalloon(&bp, 0, cases[c][0], cases[c][1], R);
                placeBalloon(&bp, 1, cases[c][0], cases[c][1] + dmin, R);
                if (!balloonKill(&bp, &p, &f, 0.0, 1, NULL, NULL)) { ++badKill; continue; }
                dx = bp.slots[1].ax - bp.slots[0].x;
                dy = bp.slots[1].ay - bp.slots[0].y;
                d  = sqrtf(dx * dx + dy * dy);
                if (d < dmin - 1e-3f || d > dmax + 1e-3f) ++badRange;
                /* 新球必须整个留在出球区里 */
                if (bp.slots[1].ax < f.x0 + R - 1e-3f || bp.slots[1].ax > f.x1 - R + 1e-3f ||
                    bp.slots[1].ay < f.y0 + R - 1e-3f || bp.slots[1].ay > f.y1 - R + 1e-3f)
                    ++badWall;
                {
                    float ex = bp.slots[1].ax - bp.slots[1].deadX;
                    float ey = bp.slots[1].ay - bp.slots[1].deadY;
                    if (sqrtf(ex * ex + ey * ey) < 2.0f * R - 1e-3f) ++badDead;
                }
            }
        }
        CHECK(badKill == 0, L"（前置）四面墙 × 300 次补位全部成功");
        CHECK(badWall == 0,
              L"★ 贴着四面墙生成时：新球整个留在出球区里（环带被墙切掉一块也不许出墙）");
        CHECK(badRange == 0,
              L"★ 贴着四面墙生成时，圆心距照样落在 [2R, 4R] 里（墙的截取不会把范围撑破）");
        CHECK(badDead == 0,
              L"★ 贴着四面墙生成时也没有压在刚被打掉的位置上");
    }

    /* ---------------------------------------------------- 环带整块在场外：兜底
     *
     * 造一个只比两个球位大一点点的出球区：新球的圆心只能落在 ±R 的方框里，
     * 离搭档最远 R√2 ≈ 1.41R，而环带内圈就有 2R —— 整块环带都在场外。
     * 这时正确的做法是"保墙内、距离范围让位"，而不是空转或把球扔到墙外。
     */
    {
        FieldRect tiny;
        BalloonPool tp;
        Params q = p;
        float cx = 0.0f, cy = 0.0f;
        tiny.x0 = cx - 2.0f * R; tiny.x1 = cx + 2.0f * R;
        tiny.y0 = cy - 2.0f * R; tiny.y1 = cy + 2.0f * R;
        balloonPoolInit(&tp, &q, 5u, &tiny);
        placeBalloon(&tp, 0, cx, cy, R);
        placeBalloon(&tp, 1, cx + dmin, cy, R);
        CHECK(balloonKill(&tp, &q, &tiny, 0.0, 1, NULL, NULL),
              L"★ 环带整块在场外时也不空转：补位照常发生（走「尽力而为」那条路）");
        CHECK(tp.slots[1].x >= tiny.x0 + R - 1e-3f && tp.slots[1].x <= tiny.x1 - R + 1e-3f &&
              tp.slots[1].y >= tiny.y0 + R - 1e-3f && tp.slots[1].y <= tiny.y1 - R + 1e-3f,
              L"★ 兜底把球夹回出球区内（此时距离范围只能让位给「不许出墙」）");
    }

    /* ---------------------------------------------------- 数量不是 2 时回落 */
    {
        Params q = p;
        int k, bad = 0;
        q.balloonCount = 1;
        paramsClamp(&q);
        balloonPoolInit(&bp, &q, 21u, &f);
        CHECK(bp.n == 1, L"数量调成 1 时池里就 1 个槽位");
        e = 0.0;
        advancePool(&bp, &q, &f, &e, 1.0f, NULL);
        CHECK(bp.slots[0].state == BSLOT_LIVE &&
              bp.slots[0].x >= f.x0 + q.radiusMin - 1e-3f &&
              bp.slots[0].x <= f.x1 - q.radiusMin + 1e-3f,
              L"★ 只有 1 颗球时「邻位生成」没有对象，回落到全场随机（球照常落在出球区里）");

        q.balloonCount = 8;
        q.radiusMin = q.radiusMax = R;
        paramsClamp(&q);
        for (k = 0; k < 20; ++k) {
            int a, b2;
            e = 0.0;
            balloonPoolInit(&bp, &q, 300u + (unsigned)k, &f);
            advancePool(&bp, &q, &f, &e, 1.0f, NULL);
            if (bp.n != 8) { ++bad; continue; }
            for (a = 0; a < bp.n; ++a) {
                if (bp.slots[a].state != BSLOT_LIVE) ++bad;
                if (bp.slots[a].x < f.x0 + R - 1e-3f || bp.slots[a].x > f.x1 - R + 1e-3f ||
                    bp.slots[a].y < f.y0 + R - 1e-3f || bp.slots[a].y > f.y1 - R + 1e-3f) ++bad;
                for (b2 = a + 1; b2 < bp.n; ++b2)
                    if (spheresOverlap2D(bp.slots[a].x, bp.slots[a].y, bp.slots[a].baseR,
                                         bp.slots[b2].x, bp.slots[b2].y, bp.slots[b2].baseR,
                                         1.0f)) ++bad;
            }
        }
        CHECK(bad == 0,
              L"★ 数量调成 8 时退化为全场随机：8 颗球都挂在墙上、都在出球区里、两两不重叠");
    }

    /* ---------------------------------------------------- 别的预设不受影响 */
    {
        Params q = p;
        int k, outside = 0;
        int presetBad = 0;
        for (k = 0; k < PRESET_COUNT; ++k) {   /* 跟踪训练以外那 7 套：生成位置必须还是
                                                   "全场随机"、范围 1.0 ——
                                                   这里按枚举值逐套查，不能按固定的前 7 个下标数。*/
            Params s;
            if (k == PRESET_TRACK) continue;
            paramsDefault(&s);
            paramsApplyPreset(&s, k);
            paramsClamp(&s);
            if (s.spawnRule != SPAWN_RANDOM ||
                !feq(s.spawnDistMin, 1.0f) || !feq(s.spawnDistMax, 1.0f)) ++presetBad;
        }
        CHECK(presetBad == 0,
              L"★ 其余 7 套预设的「生成位置」一律是全场随机、范围 1.0~1.0（新参数没串味）");

        q.balloonCount = 2;
        q.spawnRule = SPAWN_RANDOM;
        paramsClamp(&q);
        for (k = 0; k < 40; ++k) {        /* SPAWN_RANDOM 下两球不该总落在环带里 */
            float d;
            e = 0.0;
            balloonPoolInit(&bp, &q, 700u + (unsigned)k, &f);
            d = dist2D(&bp.slots[0], &bp.slots[1]);
            if (d < dmin - 1e-3f || d > dmax + 1e-3f) ++outside;
        }
        CHECK(outside >= 35,
              L"★ 「生成位置」= 全场随机时两颗球大多落在环带之外（老预设的行为一字未改）");
    }

    /* ---------------------------------------------------- 存档兼容 */
    {
        wchar_t dir[MAX_PATH], pathA[MAX_PATH], pathB[MAX_PATH];
        FILE *fp;
        unsigned char buf[16 + sizeof(Params)];
        size_t got;
        /* 两个历史长度**写死**（不写 sizeof(Params)−4 那种跟着走的算式 ——
           那样一加字段，"旧档"就悄悄变成了新档，测的东西全变味了）：
             176 = 44 个字段的旧布局（末尾还没有 spawnRule）
             180 = 45 个字段的旧布局（末尾多了 spawnRule）
           两份都还愿意读：少掉的那截 memset 补 0，再由 paramsClamp 夹成默认。*/
        const int oldSizes[2] = { 176, 180 };
        const int wantRule[2] = { SPAWN_RANDOM, SPAWN_ADJACENT };

        dir[0] = L'\0'; pathA[0] = L'\0'; pathB[0] = L'\0';
        if (GetTempPathW((DWORD)ARRAY_COUNT(dir), dir) > 0) {
            _snwprintf(pathA, ARRAY_COUNT(pathA) - 1, L"%lsbr_track_new.dat", dir);
            _snwprintf(pathB, ARRAY_COUNT(pathB) - 1, L"%lsbr_track_old.dat", dir);
        }
        if (!pathA[0]) { CHECK(0, L"取到临时目录（拿不到就没法测存档兼容）"); return; }
        _wremove(pathA);
        _wremove(pathB);

        {
            Params q = p;
            q.spawnRule = SPAWN_ADJACENT;
            q.mouseSens = 0.33f;                 /* 一个非默认值，验它没被读丢 */
            paramsClamp(&q);
            CHECK(paramsSave(&q, pathA) == 0, L"（前置）新布局存档能写出来");

            /* 从刚写出的那份真档头里取魔数与版本号 —— config.cpp 哪天换了
               魔数，这里跟着换，本条不会变成一句空话。*/
            fp = _wfopen(pathA, L"rb");
            got = fp ? fread(buf, 1, sizeof(buf), fp) : 0;
            if (fp) fclose(fp);
            CHECK(got == sizeof(buf), L"（前置）读回刚写的那份新档");

            for (i = 0; i < 2; ++i) {
                uint32_t crc = crc32Buf(buf + 16, (size_t)oldSizes[i]);
                uint32_t sz  = (uint32_t)oldSizes[i];
                FILE *fo;
                Params r;
                memcpy(buf + 8, &sz, 4);
                memcpy(buf + 12, &crc, 4);
                _wremove(pathB);
                fo = _wfopen(pathB, L"wb");
                if (!fo) { CHECK(0, L"（前置）能写出那份人造旧档"); continue; }
                fwrite(buf, 1, 16 + (size_t)oldSizes[i], fo);
                fclose(fo);

                memset(&r, 0xAB, sizeof(r));     /* 先涂脏，免得"读成功"是假的 */
                CHECK(paramsLoad(&r, pathB) == 0,
                      L"★ 旧档（少一个/两个字段）能被读进来，不再被判为不兼容");
                CHECK(r.spawnRule == wantRule[i],
                      L"★ 旧档读进来后「生成位置」照着旧布局的原样（缺的补 0 = 全场随机）");
                CHECK(feq(r.spawnDistMin, 1.0f) && feq(r.spawnDistMax, 1.0f),
                      L"★ 新加的这两项从缺省补出来，正好是 1.0 ~ 1.0（原有行为）");
                CHECK(feq(r.mouseSens, 0.33f) &&
                      feq(r.radiusMin, q.radiusMin) && r.balloonCount == q.balloonCount,
                      L"★ 旧档里对得上的那些字段一个都没读错（旧档里有的设置一项不丢）");
            }
            {
                Params r;
                CHECK(paramsLoad(&r, pathA) == 0 && r.spawnRule == SPAWN_ADJACENT &&
                      feq(r.spawnDistMax, 2.0f),
                      L"新档照常读回来，「生成位置」= 邻位生成、范围 1.0~2.0");
            }
        }
        _wremove(pathA);
        _wremove(pathB);
    }

    /* ---------------------------------------------------- 成绩册兼容 */
    {
        wchar_t dir[MAX_PATH], pathA[MAX_PATH], pathB[MAX_PATH];
        ScoreBook sb, rb;
        FILE *fp;
        unsigned char buf[8 + sizeof(ScoreBook)];
        size_t got;
        int oldBytes = (int)(sizeof(BestRecord) * 7);   /* 7 套预设时代的份量 */
        /* ★ 这份人造档必须连 **magic 一起**写成旧的那个（RBS1）。
           旧写法是"把新档的 8 字节头照抄、只截短 payload"，magic 仍是新的 ——
           那测的其实是"新格式但条数少"，而不是"旧格式的档"。
           现在 magic 本身就是一个判据（RBS1 的档要按换位前的顺序重排），
           所以这里必须显式写 RBS1，否则这一组断言会"绿着但没测到迁移"。*/
        uint32_t oldMagic = SCOREBOOK_MAGIC_LEGACY;

        dir[0] = L'\0'; pathA[0] = L'\0'; pathB[0] = L'\0';
        if (GetTempPathW((DWORD)ARRAY_COUNT(dir), dir) > 0) {
            _snwprintf(pathA, ARRAY_COUNT(pathA) - 1, L"%lsbr_track_sb_new.dat", dir);
            _snwprintf(pathB, ARRAY_COUNT(pathB) - 1, L"%lsbr_track_sb_old.dat", dir);
        }
        if (!pathA[0]) { CHECK(0, L"取到临时目录（拿不到就没法测成绩册兼容）"); return; }
        _wremove(pathA);
        _wremove(pathB);

        scoreBookInit(&sb);
        for (i = 0; i < PRESET_COUNT; ++i) {
            sb.rec[i].score = 1000 + i * 111;
            sb.rec[i].bestCombo = i + 1;
            sb.rec[i].popped = 10 + i;
            sb.rec[i].accuracyPct = 50 + i;
            sb.rec[i].seed = 900u + (unsigned)i;
        }
        CHECK(scoreBookSave(&sb, pathA) == 0, L"（前置）新布局成绩册能写出来");

        fp = _wfopen(pathA, L"rb");
        got = fp ? fread(buf, 1, sizeof(buf), fp) : 0;
        if (fp) fclose(fp);
        CHECK(got == sizeof(buf), L"（前置）读回刚写的那份成绩册");

        {
            uint32_t crc = crc32Buf(buf + 8, (size_t)oldBytes);
            FILE *fo;
            memcpy(buf, &oldMagic, 4);
            memcpy(buf + 4, &crc, 4);
            fo = _wfopen(pathB, L"wb");
            CHECK(fo != NULL, L"（前置）能写出那份人造的 7 套时代成绩册");
            if (fo) {
                fwrite(buf, 1, 8 + (size_t)oldBytes, fo);
                fclose(fo);
            }
        }
        memset(&rb, 0xAB, sizeof(rb));
        CHECK(scoreBookLoad(&rb, pathB) == 1,
              L"★ 更早的旧布局 记录.dat 照旧读得进来（RBS1 与 RBS2 都认）");
        {
            /* 老档第 k 档今天排第几。★ 这张表**在断言里写死**，故意不调
               presetIndexMigrateLegacyOrder —— 断言要是也走被测那张表，表错了
               断言跟着一起错，等于没测。写死的这 7 个数是"跟踪训练插到第 4 位"
               这件事的**独立复述**：0/1/2 不动，老 3..6 各往后一位。*/
            static const int to[7] = { 0, 1, 2, 4, 5, 6, 7 };
            int bad = 0, k;
            for (k = 0; k < 7; ++k)
                if (rb.rec[to[k]].score != 1000 + k * 111) ++bad;
            CHECK(bad == 0,
                  L"★ 用户那 7 条最高分一条不差、且各自搬到对应的预设名下（不是按原下标硬塞）");
            CHECK(rb.rec[3].score == 0,
                  L"★ 第 4 位（跟踪训练）留 0 —— 换位前它排第 8，老档里本来就没它的成绩");
        }
        CHECK(scoreBookLoad(&rb, pathA) == 1 && rb.rec[7].score == 1000 + 7 * 111,
              L"新档（RBS2）照常读回来，第 8 套那一条也在");
        CHECK(scoreBookLoad(&rb, pathA) == 1 && rb.rec[3].score == 1000 + 3 * 111,
              L"★ 新档**不做**任何换算：RBS2 里的第 4 位就是今天的第 4 位");
        _wremove(pathA);
        _wremove(pathB);
    }
}

/* ============================================================ 玩家 */

static void tPlayer(void) {
    Params p;
    Player pl;
    group(L"玩家与相机");
    paramsDefault(&p);
    playerInit(&pl);

    {
        Vec3 f = playerForward(&pl);
        CHECK(feq(f.x, 0.0f) && feq(f.z, -1.0f), L"yaw = 0 时朝向 -z（正对气球墙）");
        CHECK(feq(v3len(f), 1.0f), L"前方向是单位向量");
        CHECK(pl.pos.z > PLAY_LINE_Z, L"出生点在警戒线之后（合法站位）");
        CHECK(pl.pos.z < ROOM_BACK_Z, L"出生点在房间内");
    }
    {
        Vec3 r = playerRight(&pl);
        CHECK(feq(r.x, 1.0f) && feq(r.z, 0.0f), L"yaw = 0 时右方向是 +x");
        CHECK(feq(r.y, 0.0f), L"右方向没有竖直分量（侧移不会向上飘）");
    }
    {
        Player q = pl;
        playerLook(&q, 90.0f, 0.0f, &p);
        CHECK(feq(q.yaw, PI_F * 0.5f), L"转 90° 后 yaw = π/2");
        CHECK(feq(playerForward(&q).x, -1.0f), L"左转 90° 后朝向 -x");
    }
    {
        /* 偏航累积到很大时要绕回，避免浮点精度掉光。*/
        Player q = pl;
        int i;
        for (i = 0; i < 2000; ++i) playerLook(&q, 30.0f, 0.0f, &p);
        CHECK(fabsf(q.yaw) <= PI_F * 2.0f + 1e-3f, L"偏航角被绕回一圈以内（不会无限累加）");
        CHECK(fabsf(playerForward(&q).x) <= 1.0f + 1e-4f, L"绕回后仍能算出合法的前方向");
    }
    {
        Player q = pl;
        playerLook(&q, 0.0f, 1000.0f, &p);
        CHECK(q.pitch < PI_F * 0.5f, L"俯仰被夹在 90° 以内");
        CHECK(q.pitch > 0.0f, L"抬头方向为正且没有被夹反");
        playerLook(&q, 0.0f, -5000.0f, &p);
        CHECK(q.pitch > -PI_F * 0.5f, L"俯仰下限同样被夹住");
    }
    {
        Player q = pl;
        float up;
        playerLook(&q, 0.0f, 10.0f, &p);
        up = q.pitch;
        CHECK(up > 0.0f, L"鼠标下拉前俯仰为正");
        q = pl;
        {
            Params ip = p;
            ip.invertY = 1;
            paramsClamp(&ip);
            playerLook(&q, 0.0f, 10.0f, &ip);
            CHECK(feq(q.pitch, -up), L"Y 轴反转真的把俯仰取了反（invertY 参数活性）");
        }
    }
    {
        /* 走动：不允许穿过气球墙，也不允许退出房间。*/
        Player q = pl;
        int i;
        for (i = 0; i < 400; ++i)
            playerMove(&q, &p, v3(0.0f, 0.0f, 1.0f), 0.05f, 0);   /* 一路向前 */
        CHECK(q.pos.z >= PLAY_LINE_Z + PLAYER_RADIUS - 1e-3f, L"一直往前走也越不过警戒线");
        for (i = 0; i < 900; ++i)
            playerMove(&q, &p, v3(0.0f, 0.0f, -1.0f), 0.05f, 0);  /* 一路后退 */
        CHECK(q.pos.z <= ROOM_BACK_Z - PLAYER_RADIUS + 1e-3f, L"后退不会穿出后墙");
        for (i = 0; i < 900; ++i)
            playerMove(&q, &p, v3(1.0f, 0.0f, 0.0f), 0.05f, 0);
        CHECK(q.pos.x <= ROOM_HALF_W - PLAYER_RADIUS + 1e-3f, L"右移不会穿出右墙");
        for (i = 0; i < 900; ++i)
            playerMove(&q, &p, v3(-1.0f, 0.0f, 0.0f), 0.05f, 0);
        CHECK(q.pos.x >= -ROOM_HALF_W + PLAYER_RADIUS - 1e-3f, L"左移不会穿出左墙");
        CHECK(feq(q.pos.y, 0.0f), L"走动过程中脚底始终贴地（y 恒为 0）");
    }
    {
        /* 斜着走不该比直着走快，也不该比直着走慢。
           起点必须挑在**远离任何边界**的地方：出生点距警戒线只有 1.66 米，
           直着走 1.7 米会撞上房间夹取，位移被吃掉之后这条断言测的就不再是
           归一化，而会莫名其妙地报红（出生点从 z=0.8 挪到 z=-1.2 时就真报了）。
           所以这里换到房间中段起跑，另外补一条断言专门盯住"没被夹取"这个
           前提 —— 将来谁再挪出生点，看到的是前提断言失败，而不是这条误报。*/
        Player a = pl, b = pl;
        Vec3 an, bn;
        const float z0 = 2.0f;
        int i;
        a.pos = v3(0.0f, 0.0f, z0);
        b.pos = v3(0.0f, 0.0f, z0);
        for (i = 0; i < 10; ++i) {
            playerMove(&a, &p, v3(0.0f, 0.0f, 1.0f), 0.05f, 0);
            playerMove(&b, &p, v3(1.0f, 0.0f, 1.0f), 0.05f, 0);
        }
        an = v3(0.0f, 0.0f, fabsf(a.pos.z - z0));
        bn = v3(fabsf(b.pos.x), 0.0f, fabsf(b.pos.z - z0));
        CHECK(bn.x > 0.0f && bn.z > 0.0f, L"斜走同时产生了 x 与 z 的位移");
        CHECK(feq(an.z, 10.0f * 0.05f * p.moveSpeed),
              L"直走 10 步的位移没有被房间边界吃掉（本组断言的前提）");
        CHECK(v3len(bn) <= v3len(an) + 1e-3f, L"斜走不加速（意图向量被归一化）");
        CHECK(v3len(bn) > v3len(an) * 0.9f, L"斜走也不减速（不是把速度打了个折）");
    }
    {
        /* 蹲下：视点变低，且是平滑过渡不是瞬跳。*/
        Player q = pl;
        float h0 = q.eyeHeight;
        int i;
        playerMove(&q, &p, v3zero(), (float)FIXED_DT, 1);
        CHECK(q.eyeHeight < h0, L"蹲下时视点开始下降");
        CHECK(q.eyeHeight > h0 * 0.6f, L"蹲下是平滑的，不是一帧到位");
        for (i = 0; i < 200; ++i) playerMove(&q, &p, v3zero(), (float)FIXED_DT, 1);
        CHECK(q.eyeHeight < h0 * 0.7f, L"持续蹲下后视点稳定在低位");
        {
            float low = q.eyeHeight;
            for (i = 0; i < 400; ++i) playerMove(&q, &p, v3zero(), (float)FIXED_DT, 0);
            CHECK(q.eyeHeight > low, L"站起来后视点回升");
            CHECK(fabsf(q.eyeHeight - h0) < 1e-2f, L"完全站起后视点回到基准高度");
        }
    }
    {
        /* 相机射线与渲染用的朝向必须完全一致 —— 这是"所见即所得"的地基。*/
        Player q = pl;
        Vec3 ro, rd, f, eye;
        playerLook(&q, 23.0f, -11.0f, &p);
        playerRay(&q, &ro, &rd);
        CHECK(feq(v3len(rd), 1.0f), L"相机射线方向已归一化");
        f = playerForward(&q);
        CHECK(feq(rd.x, f.x) && feq(rd.y, f.y) && feq(rd.z, f.z),
              L"相机射线方向 == 渲染用的前方向");
        eye = playerEyePos(&q);
        CHECK(feq(ro.x, eye.x) && feq(ro.y, eye.y) && feq(ro.z, eye.z),
              L"相机射线起点 == 渲染用的眼点");
        CHECK(feq(ro.y, q.pos.y + q.eyeHeight + q.bobOffset),
              L"眼点高度 = 脚底 + 视高 + 晃动");
    }
    {
        /* 视点高度从面板上删掉了（真实身高是别人的习惯，不是玩法），
           固定成 EYE_HEIGHT_DEF。参数没了，**行为还在** —— 断言它确实还被
           读到，而不是跟着滑杆一起被删干净了。*/
        Params e = p;
        Player q;
        playerInit(&q);
        CHECK(feq(q.eyeHeight, EYE_HEIGHT_DEF), L"视点高度取值就是 EYE_HEIGHT_DEF");
        CHECK(EYE_HEIGHT_DEF > 1.0f && EYE_HEIGHT_DEF < 2.2f,
              L"视点高度在一个像人的范围里");
        CHECK(feq(playerEyePos(&q).y, q.pos.y + EYE_HEIGHT_DEF + q.bobOffset),
              L"眼点高度读的确实是这个常量（删了滑杆但没有删掉行为）");
    }
    {
        /* 删字段最容易留下的残影：描述表里两项指着同一个字段。那会让面板上
           冒出两根滑杆调同一个东西，看起来还挺正常。一口气删了十来项，
           正是最容易串行号的时候。*/
        int k, m, dup = 0;
        for (k = 0; k < g_paramDescCount; ++k)
            for (m = k + 1; m < g_paramDescCount; ++m)
                if (g_paramDescs[k].offset == g_paramDescs[m].offset) dup = 1;
        CHECK(!dup, L"描述表里没有两项指向同一个字段");
    }
    {
        /* 移动速度参数必须是活的。*/
        /* 这里曾经拿硬编码的 0.8 当"出生点"去比位移，出生点一改这条就废了。
           改成从 a 身上取实际的出生 z，断言就与出生点解耦了。*/
        Params f = p;
        Player a, b;
        float z0;
        f.moveSpeed = 1.0f;
        paramsClamp(&f);
        playerInit(&a);
        z0 = a.pos.z;
        playerMove(&a, &f, v3(0.0f, 0.0f, 1.0f), 1.0f, 0);
        f.moveSpeed = 8.0f;
        paramsClamp(&f);
        playerInit(&b);
        playerMove(&b, &f, v3(0.0f, 0.0f, 1.0f), 1.0f, 0);
        CHECK(fabsf(b.pos.z - z0) > fabsf(a.pos.z - z0),
              L"移动速度拨大后一秒走得更远（moveSpeed 参数活性）");
    }
    {
        /* 观感的地基：把相机摆到正对气球，射线必须命中。
           这一条把"准星指着什么就打中什么"从注释变成了断言。*/
        Params s;
        BalloonPool bp;
        FieldRect fr;
        Vec3 ro, rd;
        float t;
        Player q;
        paramsDefault(&s);
        s.balloonCount = 1;
        s.radiusMin = s.radiusMax = 0.6f;
        s.allowSpecial = 0;
        s.motion = MOVE_STILL;
        s.bobAmp = 0.0f;
        s.lifetimeSec = 0.0f;
        paramsClamp(&s);
        sceneComputeField(&s, &fr);
        balloonPoolInit(&bp, &s, 8080u, &fr);
        {
            double e2 = 0.0;
            advancePool(&bp, &s, &fr, &e2, 1.0f, NULL);
        }
        {
            Vec3 c = balloonWorldPos(&bp.slots[0]);
            q = pl;
            q.yaw = 0.0f;
            q.pitch = 0.0f;
            q.pos = v3(c.x, 0.0f, 1.0f);
            /* 把视点抬到与球心等高，射线就是一条水平线正对球心 ——
               不靠任何三角函数凑角度，结果完全确定。*/
            q.eyeHeight = c.y;
            q.bobOffset = 0.0f;
            playerClampToRoom(&q);
            playerRay(&q, &ro, &rd);
            CHECK(feq(ro.y, c.y), L"眼点高度确实落在球心高度上");
            CHECK(feq(ro.z, 1.0f) && feq(ro.x, bp.slots[0].x), L"眼点位置与设定的站位一致");
            CHECK(feq(rd.z, -1.0f), L"水平朝前时射线方向是 -z");
            CHECK(balloonPick(&bp, &s, ro, rd, &t) == 0,
                  L"★ 视点正对气球时射线命中（准星指着什么就打中什么）");
        }
    }
}

/* ============================================================ 字形图集
 *
 * 只碰 hudCollectCharset / hudCharIndex 这两个纯计算的函数，
 * 不建 GDI 位图也不碰 GL —— 自检模式底下没有窗口。
 */
static void tHud(void) {
    static HudFont h;          /* 12KB 出头，放静态区免得压爆栈 */
    int i, j, bad = 0, unsorted = 0, dup = 0;

    group(L"中文字形图集（字符集合与查找）");

    hudCollectCharset(&h);

    /* 1) 容量。收集阶段装的是"多重集"，撞上限会静默丢字 ——
          必须在自检里就报出来，而不是等 HUD 上冒出空心方块。*/
    CHECK(!h.overflow, L"★ 文案字符总数没有超过 HUD_MAX_CHARS（撞上限会丢字）");
    CHECK(h.total <= HUD_MAX_CHARS, L"收进的字符数不超过容量");
    CHECK(h.count > 0 && h.count <= h.total, L"去重后的字数不超过收进的个数");

    /* 2) 排序 + 去重的不变式：严格递增。第二条断言一旦失败，
          二分查找就会漏字，而漏字的表现是"某些字变成空心方块"，
          离原因很远，所以这里要单独盯住。*/
    for (i = 1; i < h.count; ++i) {
        if (h.chars[i] <= h.chars[i - 1]) {
            if (h.chars[i] == h.chars[i - 1]) ++dup; else ++unsorted;
        }
    }
    CHECK(dup == 0, L"★ 字符集合里没有重复项");
    CHECK(unsorted == 0, L"★ 字符集合按码点严格递增");

    /* 3) 二分查找与线性查找结果一致。*/
    bad = 0;
    for (i = 0; i < h.count; ++i) {
        int found = -1;
        for (j = 0; j < h.count; ++j)
            if (h.chars[j] == h.chars[i]) { found = j; break; }
        if (hudCharIndex(&h, h.chars[i]) != found) ++bad;
    }
    CHECK(bad == 0, L"二分查找与线性查找对每个字都给同一个下标");

    /* 4) 表里用到的字一个都不能少 —— 这就是那次截断 bug 的正面断言。
          遍历静态文案表，逐字回查，缺一个就失败。*/
    bad = 0;
    for (i = 0; i < g_hudStringCount; ++i) {
        const wchar_t *s = g_hudStrings[i];
        for (j = 0; s && s[j]; ++j) {
            if (s[j] < 32) continue;
            if (hudCharIndex(&h, s[j]) < 0) ++bad;
        }
    }
    CHECK(bad == 0, L"★ 文案表里的每一个字都能在图集里查到（缺一个就是一个空心方块）");

    /* 4b) 参数面板上会出现的每一段文字，它的每个字也必须在图集里。
           参数名、分组名、枚举名、单位名都由 config.cpp 的描述表提供，
           不经过 g_hudStrings —— 上面第 4 条查不到它们。面板是后加的界面，
           最容易出现"改个名字，界面上多出一个空心方块"这种事。*/
    bad = 0;
    {
        int g;
        for (g = 0; g < g_paramGroupCount; ++g) {
            const wchar_t *s = g_paramGroupNames[g];
            for (j = 0; s && s[j]; ++j) if (s[j] >= 32 && hudCharIndex(&h, s[j]) < 0) ++bad;
        }
        for (i = 0; i < g_paramDescCount; ++i) {
            const ParamDesc *d = &g_paramDescs[i];
            const wchar_t *strs[2];
            int k;
            strs[0] = d->name;
            strs[1] = d->unit;
            for (k = 0; k < 2; ++k) {
                const wchar_t *s = strs[k];
                for (j = 0; s && s[j]; ++j) if (s[j] >= 32 && hudCharIndex(&h, s[j]) < 0) ++bad;
            }
            if (d->kind == PK_ENUM) {
                int v;
                for (v = 0; v < d->enumCount; ++v) {
                    const wchar_t *s = paramEnumName(d, v);
                    for (j = 0; s && s[j]; ++j) if (s[j] >= 32 && hudCharIndex(&h, s[j]) < 0) ++bad;
                }
            }
        }
    }
    CHECK(bad == 0, L"★ 参数面板上会出现的每一个字都能在图集里查到");

    /* 5) 排版符号逐项检查。它们只出现在拼接格式串里，最容易漏。*/
    CHECK(hudCharIndex(&h, L'·') >= 0, L"间隔号 ·（模式 · 难度）在图集里");
    CHECK(hudCharIndex(&h, L'×') >= 0, L"乘号 ×（连击倍率）在图集里");
    CHECK(hudCharIndex(&h, L'｜') >= 0, L"竖线 ｜ 在图集里");

    /* 6) 动态拼出来的数字必须有：得分、FPS 这些都不是静态文案。*/
    bad = 0;
    for (j = 32; j < 127; ++j)
        if (hudCharIndex(&h, (wchar_t)j) < 0) ++bad;
    CHECK(bad == 0, L"可打印 ASCII 全在集合里（数字与百分号靠它兜底）");

    /* 7) 查不到的字符返回 -1，而不是瞎给一个下标。*/
    CHECK(hudCharIndex(&h, (wchar_t)0x0001) < 0, L"控制字符查不到");
    CHECK(hudCharIndex(&h, (wchar_t)0xE000) < 0, L"私用区码点查不到");
}

/* ============================================================ 计分与成绩记录 */

/* 这一组是收尾前逐函数点数才补上的：原设计里写着 L1 覆盖"漏球/连击/计分
   公式"，可 score.cpp 与 score.h 当时一行断言都没有 —— 计分是整个"训练"
   玩法的骨架，没有断言就等于没验。
   断言用的是**手算出来的期望值**（写在注释里），不是把公式照抄一遍再算
   一遍：照抄等于什么都没验，公式写错了它也跟着错。*/
static void tScore(void) {
    Params p;
    int i;

    repW(L"\n== 计分与成绩记录 ==\n");
    /* 这一组要手算分数，需要一份半径上下限明确的参数。出厂默认是
       "大小全部相同"（0.40 / 0.40），退化成一个点，算不出 14..42 那三个
       基准点 —— 那不是 bug，是"固定靶"这个预设的定义。所以这里显式取
       移动靶那套（大小随机），手算值与早先一致。*/
    paramsApplyPreset(&p, 1);
    p.allowSpecial = 0;               /* 全普通球，半径倍率恒为 1.00 */
    p.radiusMin = 0.30f;
    p.radiusMax = 0.75f;
    paramsClamp(&p);
    CHECK(feq(p.radiusMin, 0.30f) && feq(p.radiusMax, 0.75f),
          L"计分测试的半径基准就位（0.30 / 0.75）");

    /* --------------------------------------------- 连击倍率 */
    CHECK(feq(scoreComboMultiplier(0), 1.0f), L"连击 0 → 倍率 1.00");
    CHECK(feq(scoreComboMultiplier(10), 1.5f), L"连击 10 → 倍率 1.50");
    CHECK(feq(scoreComboMultiplier(20), 2.0f), L"连击 20 → 倍率 2.00（封顶）");
    CHECK(feq(scoreComboMultiplier(999), COMBO_MULT_MAX), L"连击再高也不越过上限");
    CHECK(feq(scoreComboMultiplier(-5), 1.0f), L"负连击按 0 处理，不产生小于 1 的倍率");
    {
        int bad = 0;
        for (i = 1; i <= 60; ++i)
            if (scoreComboMultiplier(i) < scoreComboMultiplier(i - 1) - 1e-6f) ++bad;
        CHECK(bad == 0, L"连击倍率随连击数单调不减");
    }

    /* ------------------------------------------------------ 基础分 520/r
       默认参数下 0.30 m 对应 14 px、0.75 m 对应 42 px、0.525 m 对应 28 px，
       手算：520/14 = 37.14 → 37；520/42 = 12.38 → 12；520/28 = 18.57 → 19。*/
    CHECK(feq((float)scoreBaseOf(0.30f, &p), 37.0f), L"最小半径 → 基础分 37（手算值）");
    CHECK(feq((float)scoreBaseOf(0.75f, &p), 12.0f), L"最大半径 → 基础分 12（手算值）");
    CHECK(feq((float)scoreBaseOf(0.525f, &p), 19.0f), L"半径中点 → 基础分 19（手算值）");
    CHECK(scoreBaseOf(0.05f, &p) == 37, L"小于下限的半径按 14 px 算，不越界");
    CHECK(scoreBaseOf(9.00f, &p) == 12, L"大于上限的半径按 42 px 算，不越界");
    CHECK(scoreBaseOf(0.30f, &p) >= SCORE_MIN && scoreBaseOf(0.30f, &p) <= SCORE_MAX,
          L"基础分落在 10..40 的区间内");
    {
        int bad = 0, prev = scoreBaseOf(0.30f, &p);
        float r;
        for (r = 0.32f; r <= 0.76f; r += 0.02f) {
            int cur = scoreBaseOf(r, &p);
            if (cur > prev) ++bad;
            prev = cur;
        }
        CHECK(bad == 0, L"半径越大基础分越低（单调不减地掉）");
    }

    /* ------------------------------------------------------------ 实得分
       手算：37 × 1.00 = 37；37 × 1.50 = 55.5，加 0.5 取整 = 56；
             37 × 2.00 = 74，加 0.5 取整 = 74。*/
    CHECK(scoreGainOf(0.30f, &p, 0) == 37, L"没有连击时实得分 = 基础分");
    CHECK(scoreGainOf(0.30f, &p, 10) == 56, L"连击 10 → 37×1.5 = 55.5，四舍五入到 56");
    CHECK(scoreGainOf(0.30f, &p, 20) == 74, L"满连击 → 37×2 = 74");
    {
        int bad = 0, prev = scoreGainOf(0.30f, &p, 0);
        for (i = 1; i <= 40; ++i) {
            int cur = scoreGainOf(0.30f, &p, i);
            if (cur < prev) ++bad;
            prev = cur;
        }
        CHECK(bad == 0, L"同一半径下连击越高实得分不降");
    }

    /* ---------------------------------------------------------- 成绩单字段 */
    {
        RunResult r;
        scoreFillResult(&r, 1234, 40, 5, 50, 30, 12, MODE_TIME, 4, 61.5, 20260930u);
        CHECK(r.score == 1234 && r.popped == 40 && r.missed == 5,
              L"成绩单原样记下分数、击破数与漏球数");
        CHECK(feq(r.accuracy, 0.6f), L"命中率 = 命中 30 / 出枪 50");
        CHECK(feq((float)r.durationSec, 61.5f), L"成绩单记下本局时长");
        CHECK(r.seed == 20260930u, L"成绩单记下本局种子，便于复现");
        /* 规则与预设是两个字段：规则决定怎么玩，预设决定记录归到哪一档。
           默认那套预设的规则恰好也是 MODE_RANGE，所以这里特意选了不一样的
           一对（计时挑战规则 + 第 4 套预设 = 计时挑战本身），免得两个字段
           被写成一个。换位前第 4 位是渐进训练，两个字段当时并不对应
           同一套预设 —— 现在对应上了，这一条仍只用来分辨"两个字段各自可辨"。*/
        CHECK(r.mode == MODE_TIME && r.preset == 4,
              L"成绩单分别记下规则与预设（两者不是一回事）");

        scoreFillResult(&r, 0, 0, 0, 0, 0, 0, MODE_RANGE, 0, 0.0, 1u);
        CHECK(feq(r.accuracy, 0.0f), L"一枪没开时命中率记 0，不出 NaN");

        scoreFillResult(&r, 1, 1, 0, 0, 1, 1, 99, 99, 1.0, 1u);
        CHECK(r.mode == MODE_COUNT - 1 && r.preset == PRESET_COUNT - 1,
              L"越界的规则号与预设号被夹回合法值");
    }

    /* ------------------------------------------------------------ 成绩簿
       按**预设**分档（八项）。早先曾按规则分四档 —— 现在记录跟着
       g_presets 走，在菜单上拨到哪一套，破的就是那一套的纪录。*/
    {
        ScoreBook b;
        RunResult r;

        scoreBookInit(&b);
        {
            int bad = 0;
            for (i = 0; i < PRESET_COUNT; ++i) if (b.rec[i].score != 0) ++bad;
            CHECK(bad == 0, L"新建的成绩簿各预设都是空的");
        }

        scoreFillResult(&r, 100, 10, 1, 12, 10, 5, MODE_RANGE, 0, 30.0, 7u);
        CHECK(scoreBookSubmit(&b, 0, &r) == 1, L"第一局必然破纪录");
        CHECK(b.rec[0].score == 100, L"破纪录后最高分被写入");

        scoreFillResult(&r, 80, 8, 0, 9, 8, 6, MODE_RANGE, 0, 30.0, 8u);
        CHECK(scoreBookSubmit(&b, 0, &r) == 0, L"分数更低不算破纪录");
        CHECK(b.rec[0].score == 100, L"低分不覆盖已有的最高分");
        CHECK(b.rec[0].bestCombo == 6, L"分数没破但连击破了，单独特记一笔");

        scoreFillResult(&r, 100, 10, 1, 12, 10, 9, MODE_RANGE, 0, 30.0, 9u);
        CHECK(scoreBookSubmit(&b, 0, &r) == 0,
              L"分数相同不算破纪录（免得刷同一个分数一直报纪录）");
        CHECK(b.rec[0].seed == 7u, L"没破纪录时连种子都不动");

        CHECK(b.rec[3].score == 0, L"打一档不影响别的档的记录");

        scoreFillResult(&r, 500, 30, 2, 40, 30, 11, MODE_TIME, 3, 20.0, 11u);
        scoreBookSubmit(&b, 3, &r);
        CHECK(b.rec[3].seed == 11u, L"连种子一起记下，那一局可以复现");
        CHECK(b.rec[3].accuracyPct == 75, L"命中率按百分数存（0.75 → 75）");
        CHECK(scoreBookSubmit(&b, -1, &r) == 0 && scoreBookSubmit(&b, PRESET_COUNT, &r) == 0,
              L"越界的预设号被拒绝，而不是越界写数组");
        /* 记录要覆盖到**每一套**预设 —— 档位比预设少，用户在第 6 套上
           打出的成绩就没地方放（写进别的档或者直接丢）。*/
        CHECK(b.rec[PRESET_COUNT - 1].score == 0, L"最高那一套预设也有自己的档位");
    }

    /* -------------------------------------------------------- 成绩簿持久化 */
    {
        wchar_t path[MAX_PATH];
        ScoreBook b, c, d;
        RunResult r;

        GetTempPathW(MAX_PATH, path);
        wcscat(path, L"balloon_range_score_selftest.dat");
        _wremove(path);

        scoreBookInit(&b);
        scoreFillResult(&r, 777, 42, 3, 50, 42, 15, MODE_PROGRESS, 6, 45.0, 20260930u);
        scoreBookSubmit(&b, 6, &r);
        CHECK(scoreBookSave(&b, path) == 0, L"成绩簿写入成功");

        scoreBookInit(&c);
        CHECK(scoreBookLoad(&c, path) == 1, L"成绩簿读回成功");
        CHECK(memcmp(&b, &c, sizeof(ScoreBook)) == 0, L"成绩簿往返后逐字节一致");

        /* 篡改 payload 的一个字节：校验和必须拦住它，而不是把脏记录当成纪录。*/
        {
            FILE *f = _wfopen(path, L"r+b");
            if (f) { fseek(f, 12, SEEK_SET); fputc(0x7F, f); fclose(f); }
        }
        scoreBookInit(&d);
        CHECK(scoreBookLoad(&d, path) == 0, L"被篡改的成绩簿被拒绝（CRC 拦下）");
        CHECK(d.rec[6].score == 0, L"拒绝之后簿子是空的，不会留下半份脏数据");

        /* 截断。*/
        {
            FILE *f = _wfopen(path, L"wb");
            if (f) { fwrite("RBS", 1, 3, f); fclose(f); }
        }
        CHECK(scoreBookLoad(&d, path) == 0, L"过短的成绩簿被拒绝");

        /* 魔数不对：长度够，但不是本程序写的。*/
        {
            FILE *f = _wfopen(path, L"wb");
            if (f) {
                const char *junk = "NOT-OUR-FILE-AT-ALL-0123456789ABCDEF";
                fwrite(junk, 1, 40, f);
                fclose(f);
            }
        }
        CHECK(scoreBookLoad(&d, path) == 0, L"不是本程序写的成绩簿被拒绝");

        _wremove(path);
        CHECK(scoreBookLoad(&d, path) == 0, L"成绩簿不存在时返回 0 而不是崩溃");
        CHECK(scoreBookSave(&b, L"Z:\\__no_such_dir__\\y.dat") != 0,
              L"成绩簿写到不存在的目录时返回失败而不是崩溃");
    }
}

/* ============================================================ 合成音效与声像 */

/* 数 y 轴上穿零的次数。正弦波的过零数大约是频率的两倍，
   所以它可以用来看"音高是不是真的升上去了" —— 人耳验不了的东西，
   至少能把"频率确实变了"这件事钉住。*/
static int zeroCrossings(const short *pcm, int frames, int from, int to) {
    int i, n = 0;
    if (!pcm || frames <= 0) return 0;
    if (from < 1) from = 1;
    if (to > frames) to = frames;
    for (i = from; i < to; ++i)
        if ((pcm[i - 1] < 0 && pcm[i] >= 0) || (pcm[i - 1] >= 0 && pcm[i] < 0)) ++n;
    return n;
}

/* 一段 PCM 的峰值与某一段的均方根。*/
static int   pcmPeak(const short *pcm, int frames) {
    int i, pk = 0;
    for (i = 0; i < frames; ++i) {
        int v = pcm[i] < 0 ? -pcm[i] : pcm[i];
        if (v > pk) pk = v;
    }
    return pk;
}

static float pcmRms(const short *pcm, int from, int to) {
    double acc = 0.0;
    int i;
    if (to <= from) return 0.0f;
    for (i = from; i < to; ++i) acc += (double)pcm[i] * (double)pcm[i];
    return (float)sqrt(acc / (double)(to - from));
}

static void tAudio(void) {
    static short buf[AUDIO_MAX_FRAMES];
    static short buf2[AUDIO_MAX_FRAMES];
    static const wchar_t *names[SFX_COUNT] = { L"击破", L"漏球", L"界面", L"收尾" };
    int i;

    repW(L"\n== 合成音效与声像 ==\n");

    /* -------------------------------------------------- 四个音效都合得出来
       峰值卡在 3000..32000：下限证明它**不是一段静音**（比"长度不为零"
       严格得多 —— 长度对而全是 0 是最典型的那种"看起来做了"），
       上限证明没有削顶削到失真。*/
    for (i = 0; i < SFX_COUNT; ++i) {
        int n = audioRenderClip(i, 0, buf, (int)ARRAY_COUNT(buf));
        int pk;
        CHECK_NAMED(n >= AUDIO_RATE / 20, L"音效时长不少于 50 毫秒", names[i]);
        CHECK_NAMED(n <= AUDIO_MAX_FRAMES, L"音效时长不超过上限", names[i]);
        pk = pcmPeak(buf, n);
        CHECK_NAMED(pk > 3000, L"音效不是静音（峰值够大）", names[i]);
        CHECK_NAMED(pk <= 32000, L"音效没有削顶失真（峰值不过界）", names[i]);
        /* 包络一定在衰减：最后 10% 的均方根必须明显小于最前 10%。
           这两条一起，把"响了一下并且收得住"钉住 —— 一个不衰减的音
           会一直压在混音里，听起来是啸叫。*/
        CHECK_NAMED(pcmRms(buf, n - n / 10, n) < pcmRms(buf, 0, n / 10) * 0.55f,
                    L"尾段比首段轻（包络在衰减）", names[i]);
    }

    /* ------------------------------------------------ 音高：升半音真的升频
       量"音高"用一段窗口里的过零数。窗口的选取不是随便定的：击破音前 60
       毫秒是白噪声瞬态主导，那一段的过零数由噪声决定、与音高无关 ——
       最初取 60..160 ms 量出来是 209/201/258/259，比值只有 1.24，
       断言当场就失败了。它反过来证明了这个音确实有真噪声瞬态，
       也说明窗口必须落在音身主导的那一段。
       实测 140..280 ms：99 / 107 / 132 / 165，比值 1.67。
       阈值取 1.45，给残余噪声留余量（正弦的过零率是频率的两倍，
       +12 半音理论上应当是 2.0 倍）。*/
    {
        int z[4], k;
        const int semi[4] = { 0, 4, 8, 12 };
        for (k = 0; k < 4; ++k) {
            int n = audioRenderClip(SFX_POP, semi[k], buf, (int)ARRAY_COUNT(buf));
            z[k] = zeroCrossings(buf, n, AUDIO_RATE * 14 / 100, AUDIO_RATE * 28 / 100);
        }
        repFmt(L"  击破音过零数（140..280 ms）：半音 0 → %d，4 → %d，8 → %d，12 → %d\n",
               z[0], z[1], z[2], z[3]);
        CHECK(z[1] > z[0] && z[2] > z[1] && z[3] > z[2],
              L"半音每升 4 度，过零数（≈音高）都往上走");
        CHECK((float)z[3] > (float)z[0] * 1.45f,
              L"升满 12 个半音后音高接近翻倍（过零数 ≥ 1.45 倍）");
    }

    /* -------------------------------------------------- 只有击破音认半音 */
    {
        int n0 = audioRenderClip(SFX_MISS, 0, buf, (int)ARRAY_COUNT(buf));
        int n12 = audioRenderClip(SFX_MISS, 12, buf2, (int)ARRAY_COUNT(buf2));
        CHECK(n0 == n12, L"漏球音不理会半音偏移（长度不变）");
        CHECK(n0 > 0 && memcmp(buf, buf2, (size_t)n0 * sizeof(short)) == 0,
              L"漏球音不理会半音偏移（样本逐字节一致）");
    }

    /* -------------------------------------------------------- 越界与空指针 */
    CHECK(audioRenderClip(SFX_POP, 0, NULL, 100) == 0, L"输出缓冲为空时返回 0 而不是写空指针");
    CHECK(audioRenderClip(SFX_POP, 0, buf, 0) == 0, L"缓冲长度为 0 时返回 0");
    CHECK(audioRenderClip(-1, 0, buf, (int)ARRAY_COUNT(buf)) == 0, L"越界的音效号合成 0 帧");
    CHECK(audioRenderClip(SFX_COUNT, 0, buf, (int)ARRAY_COUNT(buf)) == 0, L"越界的音效号（上界）合成 0 帧");
    {
        int n = audioRenderClip(SFX_POP, 999, buf, (int)ARRAY_COUNT(buf));
        int m = audioRenderClip(SFX_POP, AUDIO_SEMITONE_MAX, buf2, (int)ARRAY_COUNT(buf2));
        CHECK(n > 0 && n == m && memcmp(buf, buf2, (size_t)n * sizeof(short)) == 0,
              L"过大的半音数被夹到上限，不会越界读表");
    }
    {
        int tiny = audioRenderClip(SFX_POP, 0, buf, 100);
        CHECK(tiny == 100, L"缓冲太小的时候按容量截断（不是溢出写）");
    }

    /* ------------------------------------------------------------ 声像定律 */
    {
        float l, r;
        audioPanGains(-1.0f, &l, &r);
        CHECK(feq(l, 1.0f) && feq(r, 0.0f), L"全左：左 1.0、右 0.0");
        audioPanGains(1.0f, &l, &r);
        CHECK(feq(l, 0.0f) && feq(r, 1.0f), L"全右：左 0.0、右 1.0");
        audioPanGains(0.0f, &l, &r);
        CHECK(fabsf(l - 0.7071f) < 1e-3f && fabsf(r - 0.7071f) < 1e-3f,
              L"正中：两边都是 0.7071（等功率，不是各 0.5）");
        audioPanGains(-9.0f, &l, &r);
        CHECK(feq(l, 1.0f) && feq(r, 0.0f), L"越界的 pan 被夹到 -1");
        audioPanGains(9.0f, &l, &r);
        CHECK(feq(l, 0.0f) && feq(r, 1.0f), L"越界的 pan 被夹到 +1");
        {
            int badPower = 0, badMono = 0;
            float prevL = 2.0f, prevR = -1.0f, p;
            for (p = -1.0f; p <= 1.0001f; p += 0.1f) {
                float a, b;
                audioPanGains(p, &a, &b);
                if (fabsf(a * a + b * b - 1.0f) > 2e-3f) ++badPower;
                if (a > prevL + 1e-4f || b < prevR - 1e-4f) ++badMono;
                prevL = a; prevR = b;
            }
            CHECK(badPower == 0, L"从左扫到右，L²+R² 恒为 1（响度不塌陷）");
            CHECK(badMono == 0, L"从左扫到右，左声道只减、右声道只增");
        }
    }

    /* ------------------------------------------------------------ 距离衰减 */
    CHECK(feq(audioDistanceGain(0.0f), 1.0f), L"零距离不衰减");
    CHECK(feq(audioDistanceGain(-3.0f), 1.0f), L"负距离按零处理，不出大于 1 的增益");
    {
        int bad = 0;
        float prev = audioDistanceGain(0.0f), d;
        for (d = 0.5f; d <= 40.0f; d += 0.5f) {
            float cur = audioDistanceGain(d);
            if (cur > prev + 1e-6f) ++bad;
            if (cur <= 0.0f) ++bad;
            prev = cur;
        }
        CHECK(bad == 0, L"距离越远增益越小，且始终为正");
        CHECK(audioDistanceGain(6.0f) < 0.5f && audioDistanceGain(6.0f) > 0.2f,
              L"六米处衰减到一半上下（听得出远近，但不至于听不见）");
    }

    /* ------------------------------------------------------------------ WAV
       导出的文件是给**人耳**听的，所以这里把它的头与数据段都读回来验一遍：
       头写错了的话，用户双击打开只会看到"无法播放"，而自检里什么都看不见。*/
    {
        wchar_t path[MAX_PATH];
        int n = audioRenderClip(SFX_POP, 0, buf, (int)ARRAY_COUNT(buf));

        GetTempPathW(MAX_PATH, path);
        wcscat(path, L"balloon_range_sfx_selftest.wav");
        _wremove(path);

        CHECK(audioWriteWav(path, buf, n, 1, AUDIO_RATE) == 0, L"wav 写出成功");
        {
            FILE *f = _wfopen(path, L"rb");
            CHECK(f != NULL, L"写出的 wav 能被重新打开");
            if (f) {
                unsigned char hdr[44];
                uint32_t rate = 0, dataLen;
                uint16_t chans = 0, bits = 0;
                long total;
                size_t got = fread(hdr, 1, 44, f);

                fseek(f, 0, SEEK_END);
                total = ftell(f);
                dataLen = (uint32_t)(total - 44);
                memcpy(&rate, hdr + 24, 4);
                memcpy(&chans, hdr + 22, 2);
                memcpy(&bits, hdr + 34, 2);

                CHECK(got == 44 && memcmp(hdr, "RIFF", 4) == 0 && memcmp(hdr + 8, "WAVE", 4) == 0,
                      L"wav 头是 RIFF/WAVE");
                CHECK(dataLen == (uint32_t)n * 2u, L"wav 数据段长度 = 帧数 × 2 字节");
                CHECK(rate == (uint32_t)AUDIO_RATE && chans == 1 && bits == 16,
                      L"wav 头里的采样率、声道数、位深都对");
                fseek(f, 44, SEEK_SET);
                {
                    short first = 0;
                    if (fread(&first, 1, 2, f) == 2)
                        CHECK(first == buf[0], L"wav 第一个样本与合成缓冲一致");
                    else
                        CHECK(0, L"wav 数据段读得回来");
                }
                fclose(f);
            }
        }
        _wremove(path);

        CHECK(audioWriteWav(L"Z:\\__no_such_dir__\\x.wav", buf, n, 1, AUDIO_RATE) != 0,
              L"写到不存在的目录时返回失败而不是崩溃");
        CHECK(audioWriteWav(path, NULL, n, 1, AUDIO_RATE) != 0, L"样本指针为空时返回失败");
        CHECK(audioWriteWav(path, buf, 0, 1, AUDIO_RATE) != 0, L"帧数为 0 时返回失败");
    }

    /* ------------------------------------------------ 没有设备时也必须安全
       这是整个音频模块最重要的一条：音频是**可选的**。没声卡、被独占、
       远程会话下设备都可能开不出来，那时游戏必须照跑。*/
    {
        static Audio a;          /* Audio 有十几 KB 缓冲，不放栈上 */
        wchar_t msg[128];

        memset(&a, 0, sizeof(a));
        CHECK(audioStatusText(&a) != NULL, L"没初始化时状态文本也不返回空指针");
        audioPlay(&a, SFX_POP, 0.0f, 1.0f, 3);
        audioPlay(&a, -1, 0.0f, 0.0f, 0);
        audioPlay(&a, SFX_COUNT, 0.0f, 0.0f, 0);
        audioStopAll(&a);
        audioSetVolume(&a, 0.5f);
        audioSetMuted(&a, 1);
        audioSetMuted(&a, 0);
        audioSetVolume(&a, 9.0f);
        CHECK(a.volume <= 1.0f, L"音量上界被夹住");
        audioSetVolume(&a, -9.0f);
        CHECK(a.volume >= 0.0f, L"音量下界被夹住");
        audioClose(&a);
        audioClose(&a);          /* 幂等：没开过也能关，关两次也不出事 */
        CHECK(1, L"设备没开成时，播放/停止/调音量/关闭全部安全（不崩、不越界）");

        memset(msg, 0, sizeof(msg));
        CHECK(audioOpen(NULL, NULL, msg, (int)ARRAY_COUNT(msg)) != 0,
              L"audioOpen 传 NULL 时返回失败而不是崩溃");
        audioPlay(NULL, SFX_POP, 0.0f, 0.0f, 0);
        audioStopAll(NULL);
        audioSetVolume(NULL, 0.5f);
        audioSetMuted(NULL, 1);
        audioClose(NULL);
        CHECK(1, L"所有音频接口传 NULL 都不崩");
    }

    /* -------------------------------------------- 音效导出（给人耳听） */
    {
        wchar_t dir[MAX_PATH];
        int dumped = 0;
        GetTempPathW(MAX_PATH, dir);
        CHECK(audioDumpClips(dir, &dumped) == 0, L"音效导出成功");
        CHECK(dumped == SFX_COUNT - 1 + (AUDIO_SEMITONE_MAX + 1),
              L"导出的 wav 个数 = 击破音 13 个半音变体 + 另三个各一份");
    }
}

/* ============================================================ 历史记录
 *
 * 盯的是"记录近 20 次的成绩，且关闭程序仍然保存"。这一组盯三件事：
 * 容量与顺序（第 1 次恒为最近那次）、落盘往返、以及**存档坏掉时怎么办**。
 *
 * 第三件是重点。这个文件是外来的数据，用户手改过一个字节、程序崩过一回
 * 写到一半、换了版本，都可能出现。所以下面反复验同一件事：**读失败一律
 * 当作空历史，绝不能崩、更不能顺手把文件删了** —— 用户的数据轮不到
 * 程序来清理。
 */
static void tHistory(void) {
    static RunHistory h;
    HistoryEntry e;
    wchar_t path[MAX_PATH];
    wchar_t dir[MAX_PATH];
    wchar_t when[64];
    FILE *f;
    int i;

    group(L"历史记录");

    /* ---------------------------------------------------------- 容量与顺序 */
    historyInit(&h);
    CHECK(h.count == 0, L"初始化后是空的");

    for (i = 0; i < 3; ++i) {
        memset(&e, 0, sizeof(e));
        e.score = 101 + i;              /* 101, 102, 103 */
        e.seed  = (unsigned)i;
        historyPush(&h, &e);
    }
    CHECK(h.count == 3, L"推三条之后条数是 3");
    CHECK(historyAt(&h, 0) && historyAt(&h, 0)->score == 103,
          L"槽位 0 是最后推的那条（最近的一次在最上面）");
    CHECK(historyAt(&h, 2) && historyAt(&h, 2)->score == 101,
          L"槽位 2 是最早那条");

    /* 推满再溢出：条数封顶，最早的被挤掉。 */
    for (i = 3; i < HISTORY_CAP + 5; ++i) {
        memset(&e, 0, sizeof(e));
        e.score = 100 + i;
        historyPush(&h, &e);
    }
    CHECK(h.count == HISTORY_CAP, L"推超过容量之后条数封顶在 HISTORY_CAP");
    CHECK(historyAt(&h, 0) && historyAt(&h, 0)->score == 100 + HISTORY_CAP + 4,
          L"封顶之后槽位 0 仍是最新那条");
    CHECK(historyAt(&h, HISTORY_CAP - 1) != NULL, L"最后一个槽位可读");
    CHECK(historyAt(&h, HISTORY_CAP) == NULL, L"越界下标返回 NULL 而不是越界读");
    CHECK(historyAt(&h, -1) == NULL, L"负下标返回 NULL");
    CHECK(historyAt(NULL, 0) == NULL, L"空句柄返回 NULL");
    /* 空句柄推入不能崩，也不能动到别人。historyPush 是 void，
       所以这里只能拿"推之前那条还在不在"当证据。*/
    {
        int keepCount = h.count;
        int keepTop = historyAt(&h, 0) ? historyAt(&h, 0)->score : -1;
        historyPush(NULL, &e);
        CHECK(h.count == keepCount && historyAt(&h, 0) &&
              historyAt(&h, 0)->score == keepTop,
              L"空句柄推入被忽略（不崩、不动已有的记录）");
    }

    /* ---------------------------------------------------------- 落盘往返 */
    if (GetTempPathW((DWORD)ARRAY_COUNT(dir), dir) > 0) {
        _snwprintf(path, ARRAY_COUNT(path) - 1, L"%lsbr_hist_selftest.dat", dir);
        path[ARRAY_COUNT(path) - 1] = L'\0';

        CHECK(historySave(&h, path) == 0, L"写出历史文件成功");
        {
            static RunHistory r;
            CHECK(historyLoad(&r, path) == 0, L"读回自己刚写的文件成功");
            CHECK(r.count == h.count, L"读回来的条数一致");
            CHECK(memcmp(&r, &h, sizeof(RunHistory)) == 0,
                  L"读回来的内容与写出去的一模一样（逐字节）");
        }

        /* 坏文件之一：整体截断。 */
        f = _wfopen(path, L"r+b");
        if (f) {
            static RunHistory r;
            long len;
            fseek(f, 0, SEEK_END);
            len = ftell(f);
            fclose(f);
            f = _wfopen(path, L"r+b");
            if (f && len > 8) {
                /* 砍掉尾巴。用 _chsize 而不是重写一遍文件 —— 重写就变成
                   "写了个坏文件"，砍尾巴才是"文件没写完"。*/
                _chsize(_fileno(f), len - 8);
                fclose(f);
                CHECK(historyLoad(&r, path) != 0, L"截断的文件读失败");
                CHECK(r.count == 0, L"读失败时留下的是**空历史**，不是半截数据");
            }
        }

        /* 坏文件之二：只改中间一个字节。头没动、长度没动，只有 CRC 能发现。 */
        historySave(&h, path);
        f = _wfopen(path, L"r+b");
        if (f) {
            static RunHistory r;
            long len;
            unsigned char b = 0;
            long at;
            fseek(f, 0, SEEK_END);
            len = ftell(f);
            at = len - 4;                     /* 落在数据区里 */
            fseek(f, at, SEEK_SET);
            if (fread(&b, 1, 1, f) == 1) {
                b ^= 0x5Au;                   /* 翻六个二进制位 */
                fseek(f, at, SEEK_SET);
                fwrite(&b, 1, 1, f);
            }
            fclose(f);
            CHECK(historyLoad(&r, path) != 0, L"数据区被改过一个字节就校验不过");
            CHECK(r.count == 0, L"校验不过时留空历史");
        }

        /* 坏文件之三：文件还在，但是空的（比如刚创建就被打断）。 */
        f = _wfopen(path, L"wb");
        if (f) fclose(f);
        {
            static RunHistory r;
            CHECK(historyLoad(&r, path) != 0, L"零字节文件读失败");
            CHECK(r.count == 0, L"零字节文件留空历史");
        }

        /* 读失败之后文件必须**还在** —— 程序没有资格替用户删档。 */
        f = _wfopen(path, L"rb");
        CHECK(f != NULL, L"读失败之后文件仍原样留在磁盘上（没有被删）");
        if (f) fclose(f);

        /* 文件不存在 = 第一次跑，不算错误，留空历史即可。 */
        {
            static RunHistory r;
            DeleteFileW(path);
            CHECK(historyLoad(&r, path) != 0, L"文件不存在时报失败");
            CHECK(r.count == 0, L"文件不存在时是空历史（不是垃圾数据）");
        }
        DeleteFileW(path);
    } else {
        repW(L"  --   拿不到临时目录，落盘相关的断言整组跳过\n");
    }

    /* ---------------------------------------------------------- 时间格式
       只验**形状**（长度、分隔符位置、各位是不是数字），不验具体日期 ——
       格式用的是本地时间，同一个时间戳在不同的时区本来就不该显示成同一串，
       写死"1970-01-01 08:00"这种断言换台机器就红，红得毫无意义。*/
    historyFormatTime(1700000000LL, when, (int)ARRAY_COUNT(when));
    CHECK(wcslen(when) == 16, L"时间戳格式化后固定 16 个字符（YYYY-MM-DD HH:MM）");
    CHECK(when[4] == L'-' && when[7] == L'-' && when[10] == L' ' && when[13] == L':',
          L"时间戳的分隔符位置固定");
    {
        int ok = 1, k;
        for (k = 0; k < 16; ++k) {
            if (k == 4 || k == 7 || k == 10 || k == 13) continue;
            if (when[k] < L'0' || when[k] > L'9') ok = 0;
        }
        CHECK(ok, L"除分隔符外全是数字");
    }
    CHECK(when[0] == L'1' || when[0] == L'2', L"年份是四位（以 1 或 2 开头）");

    /* 取不到时间时照实说"时间未知"，不拿 1970 年冒充真数据。*/
    historyFormatTime(0, when, (int)ARRAY_COUNT(when));
    CHECK(wcscmp(when, L"时间未知") == 0, L"时间戳为 0 时写「时间未知」");
    historyFormatTime(-5, when, (int)ARRAY_COUNT(when));
    CHECK(wcscmp(when, L"时间未知") == 0, L"负数时间戳同样写「时间未知」");

    /* 缓冲区再小也不能写溢出：count = 1 时连终止符都放不下，应当原样返回。*/
    {
        wchar_t tiny[2];
        tiny[0] = L'X'; tiny[1] = L'X';
        historyFormatTime(1700000000LL, tiny, 1);
        CHECK(tiny[0] == L'X', L"缓冲区太小时一个字节都不写（不溢出）");
        historyFormatTime(1700000000LL, tiny, 2);
        CHECK(tiny[1] == L'\0', L"两字节的缓冲区也被终止符收住");
    }
}

/* ============================================================ 面板：分类
 *
 * 面板要有"预设方案设置"和"用户偏好设置"两个标题并分类。分类的判据只有
 * 一条：**组号**。这一组断言把这条判据钉住，顺带钉住"两类各自连续" ——
 * 一旦有人往枚举中间插一个组，或者把某个组挪到错误的一侧，这里立刻红。
 */
static void tPanelSections(void) {
    int g, i;
    int rowsInSection[PARAM_SECTION_COUNT];
    int groupsInSection[PARAM_SECTION_COUNT];

    group(L"参数面板：两分类");

    CHECK(PARAM_SECTION_COUNT == 2, L"大类正好两个");
    CHECK(PARAM_GROUP_SECTION_SPLIT > 0 &&
          PARAM_GROUP_SECTION_SPLIT < PARAM_GROUP_COUNT,
          L"分界点落在组数区间内（否则有一类是空的）");

    /* ★ 这里**必须写死每个组属于哪一类**，不能拿 PARAM_GROUP_SECTION_SPLIT
       去推期望值。早先就是推的，结果把分界点从 5 改成 3，断言两边一起变，
       照样全绿 —— 那等于没验。写死之后，往后谁把枚举顺序调了、或者把某个
       组挪错了一侧，这里立刻红。*/
    CHECK_NAMED(paramGroupSection(PARAM_GROUP_BALLOON)  == 0, L"归在玩法那一类", g_paramGroupNames[PARAM_GROUP_BALLOON]);
    CHECK_NAMED(paramGroupSection(PARAM_GROUP_MOTION)   == 0, L"归在玩法那一类", g_paramGroupNames[PARAM_GROUP_MOTION]);
    CHECK_NAMED(paramGroupSection(PARAM_GROUP_RHYTHM)   == 0, L"归在玩法那一类", g_paramGroupNames[PARAM_GROUP_RHYTHM]);
    CHECK_NAMED(paramGroupSection(PARAM_GROUP_SCORE)    == 0, L"归在玩法那一类", g_paramGroupNames[PARAM_GROUP_SCORE]);
    CHECK_NAMED(paramGroupSection(PARAM_GROUP_TRAINING) == 0, L"归在玩法那一类", g_paramGroupNames[PARAM_GROUP_TRAINING]);
    CHECK_NAMED(paramGroupSection(PARAM_GROUP_PLAYER)   == 1, L"归在偏好那一类", g_paramGroupNames[PARAM_GROUP_PLAYER]);
    CHECK_NAMED(paramGroupSection(PARAM_GROUP_GRAPHICS) == 1, L"归在偏好那一类", g_paramGroupNames[PARAM_GROUP_GRAPHICS]);
    CHECK_NAMED(paramGroupSection(PARAM_GROUP_AUDIO)    == 1, L"归在偏好那一类", g_paramGroupNames[PARAM_GROUP_AUDIO]);

    /* 两个大标题的文字是固定名字，不能改。*/
    CHECK(wcscmp(g_paramSectionNames[0], L"预设方案设置") == 0,
          L"第一类标题就叫「预设方案设置」");
    CHECK(wcscmp(g_paramSectionNames[1], L"用户偏好设置") == 0,
          L"第二类标题就叫「用户偏好设置」");

    /* "两类各自连续"就是"组号单调不减"：既然分类是拿组号跟分界点比，
       单调不减就等价于"前一段全是 0、后一段全是 1"。上面那八条已经把
       每个组的归属钉死了，这一条是廉价的结构性兜底。*/
    for (g = 1; g < PARAM_GROUP_COUNT; ++g)
        CHECK_NAMED(paramGroupSection(g) >= paramGroupSection(g - 1),
                    L"大类号随组号单调不减（同一类连续）",
                    g_paramGroupNames ? g_paramGroupNames[g] : L"?");

    CHECK(paramGroupSection(-1) == 1 && paramGroupSection(PARAM_GROUP_COUNT) == 1,
          L"越界组号退回后一类而不是越界读数组");

    for (i = 0; i < PARAM_SECTION_COUNT; ++i) {
        rowsInSection[i] = 0;
        groupsInSection[i] = 0;
        CHECK(g_paramSectionNames[i] && g_paramSectionNames[i][0],
              L"大类有中文名");
    }

    /* 每一行都要能归到某一类，而且归类要与 g_paramGroupFirst 对得上 ——
       渲染层插大标题条时用的是后者，两边不一致就会出现"标题条插错了地方"。*/
    for (i = 0; i < g_paramDescCount; ++i) {
        int s = paramGroupSection(g_paramDescs[i].group);
        int gidx;
        CHECK_NAMED(s >= 0 && s < PARAM_SECTION_COUNT,
                    L"每一行都归到了某个大类", g_paramDescs[i].name);
        rowsInSection[s] += 1;

        /* 这一行的组号必须真的对应 g_paramGroupFirst 划出来的那段，
           否则"行属于哪组"和"组从第几行开始"这两张表就打架了。*/
        gidx = g_paramDescs[i].group;
        CHECK_NAMED(i >= g_paramGroupFirst[gidx] &&
                    i < g_paramGroupFirst[gidx + 1],
                    L"行的位置与分组的行区间一致", g_paramDescs[i].name);
    }
    for (g = 0; g < PARAM_GROUP_COUNT; ++g)
        groupsInSection[paramGroupSection(g)] += 1;

    for (i = 0; i < PARAM_SECTION_COUNT; ++i) {
        CHECK_NAMED(rowsInSection[i] > 0, L"这一类至少有一行", g_paramSectionNames[i]);
        CHECK_NAMED(groupsInSection[i] > 0, L"这一类至少有一组", g_paramSectionNames[i]);
    }

    /* 主标题条本身也是要占地方的 —— 渲染层按"两条标题条满占"预留行数，
       这里把"到底有几条"钉住，别让那边少留或多留。*/
    CHECK(PARAM_SECTION_COUNT == 2, L"渲染层预留的标题条数 = 大类数");

    /* ---------------------------------------------------------- 组标题占行
     *
     * 曾经出现"气球、运动、节奏等小标题和选项重叠了"。组标题从
     * "塞在参数行里"改成**独占一行**，从此行数预算里必须给它留出位置。
     *
     * 底下这几条**把可见行数钉成具体数字**。为什么值得写死：算错这一项不会
     * 有任何编译期或运行期错误，症状只是"最后一行被页脚盖住"或者"标题压住了
     * 选项" —— 全都是要用眼睛看才能发现的那种。
     *
     * ★ 这一笔账**换过算法**：不再是"按最坏情况预扣"（那是早先的口径，
     * 也正是"光标还在中间、列表就提前滚"的根因），而是**按几何一格一格排**，
     * 排得下几行就是几行。所以下面这些数字必须**手算出来**再往上写，
     * 不是"跑出来多少写多少"。
     *
     * ★ "格"落成了一个定值：**一格的像素高就是行高 30**，组标题条
     * 与参数行同高（原先组标题另占 20，见 render.cpp 里 PARAM_ROW_H 那一段
     * 说明：那个常量与行高不等时，滚动窗口的容量就不再是个常量，光标会在
     * 屏幕上上下乱跳）。于是这一页排得下几行，数"格"就行：
     *
     *   窗口 1600×900 → u = 1。列表区净高
     *     listH = 900 − (2×40 边距 + 64 抬头 + 40 菜单栏 + 104 页脚) = 612
     *   容量（paramItemCapZero）= ⌊(612 − 30 顶上留给 ︿ 的一格 − 4 余量) / 30⌋
     *     = 19 格
     *
     *   预设页从 top=0 往下排。带组标题条的行占 2 格，其余占 1 格：
     *     行 0「当前预设方案」                            1 格 →  1
     *     行 1（第 0 项，气球，另起组标题条）             2 格 →  3
     *     行 2..12（气球剩 11 项）                       11 格 → 14
     *     行 13（第 12 项，运动，另起组标题条）           2 格 → 16
     *     行 14..16（运动剩 3 项）                        3 格 → 19
     *   19 格正好排满**行 0..16 共 17 行**；下一个块是行 17（运动第 5 项，
     *   挨着上一项、不带组标题条），1 格 → 20 格，超了，排不下。
     *
     *   ★ 气球组加了「生成位置」这一项之后，上面这几行的**逐行数字全变了**
     *   （气球从 11 项变 12 项，运动组标题条从行 12 挪到行 13），但**结论没变**：
     *   填满 19 格的仍然是"1 + 2 + 11 + 2 + 3"，可见行数照旧 17。注释已按新表改准。
     *   → **可见 17 行**（顶上有 ︿ 时那格已扣在 paramItemCapZero 里；
     *     下面还有内容时 paramPanelItemCap 再扣的 ﹀ 那格，被"顶上没内容 +1"
     *     抵掉了，所以这一处恰好还是 19 格 —— 两个数各自怎么来的，见
     *     render.cpp 里那两个函数，别拿"恰好相等"当理由省掉一边）。*/
    {
        static App a;
        int rows;
        memset(&a, 0, sizeof(a));
        a.winW = 1600; a.winH = 900;
        a.paramPage = 0;                    /* 预设页 */
        a.paramTop = 0;
        rows = paramPanelRows(&a);
        CHECK(rows == 17,
              L"1600×900、预设页顶部：可见 17 行（19 格排满行 0..16）");
        /* 偏好页满打满算 12 行，几何容量绰绰有余 → 整页都在，不留箭头。*/
        a.paramPage = 1;
        a.paramTop = 0;
        CHECK(paramPanelRows(&a) == appParamPageRowCount(1),
              L"★ 偏好页那一页整个放得下（12 行全在），所以不让格、不画 ﹀");
        {
            int arrowBot = 1;
            paramPanelFit(&a, 0, NULL, &arrowBot);
            CHECK(!arrowBot, L"★ 放得满时下箭头不出现（那一格还给真实行）");
        }
        /* 预设页顶部：下面还有内容，所以下箭头必须出现。*/
        a.paramPage = 0;
        {
            int arrowTop = 0, arrowBot = 0, fit;
            fit = paramPanelFit(&a, 0, &arrowTop, &arrowBot);
            CHECK(arrowBot, L"★ 预设页顶部的下箭头要画（下面还有行）");
            CHECK(!arrowTop, L"顶部时上箭头不画");
            CHECK(fit == rows, L"paramPanelFit 与 paramPanelRows 是同一个数");
            /* 往下滚一格之后上箭头就该出现了。*/
            fit = paramPanelFit(&a, 1, &arrowTop, &arrowBot);
            CHECK(arrowTop, L"★ 滚下来之后上箭头出现（上面还有行）");
            CHECK(fit > 0, L"滚下来之后窗口里还有行");
        }
        /* 窗口再小也得留得下几行，否则小窗口下会退化成"一行都看不见"。*/
        a.winH = 600;
        CHECK(paramPanelRows(&a) >= 3, L"600 高的小窗口下仍至少显示 3 行");
        a.winH = 0;
        CHECK(paramPanelRows(&a) >= 3, L"winH 为 0 时退到一个安全的默认值而不是除零");
    }

    /* 每一组都得有参数，否则渲染层为它预留的那 20×u 像素就是白留的
       （预算按 PARAM_GROUP_COUNT 扣，实际画出来的条数必须相等）。*/
    for (g = 0; g < PARAM_GROUP_COUNT; ++g) {
        CHECK_NAMED(g_paramGroupFirst[g + 1] > g_paramGroupFirst[g],
                    L"这一组不是空的（渲染层为它留了一整行）",
                    g_paramGroupNames[g]);
    }
}

/* ============================================================ 参数：恢复
 *
 * 曾经的毛病：按 R 恢复默认之后，黄色小点（「（已被修改）」）还在。
 * 根因是 R 原来退回的是**出厂默认值**，而黄点比的是
 * **当前预设的原值**，两者不是同一把尺子 —— 套过预设的项，退到出厂默认
 * 当然还是"和预设不一样"，点自然不消失。
 *
 * 现在 R 退到"当前预设的值"，黄点的判据一个字没改，两者从构造上就一致。
 * 这一组断言就盯这个一致性：R 之后 `appParamDiffersFromPreset` 必须为 0。
 */
static void tParamReset(void) {
    static App a;
    int preset, i;

    group(L"参数面板：恢复修改");

    for (preset = 0; preset < PRESET_COUNT; ++preset) {
        int covered = 0, uncovered = 0;

        for (i = 0; i < g_paramDescCount; ++i) {
            const ParamDesc *d;
            Params ref;
            float presetVal, defVal, before, lo, hi;
            int usedDefault;

            memset(&a, 0, sizeof(a));
            a.winW = 1600; a.winH = 900;
            paramsDefault(&a.params);
            paramsApplyPreset(&a.params, preset);

            d = &g_paramDescs[i];
            ref = a.params;
            paramsApplyPreset(&ref, preset);
            presetVal = paramDescGet(&ref, d);

            {
                Params def;
                paramsDefault(&def);
                defVal = paramDescGet(&def, d);
            }

            /* 先把它拧到一个**与预设值不同**的位置。取上下限里离得远的那
               一头：有的项默认值就等于预设值，随手改成 hi 也可能撞上，
               所以按"离预设值更远的那一端"来挑，保证真的改动了。*/
            lo = d->lo; hi = d->hi;
            paramDescSet(&a.params, d, (presetVal - lo) >= (hi - presetVal) ? lo : hi);
            before = paramDescGet(&a.params, d);
            if (before == presetVal) continue;   /* 拿不到别的值就跳过这一项 */

            /* 黄点（「（已被修改）」）的判据是"与**这一套预设的原值**不同"。
               所以只有被预设覆盖的项才可能亮起来 —— 预设不管的项（玩家手感、
               画面、声音那些）本来就没有"预设原值"可比，改了也不该标，
               标了反而让人以为"改坏了预设"。这一条把两种情形分开验。*/
            if (paramCoveredByPreset(preset, i))
                CHECK_NAMED(appParamDiffersFromPreset(&a, i) != 0,
                            L"改动之后确实被判为「已被修改」", d->name);
            else
                CHECK_NAMED(appParamDiffersFromPreset(&a, i) == 0,
                            L"预设不管的项改了也不打黄点（没有可比的原值）",
                            d->name);

            usedDefault = paramDescResetToPreset(&a.params, i);

            if (paramCoveredByPreset(preset, i)) {
                CHECK_NAMED(usedDefault == 0,
                            L"被预设覆盖的项走的是「回到预设值」这条路", d->name);
                CHECK_NAMED(paramDescGet(&a.params, d) == presetVal,
                            L"R 之后等于该预设的值", d->name);
                ++covered;
            } else {
                CHECK_NAMED(usedDefault == 1,
                            L"预设没覆盖的项走的是「回到默认值」这条路", d->name);
                CHECK_NAMED(paramDescGet(&a.params, d) == defVal,
                            L"R 之后等于出厂默认值", d->name);
                ++uncovered;
            }

            /* 这一条就是那个毛病本身。*/
            CHECK_NAMED(appParamDiffersFromPreset(&a, i) == 0,
                        L"R 之后黄点必须消失（与当前预设一致）", d->name);
        }

        CHECK(covered > 0, L"这一套预设至少覆盖了一项（否则上面那半段没验到）");
        (void)uncovered;
    }

    /* 再按一次 R 是幂等的：已经等于预设值了，再退还是它。 */
    {
        memset(&a, 0, sizeof(a));
        a.winW = 1600; a.winH = 900;
        paramsDefault(&a.params);
        paramsApplyPreset(&a.params, 1);
        a.paramSel = 0;
        paramDescResetToPreset(&a.params, 0);
        {
            float v1 = paramDescGet(&a.params, &g_paramDescs[0]);
            paramDescResetToPreset(&a.params, 0);
            CHECK(paramDescGet(&a.params, &g_paramDescs[0]) == v1,
                  L"连按两次 R 的结果与按一次相同");
        }
    }

    /* 越界下标：不能越界读，也不能改坏参数。 */
    {
        Params p, q;
        int rc1, rc2;
        paramsDefault(&p);
        paramsApplyPreset(&p, 4);
        q = p;
        rc1 = paramDescResetToPreset(&p, -1);
        rc2 = paramDescResetToPreset(&p, g_paramDescCount);
        CHECK(rc1 == 1 && rc2 == 1,
              L"越界下标的 R 报告「退回了默认值」（调用方据此换提示语）");
        CHECK(paramsEqual(&p, &q), L"越界下标的 R 一个字也没改");
    }
}

/* ============================================================ 参数落盘
 *
 * "保存 / 载入"两个键整个删了，全部参数改成**关设置窗口时写一次**自动保存。
 * 这么定的理由：只在关闭设置窗口时保存，体验上就是即写即存，
 * 又不用频繁落盘。
 *
 * 于是这一组盯三件事：
 *   ① 关面板 → 改过就落盘，没改过**一个字节都不写**（打开看一眼再关掉，
 *      不该让存档文件的时间戳动一下）；
 *   ② 路径为空（--shot 出图形态）时绝不写盘 —— 出图会把玩家的 参数.dat
 *      覆盖掉，这里踩过同类坑；
 *   ③ paramsSave / paramsLoad 本身还是好的（存的是整个 Params，不是当前项）。
 *      自动落盘只是换了**触发时机**，序列化那一层没动，但不测就等于没保证。
 *
 * 至于"S 保存 / L 载入"这两个键本身：已经把它们彻底摘了，钉"它们现在
 * 什么都不做"的断言留在 tPanelNavWASD 里（那一组本来就是管 WASD 的）。
 */
static void tParamAutoSave(void) {
    static App a;
    wchar_t path[MAX_PATH];
    wchar_t dir[MAX_PATH];
    Params saved;
    FILE *f;
    int i;

    group(L"参数落盘：关面板自动保存");

    /* 存到系统临时目录。**绝不能落到 exe 旁边**：那是玩家的 参数.dat，
       自检去动它属于越界（曾经出过一次"跑验证把存档覆盖了"）。*/
    path[0] = L'\0';
    if (GetTempPathW((DWORD)ARRAY_COUNT(dir), dir) > 0)
        _snwprintf(path, ARRAY_COUNT(path) - 1, L"%lsbr_params_selftest.dat", dir);
    if (!path[0]) { CHECK(0, L"取到临时目录（拿不到就没法测落盘）"); return; }
    _wremove(path);

    /* ---------------------------------------------------------- ① 改过再关
       判据不是"提示语说了会保存"，而是**磁盘上真的出现了那份 Params**。*/
    memset(&a, 0, sizeof(a));
    a.winW = 1600; a.winH = 900;
    paramsDefault(&a.params);
    paramsClamp(&a.params);
    _snwprintf(a.paramsPath, ARRAY_COUNT(a.paramsPath) - 1, L"%ls", path);
    appParamOpen(&a);

    for (i = 0; i < g_paramDescCount; ++i) {
        /* 偶数项拧到上限、奇数项拧到下限。**不统一拧到同一头**：
           有些项的默认值本来就在某一端，全拧到同一头会让好几项"看起来
           改了其实没改"，后面那条"确实不一样"就容易假绿。*/
        const ParamDesc *d = &g_paramDescs[i];
        paramDescSet(&a.params, d, (i & 1) ? d->lo : d->hi);
    }
    paramsClamp(&a.params);
    memcpy(&saved, &a.params, sizeof(Params));
    /* 这里是直接改内存（为了把 44 项一次拧完），没走"手改一项"那条路，
       所以手动把 dirty 立起来，模拟"确实动过"。*/
    a.paramDirty = 1;

    CHECK(GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES,
          L"（前置）关面板之前磁盘上还没有这个文件");

    appParamClose(&a);
    CHECK(!appParamIsOpen(&a), L"appParamClose 之后面板是关着的");
    CHECK(!a.paramDirty, L"落盘之后 dirty 被清掉（下次没改过就不会再写）");

    f = _wfopen(path, L"rb");
    CHECK(f != NULL, L"★ 关面板之后磁盘上确实出现了存档文件");
    if (f) {
        unsigned char buf[sizeof(Params) + 64];
        long sz;
        fseek(f, 0, SEEK_END);
        sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        CHECK(sz >= (long)sizeof(Params),
              L"文件至少有整个 Params 那么大（存的是全部，不是当前那一项）");
        if (sz >= (long)sizeof(Params) && sz <= (long)sizeof(buf)) {
            size_t got = fread(buf, 1, (size_t)sz, f);
            CHECK(got == (size_t)sz, L"文件读得完整");
            if (got == (size_t)sz) {
                const unsigned char *tail = buf + ((size_t)sz - sizeof(Params));
                CHECK(memcmp(tail, &saved, sizeof(Params)) == 0,
                      L"文件尾巴上的 Params 与内存里那 44 项逐字节相同");
            }
        } else {
            CHECK(0, L"文件长度落在预期区间内");
        }
        fclose(f);
    }

    /* ---- ② 读回来：44 项一个不差 ---- */
    {
        Params back;
        paramsDefault(&back);
        CHECK(paramsLoad(&back, path) == 0, L"paramsLoad 能读回刚才写的那一份");
        CHECK(paramsEqual(&back, &saved), L"存下来再读回来，44 项参数一个不差");
    }

    /* ---------------------------------------------------------- ③ 没改过就关
       行为是"关上就存"，但"打开看一眼再关掉"不该算改过。
       判据取最强的一版：先把文件删掉，再走一遍"开 → 关"，文件不该重新出现。*/
    _wremove(path);
    appParamOpen(&a);
    CHECK(a.paramDirty == 0, L"刚打开时 dirty 是 0");
    appParamClose(&a);
    CHECK(GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES,
          L"★ 没改过参数就关面板，磁盘上不会冒出文件来（不是无脑写）");

    /* ---------------------------------------------------------- ④ 路径为空
       --shot 出图形态就是这样（main.cpp 里显式清空的）。这一条是护栏：
       出图脚本会反复开关面板，写盘就会把用户的 参数.dat 反复覆盖。*/
    {
        int k;
        memset(&a, 0, sizeof(a));
        a.winW = 1600; a.winH = 900;
        paramsDefault(&a.params);
        a.paramsPath[0] = L'\0';
        appParamOpen(&a);
        for (k = 0; k < g_paramDescCount; ++k)
            paramDescSet(&a.params, &g_paramDescs[k],
                         (k & 1) ? g_paramDescs[k].lo : g_paramDescs[k].hi);
        a.paramDirty = 1;
        appParamClose(&a);
        CHECK(!appParamIsOpen(&a), L"没有可用路径时关面板照样关得掉，不崩");
        CHECK(GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES,
              L"★ 路径为空时一个字节都不写（出图形态不会污染用户存档）");
    }

    /* ---------------------------------------------------------- ⑤ 序列化层的边界 */
    {
        Params p;
        paramsDefault(&p);
        CHECK(paramsSave(&p, NULL) != 0, L"paramsSave 传空路径要报失败，不能静默成功");
        CHECK(paramsLoad(&p, NULL) != 0, L"paramsLoad 传空路径要报失败");
    }
}

/* ============================================================ 面板键位边界
 *
 * 设置面板里删掉 WASD 选择，统一采用上下左右键；不加 ctrl。
 * 三个界面（面板 / 弹窗 / 历史记录屏）一起删。
 *
 * 这一组盯的是**删干净了、但没删过头**。两个方向都要钉：
 *   · WASD 在三个界面里一个都不许再动光标 / 改数值；
 *   · 方向键必须照常工作 —— "摘掉 WASD"很容易手滑写成"把选行整个删了"，
 *     那种错在图上完全看不出来（面板照样能开、能显示），只有断言能发现。
 */
static void tPanelNavWASD(void) {
    static App a;
    static const int wasd[4] = { 'W', 'A', 'S', 'D' };
    int i;

    group(L"面板键位：WASD 已摘除");

    /* ---------------------------------------------------------- 第 0 行「当前预设方案」
       不再有一级 / 二级了，选行统一走 ↑↓。第 0 行也一样：
       WASD 在它上面照样什么都不该做。*/
    for (i = 0; i < 4; ++i) {
        wchar_t what[128];
        int before;
        Params pBefore;
        memset(&a, 0, sizeof(a));
        a.winW = 1600; a.winH = 900;
        paramsDefault(&a.params);
        a.paramOpen = 1; a.paramSel = 0;
        before = a.paramSel;
        pBefore = a.params;
        a.input.pressed[wasd[i]] = 1;
        a.input.down[wasd[i]] = 1;
        appParamInput(&a);
        _snwprintf(what, ARRAY_COUNT(what) - 1,
                   L"第 0 行「当前预设方案」上按 %c 既不动光标也不改数值",
                   (char)wasd[i]);
        what[ARRAY_COUNT(what) - 1] = L'\0';
        CHECK_NAMED(a.paramSel == before && paramsEqual(&a.params, &pBefore),
                    what, L"");
    }

    /* ---------------------------------------------------------- 参数行 */
    for (i = 0; i < 4; ++i) {
        wchar_t what[128];
        int before;
        Params pBefore;
        memset(&a, 0, sizeof(a));
        a.winW = 1600; a.winH = 900;
        paramsDefault(&a.params);
        a.paramOpen = 1; a.paramSel = 6;      /* 第 6 行 = 第 5 项参数 */
        before = a.paramSel;
        pBefore = a.params;
        a.input.pressed[wasd[i]] = 1;
        a.input.down[wasd[i]] = 1;
        appParamInput(&a);
        _snwprintf(what, ARRAY_COUNT(what) - 1,
                   L"参数行上按 %c 既不动光标也不改数值（A/D 也不许当 ←/→ 用）",
                   (char)wasd[i]);
        what[ARRAY_COUNT(what) - 1] = L'\0';
        CHECK_NAMED(a.paramSel == before && paramsEqual(&a.params, &pBefore),
                    what, L"");
    }

    /* ---------------------------------------------------------- 弹窗里也一样 */
    for (i = 0; i < 4; ++i) {
        wchar_t what[128];
        int before;
        Params pBefore;
        memset(&a, 0, sizeof(a));
        a.winW = 1600; a.winH = 900;
        paramsDefault(&a.params);
        a.paramOpen = 1; a.paramSel = 0;
        a.paramPopOpen = 1; a.paramPopSel = 3;
        before = a.paramPopSel;
        pBefore = a.params;
        a.input.pressed[wasd[i]] = 1;
        a.input.down[wasd[i]] = 1;
        appParamInput(&a);
        _snwprintf(what, ARRAY_COUNT(what) - 1,
                   L"预设弹窗里按 %c 既不动光标也不改数值", (char)wasd[i]);
        what[ARRAY_COUNT(what) - 1] = L'\0';
        CHECK_NAMED(a.paramPopSel == before && paramsEqual(&a.params, &pBefore),
                    what, L"");
    }

    /* ---------------------------------------------------------- 历史记录屏 */
    for (i = 0; i < 4; ++i) {
        wchar_t what[96];
        int before;
        memset(&a, 0, sizeof(a));
        a.winW = 1600; a.winH = 900;
        paramsDefault(&a.params);
        a.screen = SCREEN_HISTORY;
        a.history.count = 5;
        a.histSel = 2;
        before = a.histSel;
        a.input.pressed[wasd[i]] = 1;
        a.input.down[wasd[i]] = 1;
        historyInput(&a);
        _snwprintf(what, ARRAY_COUNT(what) - 1,
                   L"历史记录屏里按 %c 不动光标", (char)wasd[i]);
        what[ARRAY_COUNT(what) - 1] = L'\0';
        CHECK_NAMED(a.histSel == before, what, L"");
    }

    /* ---------------------------------------------------------- 没删过头
       方向键必须还是好的。少了这一条，"把选行整个删掉"也能全绿。

       ★ 注意：列表行号 = 参数下标 + 1（第 0 行是「当前预设方案」），
       所以下面凡是拿 paramSel 当参数下标用的地方都得先过 appParamRowToDesc。
       ★ 再加一层：行号是**页内**行号，换算必须把页号一起传进去。
       这一组全在预设页上，所以处处是 (0, …)。*/
    {
        int before;
        memset(&a, 0, sizeof(a));
        a.winW = 1600; a.winH = 900;
        paramsDefault(&a.params);
        a.paramPage = 0;
        a.paramOpen = 1; a.paramSel = 6;     /* 第 6 行 = 第 5 项参数「随机种子」 */
        before = a.paramSel;
        a.input.pressed[VK_DOWN] = 1;
        a.input.down[VK_DOWN] = 1;
        appParamInput(&a);
        CHECK(a.paramSel == before + 1, L"参数行上 ↓ 照常下移一行");

        memset(&a.input, 0, sizeof(a.input));
        before = a.paramSel;
        a.input.pressed[VK_UP] = 1;
        a.input.down[VK_UP] = 1;
        appParamInput(&a);
        CHECK(a.paramSel == before - 1, L"参数行上 ↑ 照常上移一行");

        memset(&a.input, 0, sizeof(a.input));
        /* 随机种子那一行的**行号现查出来**，不写死 6。
           ★ 气球组插了一项「生成位置」之后，行号整体后移一格；
           写死的行号不会报错，只会把这条断言悄悄指到隔壁参数上去 ——
           而隔壁恰好也是整数项，照样能"通过"。查偏移是唯一稳妥的写法。*/
        {
            int k, seedDesc = -1;
            for (k = 0; k < g_paramDescCount; ++k) {
                if (g_paramDescs[k].offset == (int)offsetof(Params, seed)) {
                    seedDesc = k;
                    break;
                }
            }
            a.paramSel = (seedDesc >= 0) ? seedDesc + 1 : 1;   /* 预设页第 0 行不是参数 */
        }
        {
            /* 先把这一项摆到量程正中 —— 直接拿默认值试的话，万一它本来就
               顶在上限，按 → 夹住不动，"数值变了"这条就会假红。

               ★ 这一项就是抓出 paramDescAddInt 那个 bug 的地方：量程
               0..99999999 超过 float 的 24 位有效位，中点 5 千万一带
               每加 1 都会被舍掉，按 → 完全没反应。修法见 config.cpp。*/
            const ParamDesc *d =
                &g_paramDescs[appParamRowToDesc(a.paramPage, a.paramSel)];
            Params p0;
            CHECK(d && d->kind != PK_FLOAT,
                  L"（前置）第 6 行挑出来的确实是一项整数参数");
            paramDescSet(&a.params, d, (d->lo + d->hi) * 0.5f);
            paramsClamp(&a.params);
            p0 = a.params;
            a.input.pressed[VK_RIGHT] = 1;
            a.input.down[VK_RIGHT] = 1;
            appParamInput(&a);
            CHECK(!paramsEqual(&a.params, &p0),
                  L"参数行上 → 对大整数项也照常调大（步长 1 不会被精度吃掉）");
        }
        {
            Params p0 = a.params;
            memset(&a.input, 0, sizeof(a.input));
            a.input.pressed[VK_LEFT] = 1;
            a.input.down[VK_LEFT] = 1;
            appParamInput(&a);
            CHECK(!paramsEqual(&a.params, &p0), L"参数行上 ← 照常调小数值");
        }
        /* 浮点项走的是另一条路，也要钉一条 —— 上面两条都只覆盖了整数项。*/
        {
            const ParamDesc *fd = NULL;
            Params p0;
            for (i = 0; i < g_paramDescCount; ++i)
                if (g_paramDescs[i].kind == PK_FLOAT) { fd = &g_paramDescs[i]; break; }
            CHECK(fd != NULL, L"（前置）找得到一项浮点参数");
            /* 参数下标 → 列表行号：＋1。这是唯一一处"逆着换算"的地方，
               而且这里非逆不可 —— 要的是"光标停在某一项参数上"。*/
            a.paramSel = (int)(fd - g_paramDescs) + 1;
            paramDescSet(&a.params, fd, (fd->lo + fd->hi) * 0.5f);
            paramsClamp(&a.params);
            p0 = a.params;
            memset(&a.input, 0, sizeof(a.input));
            a.input.pressed[VK_RIGHT] = 1;
            a.input.down[VK_RIGHT] = 1;
            appParamInput(&a);
            CHECK(!paramsEqual(&a.params, &p0),
                  L"参数行上 → 对浮点项照常调大（浮点这条老路没被改坏）");
        }
    }
    {
        int before;
        memset(&a, 0, sizeof(a));
        a.winW = 1600; a.winH = 900;
        paramsDefault(&a.params);
        a.screen = SCREEN_HISTORY;
        a.history.count = 5;
        a.histSel = 2;
        before = a.histSel;
        a.input.pressed[VK_DOWN] = 1;
        a.input.down[VK_DOWN] = 1;
        historyInput(&a);
        CHECK(a.histSel == before + 1, L"历史记录屏里 ↓ 照常下移一行");
    }
}

/* ============================================================ R 的作用范围
 *
 * 设置页面中的 r 键只恢复「用户偏好设置」，按下 r 后预设方案
 * 完全不动。R 早先是"整套恢复 41 项"，后来缩到「用户偏好设置」
 * 那 12 项（玩家 / 画面 / 声音三组）。
 *
 * 这一组只钉**范围**这一件事 —— 弹窗、光标、第 0 行那一摊在 tPanelMenu 里。
 * 判据分两半，缺一不可：
 *   · 玩法那 32 项**一个字节都不许动**（"预设方案完全不动"的字面含义）；
 *   · 偏好那 12 项必须真的回到**出厂默认**。参考值用 paramsDefault 现搭，
 *     和被测的那条路（逐项 paramDescReset）不是同一条代码路径 ——
 *     否则"函数恒为 no-op"和"函数正确"会一起变绿。
 */
static void tPanelRestorePreferences(void) {
    static App a;
    Params def, gameBefore, after;
    int i, gameMoved = 0, prefChanged = 0, bad = 0;

    group(L"参数面板：R 只恢复用户偏好设置");

    memset(&a, 0, sizeof(a));
    a.winW = 1600; a.winH = 900;
    paramsDefault(&a.params);
    paramsApplyPreset(&a.params, 4);       /* 渐进训练，好和出厂默认拉开距离 */
    appParamOpen(&a);

    /* （前置）玩法段故意改一项：R 不该把它拉回去。 */
    {
        const ParamDesc *d = &g_paramDescs[0];
        float v = paramDescGet(&a.params, d);
        paramDescSet(&a.params, d, (v == d->hi) ? d->lo : d->hi);
    }
    /* 偏好段逐项拨到"和现在不一样"的一头。 */
    for (i = 0; i < g_paramDescCount; ++i) {
        const ParamDesc *d = &g_paramDescs[i];
        float v0, nv;
        if (!paramIsPreference(i)) continue;
        v0 = paramDescGet(&a.params, d);
        nv = (v0 + d->step <= d->hi) ? v0 + d->step : v0 - d->step;
        if (nv == v0) continue;            /* 拨不动的项跳过 */
        paramDescSet(&a.params, d, nv);
    }
    paramsClamp(&a.params);

    /* ---- 反面：光标在「预设方案设置」页上时，R 是死键 ----
       ★ R 的生效范围**收紧过**（早先是"光标停在哪一行都管用"）。
       R 做的事全部落在偏好页上，在预设页上按它也一声不吭地改偏好，
       就是"人在这个页面、事出在那个页面"；两个页面的
       快捷键该分开，那 R 的门槛就该是"当前页是偏好页"。

       ★ 光标**必须落在参数行上**，不能停在第 0 行：第 0 行的 descIdx 是 -1，
       appParamInputMenu 会在那儿提前 return，压根走不到 R 那一段 ——
       那样测出来的"没生效"是假的（去掉页号门槛它照样通过）。
       这一条是"故意破坏"抓出来的：一开始就是这么写的，把门槛摘掉后全绿。

       ★ 还多了一句 `paramOnMenu = 0`：appParamOpen 现在把光标留在
       菜单栏上，而栏上那条路**不经过** appParamInputMenu，
       留在栏上的话这条用例测的就成了栏上那条路，注释里说的"提前 return"、
       "页号门槛"全都落空。下面这条要测的是**列表那条路**，所以显式进列表。*/
    a.paramOnMenu = 0;
    a.paramSel = 7;                     /* 预设页第 7 行 = 参数表第 6 项 */
    CHECK(appParamRowToDesc(a.paramPage, a.paramSel) == 6,
          L"（前置）光标停在预设页的参数行上（不是第 0 行）");
    gameBefore = a.params;
    panelKey(&a, 'R');
    {
        int same = 1;
        for (i = 0; i < g_paramDescCount && same; ++i) {
            const ParamDesc *d = &g_paramDescs[i];
            if (paramDescGet(&a.params, d) != paramDescGet(&gameBefore, d)) same = 0;
        }
        CHECK(same, L"★ 预设页上按 R 一项参数都不动（R 只在偏好页生效）");
        CHECK(wcscmp(a.paramMsg, L"已恢复用户偏好设置") != 0,
              L"★ 预设页上按 R 也不报「已恢复」—— 没做的事不许报成功");
    }

    /* 切到偏好页再按，这才是它该管用的地方。切页后光标落在页内第 0 行，
       所以下面那条"R 不动光标"比的就是这个 0。 */
    appParamSetPage(&a, 1);
    CHECK(a.paramPage == 1 && a.paramSel == 0,
          L"（前置）已切到偏好页，光标落在页内第 0 行");

    panelKey(&a, 'R');
    after = a.params;

    /* ---- 一半：32 项玩法参数一项都没动 ---- */
    for (i = 0; i < g_paramDescCount; ++i) {
        const ParamDesc *d = &g_paramDescs[i];
        if (paramIsPreference(i)) continue;
        if (paramDescGet(&after, d) != paramDescGet(&gameBefore, d)) ++gameMoved;
    }
    CHECK(gameMoved == 0,
          L"★ 32 项玩法参数一项都没动（「按下 r 后预设方案完全不动」）");

    /* ---- 另一半：12 项偏好参数回到了出厂默认 ---- */
    paramsDefault(&def);
    for (i = 0; i < g_paramDescCount; ++i) {
        const ParamDesc *d = &g_paramDescs[i];
        if (!paramIsPreference(i)) continue;
        if (paramDescGet(&after, d) != paramDescGet(&gameBefore, d)) ++prefChanged;
    }
    CHECK(prefChanged > 0, L"R 确实把偏好段改回去了（不是空动作）");

    {
        int prefBad = 0;
        for (i = 0; i < g_paramDescCount; ++i) {
            const ParamDesc *d = &g_paramDescs[i];
            if (!paramIsPreference(i)) continue;
            if (paramDescGet(&after, d) != paramDescGet(&def, d)) ++prefBad;
        }
        CHECK(prefBad == 0,
              L"★ 12 项偏好参数逐项等于出厂默认（参考值由 paramsDefault 单独搭出来）");
    }

    /* ---- preset 字段与光标都不该被这个键碰到 ---- */
    CHECK(after.preset == gameBefore.preset,
          L"R 不换预设（preset 字段没变）");
    CHECK(a.paramSel == 0, L"R 不动光标");

    /* ---- 幂等：再按一次结果完全一样 ---- */
    panelKey(&a, 'R');
    for (i = 0; i < g_paramDescCount && !bad; ++i) {
        const ParamDesc *d = &g_paramDescs[i];
        if (paramDescGet(&a.params, d) != paramDescGet(&after, d)) ++bad;
    }
    CHECK(bad == 0, L"连按两次 R 结果一样（恢复是幂等的）");

    CHECK(wcscmp(a.paramMsg, L"已恢复用户偏好设置") == 0,
          L"提示语写的是「已恢复用户偏好设置」（不再是「已恢复整套预设」）");

    /* ---- 在同一页里把光标挪到别的参数行上，R 照样管用、也照样不挪光标 ----
       它是"页面级的动作"，不是"某一行的动作" —— 门槛是**在哪一页**，
       不是**停在哪一行**。 */
    {
        int di;
        a.paramSel = 7;
        /* ★ 判据从"表里第 36 项"改成"它确实是个偏好参数"。
           写死的下标一插项就错位，而这条断言要的本来只是"光标停在一项
           偏好参数上"这个前提。*/
        di = appParamRowToDesc(a.paramPage, a.paramSel);
        CHECK(di >= 0 && paramIsPreference(di),
              L"（前置）偏好页第 7 行确实是参数行，且属于偏好段");
        panelKey(&a, 'R');
        CHECK(a.paramSel == 7, L"光标停在参数行上时 R 也不挪光标");
        CHECK(wcscmp(a.paramMsg, L"已恢复用户偏好设置") == 0,
              L"★ 偏好页上光标停在哪一行都照样生效（页面级动作，与行号无关）");
    }

    appParamClose(&a);
}

/* ============================================================ 界面文案
 *
 * 说明内容要用客观语句，避免大白话。
 *
 * 这一组做两件事：
 *
 *   ① 把面板页脚那几段话逐字钉住。它们现在是 hud.cpp 里的具名常量，
 *      render.cpp 只按名字取用 —— 这里漏过一步：保存键挪到
 *      Ctrl+S 之后只改了 hud.cpp，render.cpp 里那份老文案还在，屏幕上
 *      "撤销"两个字就变成了空心方块，而当时的出图脚本把控制台的告警
 *      一起吞掉了，最后是盯着图看才发现的。
 *
 *   ② 把**所有会画到屏幕上的字**扫一遍，禁掉几类口语化措辞。名单里前
 *      六条是典型的口语化原句（"想让那一套预设上场""把光标停在上面"
 *      "这一项已经说清楚了""第一次就是最近打完的那一局""拨到「开」"
 *      "能立刻听出音量合不合适"），后几条是明显在跟人聊天的小词。
 *
 *      ★ 只扫数据表，**不扫源码文件**。注释里出现第二人称代词是正常的 ——
 *      那是写代码的人之间说话，扫进去只会逼着注释也写得不自然。
 *      屏幕上真正会出现的字只有这几处来源：hud 文案表、config.cpp 的
 *      参数描述表与预设表，一个不多。
 */
static void tPanelWording(void) {
    static HudFont h;
    int i, j, bad;

    group(L"界面文案：页脚与客观措辞");

    /* ---------------------------------------------------------- ① 页脚字在图集里 */
    hudCollectCharset(&h);
    {
        /* ★ 从 8 段涨到 11 段：页脚第一行多出"菜单栏"那一态
           （g_panelHintBar），第二行的 R 单独成句（g_panelHintPref），
           右栏又多出两段"这一页是干什么的"（g_panelSectionHelp）。 */
        const wchar_t *strs[11];
        int n = 0, nulls = 0;
        strs[n++] = g_panelHintMenu;
        strs[n++] = g_panelHintPref;
        strs[n++] = g_panelCloseMenu;
        strs[n++] = g_panelHintRow0;
        strs[n++] = g_panelHintBar;
        strs[n++] = g_panelHintPop;
        strs[n++] = g_panelNoHelp;
        strs[n++] = g_panelDetailHelp;
        strs[n++] = g_panelCustomHelp;
        strs[n++] = g_panelSectionHelp[0];
        strs[n++] = g_panelSectionHelp[1];

        bad = 0;
        for (i = 0; i < n; ++i) {
            const wchar_t *s = strs[i];
            if (!s) { ++nulls; continue; }
            for (j = 0; s[j]; ++j)
                if (s[j] >= 32 && hudCharIndex(&h, s[j]) < 0) ++bad;
        }
        CHECK(nulls == 0, L"十一段面板文案都有定义（没有空指针）");
        CHECK(bad == 0,
              L"★ 面板页脚与提示的每一个字都在字形图集里（缺一个就是一个空心方块）");
    }

    /* ---------------------------------------------------------- ② 页脚的措辞
       保存 / 载入整个删了，页脚上就**不能再提 S 和 L** ——
       留着就是一句假话。
       页脚写的是什么键，就得是**真的能按**的键；而且第 0 行与弹窗那两处
       能按的键和普通参数行不一样，所以第一行必须换对象。*/
    /* ★ 这一节整体重写过。规则是"当前光标有哪些快捷键才显示
       哪些快捷键提示"，于是三条边界都得逐字钉住：
         · 回车只属于第 0 行 —— 别处一个字都不许提；
         · R 只属于偏好页 —— 别处一个字都不许提；
         · "关闭时自动保存"整句删除。*/
    CHECK(wcsstr(g_panelHintRow0, L"回车：展开预设方案") != NULL,
          L"页脚写明回车在第 0 行是展开预设方案");
    CHECK(wcsstr(g_panelHintPref, L"R：恢复用户偏好设置") != NULL,
          L"页脚写明 R 恢复的是「用户偏好设置」（改过，原来是「整套预设」）");
    /* 否定式：删掉的东西不能再留一星半点 —— 留着就是一句假话。*/
    CHECK(wcsstr(g_panelHintMenu, L"S：保存") == NULL &&
          wcsstr(g_panelHintMenu, L"L：载入") == NULL,
          L"页脚上不再出现 S 保存 / L 载入（这两个键已删）");
    CHECK(wcsstr(g_panelHintPref, L"整套预设") == NULL,
          L"旧的「整套预设」（早先 R 的范围）已经不在页脚上");
    CHECK(wcsstr(g_panelHintMenu, L"Ctrl") == NULL,
          L"页脚上不再出现 Ctrl（旧的组合键写法已经撤掉）");

    /* ---- ★ 回车只写在第 0 行 ----
       回车快捷键提示只在光标选中当前预设方案时才显示。
       在普通参数行上按回车什么都不会发生（它只负责第 0 行的弹窗），
       页脚在那儿写"回车"就是教用户去按一个死键。*/
    CHECK(wcsstr(g_panelHintMenu, L"回车") == NULL,
          L"★ 普通参数行的页脚不提回车（这一行上回车不做事）");
    CHECK(wcsstr(g_panelHintBar, L"回车") == NULL,
          L"★ 菜单栏上的页脚也不提回车（回车在栏里不进入列表）");
    CHECK(wcsstr(g_panelHintPop, L"回车") != NULL,
          L"弹窗展开时页脚仍然写回车（这一态下回车是真的能按）");

    /* ---- ★ 回车的说法叫「确认」，不叫「套用」----
       展开预设选项二级菜单时，回车键的快捷键提示叫「确认」不叫「套用」。
       否定式一并查上：「套用」是程序内部的说法（代码里那条路径就叫
       appParamApplyPreset），它不该出现在**任何**一段给用户看的页脚里。*/
    CHECK(wcsstr(g_panelHintPop, L"回车：确认") != NULL,
          L"★ 弹窗页脚写的是「回车：确认」");
    {
        int k, hit = 0;
        for (k = 0; k < 6; ++k) {
            const wchar_t *s = (k == 0) ? g_panelHintMenu :
                               (k == 1) ? g_panelHintPref :
                               (k == 2) ? g_panelCloseMenu :
                               (k == 3) ? g_panelHintRow0 :
                               (k == 4) ? g_panelHintBar : g_panelHintPop;
            if (s && wcsstr(s, L"套用")) hit = 1;
        }
        CHECK(!hit, L"★ 六段页脚里不再出现「套用」这个词（一律说「确认」）");
    }

    /* ---- ★ 弹窗那一条改叫「自定义参数」，旧叫法「自定义设置」全清
       肯定式与否定式各量一条：名字必须是新的，旧名字必须一处都不剩 ——
       只查"新名字在"会漏掉"旧名字还挂在别处"这种半吊子改法。*/
    CHECK(wcscmp(g_panelCustomName, L"自定义设置") != 0,
          L"★ 弹窗那一条不再叫「自定义设置」");
    {
        const wchar_t *pool[9];
        int k, hit = 0;
        pool[0] = g_panelHintMenu;  pool[1] = g_panelHintPref;
        pool[2] = g_panelCloseMenu; pool[3] = g_panelHintRow0;
        pool[4] = g_panelHintBar;   pool[5] = g_panelHintPop;
        pool[6] = g_panelDetailHelp; pool[7] = g_panelCustomHelp;
        pool[8] = g_panelCustomName;
        for (k = 0; k < 9; ++k)
            if (pool[k] && wcsstr(pool[k], L"自定义设置")) hit = 1;
        CHECK(!hit, L"★ 面板九段文案里「自定义设置」一处都不剩");
    }

    /* ---- ★ R 只写在偏好页那一行，而且只在偏好页上才画那一行 ----
       （"画不画"归 render.cpp 管，"写了什么"归这里管。）*/
    CHECK(wcsstr(g_panelHintMenu, L"R") == NULL &&
          wcsstr(g_panelHintRow0, L"R") == NULL &&
          wcsstr(g_panelHintBar, L"R") == NULL &&
          wcsstr(g_panelHintPop, L"R") == NULL,
          L"★ 其余三态都不提 R（R 只在偏好页生效）");

    /* ---- ★ 「关闭时自动保存」整句删除 ----
       删的是**这句话**，不是那个行为：落盘时机没变（关面板才写），
       只是页脚不再播报它 —— 页脚只管"按什么键"，不管"什么时候写盘"。*/
    CHECK(wcsstr(g_panelHintMenu, L"自动保存") == NULL &&
          wcsstr(g_panelHintPref, L"自动保存") == NULL &&
          wcsstr(g_panelCloseMenu, L"自动保存") == NULL &&
          wcsstr(g_panelHintRow0, L"自动保存") == NULL &&
          wcsstr(g_panelHintBar, L"自动保存") == NULL &&
          wcsstr(g_panelHintPop, L"自动保存") == NULL,
          L"★ 六段页脚文案里都不再出现「自动保存」这句话");

    /* ---- ★ 菜单栏那一行：左右切页、下回列表，两件事都得写全 ----
       ★ 换了写法：快捷键提示中的上下左右一律换成符号
       而不是汉字，所以这里认的是 ←→ 和 ↓。*/
    CHECK(wcsstr(g_panelHintBar, L"←→：切换分类") != NULL &&
          wcsstr(g_panelHintBar, L"↓：回到列表") != NULL,
          L"★ 菜单栏的页脚写明「←→ 切页、↓ 回列表」这两件事（用符号）");

    /* ---- ★ 方向键的提示一律用符号，不许出现「上下左右」四个汉字 ----
       逐字扫六段页脚文案，而不是一条条 wcsstr 去认 —— 后者只保证"新写法
       在里头"，管不了"旧写法还混着"。以后新加一段提示，只要写了"上下"
       就会被这条抓出来。

       ⚠ 为什么只扫这六段、不扫整个 g_hudStrings：HUD 上别处有正当用到的
       这几个字（"墙上 %d 个"的「上」就是），全局扫会冤枉它们。页脚是
       唯一"说话给用户看的方向键提示"，扫这一处就够。

       ⚠ 历史记录屏那一行提示（hud.cpp 里 g_hudStrings 的一条字面量）
       也跟着换了符号，但它是匿名条目，这里指不到 —— 它的保证来自
       字形图集那一组（每个画到屏幕上的字都在图集里）。*/
    {
        const wchar_t *dirs[6];
        const wchar_t *hanzi = L"上下左右";
        int k, m, hit = 0;
        dirs[0] = g_panelHintMenu;
        dirs[1] = g_panelHintRow0;
        dirs[2] = g_panelHintBar;
        dirs[3] = g_panelHintPop;
        dirs[4] = g_panelHintPref;
        dirs[5] = g_panelCloseMenu;
        for (k = 0; k < 6; ++k) {
            if (!dirs[k]) continue;
            for (m = 0; dirs[k][m]; ++m)
                if (wcsrchr(hanzi, dirs[k][m])) { hit = 1; break; }
        }
        CHECK(!hit,
              L"★ 六段页脚文案里一个「上下左右」汉字都不许有（一律用 ↑↓←→）");
        CHECK(wcsstr(g_panelHintMenu, L"↑↓：选择") != NULL &&
              wcsstr(g_panelHintMenu, L"←→：调整") != NULL &&
              wcsstr(g_panelHintRow0, L"↑↓：选择") != NULL &&
              wcsstr(g_panelHintPop, L"↑↓：选择") != NULL,
              L"★ 四段提示里的方向说明都换成了符号，且位置没变（还是「：选择」「：调整」）");
        /* 英文键名不属于"上下左右四个汉字"，不许被顺手改掉 —— 改掉它们
           就没人认得那一行说的是哪个键了。

           ★ PgUp / PgDn 整个删了，所以这里从
           "PgUp / PgDn / Home / End 都写着"反写成"**不许再出现**"。
           删一个功能时最容易漏的就是提示语 —— 键不响应了、页脚还写着，
           按下去没反应，玩家会以为程序卡了。Home / End 仍然要在。*/
        CHECK(wcsstr(g_panelHintMenu, L"PgUp") == NULL &&
              wcsstr(g_panelHintMenu, L"PgDn") == NULL,
              L"★ 页脚里不许再出现 PgUp / PgDn（这两个键已删除）");
        CHECK(wcsstr(g_panelHintMenu, L"Home / End") != NULL,
              L"Home / End 照旧写着（它们不是「上下左右」四个汉字，不该换）");

        /* ★ ESC 不再关面板，面板的退出口只剩 Tab。
           三段页脚（菜单栏那行、第 0 行那行、参数行那行）共用
           g_panelCloseMenu，所以查一处就够；弹窗那一行**必须**留着 ESC
           —— 那里 ESC 确实还在收列表（弹窗里保留 Esc 收起）。*/
        CHECK(wcsstr(g_panelCloseMenu, L"Tab") != NULL &&
              wcsstr(g_panelCloseMenu, L"ESC") == NULL,
              L"★ 面板的退出口只剩 Tab，页脚不再写「ESC：关闭」");
        CHECK(wcsstr(g_panelHintPop, L"ESC") != NULL,
              L"弹窗里 ESC 照旧是「收起列表」（这一处的 ESC 保留着）");
    }

    /* ---- ★ 右栏那两段"这一页是干什么的"，里面的数字得是真的 ----
       中文数字打不进 %d，所以"32 项 / 12 项"是**手写在文案里**的。
       手写的数字会跟参数表走散（增减一项参数就假了），这里现数一遍表，
       再和文案对照 —— 两边的数字必须一致。 */
    {
        int nPref = 0, nPlay = 0;
        wchar_t tag[16];
        for (i = 0; i < g_paramDescCount; ++i)
            (paramIsPreference(i) ? nPref : nPlay)++;

        _snwprintf(tag, ARRAY_COUNT(tag) - 1, L"%d 项", nPlay);
        CHECK(nPlay == 32 && wcsstr(g_panelSectionHelp[0], tag) != NULL,
              L"★ 预设页右栏写的项数 = 玩法参数的真实项数（现在都是 32）");
        CHECK(wcsstr(g_panelSectionHelp[0], L"预设方案") != NULL,
              L"预设页右栏说清了这一页会被预设方案覆盖");

        _snwprintf(tag, ARRAY_COUNT(tag) - 1, L"%d 项", nPref);
        CHECK(nPref == 12 && wcsstr(g_panelSectionHelp[1], tag) != NULL,
              L"★ 偏好页右栏写的项数 = 偏好参数的真实项数（现在都是 12）");
        CHECK(wcsstr(g_panelSectionHelp[1], L"R") != NULL,
              L"偏好页右栏说明里明确指出 R 只管这一段（与页脚那句同源）");
    }

    /* 第一行随光标位置换对象，这是"页脚不许骗人"的核心：
       第 0 行上没有数值可调，弹窗里也没有，两处都**不许**再写"←→：调整"。
       只查大写 W：页脚里除了这里没有别处会出现 W
       （'S' 不能这么查 —— "ESC" 里就含一个 S，那是真的假阳性）。

       ★ 快捷键提示中的上下左右一律换成符号，所以这几条
       查的字面量从汉字「左右」换成了「←→」。查的东西没变（还是"这一行到底
       有没有左右键的说明"），换的只是写法 —— 这一步必须跟着文案一起改，
       否则断言会变成"永远为真"（找一个已经不存在的汉字，永远找不着）。*/
    CHECK(wcsstr(g_panelHintMenu, L"←→：调整") != NULL,
          L"普通参数行的页脚仍然写明左右键调数值");
    CHECK(wcsstr(g_panelHintMenu, L"W") == NULL, L"页脚不再写 W（面板统一用方向键）");
    CHECK(wcsstr(g_panelHintRow0, L"←→") == NULL,
          L"★ 第 0 行那一行没有数值可调，页脚就不写「←→：调整」");
    CHECK(wcsstr(g_panelHintRow0, L"回车") != NULL,
          L"第 0 行的页脚写明回车能展开预设方案");
    CHECK(wcsstr(g_panelHintPop, L"←→") == NULL,
          L"★ 弹窗里也没有数值可调，页脚同样不写「←→：调整」");
    CHECK(wcsstr(g_panelHintPop, L"ESC") != NULL,
          L"弹窗的页脚写明 ESC 是收起列表（不是关面板）");

    /* 侧栏"没有说明"的占位语，就用这五个字。*/
    CHECK(wcscmp(g_panelNoHelp, L"（无说明）") == 0,
          L"没有说明的项写「（无说明）」");

    /* 第 0 行的说明里写死了"八套预设"。中文数字没法用 %d 打出来，
       所以这里只能把两个数字钉在一起：改了一边另一边就红。*/
    CHECK(PRESET_COUNT == 8 && wcsstr(g_panelDetailHelp, L"八套预设") != NULL,
          L"「当前预设方案」的说明里写的套数与 PRESET_COUNT 一致（现在都是 8）");
    CHECK(wcsstr(g_panelDetailHelp, L"自定义参数") != NULL,
          L"「当前预设方案」的说明解释了「自定义参数」是什么意思");
    CHECK(g_panelCustomHelp && g_panelCustomHelp[0] != L'\0' &&
          wcsstr(g_panelCustomHelp, L"收起") != NULL,
          L"「自定义参数」那一条在右栏有说明，且说清了它只是收起列表");

    /* ---------------------------------------------------------- ③ 客观措辞 */
    {
        /* 六句典型口语化原句 + 明显在跟人聊天的几个小词。*/
        static const wchar_t *const banned[] = {
            L"想让", L"把光标停在", L"已经说清楚了", L"第一次就是",
            L"拨到", L"能立刻听出",
            L"你", L"咱", L"吧", L"哦", L"嘛", L"啦", L"呢", L"…哈"
        };

        for (j = 0; j < (int)ARRAY_COUNT(banned); ++j) {
            wchar_t what[160];
            int hits = 0;
            const wchar_t *firstHit = NULL;

            /* 把禁用词写进断言文字里。明细报告里会连着出现十几条同样的
               "界面文字里没有这个口语化措辞"，看不出查的是哪一个 ——
               写进去之后，报告本身就是一份"查过哪些词"的清单。*/
            _snwprintf(what, ARRAY_COUNT(what) - 1,
                       L"界面文字里没有「%ls」这种口语化措辞", banned[j]);
            what[ARRAY_COUNT(what) - 1] = L'\0';

            for (i = 0; i < g_hudStringCount; ++i)
                if (g_hudStrings[i] && wcsstr(g_hudStrings[i], banned[j])) {
                    ++hits; if (!firstHit) firstHit = g_hudStrings[i];
                }

            for (i = 0; i < g_paramDescCount; ++i) {
                const ParamDesc *d = &g_paramDescs[i];
                const wchar_t *f[3];
                int k, v;
                f[0] = d->name; f[1] = d->unit; f[2] = d->help;
                for (k = 0; k < 3; ++k)
                    if (f[k] && wcsstr(f[k], banned[j])) {
                        ++hits; if (!firstHit) firstHit = f[k];
                    }
                for (v = 0; v < d->enumCount; ++v) {
                    const wchar_t *s = paramEnumName(d, v);
                    if (s && wcsstr(s, banned[j])) {
                        ++hits; if (!firstHit) firstHit = s;
                    }
                }
            }

            for (i = 0; i < g_paramGroupCount; ++i)
                if (g_paramGroupNames[i] && wcsstr(g_paramGroupNames[i], banned[j])) {
                    ++hits; if (!firstHit) firstHit = g_paramGroupNames[i];
                }

            for (i = 0; i < g_presetCount; ++i) {
                if (g_presets[i].name && wcsstr(g_presets[i].name, banned[j])) {
                    ++hits; if (!firstHit) firstHit = g_presets[i].name;
                }
                if (g_presets[i].desc && wcsstr(g_presets[i].desc, banned[j])) {
                    ++hits; if (!firstHit) firstHit = g_presets[i].desc;
                }
            }

            CHECK_NAMED(hits == 0, what,
                        firstHit ? firstHit : banned[j]);
        }
    }

    /* ---------------------------------------------------------- ④ 说明都得有主语
       客观说明与"跟人聊天"的一个硬区别：**句子说的是"这一项是什么"，
       不是"命令人该怎么做"**。用祈使/第二人称的语气词做一次兜底扫描 ——
       这几条比 ③ 宽，属于"提醒"性质，但仍然是真的会失败的断言。*/
    {
        static const wchar_t *const orders[] = { L"请", L"记住", L"别忘了" };
        for (j = 0; j < (int)ARRAY_COUNT(orders); ++j) {
            wchar_t what[160];
            int hits = 0;
            _snwprintf(what, ARRAY_COUNT(what) - 1,
                       L"说明文字里没有祈使句式的「%ls」", orders[j]);
            what[ARRAY_COUNT(what) - 1] = L'\0';
            for (i = 0; i < g_paramDescCount; ++i) {
                const ParamDesc *d = &g_paramDescs[i];
                if (d->help && wcsstr(d->help, orders[j])) ++hits;
            }
            for (i = 0; i < g_presetCount; ++i)
                if (g_presets[i].desc && wcsstr(g_presets[i].desc, orders[j])) ++hits;
            CHECK_NAMED(hits == 0, what, orders[j]);
        }
    }

    /* ---------------------------------------------------------- ⑤ 结束原因的说法
       结算屏与历史屏上"这一局怎么结束的"那几段字，来自 render.cpp 的
       endReasonText()，而图集是照 hud.cpp 的 g_hudStrings[] 收的 ——
       两边**各写一份**。这里栽过这种两处一词的跟头（改了 hud.cpp 那份、
       render.cpp 那份没动，屏幕上冒出空心方块，只有在图集里查不到的字才这样）。
       所以这里把 endReasonText 的每一条都拿到表里找一遍：
         · 每一个字都在图集里（缺一个字 = 一个空心方块）；
         · 那整句必须**出现在**表里的某一条上（漏登记这一句就红）。

       ★ 加这一节的直接由头是「自己收的」→「手动结束」：
       改的是 render.cpp 那一处，若忘了同步 hud.cpp，这条断言就会响。*/
    {
        int r, missed = 0, badChar = 0;
        CHECK(endReasonText(END_NONE)[0] == L'\0',
              L"没有结束原因时返回空串（结算屏据此不写那一行）");
        for (r = END_TIME; r < END_REASON_COUNT; ++r) {
            const wchar_t *s = endReasonText(r);
            int k, inTable = 0;
            if (!s || !s[0]) { ++missed; continue; }
            for (k = 0; s[k]; ++k)
                if (s[k] >= 32 && hudCharIndex(&h, s[k]) < 0) ++badChar;
            for (i = 0; i < g_hudStringCount; ++i)
                if (g_hudStrings[i] && wcsstr(g_hudStrings[i], s)) { inTable = 1; break; }
            if (!inTable) ++missed;
        }
        CHECK(badChar == 0,
              L"★ 每一条结束原因文字里的字都在字形图集里（render.cpp 与 hud.cpp 不许走散）");
        CHECK(missed == 0,
              L"★ 每一条结束原因文字都逐字出现在 hud.cpp 的文案表里（漏登记就画成方块）");
        CHECK(wcscmp(endReasonText(END_MANUAL), L"手动结束") == 0,
              L"★ 玩家自己结算那一局写的是「手动结束」（改掉了「自己收的」）");
    }
}

/* ============================================================ 按键语义
 *
 * r 和 q 的功能太冗余（已删除），esc 只能暂停游戏。
 * 这一组把三个键在每一屏上的效果逐格钉住 —— 键位表是最容易在改动里
 * 悄悄走样的东西：删一个键、挪一个分支，玩法上不会立刻出错，但玩家
 * 按下去没反应，只有断言能发现。
 */
static void tKeyMeanings(void) {
    static App a;

    group(L"按键：R / F / ESC / Q");

    /* ---- 游戏中按 R：结算，并已经在结算屏上 ---- */
    memset(&a, 0, sizeof(a));
    a.winW = 1600; a.winH = 900;
    paramsDefault(&a.params);
    appResetSession(&a, 4242u);
    a.score = 77; a.popped = 7;
    memset(&a.input, 0, sizeof(a.input));
    a.input.pressed['R'] = 1;
    appHandleInput(&a);
    CHECK(a.screen == SCREEN_SETTLE, L"游戏中按 R 进入结算屏");
    CHECK(a.lastResult.score == 77, L"结算用的是刚打出来的成绩");

    /* ---- 结算屏按 R：真的开新一局，分数归零 ---- */
    memset(&a.input, 0, sizeof(a.input));
    a.input.pressed['R'] = 1;
    appHandleInput(&a);
    CHECK(a.screen == SCREEN_PLAY, L"结算屏再按 R 回到游戏");
    CHECK(a.score == 0, L"新一局的分数从 0 开始");
    CHECK(a.popped == 0 && a.missed == 0, L"新一局的击破与漏球都归零");
    CHECK(a.lives > 0, L"新一局生命补满");

    /* ---- 结算屏按 ESC：什么都不做（esc 只能暂停游戏） ---- */
    memset(&a.input, 0, sizeof(a.input));
    a.input.pressed[VK_ESCAPE] = 1;
    appHandleInput(&a);
    CHECK(a.screen == SCREEN_PAUSE, L"游戏中的 ESC 进暂停");
    {
        int before = a.screen;
        memset(&a.input, 0, sizeof(a.input));
        a.input.pressed[VK_ESCAPE] = 1;
        appHandleInput(&a);
        CHECK(a.screen != before && a.screen == SCREEN_PLAY,
              L"暂停中的 ESC 回游戏");
    }
    {
        appSetScreen(&a, SCREEN_SETTLE);
        memset(&a.input, 0, sizeof(a.input));
        a.input.pressed[VK_ESCAPE] = 1;
        appHandleInput(&a);
        CHECK(a.screen == SCREEN_SETTLE, L"结算屏的 ESC 不改变屏幕");
    }

    /* ---- 结算屏按 F：进历史记录屏；其它屏按 F 无效 ---- */
    {
        memset(&a.input, 0, sizeof(a.input));
        a.input.pressed['F'] = 1;
        appHandleInput(&a);
        CHECK(a.screen == SCREEN_HISTORY, L"结算屏按 F 进历史记录");
        CHECK(a.histSel == 0, L"进去时光标停在最近一次上");

        /* ★ 历史屏**只有 F** 能关（关闭历史记录页面只能由 f 键管，
           tab 只管关闭设置页面；esc 不能用于关闭设置页面和历史记录页面）。
           所以这里从"按 ESC 退回"反写成"按 ESC **退不了**"，并把 Tab 一并
           钉住 —— 当年这两条键都是关闭路径，删一处漏一处（比如只删了 ESC）
           这里就会红。*/
        {
            const int ks[2] = { VK_ESCAPE, VK_TAB };
            int k, bad = 0;
            for (k = 0; k < 2; ++k) {
                memset(&a.input, 0, sizeof(a.input));
                a.input.pressed[ks[k]] = 1;
                appHandleInput(&a);
                if (a.screen != SCREEN_HISTORY) ++bad;
            }
            CHECK(bad == 0,
                  L"★ 历史屏按 ESC / Tab 都退不出去（只剩 F 管关闭）");
        }

        /* F 按一下退、再按一下又进 —— "同一个键开合"这条纪律照旧。
           分三段按，就是为了让"退出去"和"进得来"各自可判定。*/
        memset(&a.input, 0, sizeof(a.input));
        a.input.pressed['F'] = 1;
        appHandleInput(&a);
        CHECK(a.screen == SCREEN_SETTLE, L"历史屏按 F 退回结算屏（唯一的关闭键）");

        memset(&a.input, 0, sizeof(a.input));
        a.input.pressed['F'] = 1;
        appHandleInput(&a);
        CHECK(a.screen == SCREEN_HISTORY, L"结算屏再按 F 又进历史屏（同一个键开合）");

        memset(&a.input, 0, sizeof(a.input));
        a.input.pressed['F'] = 1;
        appHandleInput(&a);
        CHECK(a.screen == SCREEN_SETTLE, L"再按一次 F 又退回来");
    }
    {
        appSetScreen(&a, SCREEN_PLAY);
        memset(&a.input, 0, sizeof(a.input));
        a.input.pressed['F'] = 1;
        appHandleInput(&a);
        CHECK(a.screen == SCREEN_PLAY, L"游戏中的 F 不做任何事");
    }

    /* ---- Q 已经从键位表里删掉 ---- */
    {
        int i;
        const int ks[4] = { SCREEN_PLAY, SCREEN_PAUSE, SCREEN_SETTLE,
                            SCREEN_HISTORY };
        for (i = 0; i < 4; ++i) {
            int before;
            appSetScreen(&a, ks[i]);
            if (a.paramOpen) appParamClose(&a);
            before = a.screen;
            memset(&a.input, 0, sizeof(a.input));
            a.input.pressed['Q'] = 1;
            appHandleInput(&a);
            CHECK_NAMED(a.screen == before, L"按 Q 不改变屏幕（这个键已作废）",
                        (ks[i] == SCREEN_PLAY)    ? L"游戏中" :
                        (ks[i] == SCREEN_PAUSE)   ? L"暂停中" :
                        (ks[i] == SCREEN_SETTLE)  ? L"结算屏" : L"历史屏");
        }
        appSetScreen(&a, SCREEN_PLAY);
    }

    /* ---- 参数面板只在游戏中/暂停中开 ---- */
    {
        appSetScreen(&a, SCREEN_SETTLE);
        memset(&a.input, 0, sizeof(a.input));
        a.input.pressed[VK_TAB] = 1;
        appHandleInput(&a);
        CHECK(!a.paramOpen, L"结算屏的 Tab 不开参数面板");
        appSetScreen(&a, SCREEN_PLAY);
        memset(&a.input, 0, sizeof(a.input));
        a.input.pressed[VK_TAB] = 1;
        appHandleInput(&a);
        CHECK(a.paramOpen, L"游戏中的 Tab 打开参数面板");
        appParamClose(&a);
    }
}

/* ============================================================ 声音开关
 *
 * 音量相关的设置只保留开关：选"开"时播放一次音效。
 * 这一组验两件事：开关本身能拨动，以及——这是关键——拨到"开"的那一刻
 * 音频层的静音状态**当场**就翻了。
 *
 * 为什么盯这个：混音层是按帧读 muted 的，如果"拨开关"这一步只改了参数、
 * 要等主循环下一帧才把 muted 同步过去，那么调用方紧接着播放的"叮"一声
 * 会在同一帧里被自己刚恢复的静音吃掉 —— 现象就是"打开声音反而没声音"，
 * 而且只在第一次拨的时候出现，最难查。所以 appParamAfterChange 里当场同步，
 * 这里的断言就是钉住这一点。
 */
static void tSoundToggle(void) {
    static App a;
    Params before;
    int idx;

    group(L"声音开关");

    idx = -1;
    {
        int i;
        for (i = 0; i < g_paramDescCount; ++i)
            if (g_paramDescs[i].group == PARAM_GROUP_AUDIO) { idx = i; break; }
    }
    CHECK(idx >= 0, L"声音组里有至少一项（否则下面没得验）");
    if (idx < 0) return;

    CHECK(g_paramDescs[idx].kind == PK_BOOL,
          L"声音这一项是开关（只有开/关两种取值）");

    /* ---- 关 → 开：参数到位，且音频层当场不静音 ---- */
    memset(&a, 0, sizeof(a));
    a.winW = 1600; a.winH = 900;
    paramsDefault(&a.params);
    /* 不打开音频设备：这一组验的是"参数与 muted 标志同不同步"，不是
       声卡能不能响。memset 之后的 Audio 就是一个没开成的设备对象，
       正是无桌面环境下该有的样子。*/
    CHECK(a.params.soundOn == 1, L"出厂默认是开着声音的");

    before = a.params;
    a.params.soundOn = 0;
    appParamAfterChange(&a, idx, &before);
    CHECK(a.params.soundOn == 0, L"拨到关之后参数是关");
    CHECK(a.audio.muted == 1, L"拨到关之后音频层当场静音");

    /* ★ 先把音频层**手动按回静音**再拨开关。不这么做的话，上面那一步之后
       muted 本来就是 0，"拨到开解除静音"这条会因为它本来就是 0 而蒙混过关 ——
       试过故意破坏：把 appParamAfterChange 里那行同步代码整行删掉，这条
       照样是绿的。手动置 1 之后，只有当场同步这一条路能让它变回 0。*/
    audioSetMuted(&a.audio, 1);
    CHECK(a.audio.muted == 1, L"（前置）音频层被手动按回静音");

    before = a.params;
    a.params.soundOn = 1;
    appParamAfterChange(&a, idx, &before);
    CHECK(a.params.soundOn == 1, L"拨回开之后参数是开");
    CHECK(a.audio.muted == 0, L"拨到开之后音频层当场解除静音（不必等下一帧）");

    /* 反过来也一样：先把音频层解除静音，再拨到关，必须当场静音。*/
    audioSetMuted(&a.audio, 0);
    before = a.params;
    a.params.soundOn = 0;
    appParamAfterChange(&a, idx, &before);
    CHECK(a.audio.muted == 1, L"拨到关之后当场静音（反向也成立）");

    /* ---- 取值被夹到 0/1，喂 2 或 -1 进去也不该留下第三种值 ---- */
    {
        Params p;
        paramsDefault(&p);
        p.soundOn = 5;
        paramsClamp(&p);
        CHECK(p.soundOn == 1, L"越界的开关值被夹回 1");
        /* 负值按"非零即为真"归一 —— 存档里读到 -3 只可能是文件被人动过，
           这时把它当"关"还是当"开"都说得通，但必须**确定**归一到一个
           合法值上，不能原样留着让以后所有 `if (soundOn)` 各自解释一遍。*/
        p.soundOn = -3;
        paramsClamp(&p);
        CHECK(p.soundOn == 1, L"负的开关值也被归一（非零即为真）");
        p.soundOn = 0;
        paramsClamp(&p);
        CHECK(p.soundOn == 0, L"0 保持为 0");
    }

    /* ---- 关掉声音不该动别的参数 ---- */
    {
        Params p, q;
        int i, diff = 0;
        paramsDefault(&p);
        paramsApplyPreset(&p, 3);
        q = p;
        q.soundOn = q.soundOn ? 0 : 1;      /* 只拨开关 */
        paramsClamp(&q);
        for (i = 0; i < g_paramDescCount; ++i) {
            if (g_paramDescs[i].group == PARAM_GROUP_AUDIO) continue;
            if (paramDescGet(&p, &g_paramDescs[i]) !=
                paramDescGet(&q, &g_paramDescs[i])) ++diff;
        }
        CHECK(diff == 0, L"拨声音开关不会连带改动任何别的参数");
    }
}

/* ============================================================ 存档兼容：预设换位
 *
 * 「跟踪训练」从第 8 位挪到了第 4 位（放在小快靶和计时挑战
 * 中间）。搬的是**列表里的位置**，可预设下标是**写进存档**的：
 *     参数.dat → Params.preset
 *     记录.dat → ScoreBook.rec[] 的第几档
 *     历史.dat → HistoryEntry.preset
 * 三份旧档因此都带着"换位前"的下标。这一组盯三件事：
 *   ① 换算表本身是个双射，且与"跟踪训练插到第 4 位"逐位吻合；
 *   ② v4 的 参数.dat（换位前写的）读回来，preset 换成今天的下标，别的值一个不动；
 *   ③ v1 的 历史.dat 每条同样换算，其余字段一个不动。
 *
 * 两份旧档一律**手写字节**造出来，不经过各自的保存函数 —— 保存函数写的是
 * 新格式，拿它造"旧档"是造不出来的（这里栽过一次跟头）。
 * 那张表本身写死在断言里，不调被测函数来"自己验自己"。
 * 记录.dat 的 RBS1 那份在 tTrackSpawn 里已经测过（它是跨七档的搬移）。
 */
static void tPresetIndexMigration(void) {
    static const int kTo[PRESET_COUNT] = { 0, 1, 2, 4, 5, 6, 7, 3 };
    int i;

    group(L"存档兼容：预设换位");

    /* ---------------------------------------------------- ① 换算表 */
    {
        int seen[PRESET_COUNT] = { 0 };
        int same = 1, bijective = 1;
        for (i = 0; i < PRESET_COUNT; ++i) {
            int got = presetIndexMigrateLegacyOrder(i);
            if (got != kTo[i]) same = 0;
            if (got < 0 || got >= PRESET_COUNT) { bijective = 0; continue; }
            if (seen[got]) bijective = 0;       /* 两套预设被搬进同一格 */
            seen[got] = 1;
        }
        CHECK(same, L"★ 老→新换算逐位吻合：0/1/2 不动，老 3→4、4→5、5→6、6→7、7→3");
        CHECK(bijective, L"★ 它是个双射（八套一对一，没有两套记录会挤进同一格）");
        /* 越界不猜、不夹取：宁可读出一个对不上的名字，也不擅自搬去一个
           "差不多"的档 —— 这条路上本来就到不了，真到了说明档已经坏了。*/
        CHECK(presetIndexMigrateLegacyOrder(-1) == -1 &&
              presetIndexMigrateLegacyOrder(PRESET_COUNT) == PRESET_COUNT,
              L"越界的下标原样返回（不猜、不夹取）");
    }

    /* ---------------------------------------------------- ② v4 的参数档 */
    {
        wchar_t dir[MAX_PATH], pathOld[MAX_PATH], pathNew[MAX_PATH];
        unsigned char buf[16 + sizeof(Params)];
        /* 魔数与版本号**写死**：它们是磁盘格式的一部分，写成 sizeof 那种跟着
           代码走的算式，等于让"旧档"随时悄悄变成新档，测的东西就变味了。*/
        const uint32_t magic = 0x31535242u;   /* "BRS1"：SETTINGS_VERSION 4 那份 */
        uint32_t version = 4u, size = (uint32_t)sizeof(Params), crc;
        Params src, dst;
        FILE *fp;

        dir[0] = L'\0'; pathOld[0] = L'\0'; pathNew[0] = L'\0';
        if (GetTempPathW((DWORD)ARRAY_COUNT(dir), dir) > 0) {
            _snwprintf(pathOld, ARRAY_COUNT(pathOld) - 1, L"%lsbr_v15_old_params.dat", dir);
            _snwprintf(pathNew, ARRAY_COUNT(pathNew) - 1, L"%lsbr_v15_new_params.dat", dir);
        }
        if (!pathOld[0]) { CHECK(0, L"取到临时目录（拿不到就没法测参数档兼容）"); return; }
        _wremove(pathOld);
        _wremove(pathNew);

        paramsDefault(&src);
        src.preset = 7;            /* 换位前第 8 位，正是「跟踪训练」 */
        src.balloonCount = 3;      /* 一个非默认值：验"值本身不换算" */
        paramsClamp(&src);

        memcpy(buf + 0, &magic, 4);
        memcpy(buf + 4, &version, 4);
        memcpy(buf + 8, &size, 4);
        memcpy(buf + 16, &src, sizeof(Params));      /* 先摆 payload */
        crc = crc32Buf(buf + 16, sizeof(Params));    /* CRC 按文件里写的长度算 */
        memcpy(buf + 12, &crc, 4);

        fp = _wfopen(pathOld, L"wb");
        CHECK(fp != NULL, L"（前置）能写出人造的 v4 参数档");
        if (fp) { fwrite(buf, 1, 16 + sizeof(Params), fp); fclose(fp); }

        memset(&dst, 0, sizeof(dst));
        CHECK(paramsLoad(&dst, pathOld) == 0, L"★ v4（换位前）的参数档照旧读得进来");
        CHECK(dst.preset == 3, L"★ 旧档里 preset = 7（当时的「跟踪训练」）读回来是 3");
        CHECK(dst.balloonCount == 3, L"★ 参数值本身一个都不换算（只有预设下标换了口径）");

        /* 写死的那个魔数不能与代码脱节：拿保存函数真写一份，比对头四个字节。
           对不上就说明魔数换过了 —— 那时"人造旧档"其实是照着一个不存在的格式
           在造，本条会红着提醒。*/
        {
            unsigned char hdr[4];
            size_t got = 0;
            CHECK(paramsSave(&src, pathNew) == 0, L"（对照）新档能写出来");
            fp = _wfopen(pathNew, L"rb");
            if (fp) { got = fread(hdr, 1, 4, fp); fclose(fp); }
            CHECK(got == 4 && memcmp(hdr, &magic, 4) == 0,
                  L"（对照）写死的魔数就是 paramsSave 真正写出去的那个");

            /* 反过来也要钉住：现行档（v5）里 preset = 7 不做任何换算。*/
            memset(&dst, 0, sizeof(dst));
            CHECK(paramsLoad(&dst, pathNew) == 0 && dst.preset == 7 && dst.balloonCount == 3,
                  L"★ v5（现行档）里的 preset = 7 原样读回，不做换算");
        }
        _wremove(pathOld);
        _wremove(pathNew);
    }

    /* ---------------------------------------------------- ③ v1 的历史档 */
    {
        wchar_t dir[MAX_PATH], pathOld[MAX_PATH];
        unsigned char buf[16 + sizeof(RunHistory)];
        const uint32_t magic = 0x31524842u;   /* "BHR1" */
        uint32_t version = 1u, size = (uint32_t)sizeof(HistoryEntry), crc;
        RunHistory src, dst;
        FILE *fp;

        dir[0] = L'\0'; pathOld[0] = L'\0';
        if (GetTempPathW((DWORD)ARRAY_COUNT(dir), dir) > 0)
            _snwprintf(pathOld, ARRAY_COUNT(pathOld) - 1, L"%lsbr_v15_old_hist.dat", dir);
        if (!pathOld[0]) { CHECK(0, L"取到临时目录（拿不到就没法测历史档兼容）"); return; }
        _wremove(pathOld);

        historyInit(&src);
        src.count = 2;
        src.e[0].preset = 3;       /* 换位前第 4 位：「计时挑战」 */
        src.e[0].score  = 111;
        src.e[0].popped = 7;
        src.e[0].mode   = MODE_TIME;
        src.e[1].preset = 7;       /* 换位前第 8 位：「跟踪训练」 */
        src.e[1].score  = 222;
        src.e[1].mode   = MODE_RANGE;

        memcpy(buf + 0, &magic, 4);
        memcpy(buf + 4, &version, 4);
        memcpy(buf + 8, &size, 4);
        memcpy(buf + 16, &src, sizeof(RunHistory));
        crc = crc32Buf(buf + 16, sizeof(RunHistory));
        memcpy(buf + 12, &crc, 4);

        fp = _wfopen(pathOld, L"wb");
        CHECK(fp != NULL, L"（前置）能写出人造的 v1 历史档");
        if (fp) { fwrite(buf, 1, 16 + sizeof(RunHistory), fp); fclose(fp); }

        memset(&dst, 0, sizeof(dst));
        CHECK(historyLoad(&dst, pathOld) == 0, L"★ v1（换位前）的历史档照旧读得进来");
        CHECK(dst.count == 2 && dst.e[0].preset == 4 && dst.e[1].preset == 3,
              L"★ 每条的 preset 逐条换算：老 3（计时挑战）→4，老 7（跟踪训练）→3");
        CHECK(dst.e[0].score == 111 && dst.e[1].score == 222 && dst.e[0].popped == 7 &&
              dst.e[0].mode == MODE_TIME && dst.e[1].mode == MODE_RANGE,
              L"★ 分数、击破数、模式这些与预设顺序无关的字段一个都没动");

        /* 反过来也要钉住：现行档（v2）里各条的 preset 原样读回、不做换算。
           v1 与 v2 的结构体一个字节都不差，差别只在"这个下标是哪一代的口径"——
           判据只认头里的版本号，写错一个比较符就会把新档案再搬一次。*/
        {
            wchar_t pathNew[MAX_PATH];
            pathNew[0] = L'\0';
            _snwprintf(pathNew, ARRAY_COUNT(pathNew) - 1, L"%lsbr_v15_new_hist.dat", dir);
            _wremove(pathNew);
            CHECK(historySave(&src, pathNew) == 0, L"（对照）新历史档能写出来");
            memset(&dst, 0, sizeof(dst));
            CHECK(historyLoad(&dst, pathNew) == 0 && dst.count == 2 &&
                  dst.e[0].preset == 3 && dst.e[1].preset == 7,
                  L"★ v2（现行档）里各条的 preset 原样读回，不做换算");
            _wremove(pathNew);
        }
        _wremove(pathOld);
    }
}

/* ============================================================ 入口 */

int selftestRun(const wchar_t *reportPath) {
    g_pass = 0;
    g_fail = 0;
    g_groupFail = 0;

    if (reportPath) {
        g_rep = _wfopen(reportPath, L"wb");
        if (g_rep) {
            repA("BalloonRange selftest report / FPS 气球训练场 自检报告\n");
            repA("====================================================\n");
            repA("每一行 ok 表示该条断言通过；FAIL 表示失败。\n");
        }
    }

    tScalar();
    tMatrix();
    tRaySphere();
    tRng();
    tColor();
    tPng();
    tConfig();
    tPresets();
    tPanelScroll();
    tPanelScrollStable();
    tPanelArrowEnds();
    tArrowPinned();
    tPanelMenu();
    tParamCycle();
    tPanelSections();
    tParamAutoSave();
    tPanelNavWASD();
    tPanelRestorePreferences();
    tPanelWording();
    tParamReset();
    tSoundToggle();
    tKeyRepeat();
    tKeyMeanings();
    tHistory();
    tScore();
    tAudio();
    tParamDesc();
    tScene();
    tBalloon();
    tTrackSpawn();
    tPlayer();
    tHud();
    tPresetIndexMigration();

    if (g_rep) {
        if (g_groupFail > 0) repW(L"  ← 本组有失败\n");
        repFmt(L"\n----------------------------------------\n通过 %d 项，失败 %d 项\n",
               g_pass, g_fail);
        if (g_fail == 0) repW(L"结论：全部通过。\n");
        else             repW(L"结论：存在失败项，详见上面的 FAIL 行。\n");
        fclose(g_rep);
        g_rep = NULL;
    }

    fflush(stdout);
    printf("[selftest] pass=%d fail=%d\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail;
}
