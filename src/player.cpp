/* ============================================================================
 * player.cpp —— 相机与移动
 * ==========================================================================*/
#include "player.h"

/* 俯仰上限：89 度。留 1 度是防止视线与世界上方向共线，
   那样 lookAt 会退化（core.cpp 里有兜底，但没必要去踩）。*/
#define PITCH_LIMIT_DEG 89.0f

/* 警戒线：玩家不能越过。留出身体半径。*/
#define MIN_Z (PLAY_LINE_Z + PLAYER_RADIUS)

void playerInit(Player *pl) {
    if (!pl) return;
    /* 出生点：房间正中偏前，距气球墙 4.8 米。
       早先放在 z = 0.8（距墙 6.8 米），75 度视场下整面 11 米宽的墙只占
       屏幕宽度的六成，墙上一半是空的。往前挪到 4.8 米之后墙占到八成半，
       一个 0.5 米半径的气球在 1600 宽的窗口里直径约 120 像素 —— 瞄起来
       舒服，又不至于近到看不见全墙。仍然在警戒线（可站到 z = -2.86）之后。*/
    pl->pos = v3(0.0f, 0.0f, -1.2f);
    pl->yaw = 0.0f;
    pl->pitch = 0.0f;
    /* 视点高度不再是可调参数（"没人会去改自己的身高"），
       固定用 EYE_HEIGHT_DEF。pl->eyeHeight 仍然存在 —— 它是**当前**高度，
       蹲下时会变，是状态不是设置。*/
    pl->eyeHeight = EYE_HEIGHT_DEF;
    pl->bobPhase = 0.0f;
    pl->bobOffset = 0.0f;
    pl->bobSide = 0.0f;
    pl->speedSmooth = 0.0f;
    playerClampToRoom(pl);
}

void playerLook(Player *pl, float dYawDeg, float dPitchDeg, const Params *p) {
    if (!pl) return;
    pl->yaw += dYawDeg * DEG2RAD_F;
    /* Y 轴反转：把俯仰的增量取反即可，不影响别的。*/
    pl->pitch += (p->invertY ? -dPitchDeg : dPitchDeg) * DEG2RAD_F;
    pl->pitch = clampf(pl->pitch, -PITCH_LIMIT_DEG * DEG2RAD_F,
                                  PITCH_LIMIT_DEG * DEG2RAD_F);
    /* 偏航不夹：转多少圈都行，只是角度会一直累加。
       累加到很大时浮点精度会掉，所以绕回一圈以内。*/
    if (pl->yaw > PI_F * 2.0f) pl->yaw -= PI_F * 2.0f;
    if (pl->yaw < -PI_F * 2.0f) pl->yaw += PI_F * 2.0f;
}

Vec3 playerForward(const Player *pl) {
    float cp = cosf(pl->pitch);
    /* yaw = 0 时面向 -z（气球墙的方向）。*/
    return v3(-sinf(pl->yaw) * cp,
               sinf(pl->pitch),
              -cosf(pl->yaw) * cp);
}

Vec3 playerRight(const Player *pl) {
    /* 只取水平分量：右方向不该因为抬头低头而变化（否则侧移会往上飘）。*/
    return v3(cosf(pl->yaw), 0.0f, sinf(pl->yaw));
}

void playerClampToRoom(Player *pl) {
    float lim;
    if (!pl) return;
    lim = ROOM_HALF_W - PLAYER_RADIUS;
    pl->pos.x = clampf(pl->pos.x, -lim, lim);
    pl->pos.z = clampf(pl->pos.z, MIN_Z, ROOM_BACK_Z - PLAYER_RADIUS);
    pl->pos.y = 0.0f;
}

void playerMove(Player *pl, const Params *p, Vec3 wishDir, float dt, int crouch) {
    Vec3 fwd, right, dir;
    float len, speed, target;
    if (!pl) return;

    fwd = playerForward(pl);
    right = playerRight(pl);

    /* wishDir 是"前后左右"的意图（x = 右，z = 前），先合成世界方向。*/
    dir = v3add(v3scale(right, wishDir.x), v3scale(fwd, wishDir.z));
    dir.y = 0.0f;                       /* 走路不下沉也不起飞 */
    len = v3len(dir);

    if (len > 1e-4f) {
        dir = v3scale(dir, 1.0f / len);
        /* 斜着走不该比直着走快 —— 归一化之后已经是单位长度，
           这里再夹一次 len，把"同时按 W 和 D"的 1.414 倍去掉。*/
        if (len > 1.0f) len = 1.0f;
        speed = p->moveSpeed * len;
        if (crouch) speed *= 0.45f;     /* 蹲下移动更慢 */
    } else {
        speed = 0.0f;
    }

    pl->pos = v3add(pl->pos, v3scale(dir, speed * dt));
    playerClampToRoom(pl);

    /* 视点高度：蹲下时平滑下压，不是瞬间掉下去。*/
    target = crouch ? EYE_HEIGHT_DEF * 0.62f : EYE_HEIGHT_DEF;
    pl->eyeHeight += (target - pl->eyeHeight) * clampf(dt * 12.0f, 0.0f, 1.0f);

    /* 头部晃动：幅度跟着速度走，站住不动就没有晃动。
       用 sin 的两倍频做上下、一倍频做左右，是最省事的"走路感"。*/
    pl->speedSmooth += (speed - pl->speedSmooth) * clampf(dt * 8.0f, 0.0f, 1.0f);
    if (p->headBob && pl->speedSmooth > 0.05f) {
        pl->bobPhase += dt * (6.2f + pl->speedSmooth * 0.35f);
        {
            float amp = 0.022f * clampf(pl->speedSmooth / p->moveSpeed, 0.0f, 1.0f);
            pl->bobOffset = sinf(pl->bobPhase * 2.0f) * amp;
            pl->bobSide   = sinf(pl->bobPhase) * amp * 0.7f;
        }
    } else {
        pl->bobOffset += (0.0f - pl->bobOffset) * clampf(dt * 8.0f, 0.0f, 1.0f);
        pl->bobSide   += (0.0f - pl->bobSide)   * clampf(dt * 8.0f, 0.0f, 1.0f);
    }
}

Vec3 playerEyePos(const Player *pl) {
    Vec3 r = playerRight(pl);
    return v3(pl->pos.x + r.x * pl->bobSide,
              pl->pos.y + pl->eyeHeight + pl->bobOffset,
              pl->pos.z + r.z * pl->bobSide);
}

Mat4 playerViewMatrix(const Player *pl) {
    Vec3 eye = playerEyePos(pl);
    Vec3 fwd = playerForward(pl);
    return mat4LookAt(eye, v3add(eye, fwd), v3(0.0f, 1.0f, 0.0f));
}

void playerRay(const Player *pl, Vec3 *outOrigin, Vec3 *outDir) {
    /* 射线从**眼睛**出发（含晃动），方向就是视线方向。
       与渲染用的是同一个 eye 与 fwd，所以准星指着什么就打中什么。*/
    if (outOrigin) *outOrigin = playerEyePos(pl);
    if (outDir)    *outDir = v3norm(playerForward(pl));
}
