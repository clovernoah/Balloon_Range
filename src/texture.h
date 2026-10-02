/* ============================================================================
 * texture.h —— 程序生成的纹理
 *
 * 一张图片文件都不带（零素材最省事，
 * 而且程序生成意味着换风格只要改参数、不需要重新打包）。墙面四种风格、地面、
 * 天花板、光晕全部是这里用值噪声算出来的。
 * ==========================================================================*/
#ifndef TEXTURE_H
#define TEXTURE_H

#include "gfx.h"
#include "config.h"

typedef struct {
    GLuint id;
    int    w, h;
    int    ok;
} Texture;

void texDestroy(Texture *t);

/* 从 RGB8 数据建纹理（自动生成 mipmap、设环绕与过滤）。成功返回 0。*/
int  texFromRGB(Texture *t, int w, int h, const unsigned char *rgb);

/* 从 RGBA8 数据建纹理，线性过滤 + 夹边。HUD 的字形图集走这条路：
   RGB 全白、A 是字形覆盖度，绘制时用 glColor 染色即可。*/
int  texFromRGBA(Texture *t, int w, int h, const unsigned char *rgba);

/* 1x1 纯白：画纯色块时绑它，省得关纹理开关。*/
int  texMakeWhite(Texture *t);

/* 径向渐变光斑（RGBA，中心白边缘透明），用于灯具光晕与地面残斑的柔边。*/
int  texMakeGlow(Texture *t, int size);

/* 墙面贴图。style 取 WallStyle；seed 决定噪声图案，换种子 = 换一面墙的细节。*/
int  texMakeWall(Texture *t, int style, unsigned seed);

/* 地面：tint 0 偏冷（蓝灰）、1 偏暖（棕灰）。*/
int  texMakeFloor(Texture *t, float tint, unsigned seed);

/* 天花板：比地面更暗更平，只加一点点噪声避免死板。*/
int  texMakeCeiling(Texture *t, float tint);

/* 气球用的软高光贴片：中心白、边缘透明，贴在气球上做镜面点。
   固定管线的逐顶点高光在大球上会显得"糊"，叠一层贴片更接近理想观感。*/
int  texMakeSpec(Texture *t, int size);

#endif /* TEXTURE_H */
