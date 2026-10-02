/* ============================================================================
 * audio.h —— 合成音效与伪 3D 声像
 *
 * 不读任何音频文件：四个音效都是**启动时用代码合成**出来的 PCM，
 * 所以整个程序除了 Windows 自带的 winmm 之外不依赖任何东西，
 * 因此不需要任何 .wav 素材文件，音效全部现算现用。
 *
 * 声像：按气球相对玩家的左右方位做等功率声像（constant power pan），
 * 再按距离做衰减。这就是"伪 3D"那一半 —— waveOut 出立体声是免费的。
 *
 * **设备开不出来不算错误**：没有声卡、被独占、远程会话里都可能失败。
 * 那种情况下这一层整体退化成空操作，游戏照跑，只是没声音 ——
 * 自检里有一条断言专门盯着"没开设备时调用任何接口都不许崩"。
 * ==========================================================================*/
#ifndef AUDIO_H
#define AUDIO_H

#include "core.h"
/* WIN32_LEAN_AND_MEAN 会把 mmsystem.h 挡在 windows.h 外面，所以这里显式引。
   WAVEHDR / HWAVEOUT 直接进结构体，不搞 void* 遮掩 —— 零依赖不等于零类型。*/
#include <mmsystem.h>

#define AUDIO_RATE          44100
#define AUDIO_CHANNELS      2
#define AUDIO_VOICES        10      /* 同时最多混这么多个音 */
#define AUDIO_SEMITONE_MAX  12      /* 连击最多把击破音升一个八度 */
#define AUDIO_MAX_FRAMES    26460   /* 单个音效最长 0.6 秒，合成按此上限 */
#define AUDIO_BUFS          3       /* 三块缓冲轮转，够躲过一次调度抖动 */
#define AUDIO_BUF_FRAMES    1024    /* 每块约 23 ms */

typedef enum {
    SFX_POP = 0,    /* 击破（音高随连击上升） */
    SFX_MISS,       /* 漏球 / 超时逃走 */
    SFX_UI,         /* 面板拨动、模式切换 */
    SFX_END,        /* 本局结束的收尾 */
    SFX_COUNT
} SfxId;

/* ------------------------------------------------------------------ 纯函数
 * 下面三件事不碰设备、不碰线程，自检直接怼。
 */

/* 合成一个音效。semitone 是半音偏移（只有 SFX_POP 认它，其余音效忽略）。
 * 写进 out 的是**单声道** 16 位样本，返回实际帧数；maxFrames 不够则截断。*/
int audioRenderClip(int sfx, int semitone, short *out, int maxFrames);

/* 等功率声像：pan ∈ [-1,1]（-1 全左，+1 全右）。
 * pan = 0 时两边都是 0.7071 —— 不是 1.0。这样左右扫动时总功率不变，
 * 听起来不会"走到中间突然变响"。*/
void audioPanGains(float pan, float *outL, float *outR);

/* 距离衰减：0 米为 1，越远越小，永不为负也不会除零。*/
float audioDistanceGain(float dist);

/* 写一个 16 位 PCM 的 .wav（自写 44 字节头，零依赖）。
 * 把合成出来的音效导出成文件，人耳能直接听 —— 这是"音质没法自动验证"
 * 这件事唯一的补偿手段。返回 0 成功。*/
int audioWriteWav(const wchar_t *path, const short *frames,
                  int frameCount, int channels, int rate);

/* ------------------------------------------------------------------ 设备层 */

typedef struct {
    short  *pcm;
    int     frames;
} SfxClip;

typedef struct {
    const short *pcm;
    int          frames;
    int          pos;
    float        gainL, gainR;
} AudioVoice;

typedef struct {
    int           ready;        /* 设备已就绪 */
    int           tried;
    int           failed;       /* 试过但没开成 */
    int           muted;
    float         volume;       /* 0..1 */

    HWND          hwnd;
    HWAVEOUT      wo;
    HANDLE        evt;          /* 混音线程等的就是它（WOM_DONE 触发） */
    HANDLE        thread;
    CRITICAL_SECTION lock;      /* 混音线程与主线程共用 */
    int           lockInit;     /* 锁是否已初始化 —— 开设备失败过的对象不能再删一次锁 */
    int           stop;
    volatile LONG activeVoices; /* 仅用于 HUD 上"正在响几个音"，不参与逻辑 */

    SfxClip       clips[SFX_COUNT][AUDIO_SEMITONE_MAX + 1];
    int           clipsReady;
    wchar_t       statusMsg[96];  /* 当前状态的中文说明：开成了还是为什么没开成 */

    AudioVoice    voice[AUDIO_VOICES];
    short         buf[AUDIO_BUFS][AUDIO_BUF_FRAMES * AUDIO_CHANNELS];
    WAVEHDR       hdr[AUDIO_BUFS];
} Audio;

/* 打开音频设备。成功返回 0；失败返回 -1 并把原因写进 outMsg（宽字符）。
 * 失败时 Audio 依然可以安全使用 —— 所有播放调用都会静默变成空操作。*/
int  audioOpen(Audio *a, HWND hwnd, wchar_t *outMsg, int msgCount);
void audioClose(Audio *a);

/* 受控实验用：让下一次 audioOpen 不碰设备，直接按给定的 MMRESULT 出错码失败。
 * 走的是与"真失败"完全相同的那条分支（同一段状态文案、同一套清理），
 * 所以它拍出来的界面可以当作"没声卡时长什么样"的证据。
 * mmError 传 0 取消。对应命令行开关 --no-audio。*/
void audioForceFail(int mmError);

/* 播放。pan ∈ [-1,1]，dist 是米。semitone 只有 SFX_POP 认，越界会被夹住。*/
void audioPlay(Audio *a, int sfx, float pan, float dist, int semitone);

/* 立刻掐掉所有正在响的声部（切屏、重开一局时用）。*/
void audioStopAll(Audio *a);

void audioSetVolume(Audio *a, float v);     /* 0..1 */
void audioSetMuted(Audio *a, int muted);

/* 当前状态的中文说明，给 HUD 与命令行用。*/
const wchar_t *audioStatusText(const Audio *a);

/* 把四个音效（击破音含全部半音变体）导出成 wav。自检与 --dump-sfx 共用。*/
int audioDumpClips(const wchar_t *dir, int *outCount);

#endif /* AUDIO_H */
