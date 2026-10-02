/* ============================================================================
 * core.cpp —— 基础层实现。全是纯函数，便于自检逐条怼。
 * ==========================================================================*/
#include "core.h"
#include <stdio.h>
#include <stdarg.h>

/* ============================================================ 标量小工具 */

float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

float lerpf(float a, float b, float t) { return a + (b - a) * t; }
float minf(float a, float b) { return a < b ? a : b; }
float maxf(float a, float b) { return a > b ? a : b; }

float easeOutQuad(float t) {
    t = clampf(t, 0.0f, 1.0f);
    return 1.0f - (1.0f - t) * (1.0f - t);
}

float easeOutCubic(float t) {
    t = clampf(t, 0.0f, 1.0f);
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

float easeInQuad(float t) {
    t = clampf(t, 0.0f, 1.0f);
    return t * t;
}

float smoothstep01(float t) {
    t = clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/* ================================================================ 向量 */

Vec3 v3(float x, float y, float z) { Vec3 v; v.x = x; v.y = y; v.z = z; return v; }
Vec3 v3zero(void) { return v3(0.0f, 0.0f, 0.0f); }
Vec3 v3add(Vec3 a, Vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
Vec3 v3sub(Vec3 a, Vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
Vec3 v3mul(Vec3 a, Vec3 b) { return v3(a.x * b.x, a.y * b.y, a.z * b.z); }
Vec3 v3scale(Vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
Vec3 v3neg(Vec3 a) { return v3(-a.x, -a.y, -a.z); }

float v3dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vec3 v3cross(Vec3 a, Vec3 b) {
    return v3(a.y * b.z - a.z * b.y,
              a.z * b.x - a.x * b.z,
              a.x * b.y - a.y * b.x);
}

float v3lenSq(Vec3 a) { return a.x * a.x + a.y * a.y + a.z * a.z; }
float v3len(Vec3 a) { return sqrtf(v3lenSq(a)); }
float v3dist(Vec3 a, Vec3 b) { return v3len(v3sub(a, b)); }

Vec3 v3norm(Vec3 a) {
    float l = v3len(a);
    /* 零向量不做除零：返回零向量，让调用方自己判断。*/
    if (l < 1e-8f) return v3zero();
    return v3scale(a, 1.0f / l);
}

Vec3 v3lerp(Vec3 a, Vec3 b, float t) {
    return v3(lerpf(a.x, b.x, t), lerpf(a.y, b.y, t), lerpf(a.z, b.z, t));
}

/* ============================================================ 4x4 矩阵 */

Mat4 mat4Identity(void) {
    Mat4 r;
    int i;
    for (i = 0; i < 16; ++i) r.m[i] = 0.0f;
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

Mat4 mat4Mul(Mat4 a, Mat4 b) {
    Mat4 r;
    int c, row, k;
    for (c = 0; c < 4; ++c) {
        for (row = 0; row < 4; ++row) {
            float s = 0.0f;
            for (k = 0; k < 4; ++k)
                s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    }
    return r;
}

Mat4 mat4Perspective(float fovyDeg, float aspect, float znear, float zfar) {
    Mat4 r = mat4Identity();
    float f, nf;
    if (aspect < 1e-4f) aspect = 1e-4f;
    if (znear < 1e-3f) znear = 1e-3f;
    if (zfar <= znear + 1e-3f) zfar = znear + 1.0f;
    f = 1.0f / tanf(fovyDeg * DEG2RAD_F * 0.5f);
    nf = 1.0f / (znear - zfar);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zfar + znear) * nf;
    r.m[11] = -1.0f;
    r.m[14] = 2.0f * zfar * znear * nf;
    r.m[15] = 0.0f;
    return r;
}

Mat4 mat4LookAt(Vec3 eye, Vec3 target, Vec3 up) {
    Vec3 f = v3norm(v3sub(target, eye));
    Vec3 s = v3cross(f, up);
    Vec3 u;
    Mat4 r = mat4Identity();
    if (v3lenSq(s) < 1e-8f) {
        /* 视线与 up 平行：换一根参考轴，避免退化出零矩阵。*/
        s = v3cross(f, v3(0.0f, 0.0f, 1.0f));
        if (v3lenSq(s) < 1e-8f) s = v3(1.0f, 0.0f, 0.0f);
    }
    s = v3norm(s);
    u = v3cross(s, f);
    r.m[0] = s.x; r.m[4] = s.y; r.m[8]  = s.z; r.m[12] = -v3dot(s, eye);
    r.m[1] = u.x; r.m[5] = u.y; r.m[9]  = u.z; r.m[13] = -v3dot(u, eye);
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z; r.m[14] = v3dot(f, eye);
    r.m[3] = 0.0f; r.m[7] = 0.0f; r.m[11] = 0.0f; r.m[15] = 1.0f;
    return r;
}

Mat4 mat4Translate(Vec3 t) {
    Mat4 r = mat4Identity();
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
}

Mat4 mat4Scale(Vec3 s) {
    Mat4 r = mat4Identity();
    r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
    return r;
}

Mat4 mat4RotateX(float rad) {
    Mat4 r = mat4Identity();
    float c = cosf(rad), s = sinf(rad);
    r.m[5] = c; r.m[9] = -s;
    r.m[6] = s; r.m[10] = c;
    return r;
}

Mat4 mat4RotateY(float rad) {
    Mat4 r = mat4Identity();
    float c = cosf(rad), s = sinf(rad);
    r.m[0] = c;  r.m[8]  = s;
    r.m[2] = -s; r.m[10] = c;
    return r;
}

Mat4 mat4RotateZ(float rad) {
    Mat4 r = mat4Identity();
    float c = cosf(rad), s = sinf(rad);
    r.m[0] = c; r.m[4] = -s;
    r.m[1] = s; r.m[5] = c;
    return r;
}

Mat4 mat4Basis(Vec3 right, Vec3 up, Vec3 fwd) {
    Mat4 r = mat4Identity();
    r.m[0] = right.x; r.m[4] = up.x; r.m[8]  = fwd.x;
    r.m[1] = right.y; r.m[5] = up.y; r.m[9]  = fwd.y;
    r.m[2] = right.z; r.m[6] = up.z; r.m[10] = fwd.z;
    return r;
}

Vec3 mat4XformPoint(Mat4 m, Vec3 p) {
    Vec3 r;
    r.x = m.m[0] * p.x + m.m[4] * p.y + m.m[8]  * p.z + m.m[12];
    r.y = m.m[1] * p.x + m.m[5] * p.y + m.m[9]  * p.z + m.m[13];
    r.z = m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14];
    return r;
}

Vec3 mat4XformDir(Mat4 m, Vec3 d) {
    Vec3 r;
    r.x = m.m[0] * d.x + m.m[4] * d.y + m.m[8]  * d.z;
    r.y = m.m[1] * d.x + m.m[5] * d.y + m.m[9]  * d.z;
    r.z = m.m[2] * d.x + m.m[6] * d.y + m.m[10] * d.z;
    return r;
}

/* ---------------------------------------------------------- 射线与球求交 */

int raySphere(Vec3 origin, Vec3 dir, Vec3 center, float radius, float *outT) {
    Vec3 oc = v3sub(origin, center);
    float b = v3dot(oc, dir);
    float c = v3lenSq(oc) - radius * radius;
    float disc = b * b - c;      /* dir 已归一化，所以是 b²-c 而不是 b²-4ac */

    /* 起点已经在球内：最近的交点在"身后"，但目标就在眼前。
       这时返回 t = 0 —— 玩家贴着气球开枪当然该算命中。*/
    if (c <= 0.0f) {
        if (outT) *outT = 0.0f;
        return 1;
    }
    /* >= 而不是 >：擦着圆周也算命中（刻意留的手感宽容度，见 core.h 的说明）。*/
    if (disc < 0.0f) return 0;

    {
        float sq = sqrtf(disc);
        float t0 = -b - sq;
        float t1 = -b + sq;
        if (t1 < 0.0f) return 0;          /* 整根射线都在球后方 */
        if (outT) *outT = (t0 >= 0.0f) ? t0 : t1;
        return 1;
    }
}

int spheresOverlap2D(float x1, float y1, float r1,
                     float x2, float y2, float r2, float gap) {
    float dx = x2 - x1, dy = y2 - y1;
    float d2 = dx * dx + dy * dy;
    float need = (r1 + r2) * gap;
    return d2 < need * need;
}

/* ============================================================== 随机数 */

void rngSeed(Rng *r, uint32_t seed) {
    /* 种子为 0 时 xorshift 会退化成恒零，这里兜一个非零值。*/
    r->s = seed ? seed : 0x9E3779B9u;
}

uint32_t rngNext(Rng *r) {
    uint32_t x = r->s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->s = x ? x : 0x9E3779B9u;
    return r->s;
}

float rngFloat(Rng *r) {
    /* 取高 24 位凑成 [0,1)：低位在 xorshift 里质量差一些。*/
    return (float)(rngNext(r) >> 8) * (1.0f / 16777216.0f);
}

float rngRange(Rng *r, float lo, float hi) { return lo + (hi - lo) * rngFloat(r); }

int rngInt(Rng *r, int lo, int hi) {
    int span;
    if (hi <= lo) return lo;
    span = hi - lo + 1;
    return lo + (int)(rngNext(r) % (uint32_t)span);
}

int rngChance(Rng *r, float p) { return rngFloat(r) < p ? 1 : 0; }

/* ================================================================ 颜色 */

Color3 color3(float r, float g, float b) { Color3 c; c.r = r; c.g = g; c.b = b; return c; }
Color3 colorFromRGB8(int r, int g, int b) {
    return color3(r / 255.0f, g / 255.0f, b / 255.0f);
}

Color3 hsvToRgb(float h, float s, float v) {
    float c, x, m, rr = 0.0f, gg = 0.0f, bb = 0.0f;
    int seg;
    h = fmodf(h, 360.0f);
    if (h < 0.0f) h += 360.0f;
    s = clampf(s, 0.0f, 1.0f);
    v = clampf(v, 0.0f, 1.0f);

    c = v * s;
    seg = (int)(h / 60.0f);
    x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    switch (seg) {
        case 0: rr = c; gg = x; bb = 0.0f; break;
        case 1: rr = x; gg = c; bb = 0.0f; break;
        case 2: rr = 0.0f; gg = c; bb = x; break;
        case 3: rr = 0.0f; gg = x; bb = c; break;
        case 4: rr = x; gg = 0.0f; bb = c; break;
        default: rr = c; gg = 0.0f; bb = x; break;
    }
    m = v - c;
    return color3(rr + m, gg + m, bb + m);
}

Color3 colorScale(Color3 col, float k) {
    if (k >= 0.0f) return colorLerp(col, color3(1.0f, 1.0f, 1.0f), clampf(k, 0.0f, 1.0f));
    return colorLerp(col, color3(0.0f, 0.0f, 0.0f), clampf(-k, 0.0f, 1.0f));
}

Color3 colorLerp(Color3 a, Color3 b, float t) {
    return color3(lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t));
}

Color4 color4(Color3 c, float a) {
    Color4 r; r.r = c.r; r.g = c.g; r.b = c.b; r.a = a; return r;
}

/* ============================================================ 校验和 */

uint32_t crc32Buf(const void *data, size_t len) {
    static uint32_t table[256];
    static int built = 0;
    const unsigned char *p = (const unsigned char *)data;
    uint32_t c;
    size_t i;

    if (!built) {
        uint32_t n, k;
        for (n = 0; n < 256; ++n) {
            c = n;
            for (k = 0; k < 8; ++k)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        built = 1;
    }

    c = 0xFFFFFFFFu;
    for (i = 0; i < len; ++i)
        c = table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/* ================================================================ 计时 */

double nowSeconds(void) {
    static LARGE_INTEGER freq;
    static int inited = 0;
    LARGE_INTEGER c;
    if (!inited) {
        QueryPerformanceFrequency(&freq);
        if (freq.QuadPart == 0) freq.QuadPart = 1;
        inited = 1;
    }
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)freq.QuadPart;
}

void sleepMs(int ms) {
    if (ms > 0) Sleep((DWORD)ms);
}

/* ============================================================ 控制台输出 */

void consoleUseUtf8(void) {
    static int done = 0;
    if (done) return;
    done = 1;
    /* 只在真的有控制台时才切。没有控制台时这两个调用是无害的空操作，
       但把 done 置上可以省掉后面的重复系统调用。*/
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

void printUtf8(const wchar_t *s) {
    char buf[2048];
    int n;
    if (!s) return;
    n = WideCharToMultiByte(CP_UTF8, 0, s, -1, buf, (int)sizeof(buf), NULL, NULL);
    if (n > 1) fwrite(buf, 1, (size_t)(n - 1), stdout);
}

void printUtf8f(const wchar_t *fmt, ...) {
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, ARRAY_COUNT(buf) - 1, fmt, ap);
    va_end(ap);
    buf[ARRAY_COUNT(buf) - 1] = L'\0';
    printUtf8(buf);
}

void formatSeconds(double sec, wchar_t *out, int outCount) {
    int totalSec, tenths;
    if (!out || outCount <= 0) return;
    if (sec < 0.0) sec = 0.0;
    totalSec = (int)sec;
    tenths = (int)((sec - totalSec) * 10.0 + 0.5);
    if (tenths > 9) { tenths = 0; totalSec += 1; }
    if (totalSec >= 60)
        _snwprintf(out, (size_t)outCount, L"%d:%02d.%d",
                   totalSec / 60, totalSec % 60, tenths);
    else
        _snwprintf(out, (size_t)outCount, L"%d.%ds", totalSec, tenths);
    out[outCount - 1] = L'\0';
}
