/* ============================================================================
 * effect.cpp —— 击破特效的逻辑层
 * ==========================================================================*/
#include "effect.h"
#include <string.h>

/* 找第一个空槽；没有就返回 -1。池子小（几十个），线性扫足够。*/
static int findFreePop(FxPool *fx) {
    int i;
    for (i = 0; i < MAX_EFFECTS; ++i)
        if (!fx->pops[i].active) return i;
    return -1;
}

static int findFreePopup(FxPool *fx) {
    int i;
    for (i = 0; i < MAX_POPUPS; ++i)
        if (!fx->popups[i].active) return i;
    return -1;
}

void fxInit(FxPool *fx, unsigned seed) {
    if (!fx) return;
    memset(fx, 0, sizeof(*fx));
    rngSeed(&fx->rng, seed ? seed : 1u);
}

void fxReset(FxPool *fx, unsigned seed) {
    if (!fx) return;
    fxInit(fx, seed);
}

/* ---------------------------------------------------------- 残迹 */

/* 加一块墙上残迹。
 *
 * 池满时淘汰**剩余存活时间最短**的那一块，而不是最早的。这样墙上留下的
 * 永远是"最新的那一批"，且淘汰是确定的（不会因为遍历顺序忽然跳一下）。*/
static void addDecal(FxPool *fx, const Params *p, float x, float y, float r,
                     Color3 color) {
    int i, slot = -1;
    int live = 0;
    /* 残迹上限固定成池子的容量上限：纯性能旋钮，玩法上没意义。
       p 参数留着，是因为这个函数本来就要它。*/
    int maxD = MAX_DECALS;
    (void)p;

    if (maxD <= 0) return;

    for (i = 0; i < MAX_DECALS; ++i) {
        if (fx->decals[i].active) ++live;
        else if (slot < 0) slot = i;
    }

    if (live >= maxD) {
        /* 找一个剩余时间最短的干掉。*/
        float worst = 1e30f;
        slot = -1;
        for (i = 0; i < MAX_DECALS; ++i) {
            if (!fx->decals[i].active) continue;
            if (fx->decals[i].life < worst) { worst = fx->decals[i].life; slot = i; }
        }
        if (slot < 0) return;
    } else if (slot < 0) {
        /* 没有空槽但活跃数没到上限（上限被调大过）—— 退化成环形覆盖。*/
        slot = fx->decalNext % MAX_DECALS;
    }

    fx->decalNext = (slot + 1) % MAX_DECALS;

    {
        WallDecal *d = &fx->decals[slot];
        memset(d, 0, sizeof(*d));
        d->active = 1;
        d->x = x;
        d->y = y;
        /* 印子比气球本身大一圈：颜料是"溅"出来的，不是贴着球形的。*/
        d->r = r * rngRange(&fx->rng, 1.15f, 1.55f);
        d->color = color;
        d->life = FX_DECAL_LIFE;
        d->rot = rngRange(&fx->rng, 0.0f, 2.0f * PI_F);
        d->seed = rngFloat(&fx->rng);
    }
}

/* ---------------------------------------------------------- 爆散 */

void fxSpawnPop(FxPool *fx, const Params *p, Vec3 pos, float r0, Color3 color,
                int gain, int combo) {
    int slot, i, n;
    PopEffect *e;

    if (!fx) return;

    slot = findFreePop(fx);
    if (slot >= 0) {
        e = &fx->pops[slot];
        memset(e, 0, sizeof(*e));
        e->active  = 1;
        e->pos     = pos;
        e->color   = color;
        e->r0      = r0;
        e->duration = FX_POP_DURATION;
        /* 沿墙法线朝玩家弹出来一点：碎片才有"从墙上炸开"的纵深。*/
        e->zOff    = maxf(0.05f, r0 * 0.55f);

        /* 碎片上限固定用 MAX_PARTICLES_DEF 折算出的份数
           （220 / 20 = 11 片）。*/
        n = clampi(MAX_PARTICLES_DEF / 20, 6, MAX_SHARDS);
        if (n > MAX_SHARDS) n = MAX_SHARDS;
        e->shardCount = n;
        for (i = 0; i < n; ++i) {
            /* 角度均匀分布 + 一点点抖动：完全均匀看着像机械表盘。*/
            float base = 2.0f * PI_F * (float)i / (float)n;
            e->shardAngle[i] = base + rngRange(&fx->rng, -0.22f, 0.22f);
            e->shardSpeed[i] = r0 * rngRange(&fx->rng, 2.2f, 4.2f);
            e->shardSize[i]  = r0 * rngRange(&fx->rng, 0.10f, 0.22f);
            e->shade[i]      = rngInt(&fx->rng, 0, 2);
        }
    }

    addDecal(fx, p, pos.x, pos.y,
             maxf(0.08f, r0 * 0.55f), color);

    if (gain > 0) {
        int ps = findFreePopup(fx);
        if (ps >= 0) {
            ScorePopup *q = &fx->popups[ps];
            memset(q, 0, sizeof(*q));
            q->active  = 1;
            q->pos     = pos;
            q->elapsed = 0.0f;
            q->duration = FX_POPUP_DURATION;
            q->gain    = gain;
            q->combo   = combo;
        }
    }
}

/* ---------------------------------------------------------- 更新 */

void fxUpdate(FxPool *fx, const Params *p, float dt) {
    int i;
    (void)p;
    if (!fx) return;

    for (i = 0; i < MAX_EFFECTS; ++i) {
        PopEffect *e = &fx->pops[i];
        if (!e->active) continue;
        e->elapsed += dt;
        if (e->elapsed >= e->duration) e->active = 0;
    }

    for (i = 0; i < MAX_POPUPS; ++i) {
        ScorePopup *q = &fx->popups[i];
        if (!q->active) continue;
        q->elapsed += dt;
        if (q->elapsed >= q->duration) q->active = 0;
    }

    for (i = 0; i < MAX_DECALS; ++i) {
        WallDecal *d = &fx->decals[i];
        if (!d->active) continue;
        d->life -= dt;
        if (d->life <= 0.0f) { d->active = 0; d->life = 0.0f; }
    }
}

