# -*- coding: utf-8 -*-
"""
根据 hospital_db 的真实结构，生成一张可直接放进论文的 ER 图（PNG）。

约定：
  · 实线 + 鸦爪 = 已建外键约束（7 个）
  · 虚线        = 仅靠「名称」关联，没有数据库约束
  · 鸦爪在「多」的一端（也就是持有外键的那张表）
  · PK(主键) / FK(外键) / UK(唯一) 用颜色角标区分
  · 每张表都有的 created_at / updated_at 省略（图例说明）
"""
from PIL import Image, ImageDraw, ImageFont
import os

SS = 2
W, H = 2140, 1740
BW, HDR, ROW, PAD = 430, 52, 28, 16

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "数据库ER图.png")
FONT = next(p for p in [r"C:\Windows\Fonts\msyh.ttc", r"C:\Windows\Fonts\msyh.ttc",
                        r"C:\Windows\Fonts\simhei.ttf", r"C:\Windows\Fonts\simsun.ttc"]
            if os.path.exists(p))

_c = {}
def F(s):
    if s not in _c: _c[s] = ImageFont.truetype(FONT, s * SS)
    return _c[s]

# ------------------------------------------------------------------ 表
TABLES = [
    ("departments", "科室", "#2563eb", [
        ("id", "INT", "PK"), ("name", "VARCHAR(50)", "UK"), ("director", "VARCHAR(50)", ""),
        ("doctor_count", "INT", ""), ("nurse_count", "INT", ""), ("bed_count", "INT", "")]),
    ("doctors", "医生", "#0ea5e9", [
        ("id", "INT", "PK"), ("name", "VARCHAR(50)", ""), ("dept", "VARCHAR(50)", ""),
        ("title", "VARCHAR(50)", ""), ("phone", "VARCHAR(20)", "")]),
    ("schedules", "号源排班", "#d97706", [
        ("id", "INT", "PK"), ("doctor_id", "INT", "FK"), ("doctor", "VARCHAR(50)", ""),
        ("dept", "VARCHAR(50)", ""), ("work_date", "DATE", ""), ("period", "VARCHAR(10)", ""),
        ("start_time", "VARCHAR(10)", ""), ("end_time", "VARCHAR(10)", ""), ("room", "VARCHAR(40)", ""),
        ("source_type", "VARCHAR(20)", ""), ("price", "DECIMAL(10,2)", ""), ("total_quota", "INT", ""),
        ("booked", "INT", ""), ("status", "VARCHAR(10)", "")]),
    ("drugs", "药品库存", "#65a30d", [
        ("id", "INT", "PK"), ("code", "VARCHAR(20)", "UK"), ("name", "VARCHAR(100)", ""),
        ("spec", "VARCHAR(100)", ""), ("unit", "VARCHAR(20)", ""), ("stock", "INT", ""),
        ("price", "DECIMAL(10,2)", ""), ("warn", "VARCHAR(20)", ""), ("warn_stock", "INT", "")]),
    ("users", "用户账号", "#6366f1", [
        ("id", "INT", "PK"), ("username", "VARCHAR(50)", "UK"), ("password", "VARCHAR(64)", ""),
        ("role", "VARCHAR(20)", ""), ("name", "VARCHAR(50)", ""), ("phone", "VARCHAR(20)", ""),
        ("id_card", "VARCHAR(30)", ""), ("dept", "VARCHAR(50)", ""), ("patient_id", "INT", "FK")]),
    ("patients", "患者档案", "#059669", [
        ("id", "INT", "PK"), ("name", "VARCHAR(50)", ""), ("gender", "VARCHAR(10)", ""),
        ("age", "INT", ""), ("id_card", "VARCHAR(30)", "UK"), ("phone", "VARCHAR(20)", ""),
        ("dept", "VARCHAR(50)", ""), ("doctor", "VARCHAR(50)", ""), ("diagnosis", "VARCHAR(200)", ""),
        ("status", "VARCHAR(20)", "")]),
    ("appointments", "挂号记录", "#dc2626", [
        ("id", "INT", "PK"), ("patient_id", "INT", "FK"), ("patient_name", "VARCHAR(50)", ""),
        ("dept", "VARCHAR(50)", ""), ("doctor", "VARCHAR(50)", ""), ("source_type", "VARCHAR(20)", ""),
        ("appt_time", "VARCHAR(30)", ""), ("status", "VARCHAR(20)", ""), ("schedule_id", "INT", "FK"),
        ("room", "VARCHAR(40)", ""), ("price", "DECIMAL(10,2)", ""), ("queue_no", "INT", ""),
        ("phone", "VARCHAR(20)", "")]),
    ("medical_records", "门诊病历", "#7c3aed", [
        ("id", "INT", "PK"), ("patient_id", "INT", "FK"), ("patient_name", "VARCHAR(50)", ""),
        ("doctor", "VARCHAR(50)", ""), ("dept", "VARCHAR(50)", ""), ("diagnosis", "VARCHAR(200)", ""),
        ("treatment", "VARCHAR(500)", ""), ("record_date", "VARCHAR(20)", "")]),
    ("inpatient", "住院床位", "#db2777", [
        ("id", "INT", "PK"), ("bed_no", "VARCHAR(20)", "UK"), ("dept", "VARCHAR(50)", ""),
        ("floor_name", "VARCHAR(20)", ""), ("patient_id", "INT", "FK"), ("patient_name", "VARCHAR(50)", ""),
        ("doctor", "VARCHAR(50)", ""), ("admit_date", "VARCHAR(20)", ""), ("diagnosis", "VARCHAR(200)", ""),
        ("status", "VARCHAR(20)", "")]),
    ("fees", "费用结算", "#0891b2", [
        ("id", "INT", "PK"), ("patient_id", "INT", "FK"), ("patient_name", "VARCHAR(50)", ""),
        ("items", "VARCHAR(200)", ""), ("amount", "DECIMAL(10,2)", ""), ("pay_method", "VARCHAR(20)", ""),
        ("status", "VARCHAR(20)", ""), ("fee_time", "VARCHAR(30)", "")]),
]

POS = {
    "departments": (70, 150), "doctors": (560, 150), "schedules": (1050, 150), "drugs": (1620, 150),
    "users": (70, 660), "patients": (560, 660), "appointments": (1050, 660),
    "medical_records": (70, 1230), "inpatient": (560, 1230), "fees": (1050, 1230),
}
TH = {t[0]: HDR + len(t[3]) * ROW + PAD for t in TABLES}

def RX(t): return POS[t][0] + BW
def LX(t): return POS[t][0]
def TY(t): return POS[t][1]
def BY(t): return POS[t][1] + TH[t]
def CX(t): return POS[t][0] + BW // 2

img = Image.new("RGB", (W * SS, H * SS), "#ffffff")
d = ImageDraw.Draw(img)

def rr(x, y, w, h, r, fill=None, outline=None, width=1):
    d.rounded_rectangle([x*SS, y*SS, (x+w)*SS, (y+h)*SS], radius=r*SS, fill=fill, outline=outline, width=width*SS)

def tx(x, y, s, size, color="#0f172a", anchor="la"):
    d.text((x*SS, y*SS), s, font=F(size), fill=color, anchor=anchor)

def label(x, y, s, size=15, color="#334155", anchor="mm"):
    """带白底的标签，压到表格上也看得清"""
    bb = d.textbbox((x*SS, y*SS), s, font=F(size), anchor=anchor)
    d.rounded_rectangle([bb[0]-6*SS, bb[1]-3*SS, bb[2]+6*SS, bb[3]+3*SS], radius=4*SS, fill="#ffffff")
    d.text((x*SS, y*SS), s, font=F(size), fill=color, anchor=anchor)

# ---------------- 标题
tx(W//2, 34, "医院挂号管理系统 数据库 E-R 图", 38, "#0f172a", "ma")
tx(W//2, 88, "MySQL 8.0  ·  hospital_db  ·  10 个实体  ·  7 个外键约束  ·  InnoDB / utf8mb4", 20, "#64748b", "ma")

# ---------------- 实体表
KC = {"PK": "#f59e0b", "FK": "#3b82f6", "UK": "#10b981"}
KT = {"PK": "#7c2d12", "FK": "#1e3a8a", "UK": "#064e3b"}
for name, cn, color, fields in TABLES:
    x, y, h = POS[name][0], POS[name][1], TH[name]
    rr(x+5, y+5, BW, h, 10, fill="#e2e8f0")
    rr(x, y, BW, h, 10, fill="#ffffff", outline="#cbd5e1", width=2)
    d.rounded_rectangle([x*SS, y*SS, (x+BW)*SS, (y+HDR)*SS], radius=10*SS, fill=color)
    d.rectangle([x*SS, (y+HDR-12)*SS, (x+BW)*SS, (y+HDR)*SS], fill=color)
    tx(x+18, y+14, name, 21, "#ffffff")
    tx(x+BW-18, y+16, cn, 19, "#ffffff", "ra")
    d.line([x*SS, (y+HDR)*SS, (x+BW)*SS, (y+HDR)*SS], fill="#94a3b8", width=2*SS)
    for i, (fn, ft, k) in enumerate(fields):
        ry = y + HDR + i*ROW
        if i % 2:
            d.rectangle([(x+2)*SS, ry*SS, (x+BW-2)*SS, (ry+ROW)*SS], fill="#f8fafc")
        px = x + 18
        if k:
            d.rounded_rectangle([px*SS, (ry+5)*SS, (px+34)*SS, (ry+24)*SS], radius=4*SS, fill=KC[k])
            tx(px+17, ry+7, k, 13, KT[k], "ma")
            px += 46
        tx(px, ry+7, fn, 17, "#0f172a")
        tx(x+BW-18, ry+8, ft, 15, "#94a3b8", "ra")

# ---------------- 连线（手排折线：起点 -> 拐点 -> 终点）
def vert(x, y1, y2): return [(x, y1), (x, y2)]

S = []
S.append(dict(a="departments", b="doctors", fk=False,
              pts=[(RX("departments"), 260), (LX("doctors"), 260)], lab=("科室名", 530, 288)))
S.append(dict(a="doctors", b="schedules", fk=True,
              pts=[(RX("doctors"), 250), (LX("schedules"), 250)], lab=("doctor_id", 1020, 276)))
S.append(dict(a="departments", b="schedules", fk=False,
              pts=[(CX("departments"), BY("departments")), (CX("departments"), 630),
                   (LX("schedules")-40, 630), (LX("schedules")-40, 390), (LX("schedules"), 390)],
              lab=("科室名", 700, 606)))
S.append(dict(a="schedules", b="appointments", fk=True,
              pts=[(CX("schedules"), BY("schedules")), (CX("appointments"), TY("appointments"))],
              lab=("schedule_id", 1285, 636), labanchor="lm"))
S.append(dict(a="patients", b="appointments", fk=True,
              pts=[(RX("patients"), 830), (LX("appointments"), 830)], lab=("patient_id", 1020, 862)))
S.append(dict(a="patients", b="users", fk=True,
              pts=[(LX("patients"), 800), (RX("users"), 800)], lab=("patient_id", 530, 832)))
S.append(dict(a="patients", b="medical_records", fk=True,
              pts=[(LX("patients"), 950), (LX("patients")-40, 950), (LX("patients")-40, 1150),
                   (CX("medical_records"), 1150), (CX("medical_records"), TY("medical_records"))],
              lab=("patient_id", 548, 1130), labanchor="rm"))
S.append(dict(a="patients", b="inpatient", fk=True,
              pts=[(CX("patients"), BY("patients")), (CX("inpatient"), TY("inpatient"))],
              lab=("patient_id", 793, 1110), labanchor="lm"))
S.append(dict(a="patients", b="fees", fk=True,
              pts=[(RX("patients"), 940), (RX("patients")+40, 940), (RX("patients")+40, 1160),
                   (CX("fees"), 1160), (CX("fees"), TY("fees"))],
              lab=("patient_id", 1120, 1140)))

def crow(pt, dirv, many, color):
    x, y = pt; dx, dy = dirv; L = 15
    if dx:  # 水平
        s = 1 if dx > 0 else -1
        if many:
            for off in (-12, 0, 12):
                d.line([(x+s*L)*SS, y*SS, x*SS, (y+off)*SS], fill=color, width=2*SS)
        else:
            d.line([(x+s*L)*SS, (y-13)*SS, (x+s*L)*SS, (y+13)*SS], fill=color, width=3*SS)
    else:
        s = 1 if dy > 0 else -1
        if many:
            for off in (-12, 0, 12):
                d.line([x*SS, (y+s*L)*SS, (x+off)*SS, y*SS], fill=color, width=2*SS)
        else:
            d.line([(x-13)*SS, (y+s*L)*SS, (x+13)*SS, (y+s*L)*SS], fill=color, width=3*SS)

def dashed(pts, color, width=2, dash=11, gap=8):
    for i in range(len(pts)-1):
        (x1, y1), (x2, y2) = pts[i], pts[i+1]
        dist = ((x2-x1)**2 + (y2-y1)**2) ** .5
        if not dist: continue
        ux, uy = (x2-x1)/dist, (y2-y1)/dist
        p = 0
        while p < dist:
            e = min(p+dash, dist)
            d.line([(x1+ux*p)*SS, (y1+uy*p)*SS, (x1+ux*e)*SS, (y1+uy*e)*SS], fill=color, width=width*SS)
            p = e + gap

for s in S:
    pts = s["pts"]; color = "#1e293b" if s["fk"] else "#94a3b8"
    if s["fk"]:
        d.line([c*SS for p in pts for c in p], fill=color, width=3*SS, joint="curve")
    else:
        dashed(pts, color)
    # 端点方向
    (x0, y0), (x1, y1) = pts[0], pts[1]
    d0 = (-1, 0) if x1 < x0 else ((1, 0) if x1 > x0 else (0, -1 if y1 < y0 else 1))
    (xa, ya), (xb, yb) = pts[-2], pts[-1]
    d1 = (-1, 0) if xb < xa else ((1, 0) if xb > xa else (0, -1 if yb < ya else 1))
    crow(pts[0], d0, False, color)
    crow(pts[-1], d1, True, color)
    # 1 / N 标注
    import math
    def card(pt, nxt, text, along):
        ux, uy = nxt[0]-pt[0], nxt[1]-pt[1]
        n = math.hypot(ux, uy) or 1
        ux, uy = ux/n, uy/n
        px_, py_ = -uy, ux
        if py_ > 0: px_, py_ = -px_, -py_          # 垂直偏移统一朝上
        seg = math.hypot(nxt[0]-pt[0], nxt[1]-pt[1])
        if seg < 130:                              # 短线：只做垂直偏移，避免 1/N 挤在一起
            cx, cy = pt[0] + px_*19, pt[1] + py_*19
        else:
            cx = pt[0] + ux*along + px_*20
            cy = pt[1] + uy*along + py_*20
        label(cx, cy, text, 17, "#0f172a", "mm")
    card(pts[0], pts[1], "1", 26)
    card(pts[-1], pts[-2], "N", 26)
    if s.get("lab"):
        t, lx, ly = s["lab"]
        label(lx, ly, t, 15, "#475569", s.get("labanchor", "mm"))

# ---------------- 图例
LXg, LYg, LWg, LHh = 1620, 660, 430, 470
rr(LXg, LYg, LWg, LHh, 10, fill="#f8fafc", outline="#cbd5e1", width=2)
tx(LXg+20, LYg+16, "图例", 23, "#0f172a")
yy = LYg + 60
for k, desc in [("PK", "主键 Primary Key"), ("FK", "外键 Foreign Key"), ("UK", "唯一约束 Unique Key")]:
    d.rounded_rectangle([(LXg+20)*SS, (yy+2)*SS, (LXg+54)*SS, (yy+21)*SS], radius=4*SS, fill=KC[k])
    tx(LXg+37, yy+4, k, 13, KT[k], "ma")
    tx(LXg+66, yy+3, desc, 17, "#334155")
    yy += 30
yy += 10
d.line([(LXg+20)*SS, yy*SS, (LXg+80)*SS, yy*SS], fill="#1e293b", width=3*SS)
tx(LXg+92, yy-11, "实线 = 已建外键约束", 17, "#334155")
yy += 30
dashed([(LXg+20, yy), (LXg+80, yy)], "#94a3b8")
tx(LXg+92, yy-11, "虚线 = 仅按名称关联", 17, "#334155")
yy += 30
dashed([(LXg+20, yy), (LXg+80, yy)], "#94a3b8")
tx(LXg+92, yy-11, "（数据库无该约束）", 17, "#334155")
yy += 34
tx(LXg+20, yy, "1 : N    一对多关系", 17, "#334155")
yy += 28
tx(LXg+20, yy, "鸦爪在「多」端，即持有外键", 16, "#64748b")
yy += 24
tx(LXg+20, yy, "的那张表；外键可空 = 0..N", 16, "#64748b")
yy += 32
tx(LXg+20, yy, "所有表均含 created_at /", 16, "#64748b")
yy += 24
tx(LXg+20, yy, "updated_at 审计字段，图中省略", 16, "#64748b")

# ---------------- 页脚
tx(70, H-118, "说明 1：科室与医生之间按「名称」关联（doctors.dept → departments.name），号源排班 schedules 也冗余存了科室名，", 18, "#475569")
tx(70, H-92, "　　　　因此没有外键约束；改科室名时要同步更新 doctors / schedules / appointments 三张表。", 18, "#475569")
tx(70, H-60, "说明 2：appointments、medical_records、inpatient、fees 中另冗余保存 patient_name / dept / doctor 名称快照；", 18, "#475569")
tx(70, H-34, "　　　　姓名同时是业务标识，改医生或患者姓名时后端会同步所有引用该姓名的表。drugs（药品库存）目前与其它表无关联。", 18, "#475569")

img.resize((W, H), Image.LANCZOS).save(OUT, "PNG")
print("OK ->", OUT)

# ============================================================================
#   顺带导出两个配套文件（和图同一份数据源，保证不会不一致）
#      · 数据库ER图.mmd     —— Mermaid ER 源码，可粘到 draw.io / Typora / 在线渲染器
#      · 数据库关系说明.md   —— 可直接写进论文「数据库设计」章节
# ============================================================================
MDIR = os.path.dirname(os.path.abspath(__file__))
ROWS = {"patients": 5, "doctors": 5, "departments": 5, "schedules": 170, "appointments": 6,
        "users": 6, "medical_records": 4, "inpatient": 7, "drugs": 6, "fees": 4}
PURPOSE = {
    "departments": "科室基础信息（名称、主任、床位数、医护人数统计）",
    "doctors": "医生档案，用 dept 字段（科室名）归属科室",
    "schedules": "号源排班：某医生某天某午别放多少号、已约多少号",
    "users": "登录账号，三种角色 admin / doctor / patient，患者账号绑定患者档案",
    "patients": "患者档案，全院唯一身份（身份证唯一）",
    "appointments": "挂号记录（核心业务表），一次挂号一行，带排队号、诊室、费用快照",
    "medical_records": "门诊病历：诊断与处置",
    "inpatient": "住院床位：一行 = 一张床，占用时记录患者，空床时患者为空",
    "drugs": "药品库存：数量、单价、库存预警阈值",
    "fees": "费用结算单：项目、金额、支付方式、结算状态",
}
FK_RELS = [
    ("appointments", "patient_id", "patients", "id", "SET NULL", "挂号记录属于哪位患者"),
    ("appointments", "schedule_id", "schedules", "id", "SET NULL", "这次挂号约的是哪个号源"),
    ("schedules", "doctor_id", "doctors", "id", "RESTRICT", "号源属于哪位医生"),
    ("users", "patient_id", "patients", "id", "SET NULL", "患者账号绑定的患者档案"),
    ("medical_records", "patient_id", "patients", "id", "SET NULL", "病历属于哪位患者"),
    ("inpatient", "patient_id", "patients", "id", "SET NULL", "该床位当前占用的患者（空床为 NULL）"),
    ("fees", "patient_id", "patients", "id", "SET NULL", "费用单属于哪位患者"),
]
NAME_RELS = [
    ("doctors", "dept", "departments", "name", "医生归属科室"),
    ("schedules", "dept", "departments", "name", "号源的科室名（冗余，便于按科室筛号）"),
    ("schedules", "doctor", "doctors", "name", "号源的医生姓名（冗余快照）"),
    ("appointments", "dept / doctor", "departments / doctors", "name", "挂号时的科室、医生名（快照）"),
    ("appointments", "patient_name", "patients", "name", "挂号时的患者姓名（快照）"),
    ("medical_records", "doctor / dept / patient_name", "doctors / departments / patients", "name", "病历中的名称快照"),
    ("inpatient", "doctor / dept / patient_name", "doctors / departments / patients", "name", "住院记录中的名称快照"),
    ("fees", "patient_name", "patients", "name", "费用单中的患者姓名快照"),
]

m = ["erDiagram"]
for t, cn, color, fields in TABLES:
    m.append("    " + t + " {")
    for fn, ft, k in fields:
        typ = ft.split("(")[0].lower()
        m.append("        " + typ + " " + fn + (" " + k if k else ""))
    m.append("    }")
m.append("")
for fa, fc, pa, pc, rule, desc in FK_RELS:
    m.append('    ' + pa + " ||--o{ " + fa + ' : "' + fc + '"')
for fa, fc, pa, pc, desc in NAME_RELS:
    if "/" in fc or "/" in pa:
        continue
    m.append('    ' + pa + " ||--o{ " + fa + ' : "名称关联 ' + fc + '"')
open(os.path.join(MDIR, "数据库ER图.mmd"), "w", encoding="utf-8").write("\n".join(m) + "\n")

md = []
md.append("# 医院挂号管理系统 · 数据库设计说明\n")
md.append("> 对应数据库：`hospital_db`（MySQL 8.0 / InnoDB / utf8mb4），共 **10 张表**、**7 个外键**。")
md.append("> 配套图：`数据库ER图.png`；可编辑源码：`数据库ER图.mmd`\n")
md.append("## 一、实体清单\n")
md.append("| # | 表名 | 中文名 | 说明 | 记录数 |")
md.append("|---|---|---|---|---|")
for i, (t, cn, color, fields) in enumerate(TABLES, 1):
    md.append("| " + str(i) + " | `" + t + "` | " + cn + " | " + PURPOSE[t] + " | " + str(ROWS[t]) + " |")
md.append("\n## 二、关系清单\n")
md.append("### 2.1 已建外键约束（实线，7 个）\n")
md.append("| # | 从表.字段 | 到表.字段 | 基数 | 删除策略 | 业务含义 |")
md.append("|---|---|---|---|---|---|")
for i, (a, ac, b, bc, rule, desc) in enumerate(FK_RELS, 1):
    md.append("| " + str(i) + " | `" + a + "." + ac + "` | `" + b + "." + bc + "` | N : 1 | ON DELETE " + rule + " | " + desc + " |")
md.append("\n> `SET NULL`：患者（或号源）被删除时把外键置空、**保留历史单据**；")
md.append("> `RESTRICT`：医生名下还有号源时不许直接删除，必须先删号源。\n")
md.append("### 2.2 按名称关联（虚线，无数据库约束）\n")
md.append("| # | 从表.字段 | 到表.字段 | 业务含义 |")
md.append("|---|---|---|---|")
for i, (a, ac, b, bc, desc) in enumerate(NAME_RELS, 1):
    md.append("| " + str(i) + " | `" + a + "." + ac + "` | `" + b + "." + bc + "` | " + desc + " |")
md.append("""

## 三、设计说明

### 3.1 为什么外键都用 SET NULL 而不是 CASCADE
挂号记录、病历、费用单都是**历史凭证**。患者被删除时若级联删除，历史统计（挂号量、收入）会凭空消失，
所以选择「外键置空、单据保留」，统计时用名称快照兜底。

### 3.2 名称冗余与一致性保证
`appointments`、`medical_records`、`inpatient`、`fees` 除了存 `patient_id`，还冗余存了 `patient_name`、`dept`、`doctor`。原因：
1. 单据能显示当时的姓名；
2. 列表查询不用多表 JOIN，性能好。

但姓名在本系统里**同时承担业务标识的角色**：医生登录账号 `users.name` 存的就是医生姓名，
医生端「我的挂号」正是按姓名筛选（`WHERE doctor = ?`）。因此改姓名不能只改一张表，
后端在修改医生 / 患者姓名时，会把所有引用该姓名的表一并同步：

| 改谁 | 同步范围 |
|---|---|
| 医生 | `doctors` → `schedules.doctor`、`appointments.doctor`、`patients.doctor`、`medical_records.doctor`、`inpatient.doctor`、`users.name`（医生登录账号） |
| 患者 | `patients` → `users.name`（患者登录账号）、`appointments.patient_name`、`fees.patient_name`、`medical_records.patient_name`、`inpatient.patient_name` |

同步优先用 `patient_id` / `doctor_id` 精确定位；只有桌面端早期数据（无 id 关联）才按「旧姓名」兜底，
并要求旧姓名在该表里唯一，避免误改同名的人。

两个注意点：
- 改名后**该账号需要重新登录**（JWT 令牌里存的是登录时的姓名）；
- 用姓名当业务标识是这套设计的弱点，规范做法是给 `appointments` 等表加 `doctor_id`，用 id 关联、姓名只用于展示。

### 3.3 科室为什么不建外键
`departments.name` 是**业务主键**，`doctors.dept`、`schedules.dept`、`appointments.dept` 存的是科室**名称**而非 id。
好处是查询展示直接，代价是改科室名要同步三张表（后端已实现该同步）。若要更规范，可改成 `dept_id` 外键。

### 3.4 已知的可改进点
| 问题 | 现状 | 建议 |
|---|---|---|
| 日期时间用 varchar 存 | `appt_time`、`record_date`、`admit_date`、`fee_time` 都是字符串 | 改为 `DATETIME` / `DATE`（需同步改桌面端显示代码） |
| `patients.age` 存年龄 | 年龄每年会变，长期会失真 | 增加 `birth_date`，年龄由出生日期计算 |
| 药品与就诊无关联 | `drugs` 是孤立表，没有处方表 | 增加 `prescriptions` + `prescription_items` |
| 缺操作日志 | 挂号/取消/接诊的留痕只在服务内存里 | 增加 `audit_logs` 表 |
| 床位与住院记录混在一张表 | `inpatient` 一行既是床位又是住院记录，空床用 NULL 患者表示 | 拆成 `beds` + `admissions` |

## 四、如何重新生成图

```bash
python make_er.py     # 重新生成 数据库ER图.png、数据库ER图.mmd、数据库关系说明.md
```
""")
open(os.path.join(MDIR, "数据库关系说明.md"), "w", encoding="utf-8").write("\n".join(md) + "\n")
print("OK -> 数据库ER图.mmd / 数据库关系说明.md")