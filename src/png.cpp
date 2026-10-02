/* ============================================================================
 * png.cpp —— PNG 编码器实现（stored deflate）
 *
 * PNG 的结构本身很简单：
 *   8 字节签名
 *   IHDR 块（宽高、位深、颜色类型…）
 *   IDAT 块（zlib 流，可以拆成多块）
 *   IEND 块
 * 每块是 [长度(4,大端)][类型(4)][数据][CRC32(4)]，CRC 覆盖"类型+数据"。
 *
 * zlib 流是 [2 字节头][deflate 数据][adler32(4,大端)]。
 * deflate 的 stored 块是 [1 字节块头][LEN(2,小端)][NLEN(2,小端,取反)][原始字节]，
 * 单个块最多 65535 字节。
 * ==========================================================================*/
#include "png.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ 大端写入 */

static void putU32BE(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)((v >> 24) & 0xFF);
    p[1] = (unsigned char)((v >> 16) & 0xFF);
    p[2] = (unsigned char)((v >> 8) & 0xFF);
    p[3] = (unsigned char)(v & 0xFF);
}

static uint32_t getU32BE(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* -------------------------------------------------------------- adler32 */

static uint32_t adler32Buf(const unsigned char *d, size_t n) {
    uint32_t a = 1, b = 0;
    size_t i;
    /* 每 5552 字节取一次模：这是 zlib 里用的经验值，保证 32 位不会溢出。*/
    for (i = 0; i < n; ++i) {
        a += d[i];
        if (a >= 65521u) a -= 65521u;
        b += a;
        if (b >= 65521u) b -= 65521u;
    }
    return (b << 16) | a;
}

/* ---------------------------------------------------------- 动态缓冲
 * 这里不用 STL（项目风格是 C 风格），所以自己管一个可增长的字节缓冲。
 * 只在编码期存活，用完就释放。
 */
typedef struct {
    unsigned char *p;
    size_t len;
    size_t cap;
    int    oom;
} Buf;

static void bufInit(Buf *b) { b->p = NULL; b->len = 0; b->cap = 0; b->oom = 0; }

static void bufFree(Buf *b) { free(b->p); bufInit(b); }

static void bufNeed(Buf *b, size_t extra) {
    if (b->oom) return;
    if (b->len + extra <= b->cap) return;
    {
        size_t nc = b->cap ? b->cap : 4096;
        unsigned char *np;
        while (nc < b->len + extra) nc *= 2;
        np = (unsigned char *)realloc(b->p, nc);
        if (!np) { b->oom = 1; return; }
        b->p = np;
        b->cap = nc;
    }
}

static void bufPut(Buf *b, const void *data, size_t n) {
    bufNeed(b, n);
    if (b->oom) return;
    memcpy(b->p + b->len, data, n);
    b->len += n;
}

/* 写一个完整的 PNG 块：长度 + 类型 + 数据 + CRC32(类型+数据)。*/
static void bufChunk(Buf *b, const char type[4], const unsigned char *data, size_t n) {
    unsigned char hdr[8];
    unsigned char crcBytes[4];
    uint32_t crc;

    putU32BE(hdr, (uint32_t)n);
    memcpy(hdr + 4, type, 4);
    bufPut(b, hdr, 8);
    if (n) bufPut(b, data, n);

    /* CRC32 覆盖"类型 + 数据"。crc32Buf 每次从头算，所以得把两段接起来
       一次性喂给它 —— 为此开一小块临时缓冲（最大也就一行 IDAT 的量级）。*/
    {
        unsigned char *tmp = (unsigned char *)malloc(4 + n);
        if (!tmp) { b->oom = 1; return; }
        memcpy(tmp, type, 4);
        if (n) memcpy(tmp + 4, data, n);
        crc = crc32Buf(tmp, 4 + n);
        free(tmp);
    }
    putU32BE(crcBytes, crc);
    bufPut(b, crcBytes, 4);
}

/* ------------------------------------------------------ zlib：stored 块 */

static void zlibStored(Buf *b, const unsigned char *raw, size_t n) {
    size_t off = 0;
    unsigned char zh[2];
    unsigned char ad[4];

    /* CMF = 0x78（CM=8 即 deflate，CINFO=7 即 32K 窗口）
       FLG = 0x01（无预设字典、最快压缩级别），且要求 (CMF<<8|FLG) % 31 == 0：
       0x7801 = 30721 = 31 * 991，整除，合法。*/
    zh[0] = 0x78; zh[1] = 0x01;
    bufPut(b, zh, 2);

    do {
        size_t chunk = n - off;
        unsigned char bh[5];
        int final;
        if (chunk > 65535) chunk = 65535;
        final = (off + chunk >= n) ? 1 : 0;
        /* 块头字节：bit0 = BFINAL，bit1-2 = BTYPE(00 = stored)，其余补零。*/
        bh[0] = (unsigned char)(final ? 1 : 0);
        bh[1] = (unsigned char)(chunk & 0xFF);
        bh[2] = (unsigned char)((chunk >> 8) & 0xFF);
        bh[3] = (unsigned char)((~chunk) & 0xFF);
        bh[4] = (unsigned char)(((~chunk) >> 8) & 0xFF);
        bufPut(b, bh, 5);
        bufPut(b, raw + off, chunk);
        off += chunk;
    } while (off < n);

    putU32BE(ad, adler32Buf(raw, n));
    bufPut(b, ad, 4);
}

/* -------------------------------------------------------- 编码主流程 */

unsigned char *pngEncodeRGB(int w, int h, const unsigned char *rgb, size_t *outLen) {
    static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    Buf out;
    Buf raw;
    unsigned char ihdr[13];
    unsigned char *result;
    int y;

    if (outLen) *outLen = 0;
    if (w <= 0 || h <= 0 || !rgb) return NULL;

    bufInit(&out);
    bufInit(&raw);

    /* 原始流 = 每行前面加一个过滤字节 0x00（None）。
       不做行间预测（filter 1..4）：stored 块不压缩，过滤不会让数据变小，
       反而给解码端多一层出错的可能。*/
    bufNeed(&raw, (size_t)h * ((size_t)w * 3 + 1));
    for (y = 0; y < h; ++y) {
        unsigned char zero = 0x00;
        bufPut(&raw, &zero, 1);
        bufPut(&raw, rgb + (size_t)y * (size_t)w * 3, (size_t)w * 3);
        if (raw.oom) break;
    }

    if (!raw.oom) {
        bufPut(&out, sig, 8);

        putU32BE(ihdr + 0, (uint32_t)w);
        putU32BE(ihdr + 4, (uint32_t)h);
        ihdr[8]  = 8;   /* 位深 */
        ihdr[9]  = 2;   /* 颜色类型 2 = 真彩色 RGB */
        ihdr[10] = 0;   /* 压缩方法：deflate */
        ihdr[11] = 0;   /* 过滤方法：标准 */
        ihdr[12] = 0;   /* 隔行：无 */
        bufChunk(&out, "IHDR", ihdr, 13);

        if (!out.oom) {
            /* IDAT 需要先把 zlib 流编出来，再整体当作一个块写进去。*/
            Buf z;
            bufInit(&z);
            zlibStored(&z, raw.p, raw.len);
            if (!z.oom) bufChunk(&out, "IDAT", z.p, z.len);
            else out.oom = 1;
            bufFree(&z);
        }

        if (!out.oom) bufChunk(&out, "IEND", NULL, 0);
    }

    bufFree(&raw);

    if (out.oom) { bufFree(&out); return NULL; }

    result = out.p;
    if (outLen) *outLen = out.len;
    return result;              /* 调用方负责 free */
}

int pngWriteRGB(const wchar_t *path, int w, int h, const unsigned char *rgb) {
    size_t len = 0;
    unsigned char *bytes = pngEncodeRGB(w, h, rgb, &len);
    FILE *f;
    if (!path || !bytes) return -1;
    /* 走宽字符路径：截图输出目录里带中文是常态（本项目目录名就是中文），
       用 fopen 会在非中文代码页下打不开。*/
    f = _wfopen(path, L"wb");
    if (!f) { free(bytes); return -1; }
    if (fwrite(bytes, 1, len, f) != len) { fclose(f); free(bytes); return -1; }
    fclose(f);
    free(bytes);
    return 0;
}

/* ============================================================ 解码（自检用）
 * 只认自己写出来的那一种：8 位真彩、无隔行、stored 块、无附加块。
 * 所以它的作用只有一个 —— 证明"编出来的字节流确实能被解回原样"。
 * 真正的独立校验交给 tests/check_png.ps1 里的 GDI+。
 */

typedef struct {
    const unsigned char *p;
    size_t len;
    size_t off;
} Reader;

static int rdBytes(Reader *r, void *dst, size_t n) {
    if (r->off + n > r->len) return -1;
    memcpy(dst, r->p + r->off, n);
    r->off += n;
    return 0;
}

/* 读一个块头，返回数据长度；类型写进 type[4]，数据起点写进 *dataOff。*/
static int rdChunk(Reader *r, char type[4], size_t *dataOff) {
    unsigned char hdr[8];
    if (rdBytes(r, hdr, 8) != 0) return -1;
    memcpy(type, hdr + 4, 4);
    *dataOff = r->off;
    return (int)getU32BE(hdr);
}

int pngRoundTripRGB(int w, int h, const unsigned char *rgb, size_t *outBadIndex) {
    unsigned char *bytes;
    size_t len = 0;
    Reader r;
    static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    int rc = -1;
    int gotW = 0, gotH = 0;
    unsigned char *raw = NULL;
    size_t rawLen = 0, rawCap = 0;
    size_t stride = (size_t)w * 3 + 1;

    if (outBadIndex) *outBadIndex = 0;
    bytes = pngEncodeRGB(w, h, rgb, &len);
    if (!bytes) return -1;

    r.p = bytes; r.len = len; r.off = 0;
    rawCap = stride * (size_t)h;
    raw = (unsigned char *)malloc(rawCap);
    if (!raw) { free(bytes); return -1; }

    if (r.len < 8 || memcmp(r.p, sig, 8) != 0) goto done;
    r.off = 8;

    for (;;) {
        char type[5];
        size_t dataOff;
        int n = rdChunk(&r, type, &dataOff);
        if (n < 0) goto done;
        type[4] = '\0';

        if (memcmp(type, "IHDR", 4) == 0) {
            if (n != 13) goto done;
            gotW = (int)getU32BE(r.p + dataOff);
            gotH = (int)getU32BE(r.p + dataOff + 4);
        } else if (memcmp(type, "IDAT", 4) == 0) {
            /* 数据 = zlib 头(2) + 若干 stored 块 + adler32(4)。*/
            const unsigned char *d = r.p + dataOff;
            size_t dn = (size_t)n;
            size_t i = 2;
            if (dn < 6) goto done;
            if (d[0] != 0x78) goto done;
            while (i + 5 <= dn) {
                unsigned int bfinal, btype, blen, nlen;
                if (i + 5 > dn) break;
                bfinal = d[i] & 1u;
                btype  = (d[i] >> 1) & 3u;
                if (btype != 0) goto done;      /* 只认 stored */
                blen = (unsigned int)d[i + 1] | ((unsigned int)d[i + 2] << 8);
                nlen = (unsigned int)d[i + 3] | ((unsigned int)d[i + 4] << 8);
                if ((blen ^ 0xFFFFu) != nlen) goto done;
                i += 5;
                if (i + blen > dn) goto done;
                if (rawLen + blen > rawCap) goto done;
                memcpy(raw + rawLen, d + i, blen);
                rawLen += blen;
                i += blen;
                if (bfinal) break;
            }
            /* adler32 校验 */
            if (i + 4 <= dn) {
                uint32_t want = getU32BE(d + i);
                if (want != adler32Buf(raw, rawLen)) goto done;
            }
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }
        r.off = dataOff + (size_t)n + 4;    /* 跳过 CRC */
    }

    if (gotW != w || gotH != h) goto done;
    if (rawLen != rawCap) goto done;

    /* 逐像素比对（过滤字节都是 0，直接跳过）。*/
    {
        int y;
        for (y = 0; y < h; ++y) {
            const unsigned char *gotRow = raw + (size_t)y * stride;
            const unsigned char *wantRow = rgb + (size_t)y * (size_t)w * 3;
            int x;
            if (gotRow[0] != 0x00) goto done;
            for (x = 0; x < w * 3; ++x) {
                if (gotRow[1 + x] != wantRow[x]) {
                    if (outBadIndex) *outBadIndex = (size_t)y * (size_t)w * 3 + (size_t)x;
                    goto done;
                }
            }
        }
    }
    rc = 0;

done:
    free(raw);
    free(bytes);
    return rc;
}
