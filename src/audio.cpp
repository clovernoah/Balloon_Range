/* ============================================================================
 * audio.cpp —— 合成音效与伪 3D 声像
 *
 * 三层，界限分明：
 *   1) 合成：纯函数。给一个音效号与半音偏移，吐出一段单声道 PCM。
 *      不碰设备、不碰线程 —— 所以自检可以在没有声卡的机器上把它验到底。
 *   2) 混音：一个线程 + 三块缓冲 + 十个声部。声部里有"从哪儿取、取到哪、
 *      左右各多响"，混音就是一层最朴素的加法。
 *   3) 设备：waveOut。开不出来就算了，整个模块退化成空操作。
 *
 * 参数选择都写了理由：包络、频率、衰减曲线不是随手填的数。
 * ==========================================================================*/
#include "audio.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================ 合成小工具 */

static float envExp(float t, float k) {
    /* 指数衰减包络。t 秒之后剩 e^(-k t)：k 越大"死"得越快。
       击破音用 34（那一"啪"要脆），漏球音用 8（那一声要闷且拖一点尾巴）。*/
    return expf(-t * k);
}

static float noiseOf(Rng *r) {
    return rngFloat(r) * 2.0f - 1.0f;
}

/* 把浮点样本收进 16 位。硬削顶 —— 混音层最多十来个短瞬态叠在一起，
   偶尔擦到顶最多是"响了一下"，不值得为它上软削波。*/
static short toPcm(float s) {
    s = clampf(s, -1.0f, 1.0f);
    return (short)(s * 32000.0f);
}

/* ------------------------------------------------------------ 击破音
 * 两个成分：
 *   瞬态：白噪声 × 快速衰减（k = 34）。气球"啪"的那一下主要靠它。
 *   音身：从 1250 Hz 扫到 280 Hz 的正弦 × 慢一点衰减（k = 15）。
 *         扫频让听感像"啵"而不是"嘀"；音高倍率只作用在这个成分上。
 * 为什么音高只升不降、最多一个八度：升调是正反馈（连击越高越亮），
 * 一个八度以内不会尖到刺耳，也不至于高到听不见。*/
static int synthPop(short *out, int maxFrames, float pitch) {
    Rng r;
    float phase = 0.0f;
    int i, n = (int)(0.30f * (float)AUDIO_RATE);

    rngSeed(&r, 20260930u);      /* 噪声也固定种子：同一局同样打法，声音一致 */
    if (n > maxFrames) n = maxFrames;

    for (i = 0; i < n; ++i) {
        float t = (float)i / (float)AUDIO_RATE;
        float f = lerpf(1250.0f, 280.0f, clampf(t / 0.12f, 0.0f, 1.0f)) * pitch;
        float s;
        phase += 2.0f * PI_F * f / (float)AUDIO_RATE;
        s  = 0.55f * sinf(phase) * envExp(t, 15.0f);
        s += 0.85f * noiseOf(&r) * envExp(t, 34.0f);
        out[i] = toPcm(s * 0.9f);
    }
    return n;
}

/* ------------------------------------------------------------ 漏球音
 * 一声往下坠的闷响：200 Hz 扫到 90 Hz，衰减慢（k = 8），
 * 再掺一点噪声当"泄气"的质感。听感上与击破音完全区分得开 ——
 * 负反馈必须一耳朵就听出来，而不是"刚才那是什么响了一下"。*/
static int synthMiss(short *out, int maxFrames) {
    Rng r;
    float phase = 0.0f;
    int i, n = (int)(0.28f * (float)AUDIO_RATE);

    rngSeed(&r, 20260931u);
    if (n > maxFrames) n = maxFrames;

    for (i = 0; i < n; ++i) {
        float t = (float)i / (float)AUDIO_RATE;
        float f = lerpf(200.0f, 90.0f, clampf(t / 0.20f, 0.0f, 1.0f));
        float s;
        phase += 2.0f * PI_F * f / (float)AUDIO_RATE;
        s  = 0.60f * sinf(phase) * envExp(t, 8.0f);
        s += 0.22f * noiseOf(&r) * envExp(t, 20.0f);
        out[i] = toPcm(s);
    }
    return n;
}

/* ------------------------------------------------------------ 界面音
 * 一个短促的高音"嘀"：900 Hz、衰减极快（60）、只有 60 毫秒。
 * 参数面板上按着方向键连续拨动时它会连成一片，所以必须短、必须轻。*/
static int synthUi(short *out, int maxFrames) {
    float phase = 0.0f;
    int i, n = (int)(0.06f * (float)AUDIO_RATE);
    if (n > maxFrames) n = maxFrames;
    for (i = 0; i < n; ++i) {
        float t = (float)i / (float)AUDIO_RATE;
        phase += 2.0f * PI_F * 900.0f / (float)AUDIO_RATE;
        out[i] = toPcm(0.35f * sinf(phase) * envExp(t, 60.0f));
    }
    return n;
}

/* ------------------------------------------------------------ 收尾音
 * 三个音依次起来：C5 / E5 / G5，各自 6 毫秒起振（防"咔"）、指数收尾。
 * 三度堆叠是大三和弦，简单但听起来是"结束了"，不是"出错了"。*/
static int synthEnd(short *out, int maxFrames) {
    const float freq[3] = { 523.25f, 659.25f, 783.99f };   /* C5 E5 G5 */
    const float start[3] = { 0.00f, 0.13f, 0.26f };
    float phase[3] = { 0.0f, 0.0f, 0.0f };
    int i, k, n = (int)(0.60f * (float)AUDIO_RATE);

    if (n > maxFrames) n = maxFrames;

    for (i = 0; i < n; ++i) {
        float t = (float)i / (float)AUDIO_RATE;
        float s = 0.0f;
        for (k = 0; k < 3; ++k) {
            float lt = t - start[k];
            float atk;
            if (lt < 0.0f) continue;
            atk = clampf(lt / 0.006f, 0.0f, 1.0f);
            phase[k] += 2.0f * PI_F * freq[k] / (float)AUDIO_RATE;
            s += 0.38f * sinf(phase[k]) * atk * envExp(lt, 7.0f);
        }
        out[i] = toPcm(s);
    }
    return n;
}

int audioRenderClip(int sfx, int semitone, short *out, int maxFrames) {
    float pitch;
    if (!out || maxFrames <= 0) return 0;

    semitone = clampi(semitone, 0, AUDIO_SEMITONE_MAX);
    /* 2^(n/12)：等程律的半音倍率。+12 正好翻一倍频。*/
    pitch = powf(2.0f, (float)semitone / 12.0f);

    switch (sfx) {
    case SFX_POP:  return synthPop(out, maxFrames, pitch);
    case SFX_MISS: return synthMiss(out, maxFrames);
    case SFX_UI:   return synthUi(out, maxFrames);
    case SFX_END:  return synthEnd(out, maxFrames);
    default:       return 0;
    }
}

/* ============================================================ 声像与衰减 */

void audioPanGains(float pan, float *outL, float *outR) {
    /* 等功率声像：把 pan 映射到 0..90°，左右取 cos / sin。
       这样 L² + R² 恒等于 1 —— 声源从一边扫到另一边时总响度不变。
       若图省事写成 (1-pan)/2 与 (1+pan)/2，中间会凹下去 3 dB，
       听感上就是"走到正前方突然变小了"。*/
    float ang = (clampf(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * PI_F;
    if (outL) *outL = cosf(ang);
    if (outR) *outR = sinf(ang);
}

float audioDistanceGain(float dist) {
    /* 1/(1+0.28d)：0 米为 1，约 3.6 米掉一半，远处趋近 0 但永不为 0。
       不用 1/d —— 那在贴近 0 时会炸，而气球墙正好就在玩家正前方几米处。*/
    if (dist < 0.0f) dist = 0.0f;
    return 1.0f / (1.0f + dist * 0.28f);
}

/* ============================================================ WAV 写出
 * 44 字节标准头，16 位 PCM。不写 LIST/fact 之类的附加块 ——
 * 播放器不依赖它们，写多了反而多一处可能写错的地方。*/
int audioWriteWav(const wchar_t *path, const short *frames,
                  int frameCount, int channels, int rate) {
    FILE *f;
    unsigned char h[44];
    uint32_t dataBytes, byteRate;
    uint16_t blockAlign;
    int i = 0;

    if (!path || !frames || frameCount <= 0 || channels <= 0) return -1;
    dataBytes = (uint32_t)frameCount * (uint32_t)channels * 2u;
    byteRate  = (uint32_t)rate * (uint32_t)channels * 2u;
    blockAlign = (uint16_t)(channels * 2);

#define PUT4(v) do { uint32_t _v = (uint32_t)(v); \
        h[i++] = (unsigned char)(_v & 0xFF); h[i++] = (unsigned char)((_v >> 8) & 0xFF); \
        h[i++] = (unsigned char)((_v >> 16) & 0xFF); h[i++] = (unsigned char)((_v >> 24) & 0xFF); } while (0)
#define PUT2(v) do { uint16_t _v = (uint16_t)(v); \
        h[i++] = (unsigned char)(_v & 0xFF); h[i++] = (unsigned char)((_v >> 8) & 0xFF); } while (0)

    memcpy(h + i, "RIFF", 4); i += 4;
    PUT4(36u + dataBytes);
    memcpy(h + i, "WAVEfmt ", 8); i += 8;
    PUT4(16u);                       /* fmt 块长度 */
    PUT2(1u);                        /* PCM */
    PUT2((uint16_t)channels);
    PUT4((uint32_t)rate);
    PUT4(byteRate);
    PUT2(blockAlign);
    PUT2(16u);                       /* 位深 */
    memcpy(h + i, "data", 4); i += 4;
    PUT4(dataBytes);
#undef PUT4
#undef PUT2

    f = _wfopen(path, L"wb");
    if (!f) return -1;
    if (fwrite(h, 1, sizeof(h), f) != sizeof(h) ||
        fwrite(frames, 1, dataBytes, f) != dataBytes) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

/* ============================================================ 音效库
 * 启动时一次性把 16 段 PCM 合成好（击破音 13 个半音变体 + 另三个各一份）。
 * 合成一次几十毫秒，换来播放时只是"找一段现成的内存" —— 播放在开火那一帧
 * 上跑，不能有任何算力开销。*/

static int ensureClips(Audio *a) {
    int s, k;
    if (a->clipsReady) return 0;
    for (s = 0; s < SFX_COUNT; ++s) {
        int variants = (s == SFX_POP) ? (AUDIO_SEMITONE_MAX + 1) : 1;
        for (k = 0; k < variants; ++k) {
            SfxClip *c = &a->clips[s][k];
            short *buf = (short *)malloc(sizeof(short) * AUDIO_MAX_FRAMES);
            if (!buf) { a->clipsReady = 0; return -1; }
            c->frames = audioRenderClip(s, k, buf, AUDIO_MAX_FRAMES);
            if (c->frames <= 0) { free(buf); c->pcm = NULL; c->frames = 0; }
            else                { c->pcm = buf; }
        }
    }
    a->clipsReady = 1;
    return 0;
}

static void freeClips(Audio *a) {
    int s, k;
    for (s = 0; s < SFX_COUNT; ++s)
        for (k = 0; k <= AUDIO_SEMITONE_MAX; ++k) {
            free(a->clips[s][k].pcm);
            a->clips[s][k].pcm = NULL;
            a->clips[s][k].frames = 0;
        }
    a->clipsReady = 0;
}

int audioDumpClips(const wchar_t *dir, int *outCount) {
    Audio tmp;
    wchar_t path[MAX_PATH];
    const wchar_t *names[SFX_COUNT];
    int s, k, written = 0;

    names[SFX_POP]  = L"sfx_pop";
    names[SFX_MISS] = L"sfx_miss";
    names[SFX_UI]   = L"sfx_ui";
    names[SFX_END]  = L"sfx_end";

    memset(&tmp, 0, sizeof(tmp));
    if (ensureClips(&tmp) != 0) return -1;

    for (s = 0; s < SFX_COUNT; ++s) {
        int variants = (s == SFX_POP) ? (AUDIO_SEMITONE_MAX + 1) : 1;
        for (k = 0; k < variants; ++k) {
            SfxClip *c = &tmp.clips[s][k];
            if (!c->pcm || c->frames <= 0) continue;
            if (s == SFX_POP)
                _snwprintf(path, ARRAY_COUNT(path) - 1, L"%ls\\%ls_%02d.wav",
                           dir, names[s], k);
            else
                _snwprintf(path, ARRAY_COUNT(path) - 1, L"%ls\\%ls.wav", dir, names[s]);
            path[ARRAY_COUNT(path) - 1] = L'\0';
            if (audioWriteWav(path, c->pcm, c->frames, 1, AUDIO_RATE) == 0) ++written;
        }
    }
    freeClips(&tmp);
    if (outCount) *outCount = written;
    return (written > 0) ? 0 : -1;
}

/* ============================================================ 混音 */

/* 把一块立体声缓冲填满。调用时必须已持有 lock。*/
static void mixBlock(Audio *a, short *dst) {
    float vol = a->muted ? 0.0f : clampf(a->volume, 0.0f, 1.0f);
    int active = 0;
    int i, v;

    for (i = 0; i < AUDIO_BUF_FRAMES; ++i) {
        float l = 0.0f, r = 0.0f;
        for (v = 0; v < AUDIO_VOICES; ++v) {
            AudioVoice *vo = &a->voice[v];
            if (!vo->pcm || vo->pos >= vo->frames) continue;
            {
                float s = (float)vo->pcm[vo->pos] / 32768.0f;
                l += s * vo->gainL;
                r += s * vo->gainR;
            }
            /* 不管静音与否都推进位置：不然解除静音时会从半截处接着响。*/
            vo->pos++;
        }
        dst[i * 2 + 0] = toPcm(l * vol);
        dst[i * 2 + 1] = toPcm(r * vol);
    }
    for (v = 0; v < AUDIO_VOICES; ++v)
        if (a->voice[v].pcm && a->voice[v].pos < a->voice[v].frames) ++active;
    InterlockedExchange(&a->activeVoices, (LONG)active);
}

static DWORD WINAPI mixThread(LPVOID param) {
    Audio *a = (Audio *)param;
    for (;;) {
        DWORD w = WaitForSingleObject(a->evt, 200);
        int i;
        if (a->stop) break;
        if (w != WAIT_OBJECT_0 && w != WAIT_TIMEOUT) continue;

        EnterCriticalSection(&a->lock);
        for (i = 0; i < AUDIO_BUFS; ++i) {
            if (a->hdr[i].dwFlags & WHDR_DONE) {
                mixBlock(a, a->buf[i]);
                a->hdr[i].dwFlags &= ~WHDR_DONE;
                a->hdr[i].dwBufferLength = (DWORD)(AUDIO_BUF_FRAMES * AUDIO_CHANNELS * sizeof(short));
                waveOutWrite(a->wo, &a->hdr[i], sizeof(WAVEHDR));
            }
        }
        LeaveCriticalSection(&a->lock);
    }
    return 0;
}

/* ============================================================ 设备层 */

/* --no-audio 的受控实验开关：非 0 时 audioOpen 跳过 waveOutOpen 直接失败。
   默认 0 —— 正式运行路径上这一个分支根本不会被走到。*/
static MMRESULT g_forceMmError = 0;

void audioForceFail(int mmError) { g_forceMmError = (MMRESULT)mmError; }

static void describeMmError(MMRESULT mr, wchar_t *out, int count) {
    const wchar_t *why;
    switch (mr) {
    case MMSYSERR_NODRIVER:     why = L"没有音频驱动"; break;
    case MMSYSERR_ALLOCATED:    why = L"设备被别的程序独占"; break;
    case MMSYSERR_BADDEVICEID:  why = L"设备号无效"; break;
    case MMSYSERR_NOTENABLED:   why = L"驱动未启用"; break;
    case MMSYSERR_NOMEM:        why = L"内存不足"; break;
    case WAVERR_BADFORMAT:      why = L"设备不接受这个采样格式"; break;
    default:                    why = L"未知原因"; break;
    }
    _snwprintf(out, (size_t)(count - 1), L"音频设备打开失败：%ls（%u）", why, (unsigned)mr);
    out[count - 1] = L'\0';
}

int audioOpen(Audio *a, HWND hwnd, wchar_t *outMsg, int msgCount) {
    WAVEFORMATEX fmt;
    MMRESULT mr;
    int i;

    if (!a) return -1;
    memset(a, 0, sizeof(*a));
    a->volume = 0.7f;
    a->hwnd = hwnd;
    a->tried = 1;

    if (ensureClips(a) != 0) {
        a->failed = 1;
        _snwprintf(a->statusMsg, ARRAY_COUNT(a->statusMsg) - 1,
                   L"音效合成失败：内存不足");
        if (outMsg) _snwprintf(outMsg, (size_t)(msgCount - 1), L"%ls", a->statusMsg);
        return -1;
    }

    InitializeCriticalSection(&a->lock);
    a->lockInit = 1;
    a->evt = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!a->evt) {
        a->failed = 1;
        DeleteCriticalSection(&a->lock);
        a->lockInit = 0;
        _snwprintf(a->statusMsg, ARRAY_COUNT(a->statusMsg) - 1, L"音频事件创建失败");
        if (outMsg) _snwprintf(outMsg, (size_t)(msgCount - 1), L"%ls", a->statusMsg);
        return -1;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.wFormatTag      = WAVE_FORMAT_PCM;
    fmt.nChannels       = AUDIO_CHANNELS;
    fmt.nSamplesPerSec  = AUDIO_RATE;
    fmt.wBitsPerSample  = 16;
    fmt.nBlockAlign     = (WORD)(fmt.nChannels * fmt.wBitsPerSample / 8);
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    fmt.cbSize          = 0;

    /* CALLBACK_EVENT 而不是 CALLBACK_FUNCTION：回调里能做的事情限制很多，
       用事件把活交给自己的线程，混音、加锁、数声部都不受约束。*/
    if (g_forceMmError) {
        mr = g_forceMmError;    /* --no-audio：不碰设备，直接按这个码走失败分支 */
    } else {
        mr = waveOutOpen(&a->wo, WAVE_MAPPER, &fmt, (DWORD_PTR)a->evt, 0, CALLBACK_EVENT);
    }
    if (mr != MMSYSERR_NOERROR) {
        a->wo = NULL;
        a->failed = 1;
        CloseHandle(a->evt);
        a->evt = NULL;
        DeleteCriticalSection(&a->lock);
        a->lockInit = 0;
        describeMmError(mr, a->statusMsg, (int)ARRAY_COUNT(a->statusMsg));
        if (outMsg) _snwprintf(outMsg, (size_t)(msgCount - 1), L"%ls", a->statusMsg);
        return -1;
    }

    for (i = 0; i < AUDIO_BUFS; ++i) {
        memset(&a->hdr[i], 0, sizeof(WAVEHDR));
        a->hdr[i].lpData = (LPSTR)a->buf[i];
        a->hdr[i].dwBufferLength = (DWORD)sizeof(a->buf[i]);
        waveOutPrepareHeader(a->wo, &a->hdr[i], sizeof(WAVEHDR));
        mixBlock(a, a->buf[i]);
        a->hdr[i].dwFlags &= ~WHDR_DONE;
        waveOutWrite(a->wo, &a->hdr[i], sizeof(WAVEHDR));
    }

    a->thread = CreateThread(NULL, 0, mixThread, a, 0, NULL);
    if (!a->thread) {
        a->failed = 1;
        waveOutReset(a->wo);
        for (i = 0; i < AUDIO_BUFS; ++i)
            waveOutUnprepareHeader(a->wo, &a->hdr[i], sizeof(WAVEHDR));
        waveOutClose(a->wo);
        a->wo = NULL;
        CloseHandle(a->evt);
        a->evt = NULL;
        DeleteCriticalSection(&a->lock);
        a->lockInit = 0;
        _snwprintf(a->statusMsg, ARRAY_COUNT(a->statusMsg) - 1, L"混音线程创建失败");
        if (outMsg) _snwprintf(outMsg, (size_t)(msgCount - 1), L"%ls", a->statusMsg);
        return -1;
    }

    a->ready = 1;
    a->failed = 0;
    _snwprintf(a->statusMsg, ARRAY_COUNT(a->statusMsg) - 1,
               L"音频就绪（%d Hz 立体声，四种合成音效）", AUDIO_RATE);
    if (outMsg) _snwprintf(outMsg, (size_t)(msgCount - 1), L"%ls", a->statusMsg);
    return 0;
}

void audioClose(Audio *a) {
    int i;
    if (!a) return;
    if (!a->wo && !a->evt && !a->thread) { freeClips(a); return; }

    /* 顺序要紧：先让线程退出，再动设备。反过来的话线程可能正在
       waveOutWrite，而这边已经把设备关了 —— 那是一次必崩的竞争。
       置 stop 之后线程从 WaitForSingleObject 返回、看到 stop 就跳出循环。*/
    a->stop = 1;
    if (a->evt) SetEvent(a->evt);
    if (a->thread) {
        WaitForSingleObject(a->thread, 3000);
        CloseHandle(a->thread);
        a->thread = NULL;
    }
    if (a->wo) {
        waveOutReset(a->wo);
        for (i = 0; i < AUDIO_BUFS; ++i)
            waveOutUnprepareHeader(a->wo, &a->hdr[i], sizeof(WAVEHDR));
        waveOutClose(a->wo);
        a->wo = NULL;
    }
    if (a->evt) { CloseHandle(a->evt); a->evt = NULL; }
    if (a->lockInit) { DeleteCriticalSection(&a->lock); a->lockInit = 0; }
    a->ready = 0;
    freeClips(a);
}

void audioPlay(Audio *a, int sfx, float pan, float dist, int semitone) {
    float gl, gr, att;
    int v, slot = -1;

    /* 没开成设备 / 没初始化 —— 静默返回。整条链路都允许音频不存在，
       这是"无声卡也能玩"的唯一保证，也是自检里专门验的一条。*/
    if (!a || !a->ready) return;
    if (sfx < 0 || sfx >= SFX_COUNT) return;

    semitone = clampi(semitone, 0, AUDIO_SEMITONE_MAX);
    if (sfx != SFX_POP) semitone = 0;
    if (!a->clips[sfx][semitone].pcm) return;

    audioPanGains(pan, &gl, &gr);
    att = audioDistanceGain(dist);

    EnterCriticalSection(&a->lock);
    for (v = 0; v < AUDIO_VOICES; ++v)
        if (!a->voice[v].pcm || a->voice[v].pos >= a->voice[v].frames) { slot = v; break; }
    if (slot < 0) slot = 0;      /* 声部用光了就抢第 0 个 —— 连打时抢掉最旧的那声，听感上最不突兀 */
    a->voice[slot].pcm   = a->clips[sfx][semitone].pcm;
    a->voice[slot].frames = a->clips[sfx][semitone].frames;
    a->voice[slot].pos   = 0;
    a->voice[slot].gainL = gl * att;
    a->voice[slot].gainR = gr * att;
    LeaveCriticalSection(&a->lock);
}

void audioStopAll(Audio *a) {
    int v;
    if (!a || !a->ready) return;
    EnterCriticalSection(&a->lock);
    for (v = 0; v < AUDIO_VOICES; ++v) {
        a->voice[v].pos = a->voice[v].frames;
        a->voice[v].pcm = NULL;
    }
    LeaveCriticalSection(&a->lock);
}

void audioSetVolume(Audio *a, float v) {
    if (!a) return;
    a->volume = clampf(v, 0.0f, 1.0f);
}

void audioSetMuted(Audio *a, int muted) {
    if (!a) return;
    a->muted = muted ? 1 : 0;
}

const wchar_t *audioStatusText(const Audio *a) {
    if (!a) return L"音频未初始化";
    if (a->statusMsg[0]) return a->statusMsg;
    return L"音频未初始化";
}
