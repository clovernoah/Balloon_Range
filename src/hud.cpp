/* ============================================================================
 * hud.cpp —— 中文 HUD 的实现
 *
 * 三件事：
 *   1. 从文案表里收集用得到的字符（纯计算）
 *   2. 用 GDI 把这些字画进一张内存位图，转成 RGBA 传成 GL 贴图
 *   3. 每帧画带贴图的四边形拼出文字
 *
 * 关于第 2 步的一个坑：GDI 往 32 位 DIB 里画字时**不写 alpha 通道**，
 * 只写 BGR，第 4 个字节留 0。所以不能直接把 DIB 当 RGBA 用。这里的做法是
 * 画完之后自己算：字是白的，那么 max(r,g,b) 就是覆盖度，拿它当 alpha。
 * 这样抗锯齿的灰边也一并被保留下来了。
 * ==========================================================================*/
#include "hud.h"
#include "texture.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================ 文案表
 *
 * ★ 这是 HUD 上会出现的**全部**文字。图集照着它收集字符，
 *   所以往 HUD 上加新词时，往这张表里加一条就够了，不用去别处登记。
 *   万一漏了，运行时会画成空心方块并在控制台报出，不会悄悄消失。
 */
/* ---- 参数面板的成段提示语 ------------------------------------------------
 *
 * 这几段**定义在这里，而不是直接写在 render.cpp 里**，render.cpp 只按名字
 * 取用。这里有过一次教训：保存/载入挪到 Ctrl 组合键之后，hud.cpp
 * 这一边改了，render.cpp 里还留着一份老的，于是屏幕上"撤销"两个字变成了
 * 空心方块 —— 图集照 g_hudStrings[] 收字，表外的文案一律画不出来，而
 * 出图脚本当时把控制台的告警一起吞掉了，肉眼看图才发现。
 *
 * 现在文案只有一处定义，render.cpp 想写出表外的新字都写不出来；
 * 自检里还有一条断言逐字复核这几段（见 tPanelWording）。
 * ----------------------------------------------------------------------*/
/* ★ 面板只剩一层，所以这套常量从"两套（L1/L2）"并成了一套。
   三行页脚的分工没变：第一行说按键，第二行说面板级的动作，第三行说怎么退出去。

   撤掉的提示语也反映在这里：**S 保存 / L 载入整句没了**（改成关面板
   自动落盘），**「恢复整套预设」改回「恢复用户偏好设置」**。

   ★ 四段提示里的方向键一律用符号而非汉字：↑↓ / ←→ / ↓。

   ★ PgUp / PgDn 整个功能删掉了，提示里自然也不再出现这两个键名；
   ESC 也不再出现在面板的退出口上。
   Home / End 留着 —— 它们是英文键名，不属于"上下左右"四个汉字，照旧。*/
const wchar_t *const g_panelHintMenu =
    L"↑↓：选择    ←→：调整    Home / End：首尾";
/* ★ 这一行**只剩下 R**，而且只在「用户偏好设置」页上出现。
   另外两样都撤掉了：
     · 「回车：展开预设方案」—— 只在光标选中当前预设方案时才显示，
       现在只写在第 0 行那一句里（g_panelHintRow0）。
     · 「关闭时自动保存」—— 落盘行为没变，只是不再在界面上说：
       那是**写盘时机**，不是**按什么键**，页脚只管后者。*/
const wchar_t *const g_panelHintPref = L"R：恢复用户偏好设置";
/* ★ 这一行原先写着「ESC：关闭    Tab：关闭」，现在**只剩 Tab**。
   ESC 只管暂停与继续、不用于关闭设置页面和历史记录页面 —— 它在面板里
   按下去已经什么都不做了，页脚再写「ESC：关闭」就是在说一件不会发生的
   事（这一条正是这套页脚一直守着的规矩）。*/
const wchar_t *const g_panelCloseMenu = L"Tab：关闭";
/* 光标停在第 0 行时的第一行提示。**必须另写一句**：那一行左右什么都不做，
   照着 g_panelHintMenu 写"左右：调整"就是骗人 —— 用户按了没反应，
   只会以为程序卡了。哪儿能按就说哪儿，这是页脚唯一的职责。*/
const wchar_t *const g_panelHintRow0 =
    L"↑↓：选择    回车：展开预设方案";
/* ★ 光标停在**菜单栏**上时的第一行提示。
   这里**不许写回车** —— 回车不能进入列表，只用于打开预设方案的二级菜单，
   也就是说光标在栏上时回车是个死键。页脚写一句按了没反应的话，比空着更糟。*/
const wchar_t *const g_panelHintBar = L"←→：切换分类    ↓：回到列表";
/* 弹窗展开时的第一行提示。这时候底下那张列表整个被压住，能按的只有
   上下和回车/ESC 三样，所以另外两行干脆不写。

   ★ 回车的说法用「确认」而不是「套用」。「套用」是程序内部的说法（代码里
   那条路径就叫 applyPreset），玩家手上做的是"在几个选项里挑一个并认下来"，
   说「确认」才是照着玩家那一侧的动作说话。*/
const wchar_t *const g_panelHintPop =
    L"↑↓：选择    回车：确认    ESC：收起列表";
const wchar_t *const g_panelNoHelp  = L"（无说明）";
/* 列表第 0 行「当前预设方案」的说明。与页脚一样，**定义在这里、render.cpp
   只按名字取用** —— 先前栽过的那个跟头（两边各写一份、改了一边另一边
   变成空心方块）不值得再栽第二次。*/
const wchar_t *const g_panelDetailHelp =
    L"当前生效的预设方案。全部玩法参数与某一套预设完全一致时显示该"
    L"方案名，否则显示「自定义参数」。光标停在这一行按回车可展开"
    L"八套预设，末尾那条即「自定义参数」。";
/* 预设弹窗里「自定义参数」那一条的说明（名字原先叫「自定义设置」）。
   「自定义参数」= "就这样吧，自行调整"，它不做任何改动，只把弹窗收起来。*/
/* ★ 措辞：「不套用任何预设」→「不改变任何参数」。一句话里"套用"这个词
   现在整份界面上都不剩了（页脚写的是「回车：确认」），免得同一个动作在
   页脚和说明栏里叫两个名字。*/
const wchar_t *const g_panelCustomHelp =
    L"不改变任何参数，保留当前的各项取值。选中后仅收起本列表。";
/* ★ 弹窗里那一条的**名字**（「自定义参数」，原先叫「自定义设置」）。名字
   原来硬写在 render.cpp 的画字那一行里 —— 那样自检够不着它，改错了也没人
   拦。挪到这里之后与说明文字并排，两边都是具名常量，自检可以直接量。*/
const wchar_t *const g_panelCustomName = L"自定义参数";
/* ★ 光标停在**菜单栏**上时，右栏解释的是"这一页是干什么的"。
   光标底下没有参数项可解释（它在栏上），拿上一项的文字糊过去就是驴唇不对
   马嘴；而两页的分工恰恰是玩家最该一眼看见的事 —— 一页会被预设覆盖、
   另一页不会，`R` 也只管后者。两句话都说清这条边界。*/
const wchar_t *const g_panelSectionHelp[PARAM_SECTION_COUNT] = {
    L"决定玩法的那些参数：气球、运动、节奏、判定与计分、训练。"
    L"共 32 项，会被预设方案整体覆盖。",
    L"与玩法无关的个人口味：玩家、画面、声音，共 12 项。"
    L"预设方案不会动它们，R 也只恢复这一段。"
};

const wchar_t *const g_hudStrings[] = {
    /* 标题与模式 */
    L"FPS 气球训练场",
    L"恒量靶场", L"计时挑战", L"渐进训练", L"精准挑战",
    L"轻松", L"标准", L"硬核",

    /* 记分板 */
    L"得分", L"连击", L"最高连击", L"命中率", L"出枪", L"命中",
    L"击破", L"漏球", L"用时", L"剩余时间", L"历史最高", L"破纪录！",
    L"本局结束",

    /* 场上状态 */
    L"墙上", L"个", L"生命", L"无限", L"暂停", L"已暂停",
    L"按 ESC 继续",
    L"气球训练场  第 %d 局", L"本局得分 %d", L"连击 %d 倍 %.1f",
    L"命中 %d / 出枪 %d", L"命中率 %d%%", L"用时 %s",
    L"历史最高 %d", L"距离纪录还差 %d 分", L"刷新了历史最高！",

    /* 结束原因（结算屏上写出来 —— "怎么死的"比"死了"有用）
       ★ 「自己收的」改成「手动结束」，原来的说法太口水话。改的是**显示
       文字**，常量名 END_MANUAL 与落盘用的机读键名一律不动 —— 动那两样
       会让 bin\记录.dat 里已有的历史认不出来。*/
    L"时间到", L"生命耗尽", L"球跑了", L"打空了", L"手动结束",

    /* 操作提示。R/Q 那两组旧文案（"R 重置   Q 结束"、"按 Q 结束本局并结算"、
       "按 ESC 返回训练场"）都删掉了 —— 对应的界面句子已经不存在了，留在
       表里只会让人以为还有一处没改到。
       ★ 「Tab 参数」改成「Tab 设置」—— 面板标题一直叫「设置」，左下角还
       写着「参数」是两处不同的叫法指着同一个东西。*/
    L"WASD 移动", L"鼠标 瞄准", L"左键 射击", L"ESC 暂停", L"Tab 设置",
    L"窗口失去焦点", L"本局已进行 %s，得分 %d",
    L"种子 %u", L"%d", L"+%d", L"×%.2f", L"%.0f 帧/秒",
    L"命中 %d / 出枪 %d   命中率 %d%%", L"墙上 %d 个", L"生命 %d",
    L"连击 %d  ×%.2f", L"连击 0", L"历史最高 暂无",

    /* 参数面板（多选框与滑杆的标签） */
    L"参数面板", L"关闭", L"恢复默认", L"保存并关闭",
    L"气球", L"数量", L"最小半径", L"最大半径", L"饱和度", L"亮度",
    L"随机种子", L"特殊气球", L"种类权重", L"普通球", L"小快球",
    L"金球", L"慢球",
    L"运动", L"运动方式", L"静止", L"匀速漂移", L"正弦摆动", L"随机跳变",
    L"漂移速度", L"漂移幅度", L"浮动幅度", L"浮动频率",
    L"节奏", L"存活时限", L"不限", L"漏球扣命", L"补位延迟", L"出现动画",
    L"连射间隔", L"单发",
    L"判定与计分", L"判定宽容", L"连击窗口", L"计分基准",
    L"漏球断连击",
    L"玩家", L"鼠标灵敏度", L"反转 Y 轴", L"移动速度", L"视野角",
    L"视点高度", L"头部晃动",
    L"画面", L"渲染缩放", L"墙面风格", L"抹灰墙", L"砖墙", L"瓷砖",
    L"毛坯混凝土", L"主光强度", L"雾浓度", L"地面色调", L"偏冷", L"偏暖",
    L"手持装置", L"准星样式", L"十字", L"十字点", L"圆环点", L"单点",
    L"准星大小", L"准星颜色", L"红", L"绿", L"蓝", L"碎片上限",
    L"残迹上限", L"曳光",
    L"训练", L"模式", L"难度", L"初始生命", L"时限", L"无限生命",
    L"统计面板",

    /* 参数面板：操作提示与存取反馈。参数名、枚举名、分组名、单位名以及
       侧栏说明与预设说明**都不在这里抄** —— 它们来自
       config.cpp 的描述表与预设表，由 hudCollectCharset 里的
       addFromDataTables 直接收进去，自检里还有一条断言逐字复核这件事。*/
    L"多数参数即时生效；生命、时限、随机种子于下一局生效",
    /* 面板上的成段提示语。它们本来是写在这里的字面量，后来挪到了
       上面的具名常量里（理由见那边），这里改成把常量收进表 ——
       字符照收不误，但定义只剩一处。*/
    g_panelHintMenu, g_panelHintPref, g_panelCloseMenu,
    g_panelHintRow0, g_panelHintBar, g_panelHintPop,
    g_panelNoHelp, g_panelDetailHelp, g_panelCustomHelp, g_panelCustomName,
    g_panelSectionHelp[0], g_panelSectionHelp[1],

    /* 面板的固定字样。
       **规矩**：界面撤掉的字就从表里撤掉 —— 留着只会让图集多出一批永远
       画不到的字，也会让后来看代码的人以为还有一个界面块没找着。
       历次清掉的字（「详细参数」「（已被修改）」「回车进入」「↑」「↓」等）
       都以界面为准，这里不再逐年记一遍。*/
    L"设置", L"当前预设方案", L"自定义参数",
    L"选项说明",
    L"第 %d / %d 项", L"%ls · 第 %d / %d 项",
    /* 标题栏右上角的另外两种状态（弹出层开着、光标在菜单栏上）。
       ★ 弹出层开着的时候原来一直写着"ESC 关闭"，可那一下按的是**收起列表**
       而不是关面板 —— 标题栏这一句也得跟着光标/层级说话，理由与页脚同一套。
       ★ 列表上那两句尾巴上的「ESC 关闭」删了：ESC 不再关面板，留着就是
       同一类谎话。弹窗那一句照旧 —— 那里 ESC 确实还在收列表。*/
    L"菜单栏",
    L"预设方案弹窗 · 第 %d / %d 项    ESC 收起列表",
    /* 「当前预设方案」那一行右边的**实心三角**：折起来是 ▶、展开是 ▼。
       这两个字符和图集里其它字一样，漏收就是一个空心方块 ——
       运行时告警会报出。上下还有内容的箭头也在这条线上。
       ★ ↑ ↓ 换成了单书名号那对 ︿ ﹀。
       ⚠ 这两个码位（U+FE3F / U+FE40，竖排形式的书名号）**不是所有字库都收**，
       所以不是想当然换掉的。换之前实测过三条，缺一条都不敢留：
         1) --atlas 跑完没有 [hud] 告警 —— 包括新补的那道"空白格"检查
            （见 hudBuildAtlas，专门为此加的，原先两条路都看不见"字库没有
            这个字形"，缺字会静默画成一片空白）；
         2) 临时塞一个私用区码位进这张表，确认那道检查真的会响，
            它报出了 U+E000；
         3) 三张面板截图（第 0 行、菜单栏态、滚动中段）里 ︿ 和 ﹀ 都是实实在在
            的尖角，不是空白也不是方块。
       日后换字体，这三条里只要有一条变红就说明该换字形了。
       ↑ ↓ 两个码位已经没有任何地方在画，从表里撤掉 —— 图集里不留画不到的
       字（跟清「详细参数」那批字是同一条规矩）。*/
    L"▶", L"▼", L"︿", L"﹀",
    /* 「自定义参数」那一行的说明现在是上面的具名常量 g_panelDetailHelp，
       表里已经是同一份对象了，这里不再抄一遍。
       顺手记一笔**破折号**：历史记录里"没有结束原因"那一格画的是 "—"。
       它不是通过格式化串拼出来的，是一句独立的字面量，所以曾经漏在表外 ——
       出图扫告警时才查出来。凡是会画到屏幕上的字，一个都不能靠"反正
       别的字符串里有"来蒙混。*/
    L"—",

    /* 两个大类标题。名字本身来自 config.cpp 的 g_paramSectionNames，
       由 addFromDataTables 自动收；这里留着是为了万一有人把那张表改了，
       图集仍然认得这几个字。*/
    L"预设方案设置", L"用户偏好设置",

    /* 历史记录屏。"第 N 次"里的 N 是动态拼的，但"第""次"
       这两个字必须在表里。*/
    L"历史记录", L"第 %d 次", L"%d 分", L"还没有记录", L"暂无记录",
    L"暂无记录。完成一局后在此显示。",
    L"按时间由近及远排列，最多保留 20 次。",
    /* ★ 这一屏的"怎么退出去"只剩 F 一条，翻页那半句也整个删掉。
       三处提示（标题栏、页脚第一行、页脚第三行）一起改，不留半拉子。*/
    L"第 %d / %d 次    F：返回", L"暂无记录    F：返回",
    L"↑↓：选择    Home / End：首尾",
    L"F：返回",
    L"结束：%ls", L"得分　　%d", L"命中　　%d", L"用时　　%.1f 秒", L"种子　　%u",
    L"按 F 查看历史记录", L"按 R 开始新游戏", L"按 R 结算并开始新游戏",
    L"Tab：打开设置面板",
    L"该项不受预设控制，已恢复默认值", L"已恢复预设值",

    /* 声音相关的两项参数，以及"设备没开成"时暂停屏上要说的那几句话。
       加这几行不是可选项：hud.cpp 的字形图集是照着这张表收集的，
       表里没有的字画出来是个空心方块。audio.cpp 里每一句 statusMsg
       都要能在下面找到 —— 改那边的文案时记得回来补这里。*/
    /* 「音量」「静音」两项并成了一个「声音」开关（见 config.h），
       这里跟着删掉旧的两个名字 —— 但**保留**「声音」这两个字：
       它同时是新参数名和参数分组名，两处都要用。*/
    L"声音", L"画面与声音",
    L"音频设备不可用（不影响玩法）",
    L"音频设备打开失败：", L"（", L"）", L"音频事件创建失败",
    L"混音线程创建失败", L"音效合成失败：内存不足", L"音频未初始化",
    L"没有音频驱动", L"设备被别的程序独占", L"设备号无效", L"驱动未启用",
    L"内存不足", L"设备不接受这个采样格式", L"未知原因",
    L"已全部保存", L"已载入上一次保存值", L"已恢复默认值", L"保存失败", L"尚未保存过参数",
    L"普通球权重", L"小快球权重", L"金球权重", L"慢球权重",
    L"准星红", L"准星绿", L"准星蓝",
    L"米/秒", L"次/秒", L"条", L"分", L"片",

    /* 开关与单位 */
    L"开", L"关", L"是", L"否", L"秒", L"米", L"个/秒", L"度",
    L"像素", L"倍", L"帧", L"项", L"共 %d 项",

    /* 排版用的符号。放在这里是因为图集照这张表收集字符 —— 少一个，
       HUD 上就是一个空心方块。曾经真的漏过这个间隔号，靠运行时的
       "图集里缺字"告警抓出来的。*/
    L"·", L"×", L"｜",
};

const int g_hudStringCount = (int)(sizeof(g_hudStrings) / sizeof(g_hudStrings[0]));

/* ============================================================ 字符集合 */

/* 打印用得到的 ASCII 全部预置进去。
   数字、百分号、冒号这些会通过格式化字符串动态拼出来（比如 FPS 数值），
   静态表里不可能穷举，与其逐条登记不如整段收进来 —— 一共才 95 个格子。*/
static void addAscii(HudFont *h) {
    wchar_t c;
    for (c = 32; c < 127; ++c) {
        if (h->count < HUD_MAX_CHARS) h->chars[h->count++] = c;
    }
}

static int cmpWchar(const void *a, const void *b) {
    wchar_t x = *(const wchar_t *)a, y = *(const wchar_t *)b;
    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
}

/* 把一个字符串的每个字塞进多重集。撞上限就置 overflow 并停下。*/
static void addString(HudFont *h, const wchar_t *s) {
    int j;
    for (j = 0; s && s[j]; ++j) {
        wchar_t c = s[j];
        if (c < 32) continue;                     /* 控制字符不进图集 */
        if (h->count >= HUD_MAX_CHARS) { h->overflow = 1; return; }
        h->chars[h->count++] = c;
    }
}

/* 从**数据表**里收字：参数字段名、枚举名、单位、侧栏说明、预设名与说明。
 *
 * 这批文案（42 条侧栏说明 + 7 段预设说明）没有抄进 g_hudStrings[]，
 * 而是直接从 config.cpp 的 g_paramDescs / g_presets 里收。理由是那两张表本来
 * 就是这些文字的**唯一真源**，再抄一遍就多了一处会走散的地方 —— 而且是最容易
 * 走散的一处：改了说明忘了同步，症状是界面上冒出几个空心方块，很难一眼看出来。
 * 走这条路之后，"表里有的字图集里一定有"是从构造上成立的，不需要靠人记得登记。
 *
 * 代价是 hud.cpp 依赖 config.h —— 这是可以接受的：config.h 是纯数据，不含
 * 任何行为，也不会反过来依赖 hud（它连 GL 都不认识）。*/
static void addFromDataTables(HudFont *h) {
    int i, k;
    for (i = 0; i < g_paramDescCount; ++i) {
        const ParamDesc *d = &g_paramDescs[i];
        addString(h, d->name);
        addString(h, d->unit);
        addString(h, d->help);
        for (k = 0; k < d->enumCount; ++k) addString(h, d->enumNames[k]);
    }
    for (i = 0; i < g_paramGroupCount; ++i) addString(h, g_paramGroupNames[i]);
    for (i = 0; i < g_presetCount; ++i) {
        addString(h, g_presets[i].name);
        addString(h, g_presets[i].desc);
    }
}

void hudCollectCharset(HudFont *h) {
    int i, n;

    if (!h) return;
    h->count = h->total = h->overflow = 0;
    addAscii(h);

    /* 这里收的是**多重集**：同一个字出现几次就塞几次，一会儿统一去重。
       代价是容量必须按"总字数"算（见 hud.h 里 HUD_MAX_CHARS 的说明）——
       截断过一次，表尾的排版符号没了，HUD 上全是空心方块。*/
    for (i = 0; i < g_hudStringCount; ++i)
        addString(h, g_hudStrings[i]);
    addFromDataTables(h);
    h->total = h->count;

    /* 排序 + 去重：排序之后才能二分查找，去重让"字符数"这个数量有意义。*/
    qsort(h->chars, (size_t)h->count, sizeof(wchar_t), cmpWchar);
    n = 0;
    for (i = 0; i < h->count; ++i) {
        if (n == 0 || h->chars[i] != h->chars[n - 1]) h->chars[n++] = h->chars[i];
    }
    h->count = n;
}

int hudCharIndex(const HudFont *h, wchar_t c) {
    int lo, hi;
    if (!h || h->count <= 0) return -1;
    lo = 0;
    hi = h->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (h->chars[mid] == c) return mid;
        if (h->chars[mid] < c) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

/* ============================================================ 图集 */

#define ATLAS_PAD 4

/* 自校验画参考字时，格子四周要留的余量（像素）。
   参考字画在 (cell + 2*这个值) 见方的大位图上再裁中间一格，为的是
   **不裁剪** —— 详见 glyphCoverage 上面那段。32 足够容纳任何正常字形的
   下伸部与负边距（实测最"越界"的 'j' 只超出笔位 4 像素）。*/
#define ATLAS_REF_MARGIN 32

/* 自校验判"这一格装的不是自己"的差异门槛（见 glyphMismatch 里的量测说明）。*/
#define ATLAS_DIFF_TOL 0.12f

static void noteMissing(HudFont *h, wchar_t c) {
    int i;
    h->missingTotal++;
    for (i = 0; i < h->missingCount; ++i)
        if (h->missing[i] == c) return;
    if (h->missingCount < HUD_MISSING_MAX) h->missing[h->missingCount++] = c;
}

void hudReportMissing(HudFont *h) {
    int i;
    if (!h || h->missingCount <= h->reportedTo) return;
    /* 只报新出现的那些字：HUD 每帧都画，不这样会每帧刷一次屏。
       带上 U+ 码点：终端字体常常显示不出生僻字，只打字符本身等于没说。*/
    printf("[hud] 警告：有 %d 个字不在字形图集里，已画成方块 —— ",
           h->missingCount - h->reportedTo);
    for (i = h->reportedTo; i < h->missingCount; ++i)
        printf("U+%04X '%lc'  ", (unsigned)h->missing[i], (wint_t)h->missing[i]);
    printf("\n      （把用到它们的文案补进 hud.cpp 的 g_hudStrings[] 即可）\n");
    fflush(stdout);
    h->reportedTo = h->missingCount;
}

/* ---------------------------------------------------------- 图集自校验
 *
 * 把贴图第 i 格里**实际画着的**那个字，和"单独重画一次 chars[i]"逐像素
 * 比对。对不上就报出下标，并在全体字符里找出这一格真正的字。
 *
 * 为什么需要这么个东西：HUD 上出现错字时，盯着渲染结果看是靠不住的
 * ——截图缩放、描边阴影、两个字符串叠在一起，都能让眼睛读出并不存在
 * 的字，最后把时间浪费在猜上。这个函数给出的是可判定的结论：
 * 第 i 格里装的是 chars[j]，j 等于几。
 *
 * 只读贴图 + 自己重画，不改动任何状态，随时可以调。
 *
 * ★ 参考图**必须画在不裁剪的位图上**，这是这套自检能不能查出问题的关键。
 *
 * 直接把字画进一张 cell x cell 的小位图：一旦字被画偏了（比如整体下移
 * 一个 ascent），溢出的部分会被 GDI 裁掉，于是"参考图"和"图集里那一格"
 * 以同样的方式缺了同一块，逐像素比对报"通过" —— 曾经整整一行字错位
 * 到相邻格，自检却安静得很，就是这么来的。参考图和被测对象一起错、
 * 互相印证，是所有自检里最隐蔽的一种失效。
 *
 * 所以这里画在 (cell + 2*ATLAS_REF_MARGIN) 见方的大位图上，只把中间
 * cell x cell 那格取出来当参考。参考图里那个字是**完整**的；图集里若
 * 少了一截，差异就实打实地摆在那儿，普通相似度比对就能抓住。
 *
 * 这也让"边界上有没有墨"那类启发式判据变得没必要 —— 那套判据会在
 * 带下伸部、或左侧负边距的字上误报（实测 'j' 就是这样：它的下钩伸到
 * 笔位左边 4 个像素，正好压住格子左沿，其实完全正常）。
 */
/* 本来就该画成空白的字符。建图集时那道"空格子"检查要拿它排除 ——
   把空格报成"缺字"是纯噪声，报得多了真缺字就没人看了。
   名单是照着 Unicode 里的各种空白给的，不用全，够用就行：
   普通空格、不换行空格、全角空格、以及排版用的几个零宽/窄空格。*/
static int isBlankByNature(wchar_t c) {
    return c == L' ' || c == L'\t' ||
           c == 0x00A0 || c == 0x2000 || c == 0x2001 || c == 0x2002 ||
           c == 0x2003 || c == 0x2009 || c == 0x200A || c == 0x200B ||
           c == 0x202F || c == 0x205F || c == 0x3000 || c == 0xFEFF;
}

static int glyphCoverage(HDC dc, int cell, int pad,
                         wchar_t ch, unsigned char *out) {
    BITMAPINFO bi;
    HBITMAP bmp, oldBmp;
    void *bits = NULL;
    unsigned char *dib;
    const int m = ATLAS_REF_MARGIN;
    const int big = cell + ATLAS_REF_MARGIN * 2;
    int y, x;

    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = big;
    bi.bmiHeader.biHeight      = -big;          /* 自上而下，与图集一致 */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp || !bits) return -1;
    dib = (unsigned char *)bits;
    memset(dib, 0, (size_t)big * big * 4);

    oldBmp = (HBITMAP)SelectObject(dc, bmp);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    /* y 是**字符框顶**，不是基线 —— TextOut 默认 TA_TOP 对齐。
       这里曾经写成 pad + ascent（当基线用），结果整个字往下掉了一个
       ascent，大半掉出格子；自检也跟着错、还互相印证"通过"。*/
    TextOutW(dc, m + pad, m + pad, &ch, 1);
    GdiFlush();

    /* 与图集同一套转换：字是白的，max(r,g,b) 当覆盖度。只取中间那一格。*/
    for (y = 0; y < cell; ++y) {
        for (x = 0; x < cell; ++x) {
            const unsigned char *p = dib + (((size_t)(m + y) * big) + (m + x)) * 4;
            unsigned char b = p[0], g = p[1], r = p[2];
            out[y * cell + x] = r > g ? (r > b ? r : b) : (g > b ? g : b);
        }
    }

    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    return 0;
}

/* 两张同尺寸覆盖度图的差异：返回相差超过阈值的像素占比。*/
static float coverageDiff(const unsigned char *a, const unsigned char *b, int n) {
    int i, bad = 0;
    for (i = 0; i < n; ++i) {
        int d = (int)a[i] - (int)b[i];
        if (d < 0) d = -d;
        if (d > 40) ++bad;
    }
    return (float)bad / (float)n;
}

/* 两处复核共用的判据：读回来的一格（ref）里装的到底是不是 chars[self]？

   0.12 这个门槛是量出来的：同一个字重画一遍，因为抗锯齿边缘的亚像素差异，
   差异本底在 0.04 上下（笔画越细占比越高）。真正"格子装错了字"是 0.3 以上，
   两者差一个数量级，门槛放在中间。越过门槛才做全表扫描确认 —— 那个扫描是
   O(n^2) 的，不能每格都做。

   返回值（见下面两个符号）：GLYPH_NOCOVER / GLYPH_SELF / 别的字符的下标。
   这段判据两处复核（内存下标一套、绘制路径一套）原本是逐字抄了一遍的，
   连门槛 0.12f 都是两个字面量；改一处忘一处的话，两套复核就会用不同的标准
   判同一件事，而报告里两行都写"通过"。现在只有这一份。*/
#define GLYPH_NOCOVER  (-1)     /* 连"它自己"都重画不出来：调用方自己决定怎么办 */
#define GLYPH_SELF     (-2)     /* 这一格就是它自己 —— 包括"最像的仍是它" */
static int glyphMismatch(HDC dc, int cell, int pad, const wchar_t *chars, int count,
                         const unsigned char *ref, unsigned char *probe, int self) {
    int j, best;
    float d, bestD;

    if (glyphCoverage(dc, cell, pad, chars[self], probe) != 0) return GLYPH_NOCOVER;

    d = coverageDiff(ref, probe, cell * cell);
    if (d <= ATLAS_DIFF_TOL) return GLYPH_SELF;

    /* 这一格到底是谁？在全体字符里找最像的那个（真取最小值，不能用
       "小于阈值就算"来凑 —— 那样找出来的是最后一个）。只有"最像的也不是
       自己"才算真错：最像的是自己，说明这一格装的就是它，前面那点差异纯粹
       是抗锯齿噪声。*/
    best  = -1;
    bestD = -1.0f;
    for (j = 0; j < count; ++j) {
        float dj;
        if (glyphCoverage(dc, cell, pad, chars[j], probe) != 0) continue;
        dj = coverageDiff(ref, probe, cell * cell);
        if (bestD < 0.0f || dj < bestD) { bestD = dj; best = j; }
    }
    return (best < 0) ? GLYPH_SELF : best;
}

int hudVerifyAtlas(HudFont *h) {
    HDC dc;
    HFONT font, oldFont;
    unsigned char *tga = NULL;          /* 贴图里读回来的 alpha */
    unsigned char *ref = NULL, *probe = NULL;
    int aw, ah, cell, pad, i, bad = 0;
    /* 不符的格子逐条记下来一起印：单独报"第一处"用处不大，
       把全部不符排出来才能看出规律（整行错位？某类字形混淆？）。
       清单条数单独记（listCount），不复用 bad —— 两者只有在"每个 bad
       都对应一条清单项"时才相等，一旦将来某个分支只加 bad 不写清单，
       报告循环就会拿未初始化的 listIdx[0] 当字符下标用，越界读。*/
    const int VERIFY_LIST_MAX = 24;
    int listIdx[24], listGot[24];
    int listCount = 0;

    if (!h || !h->ok) return -1;
    cell = h->cell;
    pad  = ATLAS_PAD;
    aw = h->cols * cell;
    ah = h->rows * cell;

    /* 先问 GL 贴图的真实尺寸。这一步看着多余，其实是关键：
       如果贴图实际大小和 "格数 x 格边长" 对不上，采样公式再对也没用，
       而看渲染结果或者看图集 dump 都发现不了 —— 两边都会各自"自洽"。*/
    {
        GLint tw = 0, th = 0;
        glBindTexture(GL_TEXTURE_2D, h->tex);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH,  &tw);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
        glBindTexture(GL_TEXTURE_2D, 0);
        printf("[图集] 贴图真实尺寸 %dx%d，按格数算应当是 %dx%d —— %s\n",
               (int)tw, (int)th, aw, ah,
               (tw == aw && th == ah) ? "一致" : "**不一致**");
        if (tw > 0 && th > 0) { aw = tw; ah = th; }   /* 以 GL 为准读回来 */
    }

    tga = (unsigned char *)malloc((size_t)aw * ah * 4);
    ref = (unsigned char *)malloc((size_t)cell * cell);
    probe = (unsigned char *)malloc((size_t)cell * cell);
    if (!tga || !ref || !probe) { free(tga); free(ref); free(probe); return -1; }

    glBindTexture(GL_TEXTURE_2D, h->tex);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, tga);
    glBindTexture(GL_TEXTURE_2D, 0);

    dc = CreateCompatibleDC(NULL);
    if (!dc) { free(tga); free(ref); free(probe); return -1; }
    font = CreateFontW(-h->fontPx, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                       GB2312_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       L"Microsoft YaHei");
    if (!font) { DeleteDC(dc); free(tga); free(ref); free(probe); return -1; }
    oldFont = (HFONT)SelectObject(dc, font);

    for (i = 0; i < h->count; ++i) {
        int cx = (i % h->cols) * cell;
        int cy = (i / h->cols) * cell;
        int y, x, got;

        for (y = 0; y < cell; ++y)
            for (x = 0; x < cell; ++x)
                ref[y * cell + x] = tga[((cy + y) * aw + (cx + x)) * 4 + 3];

        got = glyphMismatch(dc, cell, pad, h->chars, h->count, ref, probe, i);
        if (got == GLYPH_NOCOVER) break;     /* 重画不出来：这一轮不再往下查 */
        if (got != GLYPH_SELF) {
            ++bad;
            if (listCount < VERIFY_LIST_MAX) {
                listIdx[listCount] = i;
                listGot[listCount] = got;
                ++listCount;
            }
        }
    }

    /* ---- 用真正的绘制路径再复核一遍 ----
       上面那套是**内存下标**比对：它能证明"贴图存储里第 i 格放的是第 i 个字"，
       但证明不了"画到屏幕上时采样的就是第 i 格"。UV 算错、少采一行这种事，
       内存比对一概看不见 —— 屏幕上是错字而自校验报"全对"就是这么来的。
       这里走 hudText 真正的绘制路径：把字画到屏幕上，glReadPixels 读回来，
       再和单独重画的字形比。两套结果不一致时，问题必然在 UV 采样上。*/
    {
        GLint vp[4];
        int vw, vh, perRow, start, k, gpuBad = 0;
        int gpuListIdx[24], gpuListGot[24];
        unsigned char *px = NULL;
        Color4 white;
        white.r = white.g = white.b = white.a = 1.0f;

        glGetIntegerv(GL_VIEWPORT, vp);
        vw = (vp[2] > 0) ? vp[2] : 1600;
        vh = (vp[3] > 0) ? vp[3] : 900;
        perRow = (vw - 8) / (cell + 4);
        if (perRow > h->count) perRow = h->count;
        if (perRow < 1) perRow = 1;
        px = (unsigned char *)malloc((size_t)cell * (size_t)cell * 4);

        if (px) {
            for (start = 0; start < h->count; start += perRow) {
                int n = h->count - start;
                if (n > perRow) n = perRow;

                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

                /* 背景纯黑 + 白色字，混合之后的像素值就等于字形的覆盖度，
                   于是读回来的数据可以直接和 glyphCoverage 比，不用反解。*/
                hudBeginScreen(vw, vh);
                for (k = 0; k < n; ++k) {
                    wchar_t one[2];
                    one[0] = h->chars[start + k];
                    one[1] = 0;
                    hudText(h, (float)(4 + k * (cell + 4)), (float)(pad + 2),
                            (float)h->fontPx, HUD_LEFT, white, one);
                }
                hudEndScreen();
                glFinish();          /* 不 glFinish 就 glReadPixels，读到的可能是旧帧 */

                for (k = 0; k < n; ++k) {
                    int gi = start + k;
                    int rx = 4 + k * (cell + 4);
                    int ryTop = 2;                  /* hudText 内部把行顶减了一个 pad */
                    int gy = vh - ryTop - cell;     /* glReadPixels 的 y 自下而上 */
                    int y, x, got;

                    glReadPixels(rx, gy, cell, cell, GL_RGBA, GL_UNSIGNED_BYTE, px);
                    for (y = 0; y < cell; ++y)
                        for (x = 0; x < cell; ++x)
                            ref[y * cell + x] = px[((cell - 1 - y) * cell + x) * 4];

                    got = glyphMismatch(dc, cell, pad, h->chars, h->count, ref, probe, gi);
                    /* NOCOVER 与 SELF 都不算错 —— 与上一套复核的差别只在这里：
                       上一套遇到 NOCOVER 就整轮放弃（内存比对查不动了，后面的
                       结论都没意义），这一套是逐格独立的，跳过这一格就行。*/
                    if (got < 0) continue;
                    ++gpuBad;
                    if (gpuBad <= VERIFY_LIST_MAX) {
                        gpuListIdx[gpuBad - 1] = gi;
                        gpuListGot[gpuBad - 1] = got;
                    }
                }
            }
            free(px);
        }

        if (gpuBad == 0) {
            printf("[图集] 绘制路径复核通过：%d 个字画到屏幕上都是自己\n", h->count);
        } else {
            int n = (gpuBad < VERIFY_LIST_MAX) ? gpuBad : VERIFY_LIST_MAX;
            printf("[图集] 绘制路径复核**失败**：%d/%d 个字画到屏幕上不是自己"
                   "（内存下标若是对的，说明问题出在 UV 采样）\n", gpuBad, h->count);
            for (i = 0; i < n; ++i) {
                int kk = gpuListIdx[i], g = gpuListGot[i];
                printf("[图集]   第 %d 格（列 %d 行 %d）应有 U+%04X '%lc'"
                       "  →  屏幕上却是 U+%04X '%lc'（第 %d 格），下标差 %d\n",
                       kk, kk % h->cols, kk / h->cols,
                       (unsigned)h->chars[kk], (wint_t)h->chars[kk],
                       (unsigned)h->chars[g], (wint_t)h->chars[g], g, g - kk);
            }
        }
        fflush(stdout);
    }

    SelectObject(dc, oldFont);
    DeleteObject(font);
    DeleteDC(dc);

    if (bad == 0) {
        printf("[图集] 自校验通过：%d 格里装的都是各自的字\n", h->count);
    } else {
        int n = (listCount < VERIFY_LIST_MAX) ? listCount : VERIFY_LIST_MAX;
        printf("[图集] 自校验**失败**：%d/%d 格装的不是自己那个字%s\n",
               bad, h->count, (bad > n) ? "（只列前若干条）" : "");
        for (i = 0; i < n; ++i) {
            int k = listIdx[i], g = listGot[i];
            printf("[图集]   第 %d 格（列 %d 行 %d）应有 U+%04X '%lc'"
                   "  →  实际是 U+%04X '%lc'（第 %d 格），下标差 %d\n",
                   k, k % h->cols, k / h->cols,
                   (unsigned)h->chars[k], (wint_t)h->chars[k],
                   (unsigned)h->chars[g], (wint_t)h->chars[g], g, g - k);
        }
    }
    fflush(stdout);

    free(tga); free(ref); free(probe);
    return bad;
}

int hudBuildAtlas(HudFont *h, int fontPx) {
    HDC     dc;
    HBITMAP bmp, oldBmp;
    HFONT   font, oldFont;
    BITMAPINFO bi;
    unsigned char *dib = NULL;
    unsigned char *rgba = NULL;
    TEXTMETRICW tm;
    void *bits = NULL;
    Texture tex;
    int i, cols, rows, aw, ah;
    int ascent, cell, pad = ATLAS_PAD;

    if (!h) return -1;
    memset(h, 0, sizeof(*h));
    hudCollectCharset(h);
    if (h->count <= 0) return -1;
    if (fontPx < 8) fontPx = 8;

    /* 先建字体量一遍行高：格子必须装得下"上伸 + 下伸"，
       只按字号留格子会让带下伸的字母（g、y）被切掉一截。*/
    dc = CreateCompatibleDC(NULL);            /* 不需要窗口，离屏就够了 */
    if (!dc) return -1;

    font = CreateFontW(-fontPx, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                       GB2312_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       L"Microsoft YaHei");
    if (!font) { DeleteDC(dc); return -1; }
    oldFont = (HFONT)SelectObject(dc, font);

    memset(&tm, 0, sizeof(tm));
    if (!GetTextMetricsW(dc, &tm)) {           /* 拿不到就用字号凑一个 */
        tm.tmAscent  = (LONG)(fontPx * 0.82f);
        tm.tmDescent = (LONG)(fontPx * 0.22f);
    }
    ascent = tm.tmAscent;
    cell   = ascent + tm.tmDescent + pad * 2;
    if (cell < fontPx + pad * 2) cell = fontPx + pad * 2;
    printf("[atlas] fontPx=%d tmAscent=%ld tmDescent=%ld tmHeight=%ld "
           "tmExternalLeading=%ld pad=%d cell=%d\n",
           fontPx, (long)tm.tmAscent, (long)tm.tmDescent, (long)tm.tmHeight,
           (long)tm.tmExternalLeading, pad, cell);
    fflush(stdout);


    /* 格子数与格边长都定下来之后，先看一眼这张贴图会不会超过驱动的
       最大边长。超了就**加宽换高度**（行数减半、列数加倍），加宽到极限
       还要超出才认输。老显卡的 GL_MAX_TEXTURE_SIZE 可能只有 1024，
       不查这一下就是"贴图创建失败 → HUD 整个不见"，很难查。*/
    cols = HUD_ATLAS_COLS;
    {
        GLint maxTex = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
        if (maxTex <= 0) maxTex = 1024;            /* 问不到就按最保守的来 */
        for (;;) {
            rows = (h->count + cols - 1) / cols;
            aw = cols * cell;
            ah = rows * cell;
            if ((aw <= maxTex && ah <= maxTex) || cols >= 256) break;
            cols *= 2;
        }
        if (aw > maxTex || ah > maxTex) {
            /* 加宽到 256 列还是装不下 —— 只可能是字号大得离谱。
               报出来再退，不装作成功。*/
            printf("[hud] 错误：字形图集需要 %dx%d，超过驱动上限 %d；"
                   "把字号调小或减少文案\n", aw, ah, (int)maxTex);
            SelectObject(dc, oldFont); DeleteObject(font); DeleteDC(dc);
            return -1;
        }
    }

    /* 32 位 DIB，**高度取负**表示自上而下存 —— 这样内存里的行序就和
       贴图的 v 方向一致，后面不用把图上下翻一遍。*/
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = aw;
    bi.bmiHeader.biHeight      = -ah;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp || !bits) {
        SelectObject(dc, oldFont); DeleteObject(font); DeleteDC(dc);
        return -1;
    }
    dib = (unsigned char *)bits;
    memset(dib, 0, (size_t)aw * ah * 4);       /* 背景全透明 */

    oldBmp = (HBITMAP)SelectObject(dc, bmp);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));

    for (i = 0; i < h->count; ++i) {
        int cx = (i % cols) * cell;
        int cy = (i / cols) * cell;
        SIZE sz;
        wchar_t ch = h->chars[i];

        /* 所有字共用同一个字符框顶位置，基线自然对齐。
           **这里必须是 pad，不是 pad + ascent**：TextOut 默认 TA_TOP，
           给的 y 是字符框的顶、不是基线。曾经按基线写，字整个下移了一个
           ascent，掉进下一格 —— HUD 上整整一行字全都变成了上一行的内容
           （ASCII 恰好差 16 个下标），而图集自检因为用了同一处错误写法，
           两边一起错、互相印证，报的是"通过"。*/
        TextOutW(dc, cx + pad, cy + pad, &ch, 1);

        /* 步进宽度问 GDI 要：中文是满宽、西文是比例宽，
           全按满宽排会让西文稀稀拉拉。*/
        memset(&sz, 0, sizeof(sz));
        if (!GetTextExtentPoint32W(dc, &ch, 1, &sz) || sz.cx <= 0)
            sz.cx = (LONG)cell;
        h->adv[i] = (float)sz.cx;
    }

    GdiFlush();

    /* DIB(BGRX) → RGBA：字是白的，取最大通道当覆盖度。*/
    rgba = (unsigned char *)malloc((size_t)aw * ah * 4);
    if (!rgba) {
        SelectObject(dc, oldBmp); DeleteObject(bmp);
        SelectObject(dc, oldFont); DeleteObject(font); DeleteDC(dc);
        return -1;
    }
    for (i = 0; i < aw * ah; ++i) {
        unsigned char b = dib[i * 4 + 0];
        unsigned char g = dib[i * 4 + 1];
        unsigned char r = dib[i * 4 + 2];
        unsigned char a = r > g ? (r > b ? r : b) : (g > b ? g : b);
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = a;
    }

    /* ---- 哪一格是空的（一道兜底闸）-----------------------------------------
     *
     * 缺字这件事，原先两条路都看不见：
     *
     *   ① 图集自校验（hudVerifyAtlas）：它把同一个字**再画一遍**跟格子里的比。
     *      字库没有这个码位时，重画出来还是空白，跟格子里的空白一模一样，
     *      差异 0 —— 照样报"通过"。
     *   ② 运行时的 noteMissing：它只认"这个字没进图集"。而缺字的字**是**在
     *      图集里的，只是那一格空着，它管不着。
     *
     * 于是缺字的表现是**静默画成一片空白** —— 不报错、不出方块、图集自校验
     * 全绿。（空心方块那个是"没进图集"的表现，跟这个不是一回事。）
     *
     * 判据只能落在"实际画出来是不是空白"上，不能问 GetGlyphIndicesW：那个
     * 只查当前字体的 cmap，而 TextOutW 会走 Windows 的字体回退，从别的字体
     * 借字形画出来。先写成 GetGlyphIndicesW，一跑就把 ▶（U+25B6）报了
     * 出来 —— 而它在截图里画得好好的，纯属虚惊。回退这条路正是它看不见的。
     *
     * 本来就该是空白的字符（空格之类）不算 —— isBlankByNature 那张名单。*/
    {
        wchar_t blankGl[16];
        int blankTotal = 0, blankN = 0;
        for (i = 0; i < h->count; ++i) {
            const int cx = (i % cols) * cell;
            const int cy = (i / cols) * cell;
            int y, x, lit = 0;
            for (y = 0; y < cell && !lit; ++y) {
                for (x = 0; x < cell; ++x) {
                    if (rgba[((size_t)(cy + y) * aw + (cx + x)) * 4 + 3] > 0) {
                        lit = 1;
                        break;
                    }
                }
            }
            if (lit || isBlankByNature(h->chars[i])) continue;
            ++blankTotal;
            if (blankN < (int)ARRAY_COUNT(blankGl)) blankGl[blankN++] = h->chars[i];
        }
        if (blankTotal > 0) {
            int k;
            printf("[hud] 警告：有 %d 个字画出来是一片空白（字库里没有这个字形）"
                   "—— ", blankTotal);
            for (k = 0; k < blankN; ++k)
                printf("U+%04X '%lc'  ", (unsigned)blankGl[k], (wint_t)blankGl[k]);
            if (blankTotal > blankN) printf("（还有 %d 个没列）", blankTotal - blankN);
            printf("\n      （换字体，或把用不到的文案撤出 g_hudStrings[]）\n");
            fflush(stdout);
        }
    }

    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    SelectObject(dc, oldFont);
    DeleteObject(font);
    DeleteDC(dc);

    /* ★ 这张贴图**不能走 texFromRGBA**：那条路带 gluBuild2DMipmaps，
       而图集尺寸（比如 960x1320）不是 2 的幂，GLU 会把整幅图重采样成
       2 次幂 —— 网格跟着变形，格子的位置全错。表现是 HUD 上出现**错字**：
       ASCII 刚好差一行（一行 16 格，'W' 就变成了 −16 的 'G'），中文则是
       看不出规律的邻格字。查这个 bug 花了很久，因为"字是错的"看起来
       像编码问题，其实跟编码毫无关系。
       图集是 1:1 贴的精灵表，本来也不需要 mipmap。所以自己上传：
       不生成 mipmap，过滤用线性，超界夹边。*/
    memset(&tex, 0, sizeof(tex));
    {
        glGenTextures(1, &tex.id);
        if (!tex.id) { free(rgba); return -1; }
        glBindTexture(GL_TEXTURE_2D, tex.id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, aw, ah, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, rgba);
        /* 不生成 mipmap，所以 MIN 也必须用非 mipmap 的过滤器 ——
           用了 GL_*_MIPMAP_* 而没建 mipmap，贴图会整个不显示（黑块）。*/
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        tex.w = aw;
        tex.h = ah;
        tex.ok = 1;
    }
    free(rgba);

    h->tex     = tex.id;
    h->cols    = cols;
    h->rows    = rows;
    h->cell    = cell;
    h->fontPx  = fontPx;
    h->ascent  = (float)ascent;
    h->descent = (float)tm.tmDescent;
    h->ok      = 1;
    return 0;
}

void hudDestroy(HudFont *h) {
    if (!h) return;
    if (h->tex) glDeleteTextures(1, &h->tex);
    h->tex = 0;
    h->ok  = 0;
}

/* ============================================================ 屏幕空间 */

void hudBeginScreen(int winW, int winH) {
    GLint vp[4];
    int w = winW, hgt = winH;

    /* 用视口而不是窗口尺寸：窗口尺寸可以从 WM_SIZE 拿到，但视口是 GL
       自己认的那个数，两者万一不同步，用视口画出来的 HUD 一定和 3D 画面
       对得上。这是"以 GL 为准"的写法。*/
    glGetIntegerv(GL_VIEWPORT, vp);
    if (vp[2] > 0) w = vp[2];
    if (vp[3] > 0) hgt = vp[3];

    /* 纹理矩阵也压一个单位阵进去。这一句看着多余 —— 默认就是单位阵 ——
       但 3D 那边只要有人用过 GL_TEXTURE 矩阵而忘了复位，HUD 上每个字的
       采样就会整体偏移，症状是"字都认识、但每个位置上坐的是另一个字"，
       而且怎么查 UV 都查不出问题（UV 确实是对的）。宁可按最坏情况防一手。*/
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glLoadIdentity();

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    /* top=0、bottom=h：把 y 轴翻过来，与 Win32 的屏幕坐标一致。*/
    glOrtho(0.0, (double)w, (double)hgt, 0.0, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    /* 纹理坐标生成如果被开着，glTexCoord2f 会被整个忽略，采样位置改由
       顶点坐标算出来 —— 同样表现为"字全错但字本身都是图集里的字"。*/
    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_GEN_T);
    glDisable(GL_TEXTURE_GEN_R);
    glDisable(GL_TEXTURE_GEN_Q);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

void hudEndScreen(void) {
    glMatrixMode(GL_TEXTURE);
    glPopMatrix();

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();

    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
}

/* ============================================================ 绘制 */

void hudText(HudFont *h, float x, float y, float px, HudAlign align,
             Color4 color, const wchar_t *s) {
    float scale, pen, qx, qy, qs;
    float uStep, vStep;
    int i;

    if (!s || !*s) return;
    if (!h || !h->ok || h->fontPx <= 0) {
        /* 图集没建起来也不能让 HUD 直接消失 —— 画个灰条示意"这儿有字"。
           ★ 这里原来用的是本文件自己的 `static Color4 c4(...)`，与 render.cpp
           里那个**一字不差**各写了一份（render.cpp 一天到晚在用，这个文件只有
           这一处需要），已经删掉，改用 core.h 里现成的 color3 / color4。
           少一份重复的"造颜色"函数，也就少一处两边会走散的可能（比如哪天给
           其中一份加个钳位）。*/
        hudPanel(x, y, 120.0f, px, color4(color3(0.5f, 0.2f, 0.2f), 0.6f),
                 color4(color3(0.9f, 0.4f, 0.4f), 0.8f));
        return;
    }

    scale = px / (float)h->fontPx;
    if (align != HUD_LEFT) {
        float w = hudTextWidth(h, s, px);
        if (align == HUD_CENTER) x -= w * 0.5f;
        else                      x -= w;
    }

    /* 格子是"上伸 + 下伸 + 两侧留白"，而 y 按约定是**行顶**（上伸的顶），
       所以四边形要往上挪一个 pad，字才会落在该落的地方。*/
    qy = y - (float)ATLAS_PAD * scale;
    qs = (float)h->cell * scale;
    uStep = 1.0f / (float)h->cols;
    vStep = 1.0f / (float)h->rows;

    glBindTexture(GL_TEXTURE_2D, h->tex);
    glColor4f(color.r, color.g, color.b, color.a);

    pen = x;
    for (i = 0; s[i]; ++i) {
        int idx = hudCharIndex(h, s[i]);
        if (idx >= 0) {
            float u0 = (float)(idx % h->cols) * uStep;
            float v0 = (float)(idx / h->cols) * vStep;
            qx = pen;

            glBegin(GL_QUADS);
            glTexCoord2f(u0,          v0);          glVertex2f(qx,      qy);
            glTexCoord2f(u0 + uStep,  v0);          glVertex2f(qx + qs, qy);
            glTexCoord2f(u0 + uStep,  v0 + vStep);  glVertex2f(qx + qs, qy + qs);
            glTexCoord2f(u0,          v0 + vStep);  glVertex2f(qx,      qy + qs);
            glEnd();

            pen += h->adv[idx] * scale;
        } else {
            /* 图集里没有：画个空心方块，同时记账。
               空心方块很扎眼，比"少一个字"容易被发现得多。*/
            float s2 = px * 0.72f;
            noteMissing(h, s[i]);
            glBindTexture(GL_TEXTURE_2D, 0);
            glBegin(GL_LINE_LOOP);
            glVertex2f(pen,        y);
            glVertex2f(pen + s2,   y);
            glVertex2f(pen + s2,   y + px);
            glVertex2f(pen,        y + px);
            glEnd();
            glBindTexture(GL_TEXTURE_2D, h->tex);
            pen += s2 + 2.0f;
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

float hudTextWidth(const HudFont *h, const wchar_t *s, float px) {
    float w = 0.0f, scale;
    int i;
    if (!h || !h->ok || !s) return 0.0f;
    scale = px / (float)h->fontPx;
    for (i = 0; s[i]; ++i) {
        int idx = hudCharIndex(h, s[i]);
        if (idx >= 0) w += h->adv[idx] * scale;
        else          w += px * 0.72f + 2.0f;
    }
    return w;
}

/* ---------------------------------------------------------- 底板与进度条 */

static void solidQuad(float x, float y, float w, float h) {
    /* 关掉贴图再画：虽然不绑贴图时 GL_MODULATE 也会退化成纯顶点色，
       但那是"默认贴图恰好是白的"这个约定，写明白更稳。*/
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    glVertex2f(x,     y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x,     y + h);
    glEnd();
    glEnable(GL_TEXTURE_2D);
}

void hudPanel(float x, float y, float w, float h, Color4 fill, Color4 edge) {
    glBindTexture(GL_TEXTURE_2D, 0);
    glColor4f(fill.r, fill.g, fill.b, fill.a);
    solidQuad(x, y, w, h);

    if (edge.a > 0.001f) {
        float t = 1.0f;                 /* 描边厚度，像素 */
        glColor4f(edge.r, edge.g, edge.b, edge.a);
        solidQuad(x,         y,         w, t);
        solidQuad(x,         y + h - t, w, t);
        solidQuad(x,         y,         t, h);
        solidQuad(x + w - t, y,         t, h);
    }
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

void hudBar(float x, float y, float w, float h,
            Color4 back, Color4 fill, float t) {
    glBindTexture(GL_TEXTURE_2D, 0);
    glColor4f(back.r, back.g, back.b, back.a);
    solidQuad(x, y, w, h);

    t = clampf(t, 0.0f, 1.0f);
    if (t > 0.0f) {
        glColor4f(fill.r, fill.g, fill.b, fill.a);
        solidQuad(x, y, w * t, h);
    }
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}
