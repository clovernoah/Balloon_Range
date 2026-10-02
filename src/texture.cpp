/* ============================================================================
 * texture.cpp —— 程序生成纹理
 *
 * 噪声用最朴素的值噪声（value noise）+ 分形叠加（fbm）。够用、够快、
 * 完全可以复现（同一个 seed 出同一张图）。
 * ==========================================================================*/
#include "texture.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================ 噪声 */

static unsigned hashU(unsigned x) {
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

/* 整数格点上的伪随机值 [0,1)。*/
static float hashF(int x, int y, unsigned seed) {
    unsigned h = hashU((unsigned)(x * 374761393) ^
                       (unsigned)(y * 668265263) ^
                       (seed * 1274126177u));
    return (float)(h >> 8) * (1.0f / 16777216.0f);
}

/* 取模到 [0, period)。period <= 0 表示不取模。*/
static int wrapi(int v, int period) {
    if (period <= 0) return v;
    v %= period;
    if (v < 0) v += period;
    return v;
}

/* 二维值噪声：格点值之间做平滑（smoothstep）插值。
 *
 * period > 0 时把四个格点的下标按 period 取模 —— 噪声在 [0, period) 上
 * **首尾相接**，贴图平铺时才不会出现接缝。
 *
 * 早先的噪声不周期，墙面横向平铺 2.75 次、纵向 1.25 次，于是墙上出现了
 * 三道竖接缝和一道横接缝（03_stress32.png 里一眼就能看到）。接缝不是
 * "参数没调好"，是数学上就接不上，只能这么修。*/
static float vnoiseP(float x, float y, int period, unsigned seed) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float xf = x - (float)xi, yf = y - (float)yi;
    float u = xf * xf * (3.0f - 2.0f * xf);
    float v = yf * yf * (3.0f - 2.0f * yf);
    int x0 = wrapi(xi, period), x1 = wrapi(xi + 1, period);
    int y0 = wrapi(yi, period), y1 = wrapi(yi + 1, period);
    float a = hashF(x0, y0, seed);
    float b = hashF(x1, y0, seed);
    float c = hashF(x0, y1, seed);
    float d = hashF(x1, y1, seed);
    return lerpf(lerpf(a, b, u), lerpf(c, d, u), v);
}

/* 分形叠加。频率每层**翻倍**而不是乘 2.03 —— 只有整数倍频才能让各层的
   周期同时整除，层层都是无缝的。原本乘 2.03 是为了避开整数倍带来的格子感，
   但和满墙的接缝比起来，那点格子感完全可以接受。振幅每层减半。*/
static float fbmP(float x, float y, unsigned seed, int octaves, int period) {
    float sum = 0.0f, amp = 0.5f;
    int f = 1, i;
    for (i = 0; i < octaves; ++i) {
        sum += amp * vnoiseP(x * (float)f, y * (float)f,
                             period > 0 ? period * f : 0,
                             seed + (unsigned)i * 131u);
        f *= 2;
        amp *= 0.5f;
    }
    return sum;
}

/* 不周期版本，给天花板这种只看一小块、不需要平铺的地方用。*/
static float vnoise(float x, float y, unsigned seed) {
    return vnoiseP(x, y, 0, seed);
}

/* ============================================================ GL 纹理 */

void texDestroy(Texture *t) {
    if (!t) return;
    if (t->id) glDeleteTextures(1, &t->id);
    t->id = 0;
    t->ok = 0;
}

int texFromRGB(Texture *t, int w, int h, const unsigned char *rgb) {
    if (!t || w <= 0 || h <= 0 || !rgb) return -1;
    memset(t, 0, sizeof(*t));
    glGenTextures(1, &t->id);
    if (!t->id) return -1;
    glBindTexture(GL_TEXTURE_2D, t->id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGB, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
    t->w = w; t->h = h; t->ok = 1;
    return 0;
}

/* RGBA 版本，给光斑这类需要 alpha 的图用。线性过滤、夹边。
   对外可见：HUD 的中文字形图集也是从内存位图提上来的，走的就是这条路。*/
int texFromRGBA(Texture *t, int w, int h, const unsigned char *rgba) {
    if (!t || w <= 0 || h <= 0 || !rgba) return -1;
    memset(t, 0, sizeof(*t));
    glGenTextures(1, &t->id);
    if (!t->id) return -1;
    glBindTexture(GL_TEXTURE_2D, t->id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGBA, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    t->w = w; t->h = h; t->ok = 1;
    return 0;
}

int texMakeWhite(Texture *t) {
    unsigned char px[3] = { 255, 255, 255 };
    return texFromRGB(t, 1, 1, px);
}

int texMakeGlow(Texture *t, int size) {
    unsigned char *buf;
    int x, y;
    float half = (float)size * 0.5f;
    if (size < 4) size = 64;
    buf = (unsigned char *)malloc((size_t)size * size * 4);
    if (!buf) return -1;

    for (y = 0; y < size; ++y) {
        for (x = 0; x < size; ++x) {
            float dx = ((float)x + 0.5f - half) / half;
            float dy = ((float)y + 0.5f - half) / half;
            float d = sqrtf(dx * dx + dy * dy);
            float a = 1.0f - clampf(d, 0.0f, 1.0f);
            /* 平方一下让边缘收得更利落，中心保持实心。*/
            a = a * a * a;
            buf[(y * size + x) * 4 + 0] = 255;
            buf[(y * size + x) * 4 + 1] = 255;
            buf[(y * size + x) * 4 + 2] = 255;
            buf[(y * size + x) * 4 + 3] = (unsigned char)(clampf(a, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    }
    {
        int rc = texFromRGBA(t, size, size, buf);
        free(buf);
        return rc;
    }
}

int texMakeSpec(Texture *t, int size) {
    unsigned char *buf;
    int x, y;
    float half;
    if (size < 8) size = 64;
    half = (float)size * 0.5f;
    buf = (unsigned char *)malloc((size_t)size * size * 4);
    if (!buf) return -1;

    for (y = 0; y < size; ++y) {
        for (x = 0; x < size; ++x) {
            float dx = ((float)x + 0.5f - half) / half;
            float dy = ((float)y + 0.5f - half) / half;
            float d2 = dx * dx + dy * dy;
            /* 中心实白，到半径 1 处平滑归零；用 smoothstep 收边。*/
            float a = 1.0f - smoothstep01(clampf((sqrtf(d2) - 0.25f) / 0.75f, 0.0f, 1.0f));
            buf[(y * size + x) * 4 + 0] = 255;
            buf[(y * size + x) * 4 + 1] = 255;
            buf[(y * size + x) * 4 + 2] = 255;
            buf[(y * size + x) * 4 + 3] = (unsigned char)(clampf(a, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    }
    {
        int rc = texFromRGBA(t, size, size, buf);
        free(buf);
        return rc;
    }
}

/* ============================================================ 墙面 */

/* 单像素生成：返回 RGB。x,y 是像素坐标，(u,v) 是归一化坐标。*/
typedef struct { float r, g, b; } RGBf;

static RGBf pxPlaster(int x, int y, int size, unsigned seed) {
    float u = (float)x / (float)size, v = (float)y / (float)size;
    /* 三层的周期都是整数，乘 2 之后仍然整除，所以整张图无缝平铺。*/
    float grain = fbmP(u * 96.0f, v * 96.0f, seed + 13u, 2, 96);   /* 细颗粒 */
    float fine  = fbmP(u * 22.0f, v * 22.0f, seed,        3, 22);   /* 抹刀痕 */
    float patch = fbmP(u * 3.0f,  v * 3.0f,  seed + 77u,  2, 3);    /* 大块色差 */
    /* 抹灰墙：中灰底。早先把大块色差的权重给到 0.16，出来像大理石台面；
       现在把权重压下去、补一层高频颗粒，读起来才是"刷过漆的墙"。
       底色 0.52：配 1.19 的光照算下来墙面落在 0.62 左右的中灰，
       彩色气球放上去才有对比（试过 0.645，整面墙发白，画面全糊在一起）。*/
    float base = 0.520f + (fine  - 0.47f) * 0.070f
                         + (patch - 0.47f) * 0.060f
                         + (grain - 0.50f) * 0.035f;
    RGBf c;
    c.r = base * 1.012f;
    c.g = base * 1.000f;
    c.b = base * 0.972f;
    return c;
}

static RGBf pxBrick(int x, int y, int size, unsigned seed) {
    /* 砖：64x32 一格，缝 5 像素；隔行错半砖。*/
    const float bw = 64.0f, bh = 32.0f, mortar = 5.0f;
    float row = floorf((float)y / bh);
    float ox = (fmodf(row, 2.0f) > 0.5f) ? bw * 0.5f : 0.0f;
    float lx = fmodf((float)x + ox, bw);
    float ly = fmodf((float)y, bh);
    float nx = (float)x / (float)size, ny = (float)y / (float)size;
    RGBf c;

    /* 512/64 = 8 列、512/32 = 16 行，都是整数，砖阵本身就平铺得开。*/
    if (lx < mortar || ly < mortar) {
        /* 灰缝 */
        float n = fbmP(nx * 180.0f, ny * 180.0f, seed + 11u, 3, 180);
        float g = 0.46f + (n - 0.5f) * 0.12f;
        c.r = g; c.g = g * 0.99f; c.b = g * 0.95f;
        return c;
    }
    {
        int bx = (int)floorf(((float)x + ox) / bw);
        int by = (int)floorf((float)y / bh);
        float tint = hashF(bx, by, seed);          /* 每块砖自己的色差 */
        float n = fbmP(nx * 256.0f, ny * 256.0f, seed + 23u, 3, 256);
        float base = 0.40f + tint * 0.16f + (n - 0.5f) * 0.10f;
        /* 砖块内上亮下暗，给一点体积感 */
        float vgrad = 1.0f - (ly / bh) * 0.16f;
        base *= vgrad;
        c.r = base * 1.16f;
        c.g = base * 0.88f;
        c.b = base * 0.76f;
    }
    return c;
}

static RGBf pxTile(int x, int y, int size, unsigned seed) {
    /* 瓷砖：128 一格，缝 4 像素，面上有很淡的斜向反光。
       512/128 = 4，横竖各 4 格，平铺得开。*/
    const float ts = 128.0f, grout = 4.0f;
    float lx = fmodf((float)x, ts);
    float ly = fmodf((float)y, ts);
    float nx = (float)x / (float)size, ny = (float)y / (float)size;
    RGBf c;

    if (lx < grout || ly < grout) {
        float g = 0.34f + (fbmP(nx * 205.0f, ny * 205.0f, seed + 5u, 2, 205) - 0.5f) * 0.06f;
        c.r = g; c.g = g; c.b = g * 1.02f;
        return c;
    }
    {
        float tint = hashF((int)((float)x / ts), (int)((float)y / ts), seed + 3u);
        float n = fbmP(nx * 128.0f, ny * 128.0f, seed + 19u, 3, 128);
        float base = 0.74f + tint * 0.08f + (n - 0.5f) * 0.05f;
        /* 斜向的高光带，模拟釉面反光 */
        float gloss = 0.06f * (1.0f - fabsf(((lx / ts) - (ly / ts)) * 2.0f));
        base += gloss;
        c.r = base * 0.97f;
        c.g = base * 0.99f;
        c.b = base * 1.00f;
    }
    return c;
}

static RGBf pxConcrete(int x, int y, int size, unsigned seed) {
    float u = (float)x / (float)size, v = (float)y / (float)size;
    float coarse = fbmP(u * 5.0f,  v * 5.0f,  seed,        5, 5);
    float fine   = fbmP(u * 40.0f, v * 40.0f, seed + 61u,  3, 40);
    float base = 0.36f + (coarse - 0.5f) * 0.28f + (fine - 0.5f) * 0.09f;
    {
        RGBf c;
        c.r = base * 1.00f;
        c.g = base * 1.00f;
        c.b = base * 0.98f;
        return c;
    }
}

int texMakeWall(Texture *t, int style, unsigned seed) {
    const int size = 512;
    unsigned char *buf = (unsigned char *)malloc((size_t)size * size * 3);
    int x, y;
    if (!buf) return -1;

    for (y = 0; y < size; ++y) {
        for (x = 0; x < size; ++x) {
            RGBf c;
            switch (style) {
                case WALL_BRICK:    c = pxBrick(x, y, size, seed); break;
                case WALL_TILE:     c = pxTile(x, y, size, seed); break;
                case WALL_CONCRETE: c = pxConcrete(x, y, size, seed); break;
                case WALL_PLASTER:
                default:            c = pxPlaster(x, y, size, seed); break;
            }
            /* 这里**故意不烤上下渐变**。"近地处略暗"的渐变原本是烤进贴图的，
               可贴图在墙面上纵向平铺了 1.25 次，渐变跟着重复，于是墙上多了
               一条横接缝。渐变改由 scene.cpp 用顶点色铺一次 —— 整面墙一条
               渐变，不重复。贴图只管它自己那点材质。*/
            {
                size_t o = ((size_t)y * size + x) * 3;
                buf[o + 0] = (unsigned char)(clampf(c.r, 0.0f, 1.0f) * 255.0f + 0.5f);
                buf[o + 1] = (unsigned char)(clampf(c.g, 0.0f, 1.0f) * 255.0f + 0.5f);
                buf[o + 2] = (unsigned char)(clampf(c.b, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        }
    }
    {
        int rc = texFromRGB(t, size, size, buf);
        free(buf);
        return rc;
    }
}

/* ============================================================ 地面 / 天花板 */

int texMakeFloor(Texture *t, float tint, unsigned seed) {
    const int size = 256;
    unsigned char *buf = (unsigned char *)malloc((size_t)size * size * 3);
    int x, y;
    /* 基础色整体提亮约一倍：0.27~0.40 的反照率配上 0.3 的环境光，
       算下来地面只有 0.15 的亮度，整块地板是黑的，房间像悬在虚空里。*/
    RGBf warm = { 0.52f, 0.43f, 0.34f };   /* 暖：偏棕 */
    RGBf cool = { 0.36f, 0.39f, 0.44f };   /* 冷：偏蓝灰 */
    float k = clampf(tint, 0.0f, 1.0f);

    if (!buf) return -1;
    for (y = 0; y < size; ++y) {
        for (x = 0; x < size; ++x) {
            float u = (float)x / (float)size, v = (float)y / (float)size;
            float n = fbmP(u * 56.0f, v * 56.0f, seed, 4, 56);
            /* 振幅从 0.45 收到 0.30：0.45 出来的斑块像沥青路面，
               室内训练场的地垫没这么脏。*/
            float g = 0.84f + (n - 0.47f) * 0.30f;     /* 大颗粒的橡胶地垫感 */
            float rr = lerpf(cool.r, warm.r, k) * g;
            float gg = lerpf(cool.g, warm.g, k) * g;
            float bb = lerpf(cool.b, warm.b, k) * g;
            size_t o = ((size_t)y * size + x) * 3;
            buf[o + 0] = (unsigned char)(clampf(rr, 0.0f, 1.0f) * 255.0f + 0.5f);
            buf[o + 1] = (unsigned char)(clampf(gg, 0.0f, 1.0f) * 255.0f + 0.5f);
            buf[o + 2] = (unsigned char)(clampf(bb, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    }
    {
        int rc = texFromRGB(t, size, size, buf);
        free(buf);
        return rc;
    }
}

int texMakeCeiling(Texture *t, float tint) {
    const int size = 128;
    unsigned char *buf = (unsigned char *)malloc((size_t)size * size * 3);
    int x, y;
    float k = clampf(tint, 0.0f, 1.0f);
    if (!buf) return -1;
    for (y = 0; y < size; ++y) {
        for (x = 0; x < size; ++x) {
            float n = vnoise((float)x * 0.09f, (float)y * 0.09f, 909u);
            /* 天花板只有环境光能照到，0.21 出来是纯黑的一块"虚空"，
               0.34 又嫌太亮、而且大颗粒噪声在掠射角下糊成一片脏斑。
               取中间值 0.27，并把噪声频率降到一半、振幅砍到 0.03，
               让它安静地当一个"房间的顶"就好 —— 它不是主角。*/
            float g = 0.27f + (n - 0.5f) * 0.03f;
            size_t o = ((size_t)y * size + x) * 3;
            buf[o + 0] = (unsigned char)(clampf(g * (0.98f + 0.10f * k), 0.0f, 1.0f) * 255.0f);
            buf[o + 1] = (unsigned char)(clampf(g, 0.0f, 1.0f) * 255.0f);
            buf[o + 2] = (unsigned char)(clampf(g * (1.02f - 0.10f * k), 0.0f, 1.0f) * 255.0f);
        }
    }
    {
        int rc = texFromRGB(t, size, size, buf);
        free(buf);
        return rc;
    }
}
