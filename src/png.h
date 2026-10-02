/* ============================================================================
 * png.h —— 自写 PNG 编码器
 *
 * 为什么要自己写：`--shot` 出的图是整个验证体系的主要证据，不能让证据的
 * 产生依赖外部工具；而这个项目坚持零第三方依赖，所以 PNG 自己编。
 *
 * 实现范围：8 位 / 真彩色(RGB) / 无隔行 / 无附加块。压缩用 zlib 的
 * **stored（不压缩）块** —— 合法、简单、绝不编错，代价是文件大。
 *
 * 已知取舍：stored 块不做熵编码，一张 1600x900 的
 * 截图约 4.3 MB。换一个自适应 Huffman + LZ77 能压到十分之一，但那是另一个
 * 量级的代码量和出错面。这里选了"证据优先、先保证一定看得开"。
 * ==========================================================================*/
#ifndef PNG_H
#define PNG_H

#include "core.h"

/* 写一张 PNG。rgb 是自上而下的 RGB8 行序（每行 w*3 字节，无行填充）。
 * 成功返回 0，失败返回非 0。
 * 路径收宽字符：输出目录常带中文，窄路径在非中文代码页下会打不开。*/
int pngWriteRGB(const wchar_t *path, int w, int h, const unsigned char *rgb);

/* 把内存里编码出来的字节流交出去（自检用，不落盘）。
 * 返回 malloc 出来的缓冲，长度写进 *outLen；调用方负责 free。*/
unsigned char *pngEncodeRGB(int w, int h, const unsigned char *rgb, size_t *outLen);

/* 自检用：编码一遍再**自己解回来**，逐像素比对。
 * 一致返回 0，不一致返回非 0 并把第一处差异的位置写进 *outBadIndex。
 *
 * 注意这条断言的分量：它同时验证了编码器和解码器，但两者出自同一份理解，
 * 所以**不能替代外部校验**。外部校验另有其人 —— tests/check_png.ps1 用
 * Windows 自带的 GDI+ 解码同一张图，那是完全独立的实现。*/
int pngRoundTripRGB(int w, int h, const unsigned char *rgb, size_t *outBadIndex);

#endif /* PNG_H */
