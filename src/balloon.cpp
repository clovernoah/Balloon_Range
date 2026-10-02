/* ============================================================================
 * balloon.cpp —— 气球池：生成、运动、出现、逃走、命中拾取
 * ==========================================================================*/
#include "balloon.h"
#include <string.h>

/* 每类气球的倍率。普通球是基准；小快球小且快（分数靠"更小"自然变高，
   不再额外加成分，这样计分公式仍然只有一条）；金球直接翻倍；
   慢球又大又慢，分数打折，属于"占位置但好打"的球。*/
const BalloonTypeDef g_balloonTypes[BALLOON_TYPE_COUNT] = {
    { L"普通",  1.00f, 1.00f, 1.00f },
    { L"小快",  0.62f, 1.90f, 1.00f },
    { L"金球",  0.95f, 1.15f, 2.00f },
    { L"慢球",  1.35f, 0.55f, 0.80f }
};

/* 逃走动画时长（秒）与乱序模式换窝的周期。*/
#define ESCAPE_SEC        0.32f
#define JUMP_PERIOD_SEC   1.60f

/* 气球鼓出墙面的比例：半径的 8%，最小 2 厘米。
   完全贴墙会显得像贴纸，鼓出来一点才有球的感觉。*/
#define BULGE_RATIO       0.08f
#define BULGE_MIN         0.02f

/* 与已有球的最小间距系数。值定义在 balloon.h 的 BALLOON_MIN_GAP ——
   自检要拿同一个数验"随机撒点"确实隔着下界，所以真源只有那一处。
   ★ 只在对称性检查与 findSpot 里用；「邻位生成」不用它。*/
#define MIN_GAP           BALLOON_MIN_GAP

/* ============================================================ 内部工具 */

static float fieldWidth(const FieldRect *f) { return f->x1 - f->x0; }

/* 槽位是不是"占着墙上一块地方"（出现中、活着、正在逃走都算）。*/
static int slotOccupies(const Balloon *b) {
    return b->state == BSLOT_GROW || b->state == BSLOT_LIVE || b->state == BSLOT_ESCAPE;
}

/* ---------------------------------------------------------- 类型抽选 */

static int pickType(BalloonPool *bp, const Params *p) {
    int total = 0, i, roll;
    if (!p->allowSpecial) return BALLOON_NORMAL;
    for (i = 0; i < BALLOON_TYPE_COUNT; ++i) total += p->typeWeight[i];
    if (total <= 0) return BALLOON_NORMAL;

    roll = rngInt(&bp->rng, 0, total - 1);
    for (i = 0; i < BALLOON_TYPE_COUNT; ++i) {
        if (roll < p->typeWeight[i]) return i;
        roll -= p->typeWeight[i];
    }
    return BALLOON_NORMAL;
}

/* ---------------------------------------------------------- 位置抽样
 *
 * 拒绝采样：随机点 + 与已有球比间距，最多试 SPAWN_TRIES 次。
 * 全失败时**不能空转**（这是很容易写出来的死循环），退而求其次取"试过的
 * 候选里最不挤的那个"。墙被塞满的极端情况（球多、球大、出球区小）会走到
 * 这条分支，自检里专门构造了这个用例。
 *
 * skip 是要跳过比较的槽位 —— 正在重新抽位置的那个球自己。少了这一步，
 * "新位置不能和自己旧位置太近"会白白缩掉一大块可用区域。
 */
static void findSpot(BalloonPool *bp, const FieldRect *f, float r, int skip,
                     float *outX, float *outY) {
    float loX = f->x0 + r, hiX = f->x1 - r;
    float loY = f->y0 + r, hiY = f->y1 - r;
    float bestX = (loX + hiX) * 0.5f, bestY = (loY + hiY) * 0.5f;
    float bestMin = -1e9f;
    int tries;

    /* 出球区比球还小（半径被调得很大）：退化为居中，不做采样。*/
    if (hiX < loX) { loX = hiX = (f->x0 + f->x1) * 0.5f; }
    if (hiY < loY) { loY = hiY = (f->y0 + f->y1) * 0.5f; }

    for (tries = 0; tries < SPAWN_TRIES; ++tries) {
        float cx = rngRange(&bp->rng, loX, hiX);
        float cy = rngRange(&bp->rng, loY, hiY);
        float minD = 1e9f;
        int i, ok = 1;

        for (i = 0; i < bp->n; ++i) {
            const Balloon *o = &bp->slots[i];
            float dx, dy, d;
            if (i == skip) continue;
            if (!slotOccupies(o)) continue;
            dx = cx - o->x;
            dy = cy - o->y;
            d = sqrtf(dx * dx + dy * dy) - (r + o->baseR) * MIN_GAP;
            if (d < minD) minD = d;
            if (d < 0.0f) ok = 0;
        }
        if (minD > bestMin) { bestMin = minD; bestX = cx; bestY = cy; }
        if (ok) break;
    }

    *outX = bestX;
    *outY = bestY;
}

/* ------------------------------------------------ 邻位环带取样
 *
 * 「跟踪训练」专用：新球要落在墙上**剩下的那颗球**周围的环带里，同时不许与
 * **刚被打掉的那颗**重叠。规则可等价描述为：在一个内径为 R、外径为 5R 的
 * 圆环面积作为有效区域内（内径为打剩的气球的模型），随机位置挖掉一个完整
 * 的半径为 R 的圆面积（即打掉的气球的模型），剩下的有效区域中随机找出一个完整
 * 的半径为 R 的圆面积当作新生成的气球位置。
 *
 * 两种说法的等价性（等径 R、下限 1 倍、上限 2 倍时）：
 *   新球整体落进「内径 R、外径 5R」的环 ⇒ 圆心距 ∈ [R+R, 5R−R] = [2R, 4R]；
 *   挖掉半径 R 的死球圆盘 ⇒ 与打掉那颗的圆心距 ≥ 2R。
 * 参数化之后就是：圆心距 d ∈ [下限, 上限] × (R_新 + R_搭档)。
 * （环带必须理解成"新球**整体**落进去"；若当成"圆心可去的地方"，上限就成了 6R。）
 *
 * 与 findSpot 的分工：
 *   · findSpot 管"互不重叠" —— 在矩形里撒点、比间距、最多试 SPAWN_TRIES 次；
 *   · 这里管"必须落在环带里" —— 圆心被约束在一个二维区域上，撒点撒不出来。
 *
 * 为什么还是"算"而不是"撒点 + 拒绝"：区域是"环带 ∩ 出球区 − 死球圆盘"，
 * 贴边时拒绝采样会退化（可能几十次全落空，"试不出来"本身就是个说不清的行为）。
 * 这里把每个半径上的可用角**精确算出来**，再按面积在半径方向取一个、
 * 在可用弧里取一个 —— 只要区域非空就一定能取到点，没有"运气不好"这条路。
 *
 * 采样：面元是 d·dd·dθ，所以半径 d 的权重 = d · W(d)（W = 该半径的可用角总长）。
 * 把 [dmin, dmax] 分成 RING_BINS 格，逐格算权重、做前缀和，取一个均匀数反查 CDF。
 * 这就是"有效区域内每一点等可能"（即"面积均匀"）—— 半径方向因此天然
 * 偏向外圈（外圈面积大），这是面积均匀的正确表现，不是 bug。
 *
 * ★ 与"全场随机"那条路的实质差别：这里**不乘 BALLOON_MIN_GAP（1.06）**。
 * 几何前提是"半径 R 的圆正好相切"，所以下限就是 rn+rp、挖掉的就是 rn+rd，
 * 精确、不加缝。（MIN_GAP 仍留在"全场随机"那条路上，避免随机球互相压。）
 * 若将来觉得两球贴得看不出是两颗，把预设里的下限改成 1.06 倍即可 —— 那是
 * 一行参数值，不用改代码。
 *
 * 返回 0 = 这次用不上这条规则，调用方回落到 findSpot。触发条件：
 *   · 除自己之外，墙上占位的球不是恰好 1 颗（开局第一颗、数量被调成 1
 *     或 ≥3、搭档刚被打掉 …… 都会落到这里）；
 *   · 出球区连"一个球位"都放不下。
 */
#define ARC_MAX      16
#define ARC_TWO_PI   (2.0f * PI_F)
#define RING_BINS    128

/* 圆上的一段角度区间 [a, b]，0 <= a <= b <= 2π；一整圈的集合就是 [0, 2π]。*/
typedef struct { float a, b; } ArcRange;
typedef struct { int n; ArcRange r[ARC_MAX]; } ArcSet;

static float arcNorm(float t) {
    t = fmodf(t, ARC_TWO_PI);
    if (t < 0.0f) t += ARC_TWO_PI;
    return t;
}

static void arcSetAll(ArcSet *s) {
    s->n = 1;
    s->r[0].a = 0.0f;
    s->r[0].b = ARC_TWO_PI;
}

/* 把 [a, b] 写成区间集合。span 可以超过一圈（那就是整圈），也可以跨过 0 ——
   跨过 0 的会被切成两段，因为区间表示法不允许 a > b。*/
static void arcSetFromSpan(ArcSet *s, float a, float b) {
    float span = b - a;
    float x;
    s->n = 0;
    if (span <= 0.0f) return;
    if (span >= ARC_TWO_PI) { arcSetAll(s); return; }
    x = arcNorm(a);
    if (x + span <= ARC_TWO_PI) {
        s->r[0].a = x;
        s->r[0].b = x + span;
        s->n = 1;
    } else {
        s->r[0].a = x;
        s->r[0].b = ARC_TWO_PI;
        s->r[1].a = 0.0f;
        s->r[1].b = x + span - ARC_TWO_PI;
        s->n = 2;
    }
}

static void arcSetIntersect(ArcSet *dst, const ArcSet *x, const ArcSet *y) {
    ArcSet t;
    int i, j;
    t.n = 0;
    for (i = 0; i < x->n; ++i) {
        for (j = 0; j < y->n; ++j) {
            float a = maxf(x->r[i].a, y->r[j].a);
            float b = minf(x->r[i].b, y->r[j].b);
            if (a < b && t.n < ARC_MAX) { t.r[t.n].a = a; t.r[t.n].b = b; ++t.n; }
        }
    }
    *dst = t;
}

/* 从 x 里挖掉 y 覆盖的角度。一次只减 y 的一段，循环减完整段为止 ——
   比"分类讨论区间相对位置"少一大截分支，也少一类写错了看不出来的地方。*/
static void arcSetSubtract(ArcSet *dst, const ArcSet *x, const ArcSet *y) {
    ArcSet cur = *x;
    int j, k;
    for (j = 0; j < y->n && cur.n > 0; ++j) {
        ArcSet out;
        float c = y->r[j].a, d = y->r[j].b;
        out.n = 0;
        for (k = 0; k < cur.n; ++k) {
            float a = cur.r[k].a, b = cur.r[k].b;
            if (d <= a || c >= b) {                      /* 不相交，原样留着 */
                if (out.n < ARC_MAX) { out.r[out.n].a = a; out.r[out.n].b = b; ++out.n; }
            } else {                                     /* 挖掉中间，两头各留一截 */
                if (a < c && out.n < ARC_MAX) { out.r[out.n].a = a; out.r[out.n].b = c; ++out.n; }
                if (d < b && out.n < ARC_MAX) { out.r[out.n].a = d; out.r[out.n].b = b; ++out.n; }
            }
        }
        cur = out;
    }
    *dst = cur;
}

static float arcSetLength(const ArcSet *s) {
    float t = 0.0f;
    int i;
    for (i = 0; i < s->n; ++i) t += s->r[i].b - s->r[i].a;
    return t;
}

/* 把"cosθ 落在 [lo, hi]"翻译成角度集合。lo/hi 已保证与 [-1, 1] 有交集。*/
static void arcSetFromCosRange(ArcSet *dst, float lo, float hi) {
    ArcSet a, b;
    if (lo <= -1.0f) arcSetAll(&a);
    else             arcSetFromSpan(&a, -acosf(lo), acosf(lo));
    if (hi >= 1.0f)  arcSetAll(&b);
    else             arcSetFromSpan(&b, acosf(hi), ARC_TWO_PI - acosf(hi));
    arcSetIntersect(dst, &a, &b);
}

/* 把整个角度集合整体转 d 弧度。段数只增不减（各段分别转，跨过 0 的会再切一刀），
   但源头最多两段，转完最多四段，够用。*/
static void arcSetRotate(ArcSet *s, float d) {
    ArcSet in = *s, out;
    int i;
    out.n = 0;
    for (i = 0; i < in.n; ++i) {
        ArcSet one;
        float span = in.r[i].b - in.r[i].a;
        int k;
        arcSetFromSpan(&one, in.r[i].a + d, in.r[i].a + d + span);
        for (k = 0; k < one.n && out.n < ARC_MAX; ++k)
            out.r[out.n++] = one.r[k];
    }
    *s = out;
}

/* { θ : sinθ ∈ [lo, hi] }，也就是 { θ : cos(θ − π/2) ∈ [lo, hi] } ——
   先按 cos 算出来，再把整个集合转 +90°。

   ★ y 的范围必须走 sin 版本。若图省事把 y 的范围直接丢进 cos 版本，等于把
   "y 不越界"验成了"x 不越界"。症状很阴：贴着地板/天花板补位时，新球被生成到
   地板以下，再由 balloonUpdate 的夹取逻辑拽回边界 —— 圆心一挪，紧挨的距离就
   不是 L 了，还可能与搭档重叠。出球区正中那 20000 次补位一次都碰不到墙，
   所以完全看不出来；自检里"60 个种子开局""贴着四面墙"和"数量调成 8"
   这三条能把它按住。*/
static void arcSetFromSinRange(ArcSet *dst, float lo, float hi) {
    ArcSet t;
    arcSetFromCosRange(&t, lo, hi);
    arcSetRotate(&t, PI_F * 0.5f);
    *dst = t;
}

/* 半径 d 处的可用角集：整圈 → 掐 x 越界 → 掐 y 越界 → 挖掉压着死球的弧。
   这里把它抽成"按 d 取值"的函数 —— 环带里每一个半径的可用角都不一样，
   早先把圆心距写死在函数体里，现在换成参数 d，其余一字未改。*/
static void ringAllowedArcAt(const FieldRect *f, float px, float py, float rn,
                             float d, const float *deadPos, ArcSet *out) {
    ArcSet tmp;
    arcSetAll(out);
    if (d <= 1e-6f) { out->n = 0; return; }

    {
        float lo = (f->x0 + rn - px) / d;
        float hi = (f->x1 - rn - px) / d;
        if (lo > 1.0f || hi < -1.0f) { out->n = 0; return; }
        arcSetFromCosRange(&tmp, lo, hi);
        arcSetIntersect(out, out, &tmp);
    }
    if (out->n > 0) {
        float lo = (f->y0 + rn - py) / d;
        float hi = (f->y1 - rn - py) / d;
        if (lo > 1.0f || hi < -1.0f) { out->n = 0; return; }
        arcSetFromSinRange(&tmp, lo, hi);        /* y 看 sinθ，不是 cosθ */
        arcSetIntersect(out, out, &tmp);
    }
    if (out->n > 0 && deadPos) {
        float dx = deadPos[0] - px;
        float dy = deadPos[1] - py;
        float dd = sqrtf(dx * dx + dy * dy);
        float m  = rn + deadPos[2];              /* 正好相切，不乘 MIN_GAP */
        /* dd 极小意味着"死球和搭档重合"，这在池子里不可能发生（两颗球活着时
           本来就隔着距离）——真出现了就当作没有这条约束，不做除法。*/
        if (dd > 1e-4f) {
            float T = (d * d + dd * dd - m * m) / (2.0f * d * dd);
            if (T <= -1.0f) {
                out->n = 0;                      /* 整圈都躲不开 */
            } else if (T < 1.0f) {
                ArcSet bad, sub;
                float phi = atan2f(dy, dx);
                float al  = acosf(T);
                arcSetFromSpan(&bad, phi - al, phi + al);
                arcSetSubtract(&sub, out, &bad);
                *out = sub;
            }
            /* T >= 1：这颗死球离得够远，压不着，不加约束。*/
        }
    }
}

/* 兜底：整个环带都被墙或死球吃掉时（近乎不可达，但留着）在环带上扫一圈，
   挑"越界最少、离死球最远"的那个点。与 findSpot 试满 SPAWN_TRIES 次后取
   "最不挤的候选"是同一套思路 —— 宁可位置不理想，也不空转，也不把球扔到墙外去。
   末尾那次夹取是最后一道保险：走到这里已经没有"既在墙内、又在环内"的点，
   位置只能二选一 —— 保墙内，距离范围因此可能被破。
   自检另有一条断言正面钉住"实际玩到的场合并不会走到这里"。*/
static void ringPickBestEffort(const FieldRect *f, float px, float py, float rn,
                               float dmin, float dmax, const float *deadPos,
                               float *outX, float *outY) {
    const int KD = 32, KA = 144;
    float bestX = 0.0f, bestY = 0.0f, bestScore = -1e30f;
    int a, b;
    if (dmax < dmin) { float t = dmin; dmin = dmax; dmax = t; }
    for (a = 0; a <= KD; ++a) {
        float d = dmin + (dmax - dmin) * (float)a / (float)KD;
        for (b = 0; b < KA; ++b) {
            float th = ARC_TWO_PI * (float)b / (float)KA;
            float cx = px + d * cosf(th);
            float cy = py + d * sinf(th);
            float s = 0.0f;
            s += minf(0.0f, cx - (f->x0 + rn));
            s += minf(0.0f, (f->x1 - rn) - cx);
            s += minf(0.0f, cy - (f->y0 + rn));
            s += minf(0.0f, (f->y1 - rn) - cy);
            if (deadPos) {
                float dx = cx - deadPos[0], dy = cy - deadPos[1];
                float over = sqrtf(dx * dx + dy * dy) - (rn + deadPos[2]);
                if (over < 0.0f) s += over;      /* over 是负的，越压越扣 */
            }
            if (s > bestScore) { bestScore = s; bestX = cx; bestY = cy; }
        }
    }
    *outX = clampf(bestX, f->x0 + rn, f->x1 - rn);
    *outY = clampf(bestY, f->y0 + rn, f->y1 - rn);
}

static int findSpotRing(BalloonPool *bp, const FieldRect *f, int index, float rn,
                        const float *deadPos, float loMul, float hiMul,
                        float *outX, float *outY) {
    const Balloon *partner = NULL;
    ArcSet allow;
    float px, py, rp, base, dmin, dmax, span;
    float cum[RING_BINS + 1];
    float total, u, pick, d;
    int i, k, count = 0;

    /* ---- 1) 找搭档：除自己之外，占位的球必须恰好一颗 ---- */
    for (i = 0; i < bp->n; ++i) {
        if (i == index) continue;
        if (!slotOccupies(&bp->slots[i])) continue;
        partner = &bp->slots[i];
        ++count;
    }
    if (count != 1 || !partner) return 0;

    /* 用搭档**当前**位置而不是出生点：规则问的是"围着那颗球生成"，那就得围着
       它此刻所在的地方。跟踪训练里运动方式是静止，两者本来就是同一个数。*/
    px = partner->x;
    py = partner->y;
    rp = partner->baseR;

    base = rn + rp;
    dmin = base * loMul;
    dmax = base * hiMul;
    if (dmax < dmin) { float t = dmin; dmin = dmax; dmax = t; }
    if (dmin < base) dmin = base;      /* 下限的地板：再小就是两颗球重叠 */
    if (dmax < dmin) dmax = dmin;

    /* 出球区连一个球位都容不下：交给 findSpot 的"居中"兜底。*/
    if ((f->x1 - f->x0) < 2.0f * rn || (f->y1 - f->y0) < 2.0f * rn) return 0;

    /* ---- 2) 半径方向分箱：面元是 d·dd·dθ，所以半径 d 的权重 = d · W(d) ----
       span ≈ 0 要单独走：环带退化成一根圆时梯形法的宽度是 0，total 必然算成 0，
       若放着不管就会掉进下面的"尽力而为" —— 那条路挑的是"第一个不被扣分的点"，
       角度上几乎钉死在同一个方向（每颗新球都落在老地方）。所以这里半径直接定死，
       只把角度交给第 4 步按弧长均匀取。*/
    span = (dmax - dmin) / (float)RING_BINS;
    total = 0.0f;
    if (span > 1e-6f) {
        float prev = 0.0f;
        for (k = 0; k <= RING_BINS; ++k) {
            float dk = dmin + span * (float)k;
            float wk;
            ringAllowedArcAt(f, px, py, rn, dk, deadPos, &allow);
            wk = dk * arcSetLength(&allow);
            if (k > 0) total += 0.5f * (prev + wk) * span;   /* 梯形法 */
            prev = wk;
            cum[k] = total;
        }
    }

    /* ---- 3) 按面积取一个半径：反查 CDF，格内线性插值 ----
       （格内线性插值 = 该格的密度取"格两端权重的均值"；格数够密时与真密度
        d·W(d) 的差别远小于人眼能分辨的程度，纯环带时 W 是常数、与解析解一致。）*/
    d = dmin;
    if (span <= 1e-6f) {
        /* 下限 = 上限：半径只有 dmin 这一个值可取。*/
    } else if (total <= 1e-9f) {
        ringPickBestEffort(f, px, py, rn, dmin, dmax, deadPos, outX, outY);
        return 1;
    } else {
        u = rngRange(&bp->rng, 0.0f, total);
        for (k = 0; k < RING_BINS; ++k) {
            if (u < cum[k + 1] || k == RING_BINS - 1) {
                float seg = cum[k + 1] - cum[k];
                float t = (seg > 1e-12f) ? (u - cum[k]) / seg : 0.0f;
                d = dmin + span * ((float)k + clampf(t, 0.0f, 1.0f));
                break;
            }
        }
    }

    /* ---- 4) 在这个半径上按弧长取一个角度 ---- */
    ringAllowedArcAt(f, px, py, rn, d, deadPos, &allow);
    total = arcSetLength(&allow);
    if (allow.n <= 0 || total <= 1e-6f) {
        /* 分箱时非空、插值到具体半径时恰好落到空集：相邻两格一格有、一格空，
           插值又正好落在交界上时可能发生。退回兜底，不空转。*/
        ringPickBestEffort(f, px, py, rn, dmin, dmax, deadPos, outX, outY);
        return 1;
    }

    pick = rngRange(&bp->rng, 0.0f, total);
    for (i = 0; i < allow.n; ++i) {
        float len = allow.r[i].b - allow.r[i].a;
        if (pick < len || i == allow.n - 1) {
            float th = allow.r[i].a + minf(pick, len);
            *outX = px + d * cosf(th);
            *outY = py + d * sinf(th);
            return 1;
        }
        pick -= len;
    }
    return 0;                                /* 走不到，上面已经保证会返回 */
}

/* ---------------------------------------------------------- 初始化一个球
 *
 * 注意调用时机：调用前必须先把该槽位标成"不占位"（或者传 skip = 该下标），
 * 否则它会拿自己的旧位置约束自己的新位置。
 *
 * 邻位生成规则要的"刚被打掉的那颗"从**槽位自己**读
 * （hasDead / deadX / deadY / deadR，由 balloonKill 写下）。不在调用链上传
 * 参数，是因为补位有两条路 —— 击破当帧补、延迟几帧在 WAIT 分支里补 ——
 * 后者隔了好几层，传参得一路改到底。读完即清，免得同一次击破记下的位置
 * 被下一次补位再吃一遍（开池、超时逃走后的补位本来就没有死球，读到 0 即跳过）。
 */
static void initSlot(BalloonPool *bp, int index, const Params *p,
                     const FieldRect *f, double elapsed) {
    Balloon *b = &bp->slots[index];
    float dead[3];
    const float *deadUse = NULL;
    if (b->hasDead) {
        dead[0] = b->deadX;
        dead[1] = b->deadY;
        dead[2] = b->deadR;
        deadUse = dead;
        b->hasDead = 0;
    }
    float rScale = effectiveRadiusScale(p, elapsed);
    float spd = effectiveDriftSpeed(p, elapsed);
    int t = pickType(bp, p);

    b->type    = t;
    b->baseR   = rngRange(&bp->rng, p->radiusMin, p->radiusMax) *
                 g_balloonTypes[t].radiusMul * rScale;
    b->baseR   = clampf(b->baseR, 0.06f, 2.00f);

    /* 生成位置。SPAWN_RANDOM（默认，其余七套预设）走原来的 findSpot，
       一个字节的行为都不变；SPAWN_ADJACENT（面板上的「邻位生成」）先试环带
       规则，用不上才回落。注意"用不上"是常态而不是异常：开局第一颗球、
       数量不是 2 的场合，都该老老实实按全场随机来。*/
    if (p->spawnRule != SPAWN_ADJACENT ||
        !findSpotRing(bp, f, index, b->baseR, deadUse,
                      p->spawnDistMin, p->spawnDistMax, &b->ax, &b->ay)) {
        findSpot(bp, f, b->baseR, index, &b->ax, &b->ay);
    }

    b->x = b->ax;
    b->y = b->ay;
    b->r = b->baseR * 0.35f;      /* 从 0.35 倍长出来，具体进度看 timer */
    b->zOffset = maxf(BULGE_MIN, b->baseR * BULGE_RATIO);
    b->vx = spd * g_balloonTypes[t].speedMul;
    if (rngChance(&bp->rng, 0.5f)) b->vx = -b->vx;
    b->bobPhase = rngRange(&bp->rng, 0.0f, 2.0f * PI_F);
    b->age = 0.0f;
    b->life = effectiveLifetime(p, elapsed);
    b->armed = 0;
    b->id = ++bp->nextId;

    /* 色相均匀取；饱和度/亮度在参数给定的区间里取，
       保证深色背景下鲜艳不脏。*/
    b->hue = rngRange(&bp->rng, 0.0f, 360.0f);
}

/* ============================================================ 对外接口 */

void balloonPoolInit(BalloonPool *bp, const Params *p, unsigned seed,
                     const FieldRect *f) {
    int i;
    if (!bp) return;
    memset(bp, 0, sizeof(*bp));
    rngSeed(&bp->rng, seed ? seed : 1u);
    bp->n = clampi(p->balloonCount, 1, MAX_BALLOONS);
    bp->time = 0.0f;
    bp->nextId = 0;

    for (i = 0; i < bp->n; ++i) {
        Balloon *b = &bp->slots[i];
        memset(b, 0, sizeof(*b));
        b->state = BSLOT_GROW;
        initSlot(bp, i, p, f, 0.0);   /* 开池：还没有球被打掉过 */
        /* timer 从负数起跑：开局 N 个球错开一点点出现，
           否则一墙球同时"啵"地一下冒出来，很像故障。*/
        b->timer = -(float)i * 0.02f;
    }
}

void balloonPoolFill(BalloonPool *bp, const Params *p, const FieldRect *f,
                     double elapsed) {
    int i;
    if (!bp) return;
    for (i = 0; i < bp->n; ++i) {
        Balloon *b = &bp->slots[i];
        if (slotOccupies(b)) continue;
        b->state = BSLOT_GROW;
        b->timer = 0.0f;
        b->age = 0.0f;
        initSlot(bp, i, p, f, elapsed);   /* 池子补洞，不是"击破补位" */
    }
}

int balloonActiveCount(const BalloonPool *bp) {
    int i, c = 0;
    if (!bp) return 0;
    for (i = 0; i < bp->n; ++i) {
        int s = bp->slots[i].state;
        if (s == BSLOT_GROW || s == BSLOT_LIVE) ++c;
    }
    return c;
}

Vec3 balloonWorldPos(const Balloon *b) {
    return v3(b->x, b->y, WALL_Z + b->zOffset);
}

Color3 balloonColor(const Balloon *b, const Params *p) {
    float sLo = p->colorSat * 0.70f;
    float vLo = p->colorVal * 0.88f;
    /* 用球的 id 做确定性的抖动：同一颗球每帧颜色必须一样，
       所以这里不能用 rand，得用 id 派生的固定值。*/
    float ts = (float)(b->id % 97u) / 97.0f;
    float tv = (float)((b->id * 7u) % 89u) / 89.0f;
    float s = lerpf(sLo, p->colorSat, ts);
    float v = lerpf(vLo, p->colorVal, tv);
    if (b->type == BALLOON_GOLD) return hsvToRgb(46.0f, 0.72f, 1.0f);
    return hsvToRgb(b->hue, s, v);
}

int balloonHasMinGap(const BalloonPool *bp, float gap) {
    int i, j;
    if (!bp) return 1;
    for (i = 0; i < bp->n; ++i) {
        if (!slotOccupies(&bp->slots[i])) continue;
        for (j = i + 1; j < bp->n; ++j) {
            const Balloon *a = &bp->slots[i];
            const Balloon *b = &bp->slots[j];
            if (!slotOccupies(b)) continue;
            if (spheresOverlap2D(a->x, a->y, a->baseR, b->x, b->y, b->baseR, gap))
                return 0;
        }
    }
    return 1;
}

/* ---------------------------------------------------------- 每帧更新 */

int balloonUpdate(BalloonPool *bp, const Params *p, const FieldRect *f,
                  double elapsed, float dt) {
    int i, escaped = 0;
    float spawnSec = p->spawnAnimSec;
    float life = effectiveLifetime(p, elapsed);

    if (!bp) return 0;
    bp->time += dt;

    /* 半径倍率随难度爬升会变小，但这里**故意不用它去改已经出现的球** ——
       玩家正在瞄的东西自己缩水会很难受。新球在 initSlot 里拿到的是当时的
       倍率，所以"越到后面出来的球越小"这件事自然会发生。*/

    for (i = 0; i < bp->n; ++i) {
        Balloon *b = &bp->slots[i];

        switch (b->state) {
        case BSLOT_WAIT:
            b->timer -= dt;
            if (b->timer <= 0.0f) {
                b->state = BSLOT_GROW;
                b->timer = 0.0f;
                initSlot(bp, i, p, f, elapsed);
            }
            break;

        case BSLOT_GROW:
            b->timer += (spawnSec > 1e-4f) ? (dt / spawnSec) : 1.0f;
            if (b->timer >= 1.0f) {
                b->timer = 1.0f;
                b->state = BSLOT_LIVE;
                b->age = 0.0f;
                b->life = life;
            }
            /* timer 可能从负数起跑（开局错峰），easeOutQuad 会夹到 [0,1]，
               所以负数期间球停在 0.35 倍，正好就是"还没长出来"。*/
            if (!b->armed && b->timer >= SPAWN_ARM_RATIO) b->armed = 1;
            b->r = b->baseR * lerpf(0.35f, 1.0f, easeOutQuad(b->timer));
            break;

        case BSLOT_LIVE:
            b->age += dt;
            b->r = b->baseR;
            if (b->life > 0.0f && b->age >= b->life) {
                /* 超时逃走 —— 3D 里的"气球升出屏幕"。*/
                b->state = BSLOT_ESCAPE;
                b->timer = 0.0f;
                b->armed = 0;
                ++escaped;
            }
            break;

        case BSLOT_ESCAPE:
            b->timer += dt / ESCAPE_SEC;
            if (b->timer >= 1.0f) {
                b->state = BSLOT_WAIT;
                b->timer = p->refillDelaySec;
                b->r = 0.0f;
                /* refillDelay 为 0（默认）时下一帧就补上，
                   "墙上少一个"的窗口长度是一帧，肉眼看不见。*/
                if (b->timer <= 0.0f) {
                    b->state = BSLOT_GROW;
                    b->timer = 0.0f;
                    initSlot(bp, i, p, f, elapsed);
                }
            } else {
                float t = b->timer;
                b->r = b->baseR * (1.0f - 0.92f * easeInQuad(t));
                b->zOffset = maxf(BULGE_MIN, b->baseR * BULGE_RATIO) + t * 0.45f;
            }
            break;

        default:
            break;
        }

        if (b->state == BSLOT_WAIT) {
            b->r = 0.0f;
            continue;
        }
        if (b->state == BSLOT_ESCAPE) continue;   /* 逃走的球不再参与运动 */

        /* -------------------------------------------------- 运动 */
        {
            float halfW = fieldWidth(f) * 0.5f;
            /* "漂移幅度"的统一语义：**球离自己出生点的最大横向距离**，
               占出球区半宽的比例。四种运动方式都认这一条 ——
               静止按定义不动；正弦、乱序、匀速各自照这个上限收窄。
               原先只有正弦用它，另外两种运动的幅度是满墙，于是面板上
               那个滑杆在默认运动方式下毫无作用 —— 是自检的活性断言
               （"改了它气球状态必变"）把这个不一致揪出来的。*/
            float amp = p->driftRange * maxf(0.0f, halfW - b->baseR);
            float bob = p->bobAmp * sinf(b->bobPhase + bp->time * p->bobFreq * 2.0f * PI_F);
            float lo, hi;

            switch (p->motion) {
            case MOVE_STILL:
                b->x = b->ax;
                break;

            case MOVE_SINE: {
                float ph = b->bobPhase + bp->time * 0.42f + (float)(b->id % 7u) * 0.31f;
                b->x = b->ax + amp * sinf(ph);
                break;
            }

            case MOVE_JUMP: {
                /* 每隔 JUMP_PERIOD_SEC 换一次位置。用 id 错开相位，
                   避免整墙的球一起跳（那看起来像卡顿，不像运动）。*/
                float t = bp->time + (float)(b->id % 13u) * 0.11f;
                float phase = fmodf(t / JUMP_PERIOD_SEC, 1.0f);
                if (phase < dt / JUMP_PERIOD_SEC) {
                    float nx, ny;
                    findSpot(bp, f, b->baseR, i, &nx, &ny);
                    /* 落到哪儿仍受"漂移幅度"限制，跟另外两种运动一视同仁。
                       注意 ax / ay 是**出生点**，这里不覆盖它 —— 覆盖了
                       幅度就没法当"离出生点的最大距离"用。*/
                    b->x = clampf(nx, b->ax - amp, b->ax + amp);
                    b->ay = ny;
                }
                break;
            }

            case MOVE_LINEAR:
            default: {
                /* 反射边界。这里也用"离出生点不超过 amp"来收窄：
                   driftRange = 1 时正好退化成整面墙（默认行为不变），
                   = 0 时球停在出生点不动。收窄的中心用 ax 是因为
                   出生点之间本来就满足最小间距，收窄后不会叠在一起。*/
                lo = lerpf(b->ax, f->x0 + b->baseR, p->driftRange);
                hi = lerpf(b->ax, f->x1 - b->baseR, p->driftRange);
                if (hi < lo) { float t = lo; lo = hi; hi = t; }
                if (hi - lo < 1e-4f) {
                    b->x = (lo + hi) * 0.5f;      /* 幅度收到 0：原地不动 */
                } else {
                    b->x += b->vx * dt;
                    if (b->x < lo) { b->x = lo + (lo - b->x); b->vx = -b->vx; }
                    if (b->x > hi) { b->x = hi - (b->x - hi); b->vx = -b->vx; }
                    b->x = clampf(b->x, lo, hi);
                }
                break;
            }
            }

            b->y = b->ay + bob;
            /* 最后统一夹一次：任何模式都不许把球弄到出球区外面。*/
            lo = f->x0 + b->baseR; hi = f->x1 - b->baseR;
            b->x = clampf(b->x, lo, hi);
            lo = f->y0 + b->baseR; hi = f->y1 - b->baseR;
            b->y = clampf(b->y, lo, hi);
        }
    }

    return escaped;
}

/* ---------------------------------------------------------- 击破 */

int balloonKill(BalloonPool *bp, const Params *p, const FieldRect *f,
                double elapsed, int index, float *outR, int *outType) {
    Balloon *b;
    if (!bp) return 0;
    if (index < 0 || index >= bp->n) return 0;
    b = &bp->slots[index];
    if (b->state != BSLOT_LIVE || !b->armed) return 0;

    if (outR) *outR = b->baseR;
    if (outType) *outType = b->type;

    b->age = 0.0f;
    b->armed = 0;
    b->r = 0.0f;

    /* ★ 旧圆心与旧半径**趁现在**记到槽位上：邻位生成规则要拿"刚被打掉的
       那个位置"当禁区，而 baseR 在 initSlot 里会被重新抽、ax/ay 会被新位置
       盖掉 —— 只有这一个窗口能取到它。记在槽位上而不是往下传参数，是为了
       让下面的"延迟补位"那条路也能拿到同一份数据（见 balloon.h 的说明）。*/
    b->hasDead = 1;
    b->deadX = b->ax;
    b->deadY = b->ay;
    b->deadR = b->baseR;

    /* 硬性要求 ——"打掉一个立马出现新的"—— 由 refillDelaySec = 0
       （默认值）下的这一条分支保证：击破当帧就进 GROW。
       把 refillDelaySec 调大是留给玩家观察"墙上会短暂少一个"的手段，
       此时先进等待态，由 balloonUpdate 的 WAIT 分支负责补位。
       自检里两条分支都有断言盯着，参数不会是死的。*/
    if (p->refillDelaySec > 1e-4f) {
        b->state = BSLOT_WAIT;
        b->timer = p->refillDelaySec;
    } else {
        b->state = BSLOT_GROW;
        b->timer = 0.0f;
        initSlot(bp, index, p, f, elapsed);
    }
    return 1;
}

/* ---------------------------------------------------------- 射线拾取 */

int balloonPick(const BalloonPool *bp, const Params *p,
                Vec3 rayOrigin, Vec3 rayDir, float *outT) {
    int i, best = -1;
    float bestT = 1e30f;
    if (!bp) return -1;

    for (i = 0; i < bp->n; ++i) {
        const Balloon *b = &bp->slots[i];
        float t;
        Vec3 c;
        if (b->state != BSLOT_LIVE && b->state != BSLOT_GROW) continue;
        if (!b->armed) continue;
        if (b->r <= 0.0f) continue;

        c = balloonWorldPos(b);
        /* 判定半径 = 当前视觉半径 × 宽容度。渲染用的是同一个 b->r，
           所以"看到多大就能打多大"是结构上成立的，不是靠两边各自算准。*/
        if (raySphere(rayOrigin, rayDir, c, b->r * p->hitForgive, &t)) {
            if (t < bestT) { bestT = t; best = i; }
        }
    }
    if (best >= 0 && outT) *outT = bestT;
    return best;
}
