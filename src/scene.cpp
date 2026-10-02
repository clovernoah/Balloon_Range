/* ============================================================================
 * scene.cpp —— 训练房几何
 *
 * 全部用立即模式画。三角面数很少（房间 6 个面 + 几十个小方块），
 * 固定管线的开销可以忽略 —— 探针实测 24 个带光照的球体只要 0.43 ms。
 *
 * 贴图坐标的规矩：**1 个贴图单位 = 4 米**。所以一面 14 米宽的墙 u 取 3.5，
 * 砖块/瓷砖的尺度在整个房间里是一致的，不会这面墙大那面墙小。
 * ==========================================================================*/
#include "scene.h"

#define TEX_METERS 4.0f        /* 一张贴图铺 4 米 */

/* 墙面近地处的压暗系数。整面墙从下到上由这个值线性过渡到 1.0，
   让墙"站起来"。放在顶点色里而不是贴图里，理由见 quadTexGrad 的注释。*/
#define WALL_BOTTOM_K 0.72f

/* --------------------------------------------------------------- 小工具 */

/* 画一个带贴图的四边形。四个点按逆时针给，法线朝外。*/
static void quadTex(const Texture *t, Vec3 a, Vec3 b, Vec3 c, Vec3 d,
                    float u0, float v0, float u1, float v1) {
    glBindTexture(GL_TEXTURE_2D, t->id);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex3f(a.x, a.y, a.z);
    glTexCoord2f(u1, v0); glVertex3f(b.x, b.y, b.z);
    glTexCoord2f(u1, v1); glVertex3f(c.x, c.y, c.z);
    glTexCoord2f(u0, v1); glVertex3f(d.x, d.y, d.z);
    glEnd();
}

/* 带上下渐变的版本：a/b 是下边两点，给 bottomK；c/d 是上边两点，给 topK。
   开了 GL_COLOR_MATERIAL 之后 glColor 会调制材质的 ambient+diffuse，
   所以这就是一次纯粹的"把墙压出上下层次"，不需要动贴图。

   为什么不在贴图里烤：贴图在墙上纵向平铺 1.25 次，烤进去的渐变会跟着重复，
   出现一条横接缝。顶点色只铺一次，是干净的。*/
static void quadTexGrad(const Texture *t, Vec3 a, Vec3 b, Vec3 c, Vec3 d,
                        float u0, float v0, float u1, float v1,
                        float bottomK, float topK) {
    glBindTexture(GL_TEXTURE_2D, t->id);
    glBegin(GL_QUADS);
    glColor3f(bottomK, bottomK, bottomK);
    glTexCoord2f(u0, v0); glVertex3f(a.x, a.y, a.z);
    glTexCoord2f(u1, v0); glVertex3f(b.x, b.y, b.z);
    glColor3f(topK, topK, topK);
    glTexCoord2f(u1, v1); glVertex3f(c.x, c.y, c.z);
    glTexCoord2f(u0, v1); glVertex3f(d.x, d.y, d.z);
    glEnd();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

/* 画一个纯色四边形（用 1x1 白贴图，省得来回开关纹理）。*/
static void quadSolid(const Texture *white, Color3 col, float alpha,
                      Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
    glBindTexture(GL_TEXTURE_2D, white->id);
    glColor4f(col.r, col.g, col.b, alpha);
    glBegin(GL_QUADS);
    glVertex3f(a.x, a.y, a.z);
    glVertex3f(b.x, b.y, b.z);
    glVertex3f(c.x, c.y, c.z);
    glVertex3f(d.x, d.y, d.z);
    glEnd();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

Vec3 sceneWallToWorld(float wx, float wy, float z) {
    return v3(wx, wy, z);
}

/* --------------------------------------------------------------- 出球区 */

void sceneComputeField(const Params *p, FieldRect *out) {
    float mx = FIELD_MARGIN_X;
    float my = FIELD_MARGIN_Y;
    if (!out) return;
    (void)p;
    out->x0 = -WALL_HALF_W * (1.0f - mx);
    out->x1 =  WALL_HALF_W * (1.0f - mx);
    out->y0 =  ROOM_H * my;
    out->y1 =  ROOM_H * (1.0f - my);
}

/* --------------------------------------------------------------- 构建 */

void sceneBuild(Scene *s, const Params *p, unsigned seed) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->seed = seed;
    s->builtStyle = p->wallStyle;
    s->built = 1;
    texMakeWall(&s->wall, p->wallStyle, seed);
    /* 地面色调固定成 FLOOR_TINT_DEF：纯观感微调，不值得做成可调参数。*/
    texMakeFloor(&s->floor, FLOOR_TINT_DEF, seed + 991u);
    texMakeCeiling(&s->ceiling, FLOOR_TINT_DEF);
    /* 侧墙用砖（最耐看也最能显出透视），与气球墙风格分开，场面不单调。*/
    texMakeWall(&s->side, WALL_BRICK, seed + 313u);
    texMakeGlow(&s->glow, 64);
    texMakeSpec(&s->spec, 64);
    texMakeWhite(&s->white);
}

void sceneDestroy(Scene *s) {
    if (!s) return;
    texDestroy(&s->wall);
    texDestroy(&s->floor);
    texDestroy(&s->ceiling);
    texDestroy(&s->side);
    texDestroy(&s->glow);
    texDestroy(&s->white);
    texDestroy(&s->spec);
    s->built = 0;
}

void sceneRefreshStyle(Scene *s, const Params *p) {
    if (!s || !s->built) return;
    if (s->builtStyle == p->wallStyle) return;
    texDestroy(&s->wall);
    texMakeWall(&s->wall, p->wallStyle, s->seed);
    s->builtStyle = p->wallStyle;
}

/* --------------------------------------------------------------- 绘制 */

void sceneDrawRoom(const Scene *s, const Params *p) {
    float u, v;
    (void)p;
    if (!s || !s->built) return;

    glEnable(GL_TEXTURE_2D);

    /* 墙面用 MODULATE：贴图颜色乘以光照颜色。*/
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    /* 每个面都按"玩家站在这面墙对面看到的样子"给点：左下 → 右下 → 右上 → 左上。
       quadTex 的 uv 参数是 (u0,v0,u1,v1)，对应 (左下, 右下, 右上, 左上)。
       本项目**不开启背面剔除**（glDisable(GL_CULL_FACE) + 双面光照），
       所以绕序不影响可见性，这里只需要管贴图别转 90 度。*/

    /* ---------------------------------------------------------- 地面 */
    u = (ROOM_HALF_W * 2.0f) / TEX_METERS;
    v = (ROOM_BACK_Z - WALL_Z) / TEX_METERS;
    quadTex(&s->floor,
            v3(-ROOM_HALF_W, 0.0f, ROOM_BACK_Z),
            v3( ROOM_HALF_W, 0.0f, ROOM_BACK_Z),
            v3( ROOM_HALF_W, 0.0f, WALL_Z),
            v3(-ROOM_HALF_W, 0.0f, WALL_Z),
            0.0f, 0.0f, u, v);

    /* ------------------------------------------------------ 天花板 */
    quadTex(&s->ceiling,
            v3(-ROOM_HALF_W, ROOM_H, ROOM_BACK_Z),
            v3( ROOM_HALF_W, ROOM_H, ROOM_BACK_Z),
            v3( ROOM_HALF_W, ROOM_H, WALL_Z),
            v3(-ROOM_HALF_W, ROOM_H, WALL_Z),
            0.0f, 0.0f, u, v);

    /* ------------------------------------------------- 气球墙（正前方） */
    u = (WALL_HALF_W * 2.0f) / TEX_METERS;
    v = ROOM_H / TEX_METERS;
    quadTexGrad(&s->wall,
            v3(-WALL_HALF_W, 0.0f, WALL_Z),
            v3( WALL_HALF_W, 0.0f, WALL_Z),
            v3( WALL_HALF_W, ROOM_H, WALL_Z),
            v3(-WALL_HALF_W, ROOM_H, WALL_Z),
            0.0f, 0.0f, u, v, WALL_BOTTOM_K, 1.0f);

    /* ------------------------------------------------- 左墙 / 右墙 */
    u = (ROOM_BACK_Z - WALL_Z) / TEX_METERS;
    v = ROOM_H / TEX_METERS;
    quadTexGrad(&s->side,
            v3(-ROOM_HALF_W, 0.0f, ROOM_BACK_Z),
            v3(-ROOM_HALF_W, 0.0f, WALL_Z),
            v3(-ROOM_HALF_W, ROOM_H, WALL_Z),
            v3(-ROOM_HALF_W, ROOM_H, ROOM_BACK_Z),
            0.0f, 0.0f, u, v, WALL_BOTTOM_K, 1.0f);

    quadTexGrad(&s->side,
            v3(ROOM_HALF_W, 0.0f, WALL_Z),
            v3(ROOM_HALF_W, 0.0f, ROOM_BACK_Z),
            v3(ROOM_HALF_W, ROOM_H, ROOM_BACK_Z),
            v3(ROOM_HALF_W, ROOM_H, WALL_Z),
            0.0f, 0.0f, u, v, WALL_BOTTOM_K, 1.0f);

    /* ---------------------------------------------------------- 后墙 */
    u = (ROOM_HALF_W * 2.0f) / TEX_METERS;
    quadTex(&s->side,
            v3( ROOM_HALF_W, 0.0f, ROOM_BACK_Z),
            v3(-ROOM_HALF_W, 0.0f, ROOM_BACK_Z),
            v3(-ROOM_HALF_W, ROOM_H, ROOM_BACK_Z),
            v3( ROOM_HALF_W, ROOM_H, ROOM_BACK_Z),
            0.0f, 0.0f, u, v);

    glDisable(GL_TEXTURE_2D);
}

void sceneDrawMarkings(const Scene *s, const Params *p) {
    FieldRect f;
    static const Color3 kFieldLine = { 0.62f, 0.68f, 0.78f };
    static const Color3 kWarn      = { 0.92f, 0.72f, 0.22f };
    float zLine = WALL_Z + 0.010f;      /* 贴在墙面前一丁点，避免 z-fighting */
    float w = 0.020f;
    (void)p;
    if (!s || !s->built) return;

    sceneComputeField(p, &f);

    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* 出球区四边：很淡的线，只是给玩家一个"球会出现在这块区域"的提示。*/
    quadSolid(&s->white, kFieldLine, 0.16f,
              v3(f.x0, f.y0, zLine), v3(f.x1, f.y0, zLine),
              v3(f.x1, f.y0 + w, zLine), v3(f.x0, f.y0 + w, zLine));
    quadSolid(&s->white, kFieldLine, 0.16f,
              v3(f.x0, f.y1 - w, zLine), v3(f.x1, f.y1 - w, zLine),
              v3(f.x1, f.y1, zLine), v3(f.x0, f.y1, zLine));
    quadSolid(&s->white, kFieldLine, 0.16f,
              v3(f.x0, f.y0, zLine), v3(f.x0 + w, f.y0, zLine),
              v3(f.x0 + w, f.y1, zLine), v3(f.x0, f.y1, zLine));
    quadSolid(&s->white, kFieldLine, 0.16f,
              v3(f.x1 - w, f.y0, zLine), v3(f.x1, f.y0, zLine),
              v3(f.x1, f.y1, zLine), v3(f.x1 - w, f.y1, zLine));

    /* 地面警戒线：黄色斜纹太重，就一条实线加一条更淡的复线。*/
    {
        float y = 0.004f;
        float x0 = -ROOM_HALF_W + 0.4f;
        float x1 =  ROOM_HALF_W - 0.4f;
        quadSolid(&s->white, kWarn, 0.55f,
                  v3(x0, y, PLAY_LINE_Z - 0.06f), v3(x1, y, PLAY_LINE_Z - 0.06f),
                  v3(x1, y, PLAY_LINE_Z + 0.06f), v3(x0, y, PLAY_LINE_Z + 0.06f));
        quadSolid(&s->white, kWarn, 0.22f,
                  v3(x0, y, PLAY_LINE_Z + 0.22f), v3(x1, y, PLAY_LINE_Z + 0.22f),
                  v3(x1, y, PLAY_LINE_Z + 0.30f), v3(x0, y, PLAY_LINE_Z + 0.30f));
    }

    glDisable(GL_BLEND);
}
