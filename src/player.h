/* ============================================================================
 * player.h —— 第一人称相机
 *
 * 这里只放"玩家是什么状态、怎么动"，**不碰任何输入设备**。
 * 键鼠事件在 input.cpp 里被翻译成 playerLook() / playerMove() 两个调用，
 * 于是自检可以在没有窗口、没有鼠标的情况下把玩法层完整驱动一遍。
 * 这条解耦是整套验证能成立的前提。
 * ==========================================================================*/
#ifndef PLAYER_H
#define PLAYER_H

#include "core.h"
#include "config.h"

typedef struct {
    Vec3  pos;        /* 脚底位置（y 恒为 0，视点高度单独加） */
    float yaw;        /* 偏航（弧度）：0 = 面向 -z，也就是面向气球墙 */
    float pitch;      /* 俯仰（弧度），夹在 ±89° */
    float eyeHeight;  /* 当前视点高度（蹲下会变） */
    float bobPhase;   /* 头部晃动相位 */
    float bobOffset;  /* 本帧的晃动位移（上下 + 左右各算一次） */
    float bobSide;
    float speedSmooth;/* 平滑后的移动速度，用来驱动晃动幅度 */
} Player;

void  playerInit(Player *pl);

/* 输入接口 —— 自检直接调这两个。*/
void  playerLook(Player *pl, float dYawDeg, float dPitchDeg, const Params *p);
void  playerMove(Player *pl, const Params *p, Vec3 wishDir, float dt, int crouch);

Vec3  playerForward(const Player *pl);
Vec3  playerRight(const Player *pl);
Vec3  playerEyePos(const Player *pl);
Mat4  playerViewMatrix(const Player *pl);

/* 把位置夹回可行走区域（房间内、且不越过警戒线）。纯函数式地改 pos。*/
void  playerClampToRoom(Player *pl);

/* 相机射线：从视点沿视线方向，方向已归一化（含俯仰与头部晃动之后的真实朝向）。*/
void  playerRay(const Player *pl, Vec3 *outOrigin, Vec3 *outDir);

#endif /* PLAYER_H */
