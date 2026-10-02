/* ============================================================================
 * scene.h —— 训练房的静态几何与贴图
 *
 * 房间是固定的：地面、天花板、四面墙，正前方那面就是**气球墙**。
 * 出球区（气球能出现的那块矩形）由参数算出来，气球层会读它做拒绝采样，
 * 渲染层也会在墙上画一圈很淡的边框把这块区域标出来 —— 让玩家知道球会从
 * 哪儿冒出来，而不是凭空乱闪。
 * ==========================================================================*/
#ifndef SCENE_H
#define SCENE_H

#include "gfx.h"
#include "config.h"
#include "texture.h"

typedef struct {
    Texture wall;       /* 气球墙 */
    Texture floor;
    Texture ceiling;
    Texture side;       /* 侧墙与后墙共用 */
    Texture glow;       /* 灯的光晕 */
    Texture white;
    Texture spec;       /* 气球上的镜面高光贴片 */

    int     built;
    int     builtStyle; /* 当前贴图对应的 WallStyle，换风格时重建 */
    unsigned seed;
} Scene;

/* 出球区（墙面局部坐标，单位米；x 以墙面中轴为 0，y 以地面为 0）。*/
typedef struct {
    float x0, x1;
    float y0, y1;
} FieldRect;

void sceneBuild(Scene *s, const Params *p, unsigned seed);
void sceneDestroy(Scene *s);

/* 换墙面风格时调用：只重建与风格相关的贴图，不动别的。*/
void sceneRefreshStyle(Scene *s, const Params *p);

/* 算出生球区。纯函数，自检直接怼。*/
void sceneComputeField(const Params *p, FieldRect *out);

/* 画静态几何。调用前必须已经把相机矩阵设好。*/
void sceneDrawRoom(const Scene *s, const Params *p);
/* 墙面上的出球区边框 + 地面上的警戒线（画在几何之后）。*/
void sceneDrawMarkings(const Scene *s, const Params *p);

/* 把"墙面局部坐标 (x,y)"换算成世界坐标。z 由调用方给。*/
Vec3 sceneWallToWorld(float wx, float wy, float z);

#endif /* SCENE_H */
