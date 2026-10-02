/* ============================================================================
 * core.h —— 基础层：数学、随机数、颜色、计时
 *
 * 这一层不依赖项目里任何别的模块，除了 Windows 自己的头文件之外不碰任何东西。
 * 里面全是纯函数（随机数发生器除外），所以可以被自检脚本直接逐条驱动，
 * 不需要建窗口、不需要 GL 上下文 —— 这是整个验证体系的地基。
 * ==========================================================================*/
#ifndef CORE_H
#define CORE_H

/* 加 ifndef 守卫：这两个宏 build.bat 的命令行上已经给了，
   重复定义会触发 C4005 警告。守卫一下，两边都保留也不吵。*/
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stddef.h>

#ifndef ARRAY_COUNT
#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define PI_F       3.14159265358979323846f
#define DEG2RAD_F  (PI_F / 180.0f)
#define RAD2DEG_F  (180.0f / PI_F)

/* ============================================================ 标量小工具 */

float clampf(float v, float lo, float hi);
int   clampi(int v, int lo, int hi);
float lerpf(float a, float b, float t);
float minf(float a, float b);
float maxf(float a, float b);

/* 缓动：爆炸光环用 easeOutQuad，这里沿用同一族曲线，
   保证手感节奏统一。*/
float easeOutQuad(float t);     /* 1-(1-t)^2  先快后慢 */
float easeOutCubic(float t);    /* 1-(1-t)^3  更急 */
float easeInQuad(float t);
float smoothstep01(float t);

/* ================================================================ 向量 */

typedef struct { float x, y, z; } Vec3;

Vec3  v3(float x, float y, float z);
Vec3  v3zero(void);
Vec3  v3add(Vec3 a, Vec3 b);
Vec3  v3sub(Vec3 a, Vec3 b);
Vec3  v3mul(Vec3 a, Vec3 b);
Vec3  v3scale(Vec3 a, float s);
Vec3  v3neg(Vec3 a);
float v3dot(Vec3 a, Vec3 b);
Vec3  v3cross(Vec3 a, Vec3 b);
float v3len(Vec3 a);
float v3lenSq(Vec3 a);
float v3dist(Vec3 a, Vec3 b);
Vec3  v3norm(Vec3 a);            /* 零向量返回 (0,0,0)，不做除零 */
Vec3  v3lerp(Vec3 a, Vec3 b, float t);

/* ============================================================ 4x4 矩阵
 * 列主序（与 OpenGL 的 glLoadMatrixf 一致）：m[col*4 + row]。
 * 只做本项目需要的那几种：透视、观察、平移、绕轴旋转、缩放。
 */

typedef struct { float m[16]; } Mat4;

Mat4 mat4Identity(void);
Mat4 mat4Mul(Mat4 a, Mat4 b);                 /* 先施加 b 再施加 a */
Mat4 mat4Perspective(float fovyDeg, float aspect, float znear, float zfar);
Mat4 mat4LookAt(Vec3 eye, Vec3 target, Vec3 up);
Mat4 mat4Translate(Vec3 t);
Mat4 mat4Scale(Vec3 s);
Mat4 mat4RotateX(float rad);
Mat4 mat4RotateY(float rad);
Mat4 mat4RotateZ(float rad);
Mat4 mat4Basis(Vec3 right, Vec3 up, Vec3 fwd); /* 由三根正交轴构造旋转部分 */
Vec3 mat4XformPoint(Mat4 m, Vec3 p);           /* 当成点变换（含平移） */
Vec3 mat4XformDir(Mat4 m, Vec3 d);             /* 当成方向变换（不含平移） */

/* ---------------------------------------------------------- 射线与球求交 */

/* 射线 origin+dir*t（dir 必须已归一化）与球 (center, radius) 求交。
 * 命中返回 1，并把**最近的正向 t** 写进 *outT；不命中返回 0。
 *
 * 判别式用 >= 0 而不是 > 0：**圆周上算命中**。这是有意设计
 * （刻意留的手感宽容度），不是写错了比较符。
 * 自检里有专门的边界用例盯着这一条。*/
int raySphere(Vec3 origin, Vec3 dir, Vec3 center, float radius, float *outT);

/* 两个球在墙面上是否重叠（用于拒绝采样）。gap 是额外要求的间隙比例：
 * 判定为 dist < (r1 + r2) * gap。gap = 1.0 表示刚好相切算不重叠。*/
int spheresOverlap2D(float x1, float y1, float r1,
                     float x2, float y2, float r2, float gap);

/* ============================================================== 随机数
 * 自带的 xorshift32。**可复现**是硬要求：出图、自检、批量验证都要能
 * 用同一个种子跑出同一个场景，所以不能用 rand()（它的实现随运行库变）。
 */
typedef struct { uint32_t s; } Rng;

void     rngSeed(Rng *r, uint32_t seed);
uint32_t rngNext(Rng *r);
float    rngFloat(Rng *r);                        /* [0, 1) */
float    rngRange(Rng *r, float lo, float hi);    /* [lo, hi) */
int      rngInt(Rng *r, int lo, int hi);          /* [lo, hi] 闭区间 */
int      rngChance(Rng *r, float p);              /* p 概率返回 1 */

/* ================================================================ 颜色 */

typedef struct { float r, g, b; } Color3;

Color3 color3(float r, float g, float b);
Color3 colorFromRGB8(int r, int g, int b);
/* HSV 取色：h 单位是度 [0,360)，s/v 是 [0,1]。用它生成气球配色，
   好处是"深色背景下鲜艳不脏"，连同 s/v 的取值范围一起。*/
Color3 hsvToRgb(float h, float s, float v);
/* k > 0：向白提亮；k < 0：压暗。描边用 colorScale(球色, -0.34) 画。*/
Color3 colorScale(Color3 c, float k);
Color3 colorLerp(Color3 a, Color3 b, float t);

typedef struct { float r, g, b, a; } Color4;
Color4 color4(Color3 c, float a);

/* ============================================================ 校验和
 * 配置存档与 PNG 都要用，所以放在基础层，避免两处各写一份。
 * 表在第一次调用时算出来，之后只读，不需要加锁（进程内单线程初始化）。
 */
uint32_t crc32Buf(const void *data, size_t len);

/* ================================================================ 计时 */

double nowSeconds(void);       /* 单调递增，供主循环算 dt */
void   sleepMs(int ms);

/* 把 double 秒数格式化成 "1:23.4" 或 "12.3s"，写进宽字符缓冲（HUD 用）。*/
void formatSeconds(double sec, wchar_t *out, int outCount);

/* ============================================================ 控制台输出
 * Windows 控制台的默认代码页是 936，中文经管道传给 Git Bash 会变成乱码。
 * 于是规定：**一切面向控制台的文字都先转成 UTF-8 字节再写**，
 * 配合 consoleUseUtf8() 把控制台代码页也切到 UTF-8，两边就一致了。
 * 项目里所有中文输出都走这两个函数，不允许各写一份。
 */
void consoleUseUtf8(void);              /* 切控制台输出代码页到 UTF-8（幂等） */
void printUtf8(const wchar_t *s);       /* 把宽字符按 UTF-8 写到 stdout */
void printUtf8f(const wchar_t *fmt, ...);

#endif /* CORE_H */
