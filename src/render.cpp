/* ============================================================================
 * render.cpp —— 全部绘制
 *
 * 固定管线 + 立即模式。场景里三角形总数在几千的量级，探针实测这类负载
 * 每帧只要零点几毫秒，所以"优化绘制"不是本题的矛盾所在，把观感调好才是。
 *
 * 三处刻意的选择，都写在代码里：
 *   1. 不开背面剔除 + 开双面光照 —— 房间是单面四边形，省去绕序的心智负担。
 *   2. 气球的高光不是靠材质高光"算"出来的，而是在球面上叠一张加色贴片 ——
 *      固定管线的逐顶点高光在球上会糊成一片，想要的是"一个亮点"，
 *      这里用同样的办法把它做出来。
 *   3. 气球在墙上的投影用一张暗色径向贴片，不是阴影贴图 —— 一个光源、
 *      一面墙，成本为零而立体感提升明显。
 * ==========================================================================*/
#include "render.h"
#include <stdio.h>
#include <wchar.h>

/* ============================================================ 相机基向量 */

void renderUpdateCameraBasis(App *app) {
    app->camFwd   = v3norm(playerForward(&app->player));
    app->camRight = v3norm(playerRight(&app->player));
    app->camUp    = v3norm(v3cross(app->camRight, app->camFwd));
    app->camEye   = playerEyePos(&app->player);
}

/* ============================================================ 灯光
 * 两盏灯 + 环境光。放在"气球墙对面偏上"的位置，这样气球的高光在左上，
 * 与 (−0.34r, −0.36r) 的高光方向一致。
 */
static void setupLights(const App *app) {
    GLfloat amb[]      = { 0.30f, 0.32f, 0.38f, 1.0f };
    GLfloat dir0Pos[]  = { -3.0f, 6.0f, 4.0f, 0.0f };   /* w=0：方向光 */
    GLfloat dir0Dif[]  = { 1.00f, 0.96f, 0.90f, 1.0f };
    GLfloat lampPos[]  = { 0.0f, ROOM_H - 0.35f, -1.6f, 1.0f };
    GLfloat lampDif[]  = { 0.42f, 0.46f, 0.56f, 1.0f };
    /* 主光强度固定成 LIGHT_INTENSITY_DEF（早先从面板上删掉了）。
       保留这个乘法而不是把系数乘进上面几个数里 —— 常量一旦要调，
       改一处比在四行里找系数可靠。*/
    float k = LIGHT_INTENSITY_DEF;
    (void)app;

    amb[0] *= k; amb[1] *= k; amb[2] *= k;
    dir0Dif[0] *= k; dir0Dif[1] *= k; dir0Dif[2] *= k;
    lampDif[0] *= k; lampDif[1] *= k; lampDif[2] *= k;

    glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, 1);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);

    glEnable(GL_LIGHT0);
    glLightfv(GL_LIGHT0, GL_POSITION, dir0Pos);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, dir0Dif);
    glLightfv(GL_LIGHT0, GL_SPECULAR, dir0Dif);
    glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION, 1.0f);

    glEnable(GL_LIGHT1);
    glLightfv(GL_LIGHT1, GL_POSITION, lampPos);
    glLightfv(GL_LIGHT1, GL_DIFFUSE, lampDif);
    glLightfv(GL_LIGHT1, GL_SPECULAR, lampDif);
    glLightf(GL_LIGHT1, GL_CONSTANT_ATTENUATION, 0.60f);
    glLightf(GL_LIGHT1, GL_LINEAR_ATTENUATION, 0.10f);

    /* 材质的镜面项只设一次：GL_COLOR_MATERIAL 只管 ambient/diffuse，
       所以这里的 specular 不会被 glColor 覆盖掉。*/
    {
        GLfloat spec[] = { 0.85f, 0.85f, 0.85f, 1.0f };
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec);
        glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 48.0f);
    }
}

/* ============================================================ 雾
 * 雾浓度映射成"多远变得看不清"。这个值从面板上删掉了（纯观感微调），
 * 固定成 FOG_DENSITY_DEF。那句"0 就是完全关掉雾"的分支留着 ——
 * 常量改成 0 时它是真实可达的状态，不是"调小到看不出"。
 */
static void setupFog(const App *app) {
    float d = FOG_DENSITY_DEF;
    (void)app;
    if (d <= 0.0005f) { glDisable(GL_FOG); return; }
    {
        GLfloat col[] = { 0.115f, 0.130f, 0.165f, 1.0f };
        float fogEnd   = 34.0f / (1.0f + d * 12.0f);
        float fogStart = fogEnd * 0.30f;
        glEnable(GL_FOG);
        glFogi(GL_FOG_MODE, GL_LINEAR);
        glFogfv(GL_FOG_COLOR, col);
        glFogf(GL_FOG_START, fogStart);
        glFogf(GL_FOG_END, fogEnd);
    }
}

/* ============================================================ 小工具 */

static void setColor3(Color3 c, float a) { glColor4f(c.r, c.g, c.b, a); }

/* 画一个正对相机的四边形（billboard）。pos 是中心，s 是半边长。*/
static void billboard(const App *app, Vec3 pos, float sx, float sy, int flipY) {
    Vec3 r = v3scale(app->camRight, sx);
    Vec3 u = v3scale(app->camUp, sy);
    glBegin(GL_QUADS);
    if (flipY) {
        glTexCoord2f(0.0f, 1.0f); glVertex3f(pos.x - r.x - u.x, pos.y - r.y - u.y, pos.z - r.z - u.z);
        glTexCoord2f(1.0f, 1.0f); glVertex3f(pos.x + r.x - u.x, pos.y + r.y - u.y, pos.z + r.z - u.z);
        glTexCoord2f(1.0f, 0.0f); glVertex3f(pos.x + r.x + u.x, pos.y + r.y + u.y, pos.z + r.z + u.z);
        glTexCoord2f(0.0f, 0.0f); glVertex3f(pos.x - r.x + u.x, pos.y - r.y + u.y, pos.z - r.z + u.z);
    } else {
        glTexCoord2f(0.0f, 0.0f); glVertex3f(pos.x - r.x - u.x, pos.y - r.y - u.y, pos.z - r.z - u.z);
        glTexCoord2f(1.0f, 0.0f); glVertex3f(pos.x + r.x - u.x, pos.y + r.y - u.y, pos.z + r.z - u.z);
        glTexCoord2f(1.0f, 1.0f); glVertex3f(pos.x + r.x + u.x, pos.y + r.y + u.y, pos.z + r.z + u.z);
        glTexCoord2f(0.0f, 1.0f); glVertex3f(pos.x - r.x + u.x, pos.y - r.y + u.y, pos.z - r.z + u.z);
    }
    glEnd();
}

/* ============================================================ 墙上的灯
 *
 * 这一节是**重做过的**。最初把光晕铺到 3.1 米见方、峰值加色 0.47，
 * 而墙面本身已经亮到 0.85 左右，于是墙上出现三块死白的硬边矩形
 * （shots/01_first.png 里一眼就能看见，最大的败笔）。
 *
 * 现在的做法是"小而收敛"：
 *   · 光晕降到 1.9 米宽 × 2.6 米高，峰值加色 0.22 —— 落在 0.85 的墙上
 *     刚好提亮到 1.0 出头，不再整片饱和，边缘保留可见的衰减；
 *   · 竖着拉长，像一束打在墙面上的光，而不是一个圆盘；
 *   · 顺带在它照得到的地面与天花板上各铺一张加色光斑，把"灯在发光"
 *     这件事从墙上扩到整个房间，房间立刻就不像黑盒子了。
 */

#define LAMP_COUNT 3

/* 在水平面上铺一张加色光斑（地面光池 / 天花板洗光）。y 是平面高度。*/
static void horizontalGlow(const Texture *t, float cx, float y, float cz,
                           float sx, float sz, Color3 col, float a) {
    glBindTexture(GL_TEXTURE_2D, t->id);
    glColor4f(col.r, col.g, col.b, a);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(cx - sx, y, cz + sz);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(cx + sx, y, cz + sz);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(cx + sx, y, cz - sz);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(cx - sx, y, cz - sz);
    glEnd();
}

static void drawWallLamps(const App *app) {
    /* 灯位与气球墙的横向分栏对齐：-3.9 / 0 / +3.9 米。*/
    static const float kLampX[LAMP_COUNT] = { -3.9f, 0.0f, 3.9f };
    const float lampY = ROOM_H - 0.46f;
    int i;

    glDisable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);

    /* ---- 灯座：比灯管长一点的深色底，给灯一个"装上去"的实体感 ---- */
    glBindTexture(GL_TEXTURE_2D, app->scene.white.id);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.13f, 0.14f, 0.16f, 1.0f);
    for (i = 0; i < LAMP_COUNT; ++i) {
        glBegin(GL_QUADS);
        glVertex3f(kLampX[i] - 0.58f, lampY + 0.10f, WALL_Z + 0.03f);
        glVertex3f(kLampX[i] + 0.58f, lampY + 0.10f, WALL_Z + 0.03f);
        glVertex3f(kLampX[i] + 0.58f, lampY - 0.12f, WALL_Z + 0.03f);
        glVertex3f(kLampX[i] - 0.58f, lampY - 0.12f, WALL_Z + 0.03f);
        glEnd();
    }

    /* ---- 灯管：细长条，接近纯白 ---- */
    glColor4f(0.94f, 0.95f, 0.98f, 1.0f);
    for (i = 0; i < LAMP_COUNT; ++i) {
        glBegin(GL_QUADS);
        glVertex3f(kLampX[i] - 0.50f, lampY + 0.05f, WALL_Z + 0.08f);
        glVertex3f(kLampX[i] + 0.50f, lampY + 0.05f, WALL_Z + 0.08f);
        glVertex3f(kLampX[i] + 0.50f, lampY - 0.05f, WALL_Z + 0.08f);
        glVertex3f(kLampX[i] - 0.50f, lampY - 0.05f, WALL_Z + 0.08f);
        glEnd();
    }

    /* ---- 墙面光晕：加色，竖长 ---- */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glBindTexture(GL_TEXTURE_2D, app->scene.glow.id);
    {
        static const Color3 kGlowCol = { 0.42f, 0.44f, 0.48f };
        for (i = 0; i < LAMP_COUNT; ++i) {
            float sx = 0.95f, sy = 1.30f;
            float cy = lampY - 0.55f;
            glColor4f(kGlowCol.r, kGlowCol.g, kGlowCol.b, 0.52f);
            glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 0.0f); glVertex3f(kLampX[i] - sx, cy - sy, WALL_Z + 0.05f);
            glTexCoord2f(1.0f, 0.0f); glVertex3f(kLampX[i] + sx, cy - sy, WALL_Z + 0.05f);
            glTexCoord2f(1.0f, 1.0f); glVertex3f(kLampX[i] + sx, cy + sy, WALL_Z + 0.05f);
            glTexCoord2f(0.0f, 1.0f); glVertex3f(kLampX[i] - sx, cy + sy, WALL_Z + 0.05f);
            glEnd();
        }

        /* ---- 地面光池：灯照下来的那一小片地面 ---- */
        for (i = 0; i < LAMP_COUNT; ++i) {
            horizontalGlow(&app->scene.glow, kLampX[i], 0.012f, WALL_Z + 2.2f,
                           1.70f, 2.10f, color3(0.34f, 0.35f, 0.38f), 0.42f);
        }

        /* ---- 天花板洗光：灯往上打的那一圈 ---- */
        for (i = 0; i < LAMP_COUNT; ++i) {
            horizontalGlow(&app->scene.glow, kLampX[i], ROOM_H - 0.015f,
                           WALL_Z + 0.35f, 1.25f, 1.25f,
                           color3(0.36f, 0.37f, 0.41f), 0.50f);
        }
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_LIGHTING);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

/* ============================================================ 气球 */

/* 气球在墙上的投影。光源在左上，影子就往右下偏。*/
static void drawBalloonShadow(const App *app, const Balloon *b) {
    Vec3 c = balloonWorldPos(b);
    float s = b->r * 1.35f;
    float ox = b->r * 0.42f;
    float oy = -b->r * 0.52f;
    float a = 0.34f * (b->r / maxf(0.12f, b->baseR));
    if (a <= 0.0f) return;

    glBindTexture(GL_TEXTURE_2D, app->scene.glow.id);
    glColor4f(0.03f, 0.035f, 0.05f, clampf(a, 0.0f, 1.0f));
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(c.x + ox - s, c.y + oy - s, WALL_Z + 0.004f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(c.x + ox + s, c.y + oy - s, WALL_Z + 0.004f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(c.x + ox + s, c.y + oy + s, WALL_Z + 0.004f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(c.x + ox - s, c.y + oy + s, WALL_Z + 0.004f);
    glEnd();
}

/* 一个气球：球体 + 扎口 + 线绳。
 * 球体用 gluSphere，光照是固定管线的逐顶点。*/
static void drawBalloon(const App *app, GLUquadricObj *q, const Balloon *b) {
    Color3 col = balloonColor(b, &app->params);
    Vec3 c = balloonWorldPos(b);
    float r = b->r;
    int fading = (b->state == BSLOT_ESCAPE);

    if (r <= 0.001f) return;

    glPushMatrix();
    glTranslatef(c.x, c.y, c.z);

    /* ---- 超时逃走的球会越来越淡，给玩家一个"这个球要没了"的信号 ---- */
    if (fading) {
        float k = clampf(1.0f - b->timer, 0.15f, 1.0f);
        col = colorLerp(color3(0.10f, 0.11f, 0.14f), col, k);
    }

    /* ---- 球体本体 ----
       纵向拉长 6%：真气球不是正球，吹起来之后下半截略瘦，压成一个稍稍
       竖直的椭球比正球耐看，成本是一次 glScale。*/
    glPushMatrix();
    glScalef(1.0f, 1.06f, 1.0f);
    setColor3(col, 1.0f);
    gluSphere(q, (double)r, 24, 16);
    glPopMatrix();

    /* ---- 扎口：底部一个小圆锥 ----
       气球底下的"扎口"在 3D 里对应一个小圆锥。
       最初把它做成了 0.20r/0.24r，几乎完全埋进球体里，等于没画；
       现在放大到 0.26r/0.36r，并且从 y = -0.96r 起，露出一小截。*/
    glPushMatrix();
    glTranslatef(0.0f, -r * 0.96f, 0.0f);
    /* 转 180° 是为了让圆锥朝下长：gluCylinder 沿局部 **+z** 生长，
       不转的话它会扎进球体里。（这里以前写的是"+y"，是错的 ——
       正是这处误记让人以为圆柱默认朝上，后来把枪管转错了方向。）*/
    glRotatef(180.0f, 1.0f, 0.0f, 0.0f);
    setColor3(colorScale(col, -0.34f), 1.0f);
    gluCylinder(q, (double)(r * 0.26f), 0.0, (double)(r * 0.36f), 10, 1);
    glPopMatrix();

    (void)app;
    glPopMatrix();
}

/* 气球底下的那根**线绳**（drawBalloonStrings）已经删掉了 ——
   它是球体下面挂着的一根会随时间摆动的三段折线，看起来像一根被风吹的
   绳子，属于"尾巴"。球体本身和底下那个收口的扎口小圆锥**保留**：
   那是气球本体的形状，不是"尾巴"。万一以后觉得删错了，
   重建只要把这一段粘回来。

   顺带的好处：飘分、命中和"球飞到哪儿了"这几个判断都只看球体，
   绳子的位置从来不参与任何逻辑，删掉不影响玩法。*/

/* 高光：在球面朝向"光源与视线之间的半角方向"处叠两层加色贴片。
 * 目标是"亮斑 + 一点白" —— 固定管线的材质高光在球面上
 * 会晕开成一大片，而这里要的是"一颗亮珠"。
 *
 * 最初只画了一层 s = 0.42r 的大贴片，结果高光几乎盖住半个球，
 * 气球看上去像玻璃弹珠。现在拆成两层：外圈柔光 0.30r、内芯 0.12r，
 * 比例回到 0.22r / 0.09r 附近。*/
static void drawBalloonSpec(const App *app, const Balloon *b, int layer) {
    Vec3 c, lightDir, viewDir, half;
    float r = b->r;
    float s, a;
    Color3 col;

    if (r <= 0.001f || b->state == BSLOT_ESCAPE) return;

    c = balloonWorldPos(b);
    /* 主光方向：与 setupLights 里的 LIGHT0 一致（指向光源）。*/
    lightDir = v3norm(v3(-3.0f, 6.0f, 4.0f));
    viewDir  = v3norm(v3sub(app->camEye, c));
    half     = v3norm(v3add(lightDir, viewDir));
    if (v3lenSq(half) < 1e-6f) half = viewDir;

    if (layer == 0) {
        /* 外圈：球色提亮，柔光 */
        s = r * 0.30f;
        a = 0.55f;
        col = balloonColor(b, &app->params);
        col = colorLerp(col, color3(1.0f, 1.0f, 1.0f), 0.62f);
    } else {
        /* 内芯：一点白 */
        s = r * 0.12f;
        a = 0.88f;
        col = color3(1.0f, 1.0f, 1.0f);
    }

    {
        Vec3 hp = v3add(c, v3add(v3scale(half, r * 0.74f),
                                 v3scale(viewDir, r * 0.06f)));
        glDisable(GL_LIGHTING);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, app->scene.spec.id);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
        setColor3(col, a);
        billboard(app, hp, s, s, 0);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glDisable(GL_TEXTURE_2D);
        glEnable(GL_LIGHTING);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

static void drawBalloons(const App *app, double nowSec) {
    GLUquadricObj *q = gluNewQuadric();
    int i, layer;

    /* 先画所有影子（都在墙面上，位于球体之后），再画球体。 */
    glDisable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    for (i = 0; i < app->pool.n; ++i)
        drawBalloonShadow(app, &app->pool.slots[i]);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_LIGHTING);

    for (i = 0; i < app->pool.n; ++i)
        drawBalloon(app, q, &app->pool.slots[i]);
    for (layer = 0; layer < 2; ++layer)
        for (i = 0; i < app->pool.n; ++i)
            drawBalloonSpec(app, &app->pool.slots[i], layer);

    gluDeleteQuadric(q);
    (void)nowSec;   /* 线绳删掉之后这里不再需要时间；保留形参是为了调用点不变 */
}

/* ============================================================ 击破特效
 *
 * 全部画在**墙面平面**上（z = WALL_Z 附近），因为气球是贴在墙上爆的。
 * 三样东西：扩散的光环、飞散的碎片、墙上留下的颜料印。
 */

/* 墙上的颜料印。贴图是径向渐变（中心白、边缘透明），用气球颜色染色，
   所以每一块印子都是"那一个球"的颜色。*/
static void drawWallDecals(const App *app) {
    const FxPool *fx = &app->fx;
    int i;

    glDisable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);          /* 印子是平的，不写深度，免得互相打架 */
    glBindTexture(GL_TEXTURE_2D, app->scene.glow.id);

    for (i = 0; i < MAX_DECALS; ++i) {
        const WallDecal *d = &fx->decals[i];
        float r, a, c, s, z;

        if (!d->active) continue;

        /* 最后两秒淡出。前面一直保持满强度 —— 印子该是"留痕"，
           不是"一直在呼吸"。*/
        a = clampf(d->life / 2.0f, 0.0f, 1.0f) * 0.60f;
        r = d->r;
        z = WALL_Z + 0.012f + (float)i * 0.0004f;   /* 每块错开一丁点，防 z 争用 */
        c = cosf(d->rot) * r;
        s = sinf(d->rot) * r;

        glColor4f(d->color.r, d->color.g, d->color.b, a);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex3f(d->x - c + s, d->y - s - c, z);
        glTexCoord2f(1.0f, 0.0f); glVertex3f(d->x + c + s, d->y + s - c, z);
        glTexCoord2f(1.0f, 1.0f); glVertex3f(d->x + c - s, d->y + s + c, z);
        glTexCoord2f(0.0f, 1.0f); glVertex3f(d->x - c - s, d->y - s + c, z);
        glEnd();
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

#define FX_RING_SEGS 40

static void drawPops(const App *app) {
    const FxPool *fx = &app->fx;
    int i, j;

    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (i = 0; i < MAX_EFFECTS; ++i) {
        const PopEffect *e = &fx->pops[i];
        float t, fade, ringR, z;

        if (!e->active) continue;
        t = clampf(e->elapsed / maxf(1e-4f, e->duration), 0.0f, 1.0f);
        /* 快出慢收：前半段扩得快，后半段淡出。*/
        fade = (1.0f - t) * (1.0f - t);
        z = WALL_Z + 0.03f + e->zOff * t;      /* 朝玩家这边飘出来 */

        /* ---- 扩散光环 ---- */
        ringR = e->r0 * (1.0f + (FX_RING_EXPAND - 1.0f) * t);
        glLineWidth(1.0f + 2.6f * (1.0f - t));
        glColor4f(e->color.r, e->color.g, e->color.b, fade * 0.85f);
        glBegin(GL_LINE_LOOP);
        for (j = 0; j < FX_RING_SEGS; ++j) {
            float a = 2.0f * PI_F * (float)j / (float)FX_RING_SEGS;
            glVertex3f(e->pos.x + cosf(a) * ringR,
                       e->pos.y + sinf(a) * ringR, z);
        }
        glEnd();

        /* ---- 内圈实心盘：一开始闪一下，让"破了"这件事有重量 ---- */
        if (t < 0.35f) {
            float k = 1.0f - t / 0.35f;
            glBindTexture(GL_TEXTURE_2D, app->scene.glow.id);
            glEnable(GL_TEXTURE_2D);
            glColor4f(1.0f, 1.0f, 1.0f, k * 0.55f);
            glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 0.0f); glVertex3f(e->pos.x - e->r0, e->pos.y - e->r0, z);
            glTexCoord2f(1.0f, 0.0f); glVertex3f(e->pos.x + e->r0, e->pos.y - e->r0, z);
            glTexCoord2f(1.0f, 1.0f); glVertex3f(e->pos.x + e->r0, e->pos.y + e->r0, z);
            glTexCoord2f(0.0f, 1.0f); glVertex3f(e->pos.x - e->r0, e->pos.y + e->r0, z);
            glEnd();
            glDisable(GL_TEXTURE_2D);
        }

        /* ---- 碎片 ----
           位置按 elapsed 积分一次算出来，不做逐帧累积 —— 特效"自己会算到哪"
           而不是"每帧往前推一点"，所以暂停、慢放、自检复算都是对的。*/
        glBindTexture(GL_TEXTURE_2D, app->scene.white.id);
        glEnable(GL_TEXTURE_2D);
        for (j = 0; j < e->shardCount; ++j) {
            float a = e->shardAngle[j];
            float dist = e->shardSpeed[j] * e->elapsed;
            Vec3 p;
            Color3 c = e->color;
            float sz = e->shardSize[j] * (1.0f - 0.45f * t);

            if      (e->shade[j] == 0) c = colorLerp(e->color, color3(1.0f, 1.0f, 1.0f), 0.55f);
            else if (e->shade[j] == 2) c = colorScale(e->color, 0.62f);

            p = v3(e->pos.x + cosf(a) * dist,
                   e->pos.y + sinf(a) * dist,
                   z);
            setColor3(c, fade * 0.95f);
            billboard(app, p, sz, sz, 0);
        }
        glDisable(GL_TEXTURE_2D);
    }

    glLineWidth(1.0f);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

/* 曳光：从眼睛到落点的一条加色亮线。只活 0.085 秒 —— 长了就像激光，
   短了看不见，"够看清打哪儿了"就够。*/
static void drawTracer(const App *app) {
    float a;
    if (app->tracerLife <= 0.0f) return;

    a = clampf(app->tracerLife / 0.085f, 0.0f, 1.0f);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);          /* 加色：亮线压在暗处才透亮 */
    glDepthMask(GL_FALSE);

    glLineWidth(1.0f + 2.0f * a);
    glColor4f(1.0f, 0.96f, 0.72f, a * 0.55f);
    glBegin(GL_LINES);
    glVertex3f(app->tracerFrom.x, app->tracerFrom.y, app->tracerFrom.z);
    glVertex3f(app->tracerTo.x,   app->tracerTo.y,   app->tracerTo.z);
    glEnd();

    /* 落点再点一个小亮点：打中墙的时候视线才知道"打这儿了"。*/
    glPointSize(4.0f + 4.0f * a);
    glBegin(GL_POINTS);
    glVertex3f(app->tracerTo.x, app->tracerTo.y, app->tracerTo.z);
    glEnd();
    glPointSize(1.0f);

    glLineWidth(1.0f);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LIGHTING);
}

/* ============================================================ 手持装置
 *
 * 可玩点不在枪械的使用，所以这里刻意做得**克制**：
 * 一支短管发射器挂在画面右下角，走动时轻微摇摆、开火时向后一顿。
 * 它的作用是让第一人称视角"有手"，不是让玩家去研究枪。
 * 参数里可以整个关掉（showWeapon）。
 *
 * ★ 这一块踩过两个坑，动它之前先看一眼：
 *
 * 1) **gluCylinder / gluSphere 系的圆柱轴是局部 +z**（不是 +y ——
 *    上面画气球扎口那段的注释里就写错过一次）。想让它朝前（眼空间的
 *    -z）得绕 x 转 180°。这里曾经写的是 -90°，于是管子不是朝前而是
 *    朝上，在画面右侧立成一根黑柱子 —— 看着完全像个渲染 bug，
 *    其实"枪"是画出来了，只是整个方向错了。
 *
 * 2) **别拿场景光照给这块上色**。装置的 modelview 是单位矩阵、整块挂在
 *    相机上，而场景的灯在天花板上（GL 的灯位在设置时就被变换进了眼坐标，
 *    是另一套位置）；两个空间对不上，结果是所有面一样亮，糊成一块黑板。
 *    这里改成关掉 GL_LIGHTING、每个面按法线方向手工给一档明暗：
 *    体积感稳定可控，也和气球那套低多边形上色方式统一。
 */

/* 一个带明暗的长方体。明暗是查表给的，不参与 GL 光照 —— 理由见上。*/
static void drawShadedBox(float w, float h, float d, const float *base) {
    const float x = w * 0.5f, y = h * 0.5f, z = d * 0.5f;

    /* 明暗系数按"光从左前上方来"给：顶面最亮，朝相机的面次之，
       背光的右面最暗。这几个数是照着截图调的，改之前先看一眼画面。*/
#define FACE(K, A, B, C, D)                       \
    do {                                          \
        glColor3f(base[0] * (K), base[1] * (K), base[2] * (K)); \
        glBegin(GL_QUADS);                        \
        glVertex3f A; glVertex3f B;               \
        glVertex3f C; glVertex3f D;               \
        glEnd();                                  \
    } while (0)

    FACE(1.22f, (-x,  y,  z), ( x,  y,  z), ( x,  y, -z), (-x,  y, -z)); /* 顶 */
    FACE(0.50f, (-x, -y, -z), ( x, -y, -z), ( x, -y,  z), (-x, -y,  z)); /* 底 */
    FACE(1.08f, (-x, -y,  z), ( x, -y,  z), ( x,  y,  z), (-x,  y,  z)); /* 朝相机 */
    FACE(0.60f, ( x, -y, -z), (-x, -y, -z), (-x,  y, -z), ( x,  y, -z)); /* 背 */
    FACE(0.82f, (-x, -y, -z), (-x, -y,  z), (-x,  y,  z), (-x,  y, -z)); /* 左 */
    FACE(0.62f, ( x, -y,  z), ( x, -y, -z), ( x,  y, -z), ( x,  y,  z)); /* 右 */
#undef FACE
}

/* 一支沿 **-z**（朝前）的锥台，用 seg 条棱近似圆柱。
   明暗按法线的 xy 分量算 —— 相当于给装置配一盏固定在相机上的灯。*/
static void drawTube(float rBack, float rFront, float len, int seg,
                     const float *base) {
    const float lx = -0.45f, ly = 0.78f;    /* "灯"在眼空间 xy 平面上的方向 */
    int i;

    glBegin(GL_QUADS);
    for (i = 0; i < seg; ++i) {
        float a0 = 2.0f * PI_F * (float)i / (float)seg;
        float a1 = 2.0f * PI_F * (float)(i + 1) / (float)seg;
        float n0x = cosf(a0), n0y = sinf(a0);
        float n1x = cosf(a1), n1y = sinf(a1);
        float s0 = 0.50f + 0.50f * maxf(0.0f, n0x * lx + n0y * ly);
        float s1 = 0.50f + 0.50f * maxf(0.0f, n1x * lx + n1y * ly);

        glColor3f(base[0] * s0, base[1] * s0, base[2] * s0);
        glVertex3f(n0x * rBack,  n0y * rBack,   0.0f);
        glVertex3f(n1x * rBack,  n1y * rBack,   0.0f);
        glColor3f(base[0] * s1, base[1] * s1, base[2] * s1);
        glVertex3f(n1x * rFront, n1y * rFront, -len);
        glVertex3f(n0x * rFront, n0y * rFront, -len);
    }
    glEnd();
}

/* 垂直于 z 轴的一个圆面，用来封住管口。*/
static void drawDisc(float r, float z, int seg, const float *base, float k) {
    int i;
    glColor3f(base[0] * k, base[1] * k, base[2] * k);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(0.0f, 0.0f, z);
    for (i = 0; i <= seg; ++i) {
        float a = 2.0f * PI_F * (float)i / (float)seg;
        glVertex3f(cosf(a) * r, sinf(a) * r, z);
    }
    glEnd();
}

static void drawWeapon(App *app) {
    static const float kBody[3] = { 0.300f, 0.320f, 0.372f };  /* 枪身：深灰蓝 */
    static const float kGrip[3] = { 0.245f, 0.258f, 0.292f };  /* 握把更暗 */
    static const float kRing[3] = { 0.520f, 0.552f, 0.606f };  /* 口部亮环 */
    float sway, recoil, muzzleA;
    Vec3 ez;

    if (!app->params.showWeapon) return;

    /* 装置的 modelview 用单位矩阵：它活在**眼空间**里，等于挂在相机上。
       这样走动时它不会穿进墙里 —— 视角模型的常规做法。
       画之前把深度缓冲清掉：装置离眼睛只有半米，贴着墙站的时候墙会切
       进画面把它削掉一块。清掉深度就等于"装置永远画在最上面"。*/
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_LIGHTING);              /* 手工上明暗，理由见上 */
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    /* 走动时轻微摇摆、开火时向后一顿。幅度都很小，属于手感而不是表演。*/
    sway   = sinf(app->player.bobPhase) * 0.012f * app->player.speedSmooth;
    recoil = app->fireFlash * 0.030f;

    /* 位置是照着 1600x900 / 75° 视野调的：整支装置大约占画面宽度的
       一成半、高度的两成，压在右下角，不挡气球。改数值之前先看一眼截图。*/
    glTranslatef(0.345f + sway - app->player.bobSide * 0.6f,
                 -0.315f - app->player.bobOffset * 0.9f + recoil * 0.5f,
                 -0.700f + recoil);
    /* 略微上抬、朝画面中心偏一点，看着像"端着的"而不是"贴上去的"。*/
    glRotatef(3.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(6.0f, 0.0f, 1.0f, 0.0f);

    /* ---- 枪管：从原点朝 -z 长出去 ---- */
    drawTube(0.026f, 0.021f, 0.340f, 14, kBody);
    /* 口部端面，交代"这是朝前的那一头"。*/
    drawDisc(0.021f, -0.340f, 14, kRing, 0.85f);
    /* 口部后侧一圈加粗的亮环。*/
    glPushMatrix();
    glTranslatef(0.0f, 0.0f, -0.300f);
    drawTube(0.030f, 0.028f, 0.020f, 14, kRing);
    glPopMatrix();

    /* ---- 机匣：中段加粗，给轮廓一个"主体" ---- */
    glPushMatrix();
    glTranslatef(0.0f, -0.012f, -0.072f);
    drawShadedBox(0.062f, 0.070f, 0.150f, kBody);
    glPopMatrix();

    /* ---- 顶部短导轨：让上沿不是一条直线 ---- */
    glPushMatrix();
    glTranslatef(0.0f, 0.029f, -0.094f);
    drawShadedBox(0.024f, 0.012f, 0.104f, kGrip);
    glPopMatrix();

    /* ---- 握把：向后下方斜出去 ---- */
    glPushMatrix();
    glTranslatef(0.0f, -0.084f, 0.016f);
    glRotatef(16.0f, 1.0f, 0.0f, 0.0f);
    drawShadedBox(0.046f, 0.110f, 0.056f, kGrip);
    glPopMatrix();

    /* ---- 枪口火光 ---- */
    muzzleA = app->fireFlash;
    if (muzzleA > 0.0f) {
        glEnable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
        glBindTexture(GL_TEXTURE_2D, app->scene.glow.id);

        /* 火光贴在枪口前面。眼空间里三个基向量就是三个坐标轴，不用另算。*/
        ez = v3(0.0f, 0.0f, -0.375f);
        glColor4f(1.0f, 0.93f, 0.68f, muzzleA * 0.90f);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex3f(ez.x - 0.11f, ez.y - 0.11f, ez.z);
        glTexCoord2f(1.0f, 0.0f); glVertex3f(ez.x + 0.11f, ez.y - 0.11f, ez.z);
        glTexCoord2f(1.0f, 1.0f); glVertex3f(ez.x + 0.11f, ez.y + 0.11f, ez.z);
        glTexCoord2f(0.0f, 1.0f); glVertex3f(ez.x - 0.11f, ez.y + 0.11f, ez.z);
        glEnd();

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_TEXTURE_2D);
    }

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glEnable(GL_LIGHTING);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

/* ============================================================ 主绘制 */

static void renderScene3D(App *app) {
    Mat4 proj, view;

    proj = mat4Perspective(app->params.fovDeg,
                           (float)app->fbW / (float)maxf(1.0f, (float)app->fbH),
                           0.05f, 80.0f);
    view = playerViewMatrix(&app->player);

    glViewport(0, 0, app->fbW, app->fbH);

    glClearColor(0.055f, 0.062f, 0.082f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj.m);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view.m);

    /* 灯与雾必须在载入 view 之后设：GL 的灯位是在设置时被变换到眼坐标的，
       先设后载入 view 会让灯跟着相机转。*/
    setupLights(app);
    setupFog(app);

    sceneDrawRoom(&app->scene, &app->params);
    sceneDrawMarkings(&app->scene, &app->params);
    drawWallLamps(app);

    /* 顺序：颜料印 → 气球 → 爆散 → 曳光 → 手持装置。
       印子垫在最底下（它是"墙上留下的东西"），气球盖住它；
       爆散与曳光都是加色或带透明的，压在气球上面才对。*/
    drawWallDecals(app);
    drawBalloons(app, app->elapsed);
    drawPops(app);
    if (app->params.showTracer) drawTracer(app);
    drawWeapon(app);

    glDisable(GL_FOG);
}

/* 分辨率缩放：把刚画好的低分辨率画面拷成贴图，再铺满窗口。*/
static void renderBlit(App *app) {
    if (app->fbW == app->winW && app->fbH == app->winH) return;

    glBindTexture(GL_TEXTURE_2D, app->blitTex);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, app->fbW, app->fbH, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glViewport(0, 0, app->winW, app->winH);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f( 1.0f, -1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f( 1.0f,  1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f,  1.0f);
    glEnd();
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_LIGHTING);
    glEnable(GL_DEPTH_TEST);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void renderFrame(App *app) {
    renderUpdateCameraBasis(app);
    renderScene3D(app);
    renderBlit(app);
}

/* ============================================================ HUD
 *
 * 贴到窗口分辨率上画。分四块：
 *   左上 记分板 · 右上 场上状态 · 左下 操作提示 · 右下 帧率
 * 中间是准星，击破时在球的位置飘一个 "+25"。
 * 所有尺寸都按窗口高度缩放，1600x900 是基准，换分辨率不会跑版。
 */

/* ---------------------------------------------------------- 小工具 */

static Color4 c4(float r, float g, float b, float a) {
    Color4 c; c.r = r; c.g = g; c.b = b; c.a = a; return c;
}

/* ---------------------------------------------------------- 界面调色板
 *
 * ★ 整理。这批颜色原来是以 c4(r,g,b,a) 字面量**分散写在各绘制函数
 * 里**的：全文件数下来 80 种不同取值，其中 30 种出现了两遍以上，写得最多的
 * 那个"光标黄"出现了 7 次。后果不是难看，而是**改不动** —— 想统一调一下页脚
 * 那几行字的灰蓝，得先在几千行里认出哪几处说的是同一件事；漏一处，界面上就
 * 留下两种"看着一样、并排一比又不齐"的颜色。
 *
 * 这里只收**同一取值确有重复、且含义说得清**的那些（下面这 10 个覆盖了 41 处
 * 字面量）。只用过一两次的照旧就地写 —— 硬收进来就得给它起一个勉强的名字，
 * 反而比原样写着更难懂。
 *
 * 名字按"它在界面上是干什么的"起，不按颜色起：将来换一版配色，名字仍然成立，
 * 不会出现「蓝的那个现在是绿的」这种注释。
 *
 * ★ 取值**逐位照抄，一个都没动** —— 整理前后重出 34 张成品图，33 张 md5 逐字节
 * 相同；唯一不同的是 25 号（历史记录屏），差异只有 221 个像元（占全图 0.015%），
 * 全落在"时间 / 用时"那两列上 —— 那一张本来带实时钟，同一个 exe 隔三秒跑两次
 * 也差这么多。*/
static const Color4 COL_ROW_BG      = { 0.20f, 0.32f, 0.48f, 0.62f };  /* 选中行的底色 */
static const Color4 COL_CURSOR      = { 0.98f, 0.78f, 0.34f, 0.95f };  /* 光标黄：选中行左竖条 / 当前页黄条 / 选中的滑杆 */
static const Color4 COL_TEXT_SEL    = { 1.00f, 0.98f, 0.90f, 1.00f };  /* 选中行与当前页的名字 */
static const Color4 COL_TEXT_BODY   = { 0.80f, 0.86f, 0.94f, 0.95f };  /* 普通正文：未选中行的名字、右栏说明 */
static const Color4 COL_TEXT_VALSEL = { 1.00f, 0.92f, 0.62f, 1.00f };  /* 选中行的数值 */
static const Color4 COL_TEXT_VALUE  = { 0.72f, 0.82f, 0.90f, 0.92f };  /* 未选中行的数值 */
static const Color4 COL_TEXT_HINT   = { 0.78f, 0.86f, 0.95f, 0.95f };  /* 页脚提示行、历史记录屏的按键说明 */
static const Color4 COL_TEXT_TITLE  = { 1.00f, 0.96f, 0.86f, 0.98f };  /* 面板 / 结算 / 暂停 / 历史 的标题字 */
static const Color4 COL_ARROW       = { 0.72f, 0.82f, 0.94f, 0.92f };  /* 列表两端的 ﹀ ︿ */
static const Color4 COL_TRACK_TICK  = { 0.42f, 0.52f, 0.66f, 0.55f };  /* 滑杆两端的刻度竖线 */

/* 画一个纯色矩形。进出都显式关/开贴图，免得"当前到底是不是贴着图"
   变成一个要靠上下文猜的状态 —— 这种状态在两个函数之间传递最容易出
   那种"偶发变成纯色块"的怪毛病。*/
static void fillRect(float x, float y, float w, float h, Color4 col) {
    glDisable(GL_TEXTURE_2D);
    glColor4f(col.r, col.g, col.b, col.a);
    glBegin(GL_QUADS);
    glVertex2f(x, y); glVertex2f(x + w, y);
    glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glEnable(GL_TEXTURE_2D);
}

/* 选中行的两根横条：整行底色 + 左边那 4 像素的光标黄。
 *
 * 三处画法原来各写一遍（参数列表两处 —— 第 0 行与参数行、历史记录屏），
 * 其中两处宽度写成 `listW + 8u`、另一处写成 `bw * PARAM_SPLIT - 16u` ——
 * 两个算式算出来是同一个数，但写法不同，改一处、漏一处就会出现两种行宽。
 * 现在收成一个函数：调用方给"列表区宽度"，两侧出血与左缩进在这里统一加。
 *
 * **预设弹窗那一处没收进来**，看着像同一件事，其实不是：它的底色透明度是
 * 0.75（要压在底下的参数列表上，得比列表里的选中行更实），宽度也正好是
 * listW 而不是 listW + 8u。两处并排看是有区别的，合并就等于顺手改了画面。*/
static void drawSelRowBars(float bx, float ry, float u, float areaW, float rowH) {
    fillRect(bx + 14.0f * u, ry - 2.0f * u, areaW + 8.0f * u,
             rowH - 4.0f * u, COL_ROW_BG);
    fillRect(bx + 14.0f * u, ry - 2.0f * u, 4.0f * u,
             rowH - 4.0f * u, COL_CURSOR);
}

/* 世界坐标 → 屏幕像素。返回 0 表示这个点在相机背后，调用方应当跳过。
   用的是**和 3D 那一遍完全相同的投影矩阵**，所以飘分一定落在球上，
   不会因为两处各自算一套而错位。*/
static int projectToScreen(const App *app, Vec3 p, float *ox, float *oy) {
    Mat4 mvp = mat4Mul(mat4Perspective(app->params.fovDeg,
                                       (float)app->fbW / maxf(1.0f, (float)app->fbH),
                                       0.05f, 80.0f),
                       playerViewMatrix(&app->player));
    float x = mvp.m[0] * p.x + mvp.m[4] * p.y + mvp.m[8]  * p.z + mvp.m[12];
    float y = mvp.m[1] * p.x + mvp.m[5] * p.y + mvp.m[9]  * p.z + mvp.m[13];
    float w = mvp.m[3] * p.x + mvp.m[7] * p.y + mvp.m[11] * p.z + mvp.m[15];
    if (w < 0.02f) return 0;
    x /= w; y /= w;
    *ox = (x * 0.5f + 0.5f) * (float)app->winW;
    *oy = (1.0f - (y * 0.5f + 0.5f)) * (float)app->winH;
    return 1;
}

/* mm:ss 倒计时。与 formatSeconds 分开：那个会带小数和 "s" 后缀，
   做倒计时读起来别扭。*/
static void formatClock(float sec, wchar_t *out, int count) {
    int s;
    if (!out || count <= 0) return;
    if (sec < 0.0f) sec = 0.0f;
    s = (int)(sec + 0.5f);
    _snwprintf(out, (size_t)count, L"%d:%02d", s / 60, s % 60);
    out[count - 1] = L'\0';
}

/* ---------------------------------------------------------- 准星 */

static void drawCrosshair(App *app) {
    const Params *p = &app->params;
    float u   = (float)app->winH / 900.0f;
    float cx  = (float)app->winW * 0.5f;
    float cy  = (float)app->winH * 0.5f;
    float k   = p->crossScale * u;
    /* 开火时准星张开一点再收回来 —— 代价只有几行。*/
    float gap = (3.0f + app->fireFlash * 5.0f) * k;
    float len = 9.0f * k;
    float th  = maxf(1.0f, 1.8f * k);
    /* 颜色从调色板取。早先的做法是三个 0..1 的滑杆，面板上写着"准星红/绿/蓝"，
       调的人根本不知道调哪个好看；现在面板上是一个"准星颜色"，名字和颜色
       都写在 config.cpp 的 crossColorOf 里，只有那一份。*/
    Color4 col = color4(crossColorOf(p->crossColorIdx), 0.92f);
    int i;

    if (p->crossStyle == CROSS_DOTONLY) {
        float d = 3.0f * k;
        fillRect(cx - d * 0.5f, cy - d * 0.5f, d, d, col);
    } else if (p->crossStyle == CROSS_CIRCLE) {
        /* 圆环：用一圈短线段拼。半径跟着开火张开，比纯静态的准星"活"。*/
        float r = gap + len * 0.55f;
        float t = maxf(1.0f, 1.5f * k);
        glDisable(GL_TEXTURE_2D);
        glColor4f(col.r, col.g, col.b, col.a);
        glBegin(GL_QUADS);
        for (i = 0; i < 48; ++i) {
            float a0 = 2.0f * PI_F * (float)i / 48.0f;
            float a1 = 2.0f * PI_F * (float)(i + 1) / 48.0f;
            float x0 = cx + cosf(a0) * r, y0 = cy + sinf(a0) * r;
            float x1 = cx + cosf(a1) * r, y1 = cy + sinf(a1) * r;
            float dx = x1 - x0, dy = y1 - y0;
            float l = sqrtf(dx * dx + dy * dy);
            float nx = -dy / maxf(1e-4f, l) * t * 0.5f;
            float ny =  dx / maxf(1e-4f, l) * t * 0.5f;
            glVertex2f(x0 + nx, y0 + ny); glVertex2f(x1 + nx, y1 + ny);
            glVertex2f(x1 - nx, y1 - ny); glVertex2f(x0 - nx, y0 - ny);
        }
        glEnd();
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glEnable(GL_TEXTURE_2D);
        fillRect(cx - 1.5f * k, cy - 1.5f * k, 3.0f * k, 3.0f * k, col);
    } else {
        /* 十字 / 十字点：四根短线，中间留缺口。*/
        fillRect(cx - gap - len, cy - th * 0.5f, len, th, col);
        fillRect(cx + gap,       cy - th * 0.5f, len, th, col);
        fillRect(cx - th * 0.5f, cy - gap - len, th, len, col);
        fillRect(cx - th * 0.5f, cy + gap,       th, len, col);
        if (p->crossStyle == CROSS_DOT) {
            float d = 2.6f * k;
            fillRect(cx - d * 0.5f, cy - d * 0.5f, d, d, col);
        }
    }

    /* ---- 命中标记：四条斜线闪一下，明确告诉玩家"这一枪是打中的" ---- */
    if (app->hitFlash > 0.0f) {
        float a  = clampf(app->hitFlash, 0.0f, 1.0f);
        float r0 = (gap + 1.0f) * 1.15f;
        float r1 = (gap + 5.5f * k) * 1.15f;
        float t  = maxf(1.2f, 2.0f * k) * 0.5f;
        float d  = 0.7071f;
        int sx, sy;
        glDisable(GL_TEXTURE_2D);
        glColor4f(1.0f, 1.0f, 1.0f, a * 0.95f);
        glBegin(GL_QUADS);
        for (sy = -1; sy <= 1; sy += 2) {
            for (sx = -1; sx <= 1; sx += 2) {
                float ax = cx + (float)sx * r0 * d, ay = cy + (float)sy * r0 * d;
                float bx = cx + (float)sx * r1 * d, by = cy + (float)sy * r1 * d;
                float ddx = bx - ax, ddy = by - ay;
                float l = sqrtf(ddx * ddx + ddy * ddy);
                float nx = -ddy / maxf(1e-4f, l) * t;
                float ny =  ddx / maxf(1e-4f, l) * t;
                glVertex2f(ax + nx, ay + ny); glVertex2f(bx + nx, by + ny);
                glVertex2f(bx - nx, by - ny); glVertex2f(ax - nx, ay - ny);
            }
        }
        glEnd();
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glEnable(GL_TEXTURE_2D);
    }
}

/* ---------------------------------------------------------- 飘分 */

static void drawScorePopups(App *app) {
    const FxPool *fx = &app->fx;
    int i;

    for (i = 0; i < MAX_POPUPS; ++i) {
        const ScorePopup *q = &fx->popups[i];
        float t, sx, sy, px, a;
        Vec3 p;
        wchar_t buf[64];

        if (!q->active) continue;
        t = clampf(q->elapsed / maxf(1e-4f, q->duration), 0.0f, 1.0f);

        /* 往上飘 0.45 米，同时向玩家这边来一点，免得贴在墙上分不清层次。*/
        p = v3(q->pos.x, q->pos.y + 0.45f * t, q->pos.z + 0.30f * t);
        if (!projectToScreen(app, p, &sx, &sy)) continue;

        a  = (1.0f - t) * (1.0f - t);
        px = (18.0f + 9.0f * clampf((float)q->combo / (float)COMBO_MAX, 0.0f, 1.0f))
             * ((float)app->winH / 900.0f);
        /* 冒出的一瞬间有个放大：只是缩放，不用改字号之外的任何东西。*/
        px *= 1.0f + 0.35f * (1.0f - clampf(t * 5.0f, 0.0f, 1.0f));

        _snwprintf(buf, ARRAY_COUNT(buf) - 1, L"+%d", q->gain);
        buf[ARRAY_COUNT(buf) - 1] = L'\0';

        /* 先描一圈深色再画亮色：任何背景上都读得清，这是最省事的描边法。*/
        hudText(&app->hud, sx + 1.5f, sy + 1.5f, px, HUD_CENTER,
                c4(0.05f, 0.06f, 0.08f, a * 0.85f), buf);
        hudText(&app->hud, sx, sy, px, HUD_CENTER,
                c4(1.0f, 0.92f, 0.55f, a), buf);

        if (q->combo >= 3) {
            _snwprintf(buf, ARRAY_COUNT(buf) - 1, L"×%.2f",
                       (double)scoreComboMultiplier(q->combo));
            buf[ARRAY_COUNT(buf) - 1] = L'\0';
            hudText(&app->hud, sx + 1.5f, sy + px + 2.0f, px * 0.52f, HUD_CENTER,
                    c4(0.05f, 0.06f, 0.08f, a * 0.85f), buf);
            hudText(&app->hud, sx, sy + px + 1.0f, px * 0.52f, HUD_CENTER,
                    c4(0.70f, 0.94f, 1.00f, a), buf);
        }
    }
}

/* ---------------------------------------------------------- 记分板 */

static void drawPlayHud(App *app) {
    const HudFont *f = &app->hud;
    const Params *p = &app->params;
    float u = (float)app->winH / 900.0f;
    float W = (float)app->winW;
    wchar_t buf[128], buf2[128];
    int i;

    const Color4 kText = c4(0.94f, 0.96f, 0.98f, 0.96f);
    const Color4 kDim  = c4(0.66f, 0.70f, 0.76f, 0.92f);
    const Color4 kFill = c4(0.05f, 0.07f, 0.10f, 0.46f);
    const Color4 kEdge = c4(0.55f, 0.62f, 0.72f, 0.30f);

    /* ---- 左上：记分板 ---- */
    {
        float x = 22.0f * u, y = 18.0f * u, w = 322.0f * u, h = 150.0f * u;
        hudPanel(x, y, w, h, kFill, kEdge);

        /* 第一行：预设方案名。早先把"模式 · 难度"两个名词合并成了
           一个「预设方案」，所以这里就一行。名字只有 g_presets 那一份。*/
        _snwprintf(buf, ARRAY_COUNT(buf) - 1, L"%ls",
                   g_presets[clampi(p->preset, 0, PRESET_COUNT - 1)].name);
        buf[ARRAY_COUNT(buf) - 1] = L'\0';
        hudText(&app->hud, x + 14.0f * u, y + 12.0f * u, 19.0f * u, HUD_LEFT, kDim, buf);

        /* 分数：这一屏最大的字。连击时用脉冲把字号顶一下，
           不用加任何额外元素就能读出"正在连"的压力。*/
        _snwprintf(buf, ARRAY_COUNT(buf) - 1, L"得分 %d", app->score);
        buf[ARRAY_COUNT(buf) - 1] = L'\0';
        {
            float px = (32.0f + 5.0f * clampf(app->comboPulse, 0.0f, 1.0f)) * u;
            hudText(&app->hud, x + 14.0f * u, y + 38.0f * u, px, HUD_LEFT,
                    app->combo > 0 ? c4(1.0f, 0.94f, 0.66f, 0.98f) : kText, buf);
        }

        /* 连击：条 + 数字。条是"连击窗口还剩多久"，比数字更直观。
           板内四行的纵向排布（都是行顶，不是基线）：
             30u..49u 模式行  54u..91u 分数（字号会脉冲到 37u，按最大的留）
             106u..128u 连击  134u..139u 连击条  146u..162u 命中率
           板底 168u。曾经连击条和命中率都被摆到 140u —— 条被那行字整个盖住，
           看着像"连击条不工作"。这里把每一行都写成独立算式，不再共用 y。*/
        if (app->combo > 0) {
            float tx = x + 14.0f * u, ty = y + 88.0f * u;
            float bw = w - 28.0f * u;
            _snwprintf(buf, ARRAY_COUNT(buf) - 1, L"连击 %d  ×%.2f",
                       app->combo, (double)scoreComboMultiplier(app->combo));
            buf[ARRAY_COUNT(buf) - 1] = L'\0';
            hudText(&app->hud, tx, ty, 22.0f * u, HUD_LEFT,
                    c4(0.72f, 0.95f, 1.0f, 0.98f), buf);
            hudBar(tx, ty + 28.0f * u, bw, 5.0f * u,
                   c4(0.20f, 0.26f, 0.32f, 0.55f),
                   c4(0.45f, 0.85f, 1.00f, 0.95f),
                   app->comboLeft / maxf(1e-3f, p->comboWindowSec));
        } else {
            _snwprintf(buf, ARRAY_COUNT(buf) - 1, L"连击 0");
            buf[ARRAY_COUNT(buf) - 1] = L'\0';
            hudText(&app->hud, x + 14.0f * u, y + 88.0f * u, 22.0f * u,
                    HUD_LEFT, c4(0.50f, 0.55f, 0.62f, 0.85f), buf);
        }

        /* 命中率与出枪数：一行小字。*/
        _snwprintf(buf, ARRAY_COUNT(buf) - 1, L"命中 %d / 出枪 %d   命中率 %d%%",
                   app->hits, app->shots,
                   app->shots > 0 ? (int)(100.0f * (float)app->hits / (float)app->shots + 0.5f) : 0);
        buf[ARRAY_COUNT(buf) - 1] = L'\0';
        hudText(&app->hud, x + 14.0f * u, y + 128.0f * u, 16.0f * u, HUD_LEFT, kDim, buf);
    }

    /* ---- 右上：场上状态 ---- */
    {
        float w = 250.0f * u, h = 118.0f * u;
        float x = W - 22.0f * u - w, y = 18.0f * u;
        float tx, ty;

        hudPanel(x, y, w, h, kFill, kEdge);
        tx = x + 14.0f * u; ty = y + 12.0f * u;

        _snwprintf(buf, ARRAY_COUNT(buf) - 1, L"墙上 %d 个",
                   balloonActiveCount(&app->pool));
        buf[ARRAY_COUNT(buf) - 1] = L'\0';
        hudText(&app->hud, tx, ty, 20.0f * u, HUD_LEFT, kText, buf);

        /* 计时挑战才有倒计时。剩不到 10 秒转红。*/
        if (app->timeLeft > 0.0f) {
            formatClock(app->timeLeft, buf2, ARRAY_COUNT(buf2));
            _snwprintf(buf, ARRAY_COUNT(buf) - 1, L"剩余时间 %ls", buf2);
            buf[ARRAY_COUNT(buf) - 1] = L'\0';
            hudText(&app->hud, tx, ty + 30.0f * u, 20.0f * u, HUD_LEFT,
                    app->timeLeft < 10.0f ? c4(1.0f, 0.48f, 0.42f, 0.98f) : kText, buf);
        }

        if (p->infiniteLives) {
            hudText(&app->hud, tx, ty + 60.0f * u, 20.0f * u, HUD_LEFT, kDim, L"生命 无限");
        } else {
            _snwprintf(buf, ARRAY_COUNT(buf) - 1, L"生命 %d", app->lives);
            buf[ARRAY_COUNT(buf) - 1] = L'\0';
            hudText(&app->hud, tx, ty + 60.0f * u, 20.0f * u, HUD_LEFT,
                    app->lives <= 1 ? c4(1.0f, 0.48f, 0.42f, 0.98f) : kText, buf);
        }

        {
            int best = app->book.rec[clampi(p->preset, 0, PRESET_COUNT - 1)].score;
            _snwprintf(buf, ARRAY_COUNT(buf) - 1,
                       best > 0 ? L"历史最高 %d" : L"历史最高 暂无", best);
            buf[ARRAY_COUNT(buf) - 1] = L'\0';
            hudText(&app->hud, tx, ty + 88.0f * u, 16.0f * u, HUD_LEFT, kDim, buf);
        }
    }

    /* ---- 左下：操作提示 ----
     *
     * 这里两处的由来：
     *   1. "R 重置   Q 结束" 合成一句 "R 结算并开始新游戏" —— 那两个键在
     *      玩家眼里本来就是一回事（"这局不要了"），只留 R 一个。
     *   2. 整排**换了个颜色档位**：结算屏上这一排照旧要画，可那时候
     *      WASD / 鼠标 / 左键全都不响应了。
     *      原样的亮度会让人以为还能动，所以在结算屏上把整排压到 0.45 的
     *      不透明度，颜色档位和文字一个字都不改。*/
    {
        const wchar_t *keys[4] = { NULL, NULL, NULL, NULL };
        float tx = 22.0f * u;
        float ty = (float)app->winH - 30.0f * u;
        Color4 kLitKey, kDimKey;
        /* 结算屏与历史记录屏上，玩法输入一概不响应，所以这两屏要画暗。
           暂停屏不压暗：那一屏按 ESC 就回去了，键还是活的。*/
        int isSettle = (app->screen == SCREEN_SETTLE ||
                        app->screen == SCREEN_HISTORY);

        keys[0] = L"WASD 移动";
        keys[1] = L"鼠标 瞄准";
        keys[2] = L"左键 射击";
        /* ★ 「Tab 参数」改成「Tab 设置」。面板标题、
           设置列表、左下角提示三处现在都叫「设置」，一个东西一个名字。*/
        keys[3] = L"ESC 暂停   R 结算并开始新游戏   Tab 设置";

        if (isSettle) {
            /* 结算屏：整排压暗。0.45 是量出来的 —— 再暗就看不清字，
               再亮就还是会让人误以为按得动。*/
            kLitKey = c4(0.72f, 0.78f, 0.86f, 0.45f);
            kDimKey = c4(0.60f, 0.66f, 0.74f, 0.40f);
        } else {
            kLitKey = c4(0.72f, 0.78f, 0.86f, 0.86f);
            kDimKey = c4(0.60f, 0.66f, 0.74f, 0.78f);
        }

        for (i = 0; i < 4; ++i) {
            hudText(&app->hud, tx, ty, 16.0f * u, HUD_LEFT,
                    i == 3 ? kDimKey : kLitKey, keys[i]);
            tx += hudTextWidth(f, keys[i], 16.0f * u) + 20.0f * u;
        }
    }

    /* ---- 右下：帧率 ---- */
    {
        _snwprintf(buf, ARRAY_COUNT(buf) - 1, L"%.0f 帧/秒", app->fpsAvg);
        buf[ARRAY_COUNT(buf) - 1] = L'\0';
        hudText(&app->hud, W - 22.0f * u, (float)app->winH - 30.0f * u,
                16.0f * u, HUD_RIGHT, c4(0.60f, 0.66f, 0.74f, 0.80f), buf);
    }

    /* ---- 失手的红色边框：不必看清文字，扫一眼就知道刚才漏了一个 ---- */
    if (app->missFlash > 0.0f) {
        float a = clampf(app->missFlash, 0.0f, 1.0f) * 0.30f;
        glDisable(GL_TEXTURE_2D);
        glColor4f(0.95f, 0.20f, 0.18f, a);
        glBegin(GL_QUADS);
        {
            float b = 14.0f * u;
            float w = W, h = (float)app->winH;
            glVertex2f(0, 0); glVertex2f(w, 0); glVertex2f(w, b); glVertex2f(0, b);
            glVertex2f(0, h - b); glVertex2f(w, h - b); glVertex2f(w, h); glVertex2f(0, h);
            glVertex2f(0, b); glVertex2f(b, b); glVertex2f(b, h - b); glVertex2f(0, h - b);
            glVertex2f(w - b, b); glVertex2f(w, b); glVertex2f(w, h - b); glVertex2f(w - b, h - b);
        }
        glEnd();
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glEnable(GL_TEXTURE_2D);
    }
}

/* ---------------------------------------------------------- 暂停 */

/* ============================================================ 参数面板
 *
 * 它拆成两级：一级是菜单，具体参数放在二级。
 * 后来两边都收敛了一遍：
 *   一级 = 八条预设 + 「自定义参数」入口，共 9 行（声音那一栏整栏撤了）；
 *   二级 = 那张参数表，44 项归到「预设方案设置 / 用户偏好设置」两个大类下。
 * 右边一栏叫「选项说明」：一级里说光标停的那套预设怎么玩，二级里说光标停的
 * 那一项是什么意思。名字已经说清楚的项**不硬凑说明**（简单名词不必写），
 * 但也不留一片空白 —— 写一句"这一项没有额外说明"比看着像排版崩了强。
 */

/* 排版常量。滚动窗口在 app.cpp（收键盘的那边），可见行数由 paramPanelRows
   算出来给它 —— 两边用同一组常量，不许各写一份，否则调到最后一行时
   会看不见自己在调什么。*/
#define PARAM_ROW_H     30.0f     /* 一行的高度（以 900 参考高为准） */
#define PARAM_HEAD_H    64.0f     /* 标题栏 */
#define PARAM_FOOT_H   104.0f     /* 页脚：三行提示 + 一行消息 */
#define PARAM_MARGIN_Y  40.0f     /* 面板上下留白 */
#define PARAM_BW_MAX   1180.0f    /* 面板最大宽度（为了多一栏说明，比早先宽） */
#define PARAM_SPLIT     0.60f     /* 左栏（列表）占面板宽的比例 */
/* ★★ **删掉了** PARAM_GROUP_H 这个常量（它一直是 20）。
 *
 * 它原来单占一个数，是因为组标题条当初只占 20 像素、比参数行矮。后来把它
 * 抬到与参数行同高（为了滚动稳定，理由见下面「列表可见行数」那一段），两个数
 * 就必然相等了。**相等的东西要合并**：留着一个恒等于 PARAM_ROW_H 的常量，
 * 等于给"有人只改其中一个"留了一道门 —— 而这一对一旦走散，本项目的滚动账
 * （按"格"算）和绘制账（按像素推移）就各算各的，症状是光标又开始上下乱抖，
 * 且**自检抓不到**（它读的是格模型）。实测过：把组标题改回 20 像素高，
 * 3000 多条自检照样全绿。所以不是"改成同高"，是把这个数**去掉**。
 */
/* ★ 菜单栏（「预设方案设置」/「用户偏好设置」）的高度。
   它把列表里原来那两条 PARAM_SECTION_H 大字条搬了出来 —— 那两条从此
   不再进列表，也就不再参与行数预算；换成了这一条恒定占位的栏。*/
#define PARAM_MENUBAR_H 40.0f

/* ------------------------------------------------ 列表可见行数（重写，后补稳定）
 *
 * ★ 这一组函数是"画出来的行数"与"滚动窗口的行数"**唯一的来源**。
 *
 * 早先是两套算法：滚动窗口用 paramPanelRows 这个**保守值**（按"本页所有
 * 组标题都露在窗口里"预扣高度），绘制却走一步算一步地按几何判断。两者不相等 ——
 * 保守值偏小，于是"画出来的行数比窗口行数多"，表现就是那个毛病：
 * **光标还在画面中间，列表就整页往上跳了一格**（窗口以为到底了，其实没有）。
 *
 * 所以把两套算法合成一套：绘制循环与滚动窗口都调 paramPanelFit /
 * paramPanelFit，同一个 top 必然得到同一个行数。
 *
 * ---------------------------------------------------------------------------
 * ★★ 第二个毛病："滚动时栏目有点不稳定，光标选项会上下乱动"。
 * 实测（自检里的滚动台账）抓到两处，都在"窗口每滚一格、内容却不老老实实
 * 上移一行"上：
 *
 *   ① 光标的屏幕 y 会**倒退**。按 ↓ 时它从 570 跳到 540（往上跑 30 像素）——
 *      往下按、光标反而往上跳，这就是"上下乱动"。
 *   ② 组标题一进一出窗口，y 又抖 ±20 像素。
 *
 *   根因是**选项位不等高**：参数行 30、组标题 20，于是"从 top 起放得下几行"
 *   这个数会随 top 变。滚动窗口每推一格，能装的行数就可能少一行；少的那一行
 *   恰好是光标要落进去的那一行时，窗口只能再推一格 —— 窗口走两行、光标走一行，
 *   光标在屏幕上就倒退了一格。②是同一件事的余波：行高不一样，光标上面压着的
 *   组标题一变，它的 y 就跟着变。
 *
 *   修法三条，缺一不可：
 *
 *   · **每个选项位等高。** 组标题抬到与参数行同高（组标题条与参数行同高）。
 *     像素预算除以行高就是"几格"，与 top 无关 —— 容量成了常量，窗口每滚一格
 *     必然正好上移一格，光标老老实实待在原处。副产品：光标 y 只可能是行高的
 *     整数倍，再没有 20 像素的碎抖。
 *   · **窗口起点记"格"，不记"行"。** 见 paramTopSkipOf：一行可以占两格
 *     （组标题条 + 本行），起点允许落在块的中间，于是光标离窗口边沿的距离
 *     永远是个常量。起点只许落在块头上时，末段会白丢一格，光标往上跳 30 像素。
 *   · **上/下两条锚定规则都按"格"对齐。** 见 app.cpp 的 appParamScrollIntoView。
 *
 *   自检「参数面板：滚动稳定性」把这三条合成一句可判定的断言：**一路按 ↓，
 *   光标的屏幕 y 只许原地不动或往下走；一路按 ↑ 反之**。两页 × 四种窗口高度
 *   各走一遍。3000 条断言全绿而光标照跳 —— 它们查的全是状态，
 *   没有一条算屏幕坐标。
 * -------------------------------------------------------------------------*/

/* 参考窗口高度下的缩放系数。winH 为 0 时退到 1，免得除零。*/
static float paramPanelU(const App *app) {
    return (app && app->winH > 0) ? ((float)app->winH / 900.0f) : 1.0f;
}

/* 列表区的净高度（菜单栏下沿 → 页脚上沿）。与 drawParamPanel 里
   listTop / footY 用的是同一组常量，这里只是把它们合并成一个数。*/
static float paramListHeight(const App *app) {
    const float H = (app && app->winH > 0) ? (float)app->winH : 900.0f;
    const float u = H / 900.0f;
    return H - (2.0f * PARAM_MARGIN_Y + PARAM_HEAD_H + PARAM_MENUBAR_H
                + PARAM_FOOT_H) * u;
}

/* 这一行上面要不要顶着一条组标题（组标题跟着行一起滚，所以它占的高度
   会随 top 变 —— 这正是"必须走一步算一步"的原因）。规则与绘制循环里那句
   `grpBar` 完全一致，两处必须同时改。

   ★ 它不再是 static：自检要拿它算"光标行的屏幕 y"，才能断言
   "滚动时选中行不会上下乱跳"。让自检自己再抄一份行高规则，就是把
   刚合起来的"两个来源"又拆开一次 —— 吃过的亏不再吃第二遍。*/
int paramRowHasGroupBar(const App *app, int row) {
    const int idx = appParamRowToDesc(app ? app->paramPage : 0, row);
    if (idx < 0) return 0;                 /* 第 0 行「当前预设方案」上面没有组标题 */
    if (row == 0 || idx == 0) return 1;    /* 本页第一项参数：另起一组 */
    return g_paramDescs[idx].group != g_paramDescs[idx - 1].group;
}

/* ---- 格子 ---------------------------------------------------------------
 *
 * 列表的纵向空间按**格子**分，一格的像素高度就是 PARAM_ROW_H（组标题与参数行
 * 同高，见 PARAM_ROW_H 处那段说明）。第 N 行占的**不是**一格，而是它的"块"：
 * 带组标题的行占两格（上面那格是组标题条），不带的一格。
 *
 * 为什么要引入"格"这层：滚动稳不稳，取决于"窗口一次滚多少"。按行算的话，
 * 同样滚一行、消耗的格数却可能是 1 也可能是 2（撞上块头），窗口能装的行数
 * 就跟着变，光标在屏幕上就会挪位置。按格算，容量是个**常量**，光标贴哪一格
 * 就永远贴哪一格。
 * -----------------------------------------------------------------------*/

/* 第 row 行连它顶上的组标题条一共占几格。*/
int paramRowSpans(const App *app, int row) {
    return 1 + (paramRowHasGroupBar(app, row) ? 1 : 0);
}

/* 这一行占的**第一格**在整页里是第几格。*/
int paramItemIndexOfRow(const App *app, int row) {
    const int total = appParamPageRowCount(app ? app->paramPage : 0);
    int i, n = 0;

    if (row < 0) row = 0;
    if (row > total) row = total;
    for (i = 0; i < row; ++i) n += paramRowSpans(app, i);
    return n;
}

/* 本页一共几格。*/
int paramItemCount(const App *app) {
    return paramItemIndexOfRow(app, appParamPageRowCount(app ? app->paramPage : 0));
}

/* 第 item 格落在哪一行上（组标题条算它下面那一行的）。*/
int paramRowAtItem(const App *app, int item) {
    const int total = appParamPageRowCount(app ? app->paramPage : 0);
    int row, at = 0;

    if (item < 0) item = 0;
    for (row = 0; row < total; ++row) {
        const int span = paramRowSpans(app, row);
        if (item < at + span) return row;
        at += span;
    }
    return (total > 0) ? total - 1 : 0;
}

/* 第 0 行右边那个名字显示什么。命中八套预设之一就是它的名字，否则是
   「自定义参数」—— 与弹窗里最后那一条同一个常量（g_panelCustomName）。

   ★ 从绘制代码里提出来：原先它是 drawParamPanel 里一个三元表达式，
   字符串还是**又一处的字面量**。两个地方各写
   一份、只改一处就会在屏幕上留下两个叫法。提成函数之后，自检量的是
   **屏幕上会出现的那个字串本身**，而不是"匹配到第几套"这个状态 ——
   改名这类改动，状态断言一条都拦不住。*/
const wchar_t *paramRow0Name(const App *app) {
    const int matched = appParamMatchedPreset(app);

    if (matched < 0) return g_panelCustomName;
    return g_presets[clampi(matched, 0, PRESET_COUNT - 1)].name;
}

/* 内容区能装几格：总高度扣掉上下留白、页脚、菜单栏，再扣掉顶上那格（︿ 的位子），
   剩下的按格高除。**与 top 无关** —— 这就是滚动稳定的根据。*/
static int paramItemCapZero(const App *app) {
    const float u = paramPanelU(app);
    const float contentH = paramListHeight(app) - PARAM_ROW_H * u;
    int cap = (int)((contentH - 4.0f * u) / (PARAM_ROW_H * u));

    if (cap < 1) cap = 1;
    return cap;
}

/* 滚动锚定专用的容量：把 ﹀ 那一格**预扣掉**。
 *
 * 与 paramPanelItemCap 的差别只在末段 —— 末段 ﹀ 消失、容量多出一格，
 * 若锚定跟着用"实时容量"，光标就会在最后一屏往下跳一格。锚定钉在这个保守值
 * 上，多出来的那一格正好用来"让最后一项显示出来"，光标不动。
 * 它必然 ≤ 实时容量，所以按它锚定出来的光标一定在窗口里。
 *
 * ★ 这里试过补一条例外（光标停在最后一行时 +1、不预扣），
 *   理由写得很像回事：这样上一步与这一步会落在同一个起点上。**试完删掉了** ——
 *   把那半句去掉再跑自检，3049 项全绿，一条断言都动不了它，也就是
 *   说它对行为毫无影响。真正管住"末段不滚动"的是 paramPanelItemCap 里那条
 *   例外（光标到末行时窗口多出一格，光标因此本来就在窗口里，锚定提前返回、
 *   压根没走到这里）。留着那半句就是留一段没人管的代码，删掉。
 *   —— 一个反例：往"看起来该有"的地方加一行，先问它有没有人管得着。*/
int paramPanelSlotCap(const App *app) {
    int cap = paramItemCapZero(app) - 1;

    if (cap < 1) cap = 1;
    return cap;
}

/* 窗口顶行被滚掉了上面几格（0 或 1）。
 *
 * 只对 app->paramTop 那一行算数：窗口起点记的是"top 这一行 + 滚掉几格"，
 * 只有它可能停在块中间。别的行一律按"整块从块头起"算 —— 自检探几何时用的
 * 就是这种口径。越界（≥ 该行块宽）时当 0 处理，等于"这一行的组标题条还在"，
 * 宁可多画一条也不能少画一行。*/
int paramTopSkipOf(const App *app, int top) {
    int skip;

    if (!app || top != app->paramTop) return 0;
    skip = app->paramTopSkip;
    if (skip <= 0) return 0;
    if (skip >= paramRowSpans(app, top)) return 0;
    return skip;
}

/* 窗口起点落在**第几格**上。绘制、滚动、自检三处算坐标都从这里起。*/
int paramWindowStartItem(const App *app) {
    if (!app) return 0;
    return paramItemIndexOfRow(app, app->paramTop) + paramTopSkipOf(app, app->paramTop);
}

/* 容量规则本体：**只看窗口起点那一格**，不看光标、不看页。
 *
 * 单独抽出来有一个实打实的用处：滚动锚定要**试算**"如果把起点放到第 S 格，
 * 窗口装得下几格"——那一刻 app->paramTop 还是旧值，借道 paramPanelItemCap
 * 会问出一份对不上号的容量。规则只有这一处，锚定、绘制、自检问的是同一句话。
 *
 *   ① 顶上没有内容（起点就在第 0 格）时，那一格不必空着给 ︿ —— 反正这时不画
 *      ︿。让给真实行，于是"第一项时上箭头消失、第一项顶上来"和底下"﹀ 消失、
 *      最后一项顶上来"是对称的同一个规矩。
 *   ② 下面还有内容（这一屏装不满）就让一格给 ﹀。
 *
 * ★★ 这里改了两处：
 *
 *   判据 `<=` 改成 `<`。原来写的是"这一屏装不装得满"（拿**没让之前**的
 *   容量去比总格数，顶到表尾也算装得满），于是最后一屏照样先让出 ﹀ 那一格，
 *   最后一项要等再按一下才露面。要的正好相反：**滚到底之后，
 *   向上没开始滚之前，﹀ 一格都不许出现**。用 `<` 之后，最后一屏
 *   （`start + cap == itemTotal`，窗口末尾正好压住表尾）判"装得满、不让"，
 *   ﹀ 当场消失、最后一项顶上来 —— 就是"向下的箭头替换掉最后一项"。
 *   剩下的最后一步差异：`start = 16` 时 16+19 = 35 = 表尾，不让；再往上滚一格
 *   `start = 15` 时 15+19 = 34 < 35，让，﹀ 回来。**箭头出现的时刻 = 页面开始
 *   滚动的时刻**。
 *
 *   删掉看光标的那条例外（`if (app->paramSel != total - 1) cap -= 1`）。
 *   原来用它让末行那一格归光标，代价是**容量跟着光标走**：同一个窗口高度
 *   下按一下 ↑，窗口一格都没动，容量却从 19 掉到 18，﹀ 当场冒出来、最后一行
 *   被挤出去（这就是"箭头不稳定"的一半）。容量是**窗口**的
 *   属性，跟光标没有半点关系 —— 光标在不在窗口里，由锚定负责，不该由容量
 *   腾格子。删掉之后容量在同一屏里是常量。
 *
 *   连带作废的一条：原来"光标到倒数第二项时 ﹀ 还在"那条规则。它与
 *   "滚到底之后箭头不许出现"在同一个状态上要求相反（箭头在场时容量 18，
 *   要让最后一项露面需要 start ≥ 17，要让箭头露面需要 start ≤ 15，无解）。
 *   以这一条为准。*/
int paramPanelCapAtStart(const App *app, int start) {
    const int itemTotal = paramItemCount(app);
    int cap;

    if (!app) return 1;
    cap = paramItemCapZero(app);
    if (start == 0) cap += 1;
    if (start + cap < itemTotal) cap -= 1;
    if (cap < 1) cap = 1;
    return cap;
}

/* 窗口的内容区装得下几**格**（不含 ︿ 那格常驻位；含给 ﹀ 让的那一格）。
   同一页同一窗口高度、同一窗口位置下它是常量 —— 滚动锚定与自检都靠这个数。*/
int paramPanelItemCap(const App *app, int top) {
    const int total = appParamPageRowCount(app ? app->paramPage : 0);

    if (!app) return 1;
    if (top < 0) top = 0;
    if (top > total) top = total;
    return paramPanelCapAtStart(app,
                                paramItemIndexOfRow(app, top) + paramTopSkipOf(app, top));
}

/* 当前显示几行**真实选项**，以及上下箭头要不要画。
 *
 * 规则：格子从**窗口起点那一格**起算，装得下几格就是几格。下面还有内容时
 * **让出最后一格**给 ﹀（那一格画在哪儿由 paramArrowBotOffset 说了算，不在这
 * 里）；放满到底时不用让，那一格还给真实行 —— "﹀ 换成最后一项显示"
 * 就是这么落下来的，不需要额外分支。
 *
 * ★★ ︿ 在列表顶上那一格，与 ﹀ 逐字对称，两者都**占一个真实选项位**。
 * 它的出现条件与 ﹀ 对称：
 *
 *   起点 > 0（上面还有内容）→ 顶上那一格归 ︿；
 *   起点 = 0（上面没内容了）  → 不画 ︿，那一格还给第一行。
 *
 * 于是"第一项时上箭头消失、第一项顶上来"和"最后一项时下箭头消失、最后一项
 * 顶上来"是同一个规矩的两头，上箭头也是一个道理。
 *
 * ★ 注意"起点 = 0"这一屏比别的屏多一格：容量在这种时候**会**变（多一格），
 * 但光标不会因此跳 —— 滚动锚定用的是 paramPanelSlotCap 那个恒扣 ﹀ 的保守
 * 常量，多出来的那一格只会出现在光标**底下**，喂给"显示最后一项"。*/
/* 装填本体：从 top 起，整块整块地往窗口里塞，装得下几行就是几行。
 *
 * 两头各有一条"半个块"的规矩，补齐成对称的一对：
 *   顶行（条被滚出窗口上沿）→ 只数剩下那几格（skip），行照画、条不画；
 *   末行（条塞不进窗口底边）→ 行照进、条留在窗外（outClipRow 报给绘制）。
 * 末行那一条是新的。不补它，窗口底下就会空出一格 —— 而 ﹀ 钉在最底格，
 * 那一格正好落在"最后一行内容"和 ﹀ 中间，看着像漏画了一行。
 * 实测：不改这一条，预设页 328 个滚动位置里有 8 个带这种缝，而且**恰好**是
 * 「特殊气球」与「数量」两行（四个窗口高度都中）。
 *
 * 补上之后有一条干净的结论，写进了自检：
 *   **只要画 ﹀，内容就正好排到它头上，从不留缝**（内容占满容量）。*/
static int paramPanelPack(const App *app, int top, int *outClipRow,
                          int *outArrowTop, int *outArrowBot) {
    const int total = appParamPageRowCount(app ? app->paramPage : 0);
    const int itemTotal = paramItemCount(app);
    int cap, start, skip, row, end, r, clip = -1;

    if (outClipRow) *outClipRow = -1;
    if (!app) return 0;
    if (top < 0) top = 0;
    if (top > total) top = total;

    cap = paramPanelItemCap(app, top);
    skip = paramTopSkipOf(app, top);
    start = paramItemIndexOfRow(app, top) + skip;

    end = start;
    r = 0;
    for (row = top; row < total; ++row) {
        int span = paramRowSpans(app, row);
        if (row == top) span -= skip;
        if (end + span > start + cap) {
            /* 只剩一格、这一行却带着组标题条：行进窗口，条留在窗外。*/
            if (span == 2 && end + 1 <= start + cap) {
                end += 1;
                ++r;
                clip = row;
            }
            break;
        }
        end += span;
        ++r;
    }
    if (r < 1) r = 1;                 /* 窗口再小也得留得下一行 */

    if (outClipRow) *outClipRow = clip;
    if (outArrowTop) *outArrowTop = (start > 0) ? 1 : 0;
    if (outArrowBot) *outArrowBot = (start + cap < itemTotal) ? 1 : 0;
    return r;
}

int paramPanelFit(const App *app, int top, int *outArrowTop, int *outArrowBot) {
    return paramPanelPack(app, top, NULL, outArrowTop, outArrowBot);
}

/* 这一行的**组标题条**被窗口底边挡在窗外了吗。
 *
 * 只剩一格、而这一行还带着组标题条（两格）时，行照进窗口、条留在窗外 ——
 * 与顶行"条被滚出上沿"是同一件事的另一头（那一头由 paramTopSkipOf 管）。
 * 绘制拿它决定这一行画不画条；不这么办，窗口底下就会空出一格，而 ﹀ 钉在
 * 最底格，那一格正好落在"最后一行内容"和 ﹀ 中间，看着像漏画了一行。
 *
 * 判据只有 paramPanelPack 一处，绘制与它问的是同一句话。*/
int paramRowBarClipped(const App *app, int row) {
    int clip = -1;

    if (!app) return 0;
    paramPanelPack(app, app->paramTop, &clip, NULL, NULL);
    return (clip >= 0 && row == clip) ? 1 : 0;
}

/* ﹀ 画在列表区第几**格**上（从窗口顶端往下数）；不画时返回 -1。
 *
 * ★ 这个函数存在的唯一理由：**位置只有一个来源**。
 *   最初 ﹀ 画在"最后一行内容的正下方"（绘制循环里那个 ry），于是它跟着
 *   内容跑 —— 内容撞上"组标题条 + 它的行"这种塞不进窗口的两格块时会提前停
 *   一格，箭头就跟着上移一格（"向上顶了一栏"正是这一格），
 *   实测那两处就是「特殊气球」与「数量」两行（都是组标题条压在光标行头上）。
 *
 *   现在它画在**窗口起点格 + 容量**那一格上，与内容画了几行毫无关系。
 *   而且这样算出来必然落在列表区最后一格：
 *     起点 > 0 → 容量 = cap0-1，格位偏移 = 1 + (cap0-1) = cap0；
 *     起点 = 0 → 容量 = cap0+1-1，格位偏移 = 0 + cap0 = cap0。
 *   两条路都到 cap0。**"箭头不动"不是靠小心维持的，是算术上到不了别处**，
 *   这一条写进了自检（tPanelArrowEnds 末段）。*/
int paramArrowBotOffset(const App *app) {
    int arrow = 0;

    if (!app) return -1;
    paramPanelFit(app, app->paramTop, NULL, &arrow);
    if (!arrow) return -1;
    return ((paramWindowStartItem(app) > 0) ? 1 : 0)
           + paramPanelItemCap(app, app->paramTop);
}

/* 当前实际显示几行。滚动窗口、自检都用这一个数。*/
int paramPanelRows(const App *app) {
    if (!app) return 8;
    /* winH <= 0（自检里会这么试边界）时，两个几何函数各自退到 900 的参考值。*/
    return paramPanelFit(app, app->paramTop, NULL, NULL);
}

/* ---------------------------------------------------------- 自动换行
 *
 * 侧栏里的说明是一段话，写死了宽度就会在窗口变窄时顶出去。中文没有词
 * 边界，所以退化成"装得下就塞、装不下换行"——对中文来说这恰好就是正确
 * 的断行方式（不像英文那样会从单词中间断开）。
 *
 * 唯一的讲究：句号、逗号、右括号这类字符**不能落在行首**。遇到了就让
 * 它跟着上一行走（宁可那一行稍微超一点点），否则会出现"。"孤零零占一行。
 * 返回画完之后下一行的 y，方便调用方接着往下排。*/
static int hudNoLineStart(wchar_t c) {
    return c == L'。' || c == L'，' || c == L'、' || c == L'；' || c == L'：' ||
           c == L'！' || c == L'？' || c == L'）' || c == L'》' || c == L'」' ||
           c == L'』' || c == L'…';
}

/* ------------------------------------------------- 换行丢字的哨兵
 *
 * hudTextWrap 里曾有两个"静默吃掉文字"的毛病，修掉之后顺手加一道对账：
 * **画出去的那些行拼起来，必须和原文一字不差**。
 * 对不上就报一句 —— 出图脚本按 "[hud]" 抓告警并让退出码非零，所以以后再有人
 * 碰这个换行逻辑，丢字当场就会被拦下，不用靠人盯着截图看。
 *
 * 为什么值得单加一道哨兵：这个 bug 的症状是"每行行首被换成段落的第一个字"，
 * 读起来居然挺顺（"…后需重新" / "鼠定。"），很久都没人发现 —— 唯一的线索
 * 是拿放大镜去比原文。靠眼睛是靠不住的，得让机器来比。
 *
 * 只报一次：这个函数每帧都被调用，不这样会刷屏。*/
static void hudNoteWrapLoss(int at, wchar_t want, wchar_t got) {
    static int reported = 0;
    if (reported) return;
    reported = 1;
    printf("[hud] 警告：自动换行把文字画错了 —— 第 %d 个可见字符应是 U+%04X，"
           "画出来的是 U+%04X\n", at + 1, (unsigned)want, (unsigned)got);
    printf("      （右栏说明与历史记录里的那几段话会读不通，见 hudTextWrap）\n");
    fflush(stdout);
}

/* 把刚画出去的一行追加进对账缓冲。装不下就返回 cap 表示"放弃对账"，
   之后每次调用都保持这个值，不会越界写。*/
static int wrapAppendDrawn(wchar_t *dst, int cap, int dn, const wchar_t *src) {
    int i;
    for (i = 0; src[i]; ++i) {
        if (dn >= cap - 1) return cap;
        dst[dn++] = src[i];
    }
    return dn;
}

static float hudTextWrap(HudFont *h, float x, float y, float px, Color4 col,
                         const wchar_t *s, float maxW, float lineH, int maxLines) {
    wchar_t line[160];
    wchar_t drawn[512];        /* 画出去的内容，末尾与原文对账 */
    int n = 0, li = 0, dn = 0;
    const wchar_t *p = s;

    if (!h || !s || !s[0] || maxW <= 1.0f) return y;
    if (maxLines < 1) maxLines = 1;

    while (*p) {
        /* 硬换行。这段是补的：原来这个函数只按宽度折行、不认 \n，
           于是多行文案里的换行符就是一个看不见的普通字符，整段会被
           揉成一坨再按宽度重排 —— 历史记录右栏那十几行细账就是这么
           被压成三行的。\n 是调用方明确要的段落边界，必须原样落地；
           连续两个 \n 于是自然就成了空行。*/
        if (*p == L'\n' || *p == L'\r') {
            if (li >= maxLines) return y + (float)li * lineH;
            line[n] = L'\0';
            if (n > 0) {
                hudText(h, x, y, px, HUD_LEFT, col, line);
                dn = wrapAppendDrawn(drawn, (int)ARRAY_COUNT(drawn), dn, line);
            }
            ++li;
            y += lineH;
            n = 0;
            ++p;
            continue;
        }

        line[n] = *p;
        line[n + 1] = L'\0';
        /* 量一下"加上这个字之后"有多宽。超了就先把上一个字之前的内容画出去。*/
        if (n > 0 && hudTextWidth(h, line, px) > maxW) {
            if (hudNoLineStart(*p)) {
                /* 这个字不该落在行首 —— 收下它，让这一行稍微顶出去一点，
                   换行推迟到下一个字。★ 原先这里写的是 `++p; continue;`：
                   n 没往前走，下一轮 `line[n] = *p` 直接把它盖掉了 —— 等于把这个
                   标点**扔了**。文案行尾少一个句号读起来不别扭，所以一直没露馅。*/
                ++n;
                ++p;
                continue;
            }
            line[n] = L'\0';
            ++li;
            if (li >= maxLines) {
                /* 行数到顶：把最后一行末尾换成省略号，别让它悄悄截断 ——
                   截断看起来就像"说明本来就写到这儿"。这种截断是有意的，
                   后面那道对账就跳过（原文确实没被画全）。*/
                size_t L = wcslen(line);
                if (L >= 1) line[L - 1] = L'…';
                hudText(h, x, y, px, HUD_LEFT, col, line);
                return y + lineH;
            }
            hudText(h, x, y, px, HUD_LEFT, col, line);
            dn = wrapAppendDrawn(drawn, (int)ARRAY_COUNT(drawn), dn, line);
            y += lineH;
            n = 0;
            /* ★ 触发换行的这个字要**挪到新行的行首**。
               原先这里缺了这一句：`n = 0` 之后直接 `++n` 变成 1，
               于是 line[0] 还留着**上一行的第一个字** —— 每一行的行首都被换成
               段落的第一个字，而触发换行的那个字被丢掉了。症状是
               "鼠标位移到视角转角的换算系数。…后需重新" / "鼠定。"，
               读着居然挺顺，靠人是看不出来的。*/
            line[n] = *p;
            line[n + 1] = L'\0';
        }
        ++n;
        ++p;
    }
    if (n > 0) {
        line[n] = L'\0';
        hudText(h, x, y, px, HUD_LEFT, col, line);
        dn = wrapAppendDrawn(drawn, (int)ARRAY_COUNT(drawn), dn, line);
        y += lineH;
    }
    /* 对账：画出去的内容（去掉原文里的硬换行）必须与原文一字不差、一字不多。
       只在没超出对账缓冲时才有意义。*/
    if (dn < (int)ARRAY_COUNT(drawn)) {
        const wchar_t *q;
        int k = 0, bad = -1;
        for (q = s; *q; ++q) {
            if (*q == L'\n' || *q == L'\r') continue;
            if (k >= dn || drawn[k] != *q) { bad = k; break; }
            ++k;
        }
        if (bad < 0 && k != dn) bad = k;             /* 多画了字 */
        if (bad >= 0) {
            const wchar_t *q2;
            int k2 = 0;
            wchar_t want = L'?';
            for (q2 = s; *q2; ++q2) {
                if (*q2 == L'\n' || *q2 == L'\r') continue;
                if (k2 == bad) { want = *q2; break; }
                ++k2;
            }
            hudNoteWrapLoss(bad, want, (bad < dn) ? drawn[bad] : (wchar_t)0);
        }
    }
    /* 返回"下一行的 y"。原先这里写的是 y + li * lineH，把已经加进
       y 的换行又算了一遍 —— 只是当时唯一的调用方不看返回值，错了也没人
       发现。现在侧栏要接着往下排成绩表，这个数就得是准的。*/
    return y;
}

/* ---------------------------------------------------------- 侧栏说明
 *
 * 这里曾经分上下两段：上面是"当前预设（名字 + 一段说明）"，下面是
 * "光标所在那一项的说明"。后来把上面那段删掉了 ——
 * 理由站得住：一级菜单上光标划过某套预设时，下半段显示的就是**那一套**的
 * 说明，而"当前预设"那块显示的是**已经生效的那一套**，两块内容长得几乎一样
 * 却指着不同的东西，读起来要先分辨"这块说的是谁"，纯属自己给自己找麻烦。
 *
 * 于是只剩一段，标题定死叫「选项说明」（刻意不要"名词说明"
 * 这种术语，就说"这里是解释你选的东西的"）。历史记录界面共用这一个函数 ——
 * "显示格式和设置页面用同一个模板"，共用同一个函数是最硬的那种"一样"。
 */
/* 侧栏里的一行"标签 / 数值"。值先由调用方格式化好 ——
   这一层不认识分数、秒数、百分比，只负责把两边对齐排好。
   值直接开在结构体里而不是另外指一个串，是为了调用方能一行一个
   _snwprintf 填进去，不必再摆一堆临时数组。*/
typedef struct {
    const wchar_t *label;
    wchar_t        value[28];
} SideStat;

static void drawPanelSide(App *app, float x, float y, float w,
                          const wchar_t *body,
                          const SideStat *stats, int statCount) {
    const float u = (float)app->winH / 900.0f;
    float ty = y;
    int i;

    hudText(&app->hud, x, ty, 14.0f * u, HUD_LEFT, c4(0.50f, 0.60f, 0.74f, 0.95f),
            L"选项说明");
    ty += 22.0f * u;
    if (body && body[0]) {
        ty = hudTextWrap(&app->hud, x, ty, 15.0f * u, COL_TEXT_BODY,
                         body, w, 22.0f * u, 20);
    } else if (!stats || statCount <= 0) {
        /* 这一项没写说明（简单名词无需说明），也没有成绩表。
           留一整片空白看着像排版崩了，所以给一句灰字解释这里为什么是空的 ——
           它说的是"没有说明这件事"，不是"数量的说明 = 气球的数量"那种废话，
           所以不违背"宁缺毋滥"那条。*/
        ty = hudTextWrap(&app->hud, x, ty, 15.0f * u, c4(0.46f, 0.54f, 0.66f, 0.85f),
                         g_panelNoHelp, w, 22.0f * u, 20);
    }

    /* 成绩表：左标签、右数值，数值右对齐到侧栏右边。
       这与二级菜单里"参数名靠左、值靠右"是同一套排法 —— 所谓"和设置页面
       同一个模板"，说的是连列的位置都对得上，不只是外框长得像。*/
    if (stats && statCount > 0) {
        ty += 12.0f * u;
        for (i = 0; i < statCount; ++i) {
            if (!stats[i].label) continue;
            hudText(&app->hud, x, ty, 16.0f * u, HUD_LEFT,
                    c4(0.66f, 0.74f, 0.86f, 0.95f), stats[i].label);
            if (stats[i].value[0])
                hudText(&app->hud, x + w, ty, 16.0f * u, HUD_RIGHT,
                        c4(0.96f, 0.92f, 0.72f, 0.98f), stats[i].value);
            ty += 24.0f * u;
        }
    }
}

/* 二级菜单某一行的说明。按下的那一项没写说明就返回 NULL —— 侧栏空着，
   比写一句复述名字的废话好。*/
static const wchar_t *paramHelpOf(int idx) {
    if (idx < 0 || idx >= g_paramDescCount) return NULL;
    return g_paramDescs[idx].help;
}

/* ---------------------------------------------------------- 预设方案弹窗
 *
 * 预设方案浮层。列表第 0 行按回车把它拉出来，内容是**原封不动复用
 * g_paramMenu 那张表**（八套预设 + 一条入口），所以它长得和一级菜单
 * 一样 —— 二级选项卡的样式与一级菜单中的预设方案保持一致。
 *
 * 三条几何上的约定：
 *   ① **向下展开**，紧贴第 0 行的下沿，宽度与左栏列表一致；
 *   ② **画在最上层，直接遮住下面的选项**（不做推挤，底下那几十行参数不动）；
 *   ③ 不用右栏那一块地方 —— 右栏继续画「选项说明」，内容是**弹窗光标
 *      停着的那一套**的说明（显示高亮那套预设的说明）。
 *
 * 它必须在整张列表**画完之后**才调用，否则会被后面的行盖住。
 * 高度固定 = 8 行 × rowH + 上下 padding，一屏绝对放得下，所以不滚动。*/
static void drawPresetPopup(App *app, float bx, float listTop, float rowH,
                            float listW) {
    const float u = (float)app->winH / 900.0f;
    const float pad = 7.0f * u;
    float px, py, pw, ph, ry;
    int i;
    const int matched = appParamMatchedPreset(app);

    /* 左栏宽度由调用方那套算法给定（bw * PARAM_SPLIT - 24u）。这里不重算 ——
       重算就是第二处真相，改一处忘了另一处时弹窗和列表的边就不对齐了。*/
    pw = listW;
    px = bx + 14.0f * u;
    py = listTop + rowH;
    ph = (float)g_paramMenuCount * rowH + pad * 2.0f;

    /* 底 + 边：靠"比周围亮一档 + 一圈亮边"说明它浮在上面，
       不动用黄条 —— 黄条在这个界面里是"光标停在这一行"的专用记号。*/
    fillRect(px - 6.0f * u, py - 2.0f * u, pw + 12.0f * u, ph + 4.0f * u,
             c4(0.10f, 0.14f, 0.22f, 0.99f));
    hudPanel(px - 6.0f * u, py - 2.0f * u, pw + 12.0f * u, ph + 4.0f * u,
             c4(0.07f, 0.10f, 0.16f, 0.99f), c4(0.86f, 0.70f, 0.34f, 0.85f));

    ry = py + pad;
    for (i = 0; i < g_paramMenuCount; ++i) {
        const ParamMenuRow *m = &g_paramMenu[i];
        const int isPreset = (m->kind == PMENU_PRESET);
        const int sel = (i == app->paramPopSel);
        Color4 nameCol;
        float nameX;

        /* 入口行前面拉一条横线：它和上面七条不是一类东西。
           跟原来的一级菜单是同一个做法，为的是"样式一样"。*/
        if (m->kind == PMENU_CUSTOM) {
            fillRect(px + 10.0f * u, ry - 4.0f * u, pw - 20.0f * u, 1.0f * u,
                     c4(0.40f, 0.50f, 0.64f, 0.30f));
            ry += 10.0f * u;
        }

        if (sel) {
            fillRect(px, ry - 2.0f * u, pw, rowH - 4.0f * u,
                     c4(0.20f, 0.32f, 0.48f, 0.75f));
            fillRect(px, ry - 2.0f * u, 4.0f * u, rowH - 4.0f * u,
                     COL_CURSOR);
        }
        nameCol = sel ? COL_TEXT_SEL : COL_TEXT_BODY;
        nameX   = px + 38.0f * u;

        /* 实心点 = "现在就是这个状态"。判据跟列表第 0 行**共用同一个函数**
           （appParamMatchedPreset），不各算各的 —— 两处各算一遍，迟早会出现
           "第 0 行写着固定靶、弹窗里的点却在移动靶上"这种自相矛盾的画面。*/
        if (isPreset) {
            float dx = px + 20.0f * u;
            if (m->arg == matched) {
                fillRect(dx, ry + rowH * 0.5f - 8.0f * u, 8.0f * u, 8.0f * u,
                         c4(0.98f, 0.78f, 0.34f, 0.98f));
                fillRect(dx + 2.0f * u, ry + rowH * 0.5f - 6.0f * u, 4.0f * u, 4.0f * u,
                         c4(0.05f, 0.08f, 0.13f, 1.0f));
            }
            hudText(&app->hud, nameX, ry + 3.0f * u, 19.0f * u, HUD_LEFT, nameCol,
                    g_presets[clampi(m->arg, 0, PRESET_COUNT - 1)].name);
            if (m->arg == matched)
                hudText(&app->hud, px + pw - 12.0f * u, ry + 5.0f * u, 15.0f * u,
                        HUD_RIGHT, c4(0.60f, 0.72f, 0.84f, 0.90f), L"使用中");
        } else {
            /* 名字是具名常量（hud.cpp），不在这里硬写 —— 自检要量它。*/
            hudText(&app->hud, nameX, ry + 3.0f * u, 19.0f * u, HUD_LEFT, nameCol,
                    g_panelCustomName);
        }
        ry += rowH;
    }
}

/* 「大类标题条」从列表里搬进了菜单栏（见 drawParamMenuBar），
   原先画它的那个函数连同高度常量一并删掉 —— 这里不留它的墓碑。
   它带走的那条造型规矩仍然有效：**黄条只能是光标的记号**。*/

/* ---------------------------------------------------------- 组标题
 * 气球 / 运动 / 节奏 / 判定与计分 / 训练 / 玩家 / 画面 / 声音，各自独占一行。
 *
 * **这一行是为了修"小标题和选项重叠"。** 早先把组名
 * 右对齐画在**本组第一行**的空白里（`trackX0 - 10u`），于是"数量"右边写着
 * "气球"、"运动方式"右边写着"运动"、"存活时限"右边写着"节奏" ——
 * 那几行读起来像是"气球"是这一项的附属文字，而不是一个分组的标题。
 * 单独占一行就没有这个问题了，代价只是可见行数少一点（列表本来就能滚）。
 *
 * 造型上要和 drawSectionBar 明确区分（大字和小标题区分一下）：
 * 大类是"居中 + 22u + 亮白 + 左右两条短线"，组标题是
 * "左对齐 + 16u + 暗蓝 + 右侧拖一条长线"。 */
static void drawGroupBar(App *app, float x, float y, float w,
                         const wchar_t *name) {
    const float u = (float)app->winH / 900.0f;
    const float fs = 16.0f * u;
    const float lx = x + 12.0f * u;
    /* 这一格从 20 抬到 30（与参数行同高，见 PARAM_ROW_H 处那段说明），
       所以字要**在格子里竖直居中**，不能再贴着上沿画 —— 贴着画会在下面空出
       一条 12 像素的空白，看着像这一行没画完。那条装饰横线跟着字的中线走。*/
    const float ty = y + (PARAM_ROW_H * u - fs) * 0.5f;
    const float tw = hudTextWidth(&app->hud, name, fs);
    float ruleX = lx + tw + 10.0f * u;
    float ruleW = (x + w) - ruleX;

    hudText(&app->hud, lx, ty, fs, HUD_LEFT,
            c4(0.54f, 0.66f, 0.82f, 0.92f), name);
    /* 线太短时干脆不画 —— 画一条 2 像素的残线比不画更像渲染错误。*/
    if (ruleW > 12.0f * u)
        fillRect(ruleX, ty + fs * 0.5f - 0.5f * u,
                 ruleW, 1.0f * u, c4(0.36f, 0.46f, 0.60f, 0.30f));
}

/* ---------------------------------------------------------- 菜单栏
 *
 * ★ 那两条大字标题条（「预设方案设置」/「用户偏好设置」）
 * 原本是**列表里的行**，跟着参数一起滚；现在把它们拎出来，做成一根不滚、
 * 常驻的菜单栏。
 *
 * 三个造型上的取舍：
 *   · **只占左栏**，不横跨整块面板 —— 它切换的是左边这张列表，右栏的解释
 *     跟着列表走。横跨过去会压断中间那道分栏竖线，看着像另一层结构。
 *   · **当前页用一个亮底 + 一条下划线**，另一页只是暗字。这跟列表里"选中行"
 *     是同一套语言（亮底），区别在于选中行还多一道黄条 —— 黄条在这个界面里
 *     是**光标**的专用记号，所以光标真在菜单栏上时才画它。
 *   · 栏底画一条细线收口，否则它和下面第一行参数会糊在一起。
 *
 * 黄条只在 paramOnMenu 时出现，这一点很重要：光标在列表里的时候，
 * 菜单栏上的"当前页"只是**状态**（当前在哪一页），不是"光标在这儿"。*/
static void drawParamMenuBar(App *app, float x, float y, float w) {
    const float u = (float)app->winH / 900.0f;
    const float h = PARAM_MENUBAR_H * u;
    const float tabH = h - 6.0f * u;
    float tx = x + 4.0f * u;
    int i;

    fillRect(x, y, w, h, c4(0.07f, 0.10f, 0.16f, 0.92f));

    for (i = 0; i < PARAM_SECTION_COUNT; ++i) {
        const wchar_t *name = g_paramSectionNames[clampi(i, 0, PARAM_SECTION_COUNT - 1)];
        const int cur = (i == app->paramPage);
        const float tw = hudTextWidth(&app->hud, name, 18.0f * u);
        const float pw = tw + 28.0f * u;          /* 标签左右各留 14u 的内边距 */
        Color4 col;

        if (cur) {
            fillRect(tx, y + 3.0f * u, pw, tabH, c4(0.18f, 0.28f, 0.44f, 0.95f));
            /* 光标真在栏上时才画黄条 —— 与列表里的选中行同一套记号。*/
            if (app->paramOnMenu)
                fillRect(tx, y + 3.0f * u, 4.0f * u, tabH,
                         COL_CURSOR);
            /* 当前页的收口线用亮蓝，告诉人"这一页是打开的"。*/
            fillRect(tx, y + h - 3.0f * u, pw, 2.0f * u,
                     app->paramOnMenu ? COL_CURSOR
                                      : c4(0.44f, 0.66f, 0.94f, 0.90f));
        }

        /* 当前页的字最亮；另一页压暗，但仍然读得清 —— 它是"可去的另一处"，
           不是禁用项。光标在栏上时当前页再亮一档。*/
        if (cur) col = app->paramOnMenu ? COL_TEXT_SEL
                                        : c4(0.90f, 0.94f, 1.0f, 0.98f);
        else     col = c4(0.50f, 0.60f, 0.74f, 0.92f);

        hudText(&app->hud, tx + pw * 0.5f, y + 3.0f * u + (tabH - 21.0f * u) * 0.5f,
                18.0f * u, HUD_CENTER, col, name);
        tx += pw + 6.0f * u;
    }

    fillRect(x, y + h - 1.0f * u, w, 1.0f * u, c4(0.42f, 0.52f, 0.66f, 0.40f));
}

/* ---------------------------------------------------------- 设置列表
 * 菜单栏下面就是当前页的列表。**预设页**：第 0 行是「当前预设方案」，
 * 往下 32 项玩法参数；**偏好页**：直接就是那 12 项个人口味。两页都由
 * 组标题（气球 / 运动 / 节奏…）分段。左边列表，右边说明。
 *
 * "第 0 行"塞在列表最前面（「当前预设方案：固定靶」这一项在第一项），
 * 从此**列表行号 ≠ 参数下标**，两者差 1；后来行号又变成**页内行号**。
 * 换算只走 appParamRowToDesc / appParamPageRowCount，
 * 这里不自己减 1 —— 全项目散着写 `idx = sel - 1` 的话，加一行减一行的时候
 * 总有一处忘了跟。*/
static void drawParamPanelMenu(App *app, float bx, float by, float bw, float bh,
                               float listTop, float rowH, float listBottom) {
    const float u = (float)app->winH / 900.0f;
    const float listW = bw * PARAM_SPLIT - 24.0f * u;
    const float trackX0 = bx + bw * PARAM_SPLIT * 0.42f;
    const float valX    = bx + bw * PARAM_SPLIT - 24.0f * u;
    const float nameX   = bx + 40.0f * u;
    /* listBottom 由调用方**传进来**，就是页脚的上沿（drawParamPanel 里那个 footY）。
       ★ 这里出过一次实打实的错：原来这个值是在函数里自己算的 ——
           listTop + (bh - (PARAM_HEAD_H + PARAM_FOOT_H) * u)
       少扣了菜单栏那 40 像素，算出来的底边比页脚上沿低 40，于是列表把最后一行
       画进了页脚里（"漏球扣分"被那条分隔线拦腰切开，"关"两个字挂在页脚上），
       "下面还有"的 ↓ 箭头也跟着落到页脚中间。

       更值得记一笔的是**它是怎么被发现的**：自检 3001 条全绿、出图脚本一声不吭
       ——[panel] 哨兵只管"一行都没画出来"，管不着"画到了页脚里"。最后是出图
       之后肉眼在图上看出来的。把公式改回错的写法，自检照样 3001/0 全过、
       那一张图照样过哨兵 —— 这类断言本来就抓不到坐标错位。

       所以修法不是"再补一条断言"（这类断言本来就写不出来：坐标对不对，
       得有个坐标之外的参照物才判得了），而是**把坐标来源收成一个**：
       页脚上沿在 drawParamPanel 里算一次，列表底边就是它。*/
    /* 兜底哨兵：万一以后有人又给这个参数传了个"自己算的底边"，只要它越过
       页脚上沿就报 [panel] 警告 —— 出图脚本抓到 [panel] 就非零退出，
       与"列表画空了"那条走同一条路。*/
    if (listBottom > by + bh - PARAM_FOOT_H * u + 0.5f) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            printf("[panel] 警告：列表底边被算到了页脚里（底边 %.1f > 页脚上沿 %.1f）"
                   "—— 末尾几行会被页脚压住\n",
                   (double)listBottom, (double)(by + bh - PARAM_FOOT_H * u));
        }
    }
    const int rowCount = appParamPageRowCount(app->paramPage);
    /* ★ 光标在菜单栏上时，列表里**不画选中行**：焦点在栏上，底下的某一行
       还亮着黄条的话，屏幕上就有两处"光标"了。*/
    const int focusInList = !app->paramOnMenu;
    const int page = app->paramPage;
    const wchar_t *sideBody = NULL;
    int i;
    int painted = 0;
    int row0Drawn = 0;
    /* 两个"还有内容"的箭头要不要画。由 paramPanelFit 一次算出来（见下）：
       ︿ 画在列表**顶上那一格**，﹀ 画在列表让出来的**最后一格**上。
       ★ ︿ 那一格从菜单栏右端搬进了列表。位置由 paramPanelFit 在
       预算里预留（常驻），这里只负责在它该出现时把字画进去。*/
    int arrowTop = 0;
    int arrowBot = 0;
    /* 列表内容从哪儿起：窗口起点在第 0 格（上面没内容）时，顶上不留空 ——
       ︿ 反正不画，那一格归第一行。其余情况顶上那一格是 ︿ 的位子。*/
    float ry = listTop + ((paramWindowStartItem(app) > 0) ? PARAM_ROW_H * u : 0.0f);
    wchar_t buf[192];

    /* ---- 第 0 行：「当前预设方案」（只在预设页）------------------------
     *
     * 它排在**第一项**，且只在预设方案设置页 ——
     * 它拉出来的弹窗改的就是那 32 项玩法参数，摆在偏好页顶上名实不符。
     *
     * 右边那串字是**算出来的**，不是存的：每帧拿当前参数去和八套预设逐项
     * 比一遍（appParamMatchedPreset），全部对得上就写那一套的名字，
     * 否则写「自定义参数」。要的正是这个实时更新 ——
     * 以前那行黄色的「（已被修改）」已经整条删掉了。
     *
     * ★ 这一处原来也硬写了一个字面量。它和弹窗里那一条指的是同一个
     * 状态（"用的不是任何一套预设，是自己那套参数"），两处各写一个字符串，
     * 改名时只改一处、屏幕上就出现两个叫法。现在两边都取 g_panelCustomName。*/
    if (page == 0 && app->paramTop == 0) {
        const int matched = appParamMatchedPreset(app);
        /* `sel` 一律带上 focusInList：光标在菜单栏上时，底下的行谁都不许亮。*/
        const int sel = (app->paramSel == 0) && focusInList;
        const wchar_t *arrow = app->paramPopOpen ? L"▼" : L"▶";
        const float arrowW = hudTextWidth(&app->hud, arrow, 19.0f * u);
        const wchar_t *cur = paramRow0Name(app);
        Color4 nameCol, valCol;

        /* 这里原来顶一条「预设方案设置」大类标题条。后来把它整个搬进了
           菜单栏 —— 上面那根栏已经写着当前是这一页了，列表里再来一条
           就是同一句话说两遍。*/
        if (sel && focusInList)
            drawSelRowBars(bx, ry, u, listW, rowH);
        nameCol = sel ? COL_TEXT_SEL : COL_TEXT_BODY;
        /* 「自定义参数」是"什么都没匹配上"，和匹配到某套预设不是一回事，
           所以给它一个偏冷的颜色，扫一眼就能分出这两种状态。
           黄是光标色，这里不许用。*/
        valCol  = (matched >= 0)
                  ? (sel ? COL_TEXT_VALSEL : c4(0.86f, 0.90f, 0.98f, 0.95f))
                  : (sel ? c4(0.94f, 0.94f, 0.90f, 1.0f) : c4(0.68f, 0.74f, 0.84f, 0.92f));

        hudText(&app->hud, nameX, ry + 3.0f * u, 19.0f * u, HUD_LEFT, nameCol,
                L"当前预设方案：");
        /* ▶ 与方案名一起右对齐到 valX，两者间距 10u。
           ▶ / ▼ 表示"这里可以按回车拉出一层来；现在是收着还是开着"，
           位置在名字后面（"固定靶"几个字后面添一个向右的实心正三角形）。*/
        hudText(&app->hud, valX, ry + 3.0f * u, 19.0f * u, HUD_RIGHT, valCol, arrow);
        hudText(&app->hud, valX - arrowW - 10.0f * u, ry + 3.0f * u, 19.0f * u,
                HUD_RIGHT, valCol, cur);

        if (sel) sideBody = g_panelDetailHelp;

        ry += rowH;
        painted += 1;
        row0Drawn = 1;
    }

    /* ★ 画几行、滚动窗口认几行 —— **是同一个数**（paramPanelFit）。
     *
     * 早先这里是两套：滚动窗口用一个"最坏情况"的保守值（把本页全部
     * 组标题都预扣掉），绘制走一步算一步地按几何判断。保守值偏小 → 画出来的
     * 行比窗口认得多 → 光标还在画面中间，列表就提前整页跳一格。另一种
     * "底下空两百像素"是同一个病根的另一个方向。
     *
     * 现在两边都调 paramPanelFit，同一个 paramTop 必然得到同一个行数；
     * 那种"画到哪儿算哪儿"的注释到这里终于可以删掉了。下面这个 need 判断
     * **不再是决定画几行的依据**（行数由 fit 说了算），只做最后一道保险：
     * 万一以后有人改了行高常量而忘了同步几何函数，这里会拦住，别画进页脚。
     *
     * 另外 fit 已经把"下面还有内容"的那一格让给了 ﹀（见 paramPanelFit 的
     * 说明），所以循环画到 painted == fit 就该停 —— 让出来的那一格由下面
     * 单独画箭头。*/
    {
    const int fit = paramPanelFit(app, app->paramTop, &arrowTop, &arrowBot);

    for (i = 0; app->paramTop + i < rowCount && painted < fit; ++i) {
        int row = app->paramTop + i;
        int idx = appParamRowToDesc(page, row);
        const ParamDesc *d;
        int sel, isNum, grpBar;
        Color4 nameCol, valCol;
        float valW, trackEnd, need;

        /* ★ 这里是 continue，不是 break。预设页第 0 行没有对应的参数项
           （appParamRowToDesc 返回 -1），而它只有在 paramTop == 0 时才落在
           循环的第一圈上 —— 上面已经单独画过了，跳过它接着画第 1 行。
           写成 break 的话，正常打开面板（paramTop 就是 0）会**在第一圈就
           退出整个循环**：整个参数列表一条都画不出来，只剩第 0 行孤零零
           挂在那儿。出图时被 17 号图抓到的就是这个，自检抓不到 ——
           它只断言输入与状态，管不着画了多少行。*/
        if (idx < 0) continue;

        d = &g_paramDescs[idx];
        /* 组标题画在"本页第一项参数"和"换组的那一行"上。
           ★ 大类标题条已经整个挪走了（进菜单栏），所以这里不再有 secBar；
           而"本页第一项"必须单独判 —— 偏好页的第一项（下标 30）在**全表**里
           是接着上一组的下标 29 的，只看 `group != 前一项的 group` 的话，
           切到偏好页会一条组标题都不画（前一项正好是同组的）。*/
        /* `idx == 0` 那半句管预设页（它的第一项参数是 row 1、idx 0），
           `row == 0` 那半句管偏好页（它的第一项参数就在 row 0、idx 30）。
           少了任一半，对应那页的第一组就会没有组标题。*/
        grpBar = (row == 0) || (idx == 0) || (d->group != g_paramDescs[idx - 1].group);
        /* ★ 窗口顶行如果被滚掉了上面一格，滚掉的正是它的组标题条 ——
           那一格已经跑到窗口上沿外面去了，这里就不能再画。画了的话，一行会
           占掉两格而窗口只按一格给它排的位置，底下整片都跟着往下错 30 像素。*/
        if (row == app->paramTop && paramTopSkipOf(app, app->paramTop)) grpBar = 0;
        /* ★ 窗口底下还剩一格、这一行的条塞不进时，行照画、条不画 ——
           与顶行那条是同一件事的两头（见 paramPanelPack）。少了这一句，这一行
           会按两格排而实际占一格，底下整片又得往下错 30 像素。*/
        if (paramRowBarClipped(app, row)) grpBar = 0;

        /* 保险丝。多留 4 像素：选中行的底色是上下各撑 2 像素画的。
           走到这里就 break，说明 paramPanelFit 算得比实际画得下的多 ——
           两边的行高常量走散了。喊一声（[panel] 字头，出图脚本认它），
           别一声不吭把行画进页脚里。*/
        need = rowH + (grpBar ? rowH : 0.0f);          /* 组标题条与行同高 */
        if (ry + need + 4.0f * u > listBottom) {
            static int warned = 0;
            if (!warned) {
                warned = 1;
                printf("[panel] 警告：窗口认了 %d 行，实际只画得下 %d 行"
                       "（paramTop=%d）—— 行高常量与 paramPanelFit 走散了\n",
                       fit, painted, app->paramTop);
            }
            break;
        }
        painted += 1;

        sel = (row == app->paramSel) && focusInList;
        isNum = (d->kind == PK_FLOAT || d->kind == PK_INT);

        /* 换组时另起一行画组标题。四十来行不分段会糊成一片，
           看不出自己已经调到哪一组去了。*/
        if (grpBar) {
            drawGroupBar(app, bx + 26.0f * u, ry, listW - 6.0f * u,
                         g_paramGroupNames[clampi(d->group, 0,
                                                  PARAM_GROUP_COUNT - 1)]);
            ry += rowH;
        }

        if (sel) drawSelRowBars(bx, ry, u, listW, rowH);

        nameCol = sel ? COL_TEXT_SEL : COL_TEXT_BODY;
        valCol  = sel ? COL_TEXT_VALSEL : COL_TEXT_VALUE;

        hudText(&app->hud, nameX, ry + 3.0f * u, 19.0f * u, HUD_LEFT, nameCol, d->name);

        paramDescText(&app->params, d, buf, (int)ARRAY_COUNT(buf));
        /* 数值先量宽，滑杆右端再按它让开（修的是"滑杆和参数值重叠"）。
           早先把滑杆右端写死成 栏宽 - 96u，而数值是从 valX 往左排的 ——
           「20260930」「0.00 米/秒」这种长数值左端会**压到杆上**。
           现在改成"数值先摆好，再决定杆能画到哪儿"，数值多长都不会叠。*/
        valW = hudTextWidth(&app->hud, buf, 19.0f * u);
        hudText(&app->hud, valX, ry + 3.0f * u, 19.0f * u, HUD_RIGHT, valCol, buf);
        trackEnd = valX - valW - 14.0f * u;
        if (trackEnd < trackX0 + 24.0f * u) trackEnd = trackX0 + 24.0f * u;

        /* **删掉了**这一行原来那句黄色的「（已被修改）」，改由列表第 0 行的
           「当前预设方案」统一反映 ——
           整套参数对上某一套预设就报它的名字，对不上就报「自定义参数」。
           那行字是在"逐项"层面说的，第 0 行是在"整套"层面说的；
           留后者，前者反而是两处口径。*/

        /* 数值项画一根细条，把"当前值在区间里的位置"直接摆出来。
           一屏二十来行，一眼就能看出哪些项被拉到了两头。*/
        if (isNum) {
            float v = paramDescGet(&app->params, d);
            float t = (d->hi > d->lo) ? (v - d->lo) / (d->hi - d->lo) : 0.0f;
            float ty = ry + rowH * 0.5f - 2.0f * u;
            t = clampf(t, 0.0f, 1.0f);
            fillRect(trackX0, ty, trackEnd - trackX0, 4.0f * u, c4(0.16f, 0.22f, 0.32f, 0.85f));
            fillRect(trackX0, ty, (trackEnd - trackX0) * t, 4.0f * u,
                     sel ? COL_CURSOR : c4(0.42f, 0.62f, 0.86f, 0.85f));
            /* 刻度：两端各一小竖线，"头在哪儿"有个参照。*/
            fillRect(trackX0, ty - 2.0f * u, 1.0f * u, 8.0f * u, COL_TRACK_TICK);
            fillRect(trackEnd - 1.0f * u, ty - 2.0f * u, 1.0f * u, 8.0f * u,
                     COL_TRACK_TICK);
        }

        if (sel) sideBody = paramHelpOf(idx);
        ry += rowH;
    }

    /* ---- 让出来的那一格：﹀ ----------------------------------------------
     *
     * ★ 上下箭头是"上面/下面还有选项"的指示，而且**占一个
     * 真实选项位**。paramPanelFit 已经把"下面还有"的那一格让出来了
     * （容量比没让之前少 1），这里把 ﹀ 画进那一格。
     *
     * ★★ 它的位置从 `ry`（最后一行内容的正下方）改成
     * **窗口起点格 + 容量**那一格 —— 也就是列表区最后一格，一个与"内容画了
     * 几行"无关的定值。位置由 paramArrowBotOffset 一处给出，绘制与自检问的是
     * 同一个函数（原先那种"位置跟着内容跑"的毛病，就是因为它藏在绘制循环的
     * 局部变量里，自检看不到）。
     *
     * 到末段它自己会消失：窗口末尾压住表尾时容量不扣，arrowBot = 0，那一格
     * 还给真实行 —— "﹀ 换成最后一项显示"就是这么落下来的。*/
    if (arrowBot) {
        const float arrowX = bx + 14.0f * u + (listW + 8.0f * u) * 0.5f;
        const int arrowOff = paramArrowBotOffset(app);
        const float arrowY = listTop + (float)arrowOff * rowH;

        /* 与上面那条保险丝同理：箭头的位置是算出来的，算错了不许一声不吭地
           画到列表外面去（出图脚本认 [panel] 这个字头）。*/
        if (arrowY + rowH + 4.0f * u > listBottom) {
            printf("[panel] 警告：﹀ 的格位 %d 越出列表底边（paramTop=%d，容量=%d）"
                   "—— 容量规则与绘制走散了\n",
                   arrowOff, app->paramTop, paramPanelItemCap(app, app->paramTop));
        }
        hudText(&app->hud, arrowX, arrowY + 2.0f * u, 22.0f * u, HUD_CENTER,
                COL_ARROW, L"﹀");
    }
    }   /* ← 关掉"画几行 = fit"那个块 */

    /* ---- 自证：列表区空着一大片，参数行却一条都没画出来 ----------------
     *
     * 这一条是被打脸之后补的。第一轮出图的 17 号图拍出来是这样的：
     * 第 0 行「当前预设方案」孤零零挂在最上面，底下四十一条参数**一条都没有**。
     * 根因是循环里那句"第 0 行跳过"写成了 `break` —— 而打开面板时
     * paramTop 恰好就是 0，第一圈撞上第 0 行，整个循环当场结束。
     *
     * 自检抓不到它：三十三组断言全是在输入与状态上做文章，没有一条看得见
     * "画了几行"。这种"状态全对、画面全白"的故障只能靠看图或者像这样
     * 在绘制层自己喊一声。所以在这里立个哨兵：**还放得下整行却一行没画**
     * 就是出事了，喊到控制台，出图脚本认这个字头（[panel]）。
     *
     * 判据里带 `listBottom - ry >= rowH` 那一半，是为了不冤枉"窗口真的满了"
     * 这种正常情况：那时剩余空间不足一行，一声不吭是对的。*/
    if (painted - row0Drawn == 0 && listBottom - ry >= rowH) {
        static int warned = 0;
        if (!warned) {
            warned = 1;                  /* 一帧喊一次就够了，一秒钟喊六十次没人看 */
            printf("[panel] 警告：列表区还空着 %.0f 像素（够放一整行），"
                   "却一行参数都没画出来（paramTop=%d）—— 绘制循环提前退出了\n",
                   (double)(listBottom - ry), app->paramTop);
        }
    }

    /* ---- 上面还有：︿（占列表顶上那一格）---------------------------------
     *
     * 早先写的是"上面还有 / 下面还有"两句话，太罗嗦 —— 一个箭头谁都
     * 知道是什么意思。字形也换过：要的是"单书名号那种箭头"。
     *
     * ★★ 它从菜单栏右端搬到了这里。之所以先待在菜单栏右端，
     * 是怕"上下都占格"把末段挤垮；但那个位置"很奇怪" ——
     * 上箭头和下箭头是一对，一个在列表里、一个飘在
     * 角落里，本来就不像一对。搬下来之后两个箭头同宽、同列、同字形，
     * 一头一尾把列表夹在中间。
     *
     * 挤垮末段那个担心，真正的病根是"选项位不等高"，跟箭头没关系 ——
     * 组标题抬到与参数行同高之后就自动化解了，顺带把"滚动时光标
     * 上下乱动"也一起修了。账见上面「列表可见行数」那一段。
     *
     * ★ 它与 ﹀ 对称：上面没有内容了（窗口起点 = 0）就不画，那一格还给第一行。
     * 位置固定在列表顶上那一格，不跟着内容走 —— 它是"还有更多"的指示，
     * 不是某一行的附属品。
     *
     * 横位置与 ﹀ 完全一致（同一个算式），一行字的宽度里上下对齐。*/
    if (arrowTop) {
        const float arrowX = bx + 14.0f * u + (listW + 8.0f * u) * 0.5f;
        hudText(&app->hud, arrowX, listTop + 4.0f * u, 22.0f * u, HUD_CENTER,
                COL_ARROW, L"︿");
    }

    /* ★ 光标在菜单栏上时，右栏说的是**这一页是干什么的**。
       底下没有哪一行是被选中的，拿别的文字糊过去都是驴唇不对马嘴；
       而"哪些参数会被预设覆盖、哪些不会被"正是分这两页的原因。*/
    if (app->paramOnMenu)
        sideBody = g_panelSectionHelp[clampi(page, 0, PARAM_SECTION_COUNT - 1)];

    /* ---- 预设方案弹窗：**最后画**，压在整张列表上面 --------------------
     *
     * 它往下展开、处于顶层，直接遮住下面的选项，关闭二级窗口后才回到列表。
     * 所以它不是右栏里的一块，而是在左栏列表区上浮一层 —— 顺序上必须排在
     * 所有行之后，否则会被后面画的参数行盖掉。*/
    if (app->paramPopOpen) {
        drawPresetPopup(app, bx, listTop, rowH, listW);
        /* 右栏这时解释的是**弹窗里高亮的那一套**，
           而不是光标底下压着的那条参数 —— 参数这会儿被盖着，
           解释它没有意义。落在「自定义参数」上就解释「自定义参数」。*/
        if (app->paramPopSel >= 0 && app->paramPopSel < PRESET_COUNT)
            sideBody = g_presets[clampi(app->paramPopSel, 0, PRESET_COUNT - 1)].desc;
        else if (app->paramPopSel == g_paramMenuCount - 1)
            sideBody = g_panelCustomHelp;
    }

    drawPanelSide(app, bx + bw * PARAM_SPLIT + 10.0f * u, listTop,
                  bw * (1.0f - PARAM_SPLIT) - 34.0f * u,
                  sideBody, NULL, 0);
}

static void drawParamPanel(App *app) {
    const float u = (float)app->winH / 900.0f;
    const float W = (float)app->winW, H = (float)app->winH;
    float bw = W - 120.0f * u;
    float bh = H - 2.0f * PARAM_MARGIN_Y * u;
    float bx, by, listTop, rowH;
    wchar_t buf[192];

    if (bw > PARAM_BW_MAX * u) bw = PARAM_BW_MAX * u;
    if (bw < 420.0f * u) bw = 420.0f * u;
    bx = (W - bw) * 0.5f;
    by = (H - bh) * 0.5f;
    rowH = PARAM_ROW_H * u;
    /* ★ 标题栏下面先让出一根**菜单栏**，列表从它底下开始。
       这一行就是把"两个分类变成标签栏"这件事落到坐标上的地方；
       paramPanelRows 也按同一个常量扣高度，两边不会走散。*/
    listTop = by + PARAM_HEAD_H * u + PARAM_MENUBAR_H * u;
    /* 页脚上沿。算在这里、交给下面的列表当底边用 —— 列表和页脚各算一套的话，
       迟早会像早先第一稿那样差出 40 像素来（最后一行被画进页脚里）。*/
    const float footY = by + bh - PARAM_FOOT_H * u;

    /* 整屏压暗。用 0.66 而不是全黑：底下的训练场还看得见，
       调画面类参数（墙面风格、准星、曳光）时能立刻对照效果。*/
    fillRect(0.0f, 0.0f, W, H, c4(0.02f, 0.03f, 0.05f, 0.66f));
    hudPanel(bx, by, bw, bh, c4(0.045f, 0.062f, 0.10f, 0.94f),
             c4(0.62f, 0.70f, 0.82f, 0.50f));

    /* ---------------------------------------------------------- 标题栏 */
    hudText(&app->hud, bx + 22.0f * u, by + 16.0f * u, 27.0f * u, HUD_LEFT,
            COL_TEXT_TITLE, L"设置");
    {
        /* 右上角那行字说明"光标现在在哪儿"。只有一层列表时不存在
           "怎么回去"这回事，一律是"第几项 / 共几项"；加了菜单栏与两页之后，
           于是"共几项"按**本页**算，光标在栏上时根本不报项号（它不是列表里
           的位置）。
           ★ 末尾那句键提示也跟着状态换：弹窗开着时 ESC 收的是**弹窗**，
           还写"ESC 关闭"就是在说一件按下去不会发生的事。原则是
           "当前有哪些快捷键才显示哪些提示"，标题栏同样算数。*/
        const int descIdx = appParamRowToDesc(app->paramPage, app->paramSel);
        const int total   = appParamPageRowCount(app->paramPage);
        if (app->paramOnMenu) {
            _snwprintf(buf, ARRAY_COUNT(buf) - 1, L"菜单栏");
        } else if (app->paramPopOpen) {
            _snwprintf(buf, ARRAY_COUNT(buf) - 1,
                       L"预设方案弹窗 · 第 %d / %d 项    ESC 收起列表",
                       app->paramPopSel + 1, g_paramMenuCount);
        } else if (descIdx < 0) {
            /* 预设页第 0 行。它不是第 1 项参数，但排在列表第一位，
               所以按**行号**报「第 1 / 33 项」，并点出页名。*/
            _snwprintf(buf, ARRAY_COUNT(buf) - 1, L"%ls · 第 %d / %d 项",
                       g_paramSectionNames[clampi(app->paramPage, 0,
                                                  PARAM_SECTION_COUNT - 1)],
                       app->paramSel + 1, total);
        } else {
            const ParamDesc *d = &g_paramDescs[clampi(descIdx, 0, g_paramDescCount - 1)];
            _snwprintf(buf, ARRAY_COUNT(buf) - 1, L"%ls · 第 %d / %d 项",
                       g_paramGroupNames[clampi(d->group, 0, PARAM_GROUP_COUNT - 1)],
                       app->paramSel + 1, total);
        }
        buf[ARRAY_COUNT(buf) - 1] = L'\0';
        hudText(&app->hud, bx + bw - 22.0f * u, by + 19.0f * u, 20.0f * u,
                HUD_RIGHT, c4(0.66f, 0.76f, 0.88f, 0.95f), buf);
    }
    fillRect(bx + 18.0f * u, by + PARAM_HEAD_H * u - 8.0f * u,
             bw - 36.0f * u, 1.0f * u, c4(0.42f, 0.52f, 0.66f, 0.35f));

    /* 中间那道竖线：左右两栏的分界。有它才看得出右边那一段是"说明"
       而不是"这一行的补充信息"。

       ★ 它从标题栏下沿往下挪到菜单栏下沿：菜单栏只占左栏，
       竖线要是从标题栏就开始画，会在菜单栏右侧留下一段"穿过去"的线头，
       看着像菜单栏被截断了。从 listTop 起画，两栏的分界才和内容对齐。*/
    fillRect(bx + bw * PARAM_SPLIT, listTop - 4.0f * u,
             1.0f * u, bh - PARAM_HEAD_H * u - PARAM_MENUBAR_H * u
                      - PARAM_FOOT_H * u + 8.0f * u,
             c4(0.42f, 0.52f, 0.66f, 0.30f));

    /* 菜单栏本身。画在列表之前 —— 它是左栏最上面那一块。*/
    drawParamMenuBar(app, bx + 14.0f * u, listTop - PARAM_MENUBAR_H * u,
                     bw * PARAM_SPLIT - 24.0f * u);

    drawParamPanelMenu(app, bx, by, bw, bh, listTop, rowH, footY);

    /* ---------------------------------------------------------- 页脚 */
    {
        const float fy = footY;
        fillRect(bx + 18.0f * u, fy + 6.0f * u, bw - 36.0f * u, 1.0f * u,
                 c4(0.42f, 0.52f, 0.66f, 0.35f));
        /* 两处"解释性"的页脚都删了：
           二级那句"名字右边有小点…"（小点已经换成「（已被修改）」，这句
           就没用了），一级那句"预设只定玩法…"（三个大类和一条说明栏
           已经把事情说清楚了，再说一遍是啰嗦）。
           留下来的都只是"按什么键干什么"，页脚干这个最合适。*/
        /* 页脚的文字一律取自 hud.cpp 的具名常量，**这里不写字面量**。
           踩过一次：保存键改成 Ctrl+S 之后只改了 hud.cpp 那一份，
           render.cpp 里这句老文案还在，"撤销"两个字于是在屏幕上变成了
           空心方块 —— 只有在图集里查不到的字才会这样，而当时的出图脚本
           把控制台的告警吞掉了。现在两边指同一个对象，不会再走散。
           后来又踩了同一类坑的前半步：**能按的键随光标位置变**，
           页脚也得跟着变，否则光标停在「当前预设方案」上时页脚还在说
           "左右：调整" —— 那一行左右什么都不做，这就是骗人。*/
        /* ★ 这三行彻底"状态化"了。原则是
           "当前光标有哪些快捷键才显示哪些快捷键提示"，所以每一行都按
           光标现在的位置取词，**没有一句是恒定不变的**：
             · 第一行：菜单栏上 / 第 0 行 / 参数行 / 弹窗里 —— 四种；
             · 第二行：只有「R：恢复用户偏好设置」，而且**只在偏好页**上写
               （预设页的 R 已经不生效了，写出来就是骗人）；
             · 第三行：只剩 Tab（ESC 不再关面板），
               弹窗那一态不写（那一下收的是弹窗，不是面板）。
           空着的地方就留白 —— 一句按了没反应的说明比空白更糟。*/
        if (app->paramPopOpen) {
            hudText(&app->hud, bx + 22.0f * u, fy + 40.0f * u, 17.0f * u, HUD_LEFT,
                    COL_TEXT_HINT, g_panelHintPop);
        } else {
            const wchar_t *line1;
            if (app->paramOnMenu)          line1 = g_panelHintBar;
            else if (app->paramSel == 0 && app->paramPage == 0)
                                           line1 = g_panelHintRow0;
            else                           line1 = g_panelHintMenu;

            hudText(&app->hud, bx + 22.0f * u, fy + 14.0f * u, 17.0f * u, HUD_LEFT,
                    COL_TEXT_HINT, line1);
            /* R 只在偏好页上写着、也只在那儿生效。菜单栏上也一样 ——
               它认的是**当前页**，跟光标在栏上还是列表里无关。*/
            if (app->paramPage == 1)
                hudText(&app->hud, bx + 22.0f * u, fy + 40.0f * u, 17.0f * u,
                        HUD_LEFT, COL_TEXT_HINT, g_panelHintPref);
            hudText(&app->hud, bx + 22.0f * u, fy + 68.0f * u, 17.0f * u, HUD_LEFT,
                    COL_TEXT_HINT, g_panelCloseMenu);
        }

        if (app->paramMsgT > 0.0f && app->paramMsg[0]) {
            hudText(&app->hud, bx + bw - 22.0f * u, fy + 16.0f * u, 19.0f * u,
                    HUD_RIGHT, c4(1.0f, 0.84f, 0.40f, 1.0f), app->paramMsg);
        }
    }
}

static void drawPauseOverlay(App *app) {
    float u = (float)app->winH / 900.0f;
    float W = (float)app->winW, H = (float)app->winH;
    float bw = 460.0f * u, bh = 236.0f * u;
    float bx, by;
    wchar_t buf[160];
    wchar_t svcLine[160];
    svcLine[0] = L'\0';
    /* 音频设备没开成的时候面板要长高一点，多一行说明。
       "试过但没开成"才说 —— 出图与自检形态压根没试过，不该在界面上提这一茬。
       面板同时按**量出来的文本宽度**加宽：原因串长度取决于出错码，
       写死 460 的时候最长的那几句会顶出底板外（--no-audio 拍出来的图
       就是这么暴露的）。多留 56 像素是给底板内边距。*/
    if (app->audio.tried && !app->audio.ready) {
        float need;
        _snwprintf(svcLine, ARRAY_COUNT(svcLine) - 1, L"%ls，不影响玩法",
                   audioStatusText(&app->audio));
        svcLine[ARRAY_COUNT(svcLine) - 1] = L'\0';
        bh += 34.0f * u;
        need = hudTextWidth(&app->hud, svcLine, 15.0f * u) + 56.0f * u;
        if (need > bw) bw = need;
    }
    bx = (W - bw) * 0.5f;
    by = (H - bh) * 0.5f;

    fillRect(0, 0, W, H, c4(0.02f, 0.03f, 0.05f, 0.52f));
    hudPanel(bx, by, bw, bh, c4(0.05f, 0.07f, 0.11f, 0.88f),
             c4(0.60f, 0.68f, 0.80f, 0.42f));

    hudText(&app->hud, W * 0.5f, by + 26.0f * u, 38.0f * u, HUD_CENTER,
            COL_TEXT_TITLE, app->lostFocus ? L"窗口失去焦点" : L"已暂停");

    hudText(&app->hud, W * 0.5f, by + 88.0f * u, 20.0f * u, HUD_CENTER,
            c4(0.82f, 0.88f, 0.95f, 0.95f), L"按 ESC 继续");
    hudText(&app->hud, W * 0.5f, by + 122.0f * u, 20.0f * u, HUD_CENTER,
            c4(0.82f, 0.88f, 0.95f, 0.95f), L"按 R 结算并开始新游戏");
    /* ★ 句尾那个括号「（这一局会暂停）」删掉了。
       行为一点没变（按 Tab 确实还是暂停着调参数），删的只是这半句解释：
       屏幕顶端已经写着大字「已暂停」，再说一遍是重复。
       ★ 「调参数」改成「设置」，与左下角那排提示同一处理。*/
    hudText(&app->hud, W * 0.5f, by + 156.0f * u, 19.0f * u, HUD_CENTER,
            c4(0.62f, 0.68f, 0.76f, 0.88f), L"按 Tab 设置");

    {
        wchar_t sec[32];
        formatSeconds(app->elapsed, sec, ARRAY_COUNT(sec));
        _snwprintf(buf, ARRAY_COUNT(buf) - 1, L"本局已进行 %ls，得分 %d",
                   sec, app->score);
        buf[ARRAY_COUNT(buf) - 1] = L'\0';
    }
    hudText(&app->hud, W * 0.5f, by + 196.0f * u, 17.0f * u, HUD_CENTER,
            c4(0.62f, 0.68f, 0.76f, 0.90f), buf);

    /* 没声卡也要说清楚为什么，而不是让玩家自己猜"是不是设置错了"。
       原因字串由 audio.cpp 给（那里是唯一真源），这里只负责排版 ——
       不再加"音频设备不可用"那一圈前缀：原因串本身就带着"音频设备打开失败："。*/
    if (svcLine[0]) {
        hudText(&app->hud, W * 0.5f, by + 228.0f * u, 15.0f * u, HUD_CENTER,
                c4(0.72f, 0.62f, 0.56f, 0.90f), svcLine);
    }
}

/* ---------------------------------------------------------- 结算 */

static const wchar_t *modeName(int mode) {
    return g_modeNames[clampi(mode, 0, MODE_COUNT - 1)];
}

/* 结束原因的中文。顺序与 EndReason 一一对应，两边改一个就得改另一个 ——
   所以这里紧跟枚举写，并让下面那条 switch 兜住越界。*/
const wchar_t *endReasonText(int reason) {
    switch (reason) {
    case END_TIME:    return L"时间到";
    case END_LIVES:   return L"生命耗尽";
    case END_ESCAPED: return L"球跑了";
    case END_MISSED:  return L"打空了";
    /* ★ 「自己收的」→「手动结束」。这里和
       hud.cpp 的 g_hudStrings[] 必须**逐字一致** —— 图集照那张表收字，
       两边不一致时屏幕上会冒出空心方块（栽过一次：改了一处忘了
       另一处）。自检里补了一条断言盯着这件事（见 tPanelWording
       里那一组「每个结束原因文字都能在图集文案表里找到」）。*/
    case END_MANUAL:  return L"手动结束";
    default:          return L"";
    }
}

static void drawSettleOverlay(App *app) {
    const RunResult *r = &app->lastResult;
    float u = (float)app->winH / 900.0f;
    float W = (float)app->winW, H = (float)app->winH;
    float bw = 560.0f * u, bh = 442.0f * u;
    float bx = (W - bw) * 0.5f, by = (H - bh) * 0.5f;
    wchar_t buf[160], sec[32];
    float ty;
    int i;

    fillRect(0, 0, W, H, c4(0.02f, 0.03f, 0.05f, 0.58f));
    hudPanel(bx, by, bw, bh, c4(0.05f, 0.07f, 0.11f, 0.90f),
             c4(0.62f, 0.70f, 0.82f, 0.45f));

    /* 标题带上死因。"本局结束 · 时间到" 比单说"本局结束"多给了一件事，
       而这恰恰是玩家最想知道的那件。*/
    if (app->endReason != END_NONE && endReasonText(app->endReason)[0]) {
        _snwprintf(buf, ARRAY_COUNT(buf) - 1, L"本局结束 · %ls",
                   endReasonText(app->endReason));
        buf[ARRAY_COUNT(buf) - 1] = L'\0';
    } else {
        wcsncpy(buf, L"本局结束", ARRAY_COUNT(buf) - 1);
        buf[ARRAY_COUNT(buf) - 1] = L'\0';
    }
    hudText(&app->hud, W * 0.5f, by + 24.0f * u, 34.0f * u, HUD_CENTER,
            COL_TEXT_TITLE, buf);

    /* 带上种子：玩家可以照着同一个种子再来一局，复现同一串气球。
       规则名和预设名都写：预设是"选中的那套"，规则是"这局实际怎么算的"，
       玩家套完预设又改过参数时，这两个可能不是一回事。*/
    _snwprintf(buf, ARRAY_COUNT(buf) - 1, L"%ls · %ls    种子 %u",
               g_presets[clampi(r->preset, 0, PRESET_COUNT - 1)].name,
               modeName(r->mode), r->seed);
    buf[ARRAY_COUNT(buf) - 1] = L'\0';
    hudText(&app->hud, W * 0.5f, by + 70.0f * u, 17.0f * u, HUD_CENTER,
            c4(0.66f, 0.72f, 0.80f, 0.92f), buf);

    /* 分数：整屏最大的字。*/
    _snwprintf(buf, ARRAY_COUNT(buf) - 1, L"%d", r->score);
    buf[ARRAY_COUNT(buf) - 1] = L'\0';
    hudText(&app->hud, W * 0.5f, by + 96.0f * u, 62.0f * u, HUD_CENTER,
            c4(1.0f, 0.93f, 0.62f, 1.0f), buf);

    /* 两列细账。*/
    ty = by + 186.0f * u;
    formatSeconds(r->durationSec, sec, ARRAY_COUNT(sec));
    {
        const wchar_t *labels[6];
        wchar_t values[6][48];
        labels[0] = L"击破";   _snwprintf(values[0], 47, L"%d", r->popped);
        labels[1] = L"漏球";   _snwprintf(values[1], 47, L"%d", r->missed);
        labels[2] = L"出枪";   _snwprintf(values[2], 47, L"%d", r->shots);
        labels[3] = L"最高连击"; _snwprintf(values[3], 47, L"%d", r->bestCombo);
        labels[4] = L"命中率"; _snwprintf(values[4], 47, L"%d%%",
                                          (int)(r->accuracy * 100.0f + 0.5f));
        labels[5] = L"用时";   _snwprintf(values[5], 47, L"%ls", sec);
        for (i = 0; i < 6; ++i) values[i][47] = L'\0';

        for (i = 0; i < 6; ++i) {
            float col = bx + 40.0f * u + (float)(i % 2) * 250.0f * u;
            float row = ty + (float)(i / 2) * 32.0f * u;
            hudText(&app->hud, col, row, 19.0f * u, HUD_LEFT,
                    c4(0.62f, 0.68f, 0.76f, 0.92f), labels[i]);
            hudText(&app->hud, col + 110.0f * u, row, 20.0f * u, HUD_LEFT,
                    c4(0.94f, 0.96f, 0.99f, 0.98f), values[i]);
        }
    }

    /* 纪录那一行。破纪录就明说，没破就给出差距 —— 比一句"再接再厉"有用。*/
    if (app->newRecord) {
        hudText(&app->hud, W * 0.5f, ty + 108.0f * u, 24.0f * u, HUD_CENTER,
                c4(0.62f, 1.0f, 0.66f, 1.0f), L"破纪录！");
    } else if (app->lastRecordScore > 0) {
        int gap = app->lastRecordScore - r->score;
        if (gap < 0) gap = 0;
        _snwprintf(buf, ARRAY_COUNT(buf) - 1, L"历史最高 %d    距离纪录还差 %d 分",
                   app->lastRecordScore, gap);
        buf[ARRAY_COUNT(buf) - 1] = L'\0';
        hudText(&app->hud, W * 0.5f, ty + 108.0f * u, 18.0f * u, HUD_CENTER,
                c4(0.72f, 0.78f, 0.86f, 0.94f), buf);
    }

    /* 底部的两行操作。这里早先是"再来一局"+"按 ESC 返回训练场"，
       后来把第二行换成了历史记录的入口，
       而"按 ESC 返回"这条整个撤销了（ESC 现在只做暂停/继续）。*/
    hudText(&app->hud, W * 0.5f, by + bh - 66.0f * u, 20.0f * u, HUD_CENTER,
            c4(0.86f, 0.92f, 0.98f, 0.96f), L"按 R 开始新游戏");
    hudText(&app->hud, W * 0.5f, by + bh - 36.0f * u, 18.0f * u, HUD_CENTER,
            c4(0.70f, 0.78f, 0.88f, 0.92f), L"按 F 查看历史记录");

    /* 左下角那排操作提示（WASD / 鼠标 / 左键 / ESC / R / Tab）由 drawPlayHud
       画，这里不重画。**注意它在这一屏上是被刻意画暗的** ——
       结算页底下的快捷键提示是照旧保留的；但那些键在结算屏上本来就不响应，
       原样亮着等于说得到做不到。画暗一档，既留着"还能按什么"的完整清单，
       又不谎称它们此刻有效。*/
}

/* ---------------------------------------------------------- 历史记录屏
 *
 * 三条约定，都落在这里：
 *   "显示格式和设置页面用同一个模板" —— 真就调用同一组函数：
 *       hudPanel / drawPanelSide / 同一套 PARAM_* 常量，连中间那道竖线
 *       都是同一行代码。所以两边看起来就是同一个模板，而不是"照着仿的"。
 *   "左边选择栏目中可选择第几次" —— 左栏每行一个名次，光标停哪行右栏说哪行。
 *   "右边侧边栏也干脆叫选项说明" —— drawPanelSide 的标题已经定死是这四个字。
 */
int historyVisibleRows(const App *app) {
    float u, bh, listH;
    int n;
    if (!app || app->winH <= 0) return 8;
    u = (float)app->winH / 900.0f;
    bh = (float)app->winH - 2.0f * PARAM_MARGIN_Y * u;
    listH = bh - (PARAM_HEAD_H + PARAM_FOOT_H) * u;
    n = (int)(listH / (PARAM_ROW_H * u));
    if (n < 3) n = 3;
    if (n > HISTORY_CAP) n = HISTORY_CAP;
    return n;
}

/* 一局都没有时的右栏文案。写成常量而不是藏在分支里，是为了让下面的
   渲染循环保持"左栏选谁、右栏说什么"这一条线。*/
#define HISTORY_EMPTY_HINT \
    L"还没有记录。打完一局，这里就会出现第 1 次。"

static void drawHistoryPanel(App *app) {
    const float u = (float)app->winH / 900.0f;
    const float W = (float)app->winW, H = (float)app->winH;
    float bw = W - 120.0f * u;
    float bh = H - 2.0f * PARAM_MARGIN_Y * u;
    float bx, by, listTop, rowH, listBottom;
    int n = app->history.count;
    int rows, i;
    wchar_t buf[192];
    const wchar_t *sideBody = NULL;
    SideStat sideStats[9];
    int sideStatCount = 0;
    /* 表头开在函数作用域而不是 if (sel) 里面：sideBody 要一直活到循环
       结束之后才被交出去，块内数组出了块就是野指针。*/
    wchar_t sideHead[256];

    /* 与设置面板同一套尺寸约束 —— 这一行也是"同一个模板"的一部分。*/
    if (bw > PARAM_BW_MAX * u) bw = PARAM_BW_MAX * u;
    if (bw < 420.0f * u) bw = 420.0f * u;
    bx = (W - bw) * 0.5f;
    by = (H - bh) * 0.5f;
    rowH = PARAM_ROW_H * u;
    listTop = by + PARAM_HEAD_H * u;
    listBottom = listTop + (bh - (PARAM_HEAD_H + PARAM_FOOT_H) * u);

    if (n < 0) n = 0;
    if (n > HISTORY_CAP) n = HISTORY_CAP;
    rows = historyVisibleRows(app);
    if (rows < 1) rows = 1;

    /* 底下的压暗已经在 renderHud 里做过一次（那一层要盖住结算面板），
       这里不再压第二遍 —— 叠两次会把训练场压成全黑，失去对照。*/
    hudPanel(bx, by, bw, bh, c4(0.045f, 0.062f, 0.10f, 0.94f),
             c4(0.62f, 0.70f, 0.82f, 0.50f));

    /* ---------------------------------------------------------- 标题栏 */
    hudText(&app->hud, bx + 22.0f * u, by + 16.0f * u, 27.0f * u, HUD_LEFT,
            COL_TEXT_TITLE, L"历史记录");
    /* 右上角：共几局。"第 1 / 7 次"这个说法本身就说明"1 是最新的"，
       不用另写一句解释顺序。*/
    if (n > 0)
        _snwprintf(buf, ARRAY_COUNT(buf) - 1, L"第 %d / %d 次    F：返回",
                   clampi(app->histSel, 0, n - 1) + 1, n);
    else
        _snwprintf(buf, ARRAY_COUNT(buf) - 1, L"暂无记录    F：返回");
    buf[ARRAY_COUNT(buf) - 1] = L'\0';
    hudText(&app->hud, bx + bw - 22.0f * u, by + 19.0f * u, 20.0f * u,
            HUD_RIGHT, c4(0.66f, 0.76f, 0.88f, 0.95f), buf);

    fillRect(bx + 18.0f * u, by + PARAM_HEAD_H * u - 8.0f * u,
             bw - 36.0f * u, 1.0f * u, c4(0.42f, 0.52f, 0.66f, 0.35f));
    fillRect(bx + bw * PARAM_SPLIT, by + PARAM_HEAD_H * u - 4.0f * u,
             1.0f * u, bh - PARAM_HEAD_H * u - PARAM_FOOT_H * u + 8.0f * u,
             c4(0.42f, 0.52f, 0.66f, 0.30f));

    /* ---------------------------------------------------------- 左栏 */
    if (n == 0) {
        hudText(&app->hud, bx + 40.0f * u, listTop + 12.0f * u, 19.0f * u,
                HUD_LEFT, c4(0.66f, 0.72f, 0.82f, 0.92f), L"还没有记录");
        sideBody = HISTORY_EMPTY_HINT;
    } else {
        for (i = 0; i < rows; ++i) {
            int idx = app->histTop + i;
            const HistoryEntry *e;
            float ry = listTop + (float)i * rowH;
            int sel;

            if (idx < 0 || idx >= n) break;
            e = historyAt(&app->history, idx);
            if (!e) break;
            sel = (idx == app->histSel);

            /* 列表区宽度 = bw × PARAM_SPLIT − 24u，与参数面板里那个 listW
               是同一个数（那边写作 listW + 8u 的出血，这里交给函数加）。*/
            if (sel) drawSelRowBars(bx, ry, u, bw * PARAM_SPLIT - 24.0f * u, rowH);

            /* 第几次。字号与参数名一致（19），位置也与参数名一致（40）——
               两处排版对齐，切换过去才不会觉得换了个窗口。*/
            _snwprintf(buf, ARRAY_COUNT(buf) - 1, L"第 %d 次", idx + 1);
            buf[ARRAY_COUNT(buf) - 1] = L'\0';
            hudText(&app->hud, bx + 40.0f * u, ry + 3.0f * u, 19.0f * u,
                    HUD_LEFT,
                    sel ? COL_TEXT_SEL : COL_TEXT_BODY,
                    buf);

            /* 分数右对齐到与参数值同一个 x（bw * PARAM_SPLIT - 24）。*/
            _snwprintf(buf, ARRAY_COUNT(buf) - 1, L"%d 分", e->score);
            buf[ARRAY_COUNT(buf) - 1] = L'\0';
            hudText(&app->hud, bx + bw * PARAM_SPLIT - 24.0f * u,
                    ry + 3.0f * u, 19.0f * u, HUD_RIGHT,
                    sel ? COL_TEXT_VALSEL : COL_TEXT_VALUE,
                    buf);

            if (sel) {
                /* 右栏 = 表头三行（哪一局）+ 一张成绩表。
                   表头用 \n 分段，交给侧栏按普通段落排；表头之后的九个数字
                   走成绩表，左标签右数值对齐。
                   早先这里是把九项用全角空格拼成九行、整体当段落交给侧栏的，
                   出图一看就散了架 —— 中文、数字、% 的宽度各不相同，靠空格
                   凑出来的列必然歪。列对不齐，读的人就得一个个数过去，
                   "看一眼就知道这局打得怎么样"这个目的就没了。*/
                wchar_t when[48];

                sideStatCount = 9;
                sideStats[0].label = L"得分";
                sideStats[1].label = L"击破";
                sideStats[2].label = L"漏球";
                sideStats[3].label = L"出枪";
                sideStats[4].label = L"命中";
                sideStats[5].label = L"最高连击";
                sideStats[6].label = L"命中率";
                sideStats[7].label = L"用时";
                sideStats[8].label = L"种子";

                _snwprintf(sideStats[0].value, ARRAY_COUNT(sideStats[0].value) - 1,
                           L"%d", e->score);
                _snwprintf(sideStats[1].value, ARRAY_COUNT(sideStats[1].value) - 1,
                           L"%d", e->popped);
                _snwprintf(sideStats[2].value, ARRAY_COUNT(sideStats[2].value) - 1,
                           L"%d", e->missed);
                _snwprintf(sideStats[3].value, ARRAY_COUNT(sideStats[3].value) - 1,
                           L"%d", e->shots);
                _snwprintf(sideStats[4].value, ARRAY_COUNT(sideStats[4].value) - 1,
                           L"%d", e->hits);
                _snwprintf(sideStats[5].value, ARRAY_COUNT(sideStats[5].value) - 1,
                           L"%d", e->bestCombo);
                _snwprintf(sideStats[6].value, ARRAY_COUNT(sideStats[6].value) - 1,
                           L"%d%%", e->accuracyPct);
                _snwprintf(sideStats[7].value, ARRAY_COUNT(sideStats[7].value) - 1,
                           L"%.1f 秒", (double)e->durationMs / 1000.0);
                _snwprintf(sideStats[8].value, ARRAY_COUNT(sideStats[8].value) - 1,
                           L"%u", e->seed);
                {
                    int k;
                    for (k = 0; k < sideStatCount; ++k)
                        sideStats[k].value[ARRAY_COUNT(sideStats[k].value) - 1] = L'\0';
                }

                historyFormatTime(e->when, when, (int)ARRAY_COUNT(when));
                _snwprintf(sideHead, ARRAY_COUNT(sideHead) - 1,
                           L"%ls\n%ls · %ls\n结束：%ls",
                           when,
                           g_presets[clampi(e->preset, 0, PRESET_COUNT - 1)].name,
                           modeName(e->mode),
                           (e->endReason != END_NONE &&
                            endReasonText(e->endReason)[0])
                               ? endReasonText(e->endReason) : L"—");
                sideHead[ARRAY_COUNT(sideHead) - 1] = L'\0';
                sideBody = sideHead;
            }
        }

        /* 上下还有内容时给箭头，与设置列表用同一对字符、同一个位置感。
           ★ 字形换成了单书名号那对。这一屏的滚动窗口**没有**照
           设置列表那样改成"箭头占一格"——那条规则说的是设置列表里的选项位，
           历史记录是只读的一览表，没有"选项"可言；只换字形，语义与位置不动。*/
        if (app->histTop > 0)
            hudText(&app->hud, bx + bw * PARAM_SPLIT * 0.5f,
                    by + PARAM_HEAD_H * u - 20.0f * u, 22.0f * u, HUD_CENTER,
                    COL_ARROW, L"︿");
        if (app->histTop + rows < n)
            hudText(&app->hud, bx + bw * PARAM_SPLIT * 0.5f,
                    listBottom - 22.0f * u, 22.0f * u, HUD_CENTER,
                    COL_ARROW, L"﹀");
    }

    /* ---------------------------------------------------------- 右栏 */
    drawPanelSide(app, bx + bw * PARAM_SPLIT + 10.0f * u, listTop,
                  bw * (1.0f - PARAM_SPLIT) - 34.0f * u,
                  sideBody, sideStatCount > 0 ? sideStats : NULL, sideStatCount);

    /* ---------------------------------------------------------- 页脚 */
    {
        float fy = by + bh - PARAM_FOOT_H * u;
        fillRect(bx + 18.0f * u, fy + 6.0f * u, bw - 36.0f * u, 1.0f * u,
                 c4(0.42f, 0.52f, 0.66f, 0.35f));
        /* ★ 这里两处改动：
           ① 删掉「PgUp / PgDn：翻页」整段。
           ② 「上下：选择」换成「↑↓：选择」—— 这是**补上一处漏改**：
              约定是"快捷键提示中的所有的上下左右换成符号而不是汉字"，
              设置面板四段提示都换了，唯独历史屏这一句还留着汉字。自检当初只
              逐字扫了面板那几段（tPanelWording），这一句在扫描范围之外，
              于是漏了很久。现在两边一致。*/
        hudText(&app->hud, bx + 22.0f * u, fy + 16.0f * u, 17.0f * u, HUD_LEFT,
                COL_TEXT_HINT,
                L"↑↓：选择    Home / End：首尾");
        /* 退出去只剩 F。ESC 与 Tab 都不再关这一屏，
           写出来就是同一类谎话。*/
        hudText(&app->hud, bx + 22.0f * u, fy + 42.0f * u, 17.0f * u, HUD_LEFT,
                COL_TEXT_HINT,
                L"F：返回");
        hudText(&app->hud, bx + 22.0f * u, fy + 70.0f * u, 17.0f * u, HUD_LEFT,
                c4(0.62f, 0.70f, 0.82f, 0.92f),
                L"按时间由近及远排列，最多保留 20 次。");
    }
}

void renderHud(App *app) {
    if (!app) return;

    hudBeginScreen(app->winW, app->winH);

    if (app->screen == SCREEN_PLAY) {
        /* 面板开着就不画准星：这一屏的"准星"是那根选中行的高亮条，
           再画一个射击准星只会让人以为还能开枪。*/
        if (!app->paramOpen) drawCrosshair(app);
        drawScorePopups(app);
        drawPlayHud(app);
    } else if (app->screen == SCREEN_PAUSE) {
        drawPlayHud(app);
        drawPauseOverlay(app);
    } else if (app->screen == SCREEN_HISTORY) {
        /* 历史记录盖在结算屏上面。底下的训练场一样照画（drawPlayHud），
           不然从结算屏切过来会黑一下，像是换了个程序。
           不画 drawSettleOverlay —— 那一整块结算面板会从下面的历史面板
           四周露出来，两层文字互相干扰。只画压暗的第一层，够了。*/
        drawPlayHud(app);
        fillRect(0, 0, (float)app->winW, (float)app->winH,
                 c4(0.02f, 0.03f, 0.05f, 0.72f));
        drawHistoryPanel(app);
    } else {
        /* 结算屏**保留**底下的 HUD 与按键提示（进入结算页面时，
           底下的 wasd 移动等快捷键不消失）。第一遍写漏了这一行，
           出图时结算屏干净得像另一程序：左上角的比分、底部的按键行全没了，
           只剩中间一块成绩面板。drawPlayHud 里那条"结算时把按键行压到
           0.45 不透明度"的分支本来就写好了，只是没人调它。*/
        drawPlayHud(app);
        drawSettleOverlay(app);
    }
    /* 面板盖在最上面：底下那层是哪种界面都行，
       面板只在 SCREEN_PLAY 时才可能被打开。*/
    if (app->paramOpen) drawParamPanel(app);

    hudEndScreen();

    /* 图集漏字的话在这里报出来。每 2 秒查一次就够，不必每帧。*/
    if ((app->frame % 120) == 0) hudReportMissing(&app->hud);
}
