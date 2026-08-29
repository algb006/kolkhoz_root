#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Прогон v9: стартовая карта.

Вопрос прогона: сходится ли стартовая карта как чертёж — земельный баланс,
геометрия плеч, радиус стартовой пашни и цена расстояния при едином
хронометре (K=12).

Предыдущие прогоны считали труд, еду и транспорт при заданных плечах.
Здесь наоборот: плечи выводятся из самой карты и проверяется, влезает ли
стартовое хозяйство в рабочий радиус села.

Канон: manual/design/world/map/terrain.md §1, manual/design/core/start.md §1-§2,
§10-§11, manual/design/core/time.md §3, manual/design/infrastructure/transport.md §1,
core/manual/balance/49-simulations.md. Всё, что помечено ASSUMPTION, —
допущение прогона, а не решение дизайна.
"""

import math

# ─── Канон: карта ───
MAP_KM = 10.0                       # terrain.md §1
MAP_GA = MAP_KM * MAP_KM * 100      # 10 000 га
ZONES = {                           # terrain.md §1, доли площади
    "Центральная усадьба": 0.03,
    "Три деревни": 0.04,
    "Пашня": 0.45,
    "Луга и выпасы": 0.15,
    "Лес": 0.20,
    "Неудобья": 0.09,
    "Озеро": 0.02,
    "Речки и дороги": 0.02,
}
ZONES_GA_DECLARED = {"Пашня": 4500, "Лес": 2000, "Луга и выпасы": 1500, "Озеро": 200}

# ─── Канон: старт ───
NASELENIE = 80                      # start.md §1
DVORY = 21                          # CLAUDE.md §9
PASHNYA_START_GA = 160.0
LUGA_START_GA = 80.0                # 49-simulations §2
HORSES = 16                         # 49-simulations §2 (после v7)
SELO_OTSTUP_KM = (2.5, 3.0)         # terrain.md §1: отступ от края с райцентром
SELO_DO_DALNEGO_KM = (7.0, 7.5)     # terrain.md §1

# ─── Канон: время и скорости ───
K = 12                              # time.md §3: игровые часы в 12 раз быстрее
V_PESHKOM = 5.0                     # transport.md §1, км/ч реальные
V_LOSHAD_VERHOM = 12.0
V_TELEGA = 9.0                      # гружёная
CART_T = 0.75                       # transport.md §1: грузоподъёмность телеги
DAY_H = {"весна": 13.0, "лето": 16.5, "осень": 11.5, "зима": 7.5}   # игровые часы света

# ─── Цели роста ───
GA_NA_EDOKA = (2.0, 2.5)            # 49-simulations §2
CELI = {"Старт": 80, "Эпоха II": 500, "Эпоха III": 1500}   # CLAUDE.md §9
ZERNOVAYA_SPEC_GA = 2500            # terrain.md §1

# ─── Допущения ───
DVOR_GA = 0.25          # ASSUMPTION: дом с огородом ~25 соток (ЛПХ: 20 соток огорода)
ARABLE_SHARE = (0.35, 0.45, 0.55)   # ASSUMPTION: доля пашни в кольце вокруг села
POGRUZKA_H = 0.5        # ASSUMPTION (v7): погрузка+разгрузка за рейс, игровых часов
HORSE_WORK_H = 10.0     # ASSUMPTION (v7)
WORK_DAYS_YEAR = 41     # CLAUDE.md §9

line = lambda: print("─" * 78)
def h(t): print(f"\n{t}\n" + "─" * 78)

print("ПРОГОН v9: СТАРТОВАЯ КАРТА")
print(f"Карта {MAP_KM:.0f} × {MAP_KM:.0f} км = {MAP_GA:.0f} га, единый хронометр K={K}")

# ══════════════════════════════════════════ 1
h("1. Земельный баланс: сходятся ли доли и заявленные гектары")
total = 0.0
print(f"{'Зона':24}{'доля':>7}{'га':>9}{'заявлено':>10}{'сходится':>10}")
for name, share in ZONES.items():
    ga = share * MAP_GA
    total += share
    decl = ZONES_GA_DECLARED.get(name)
    mark = "—" if decl is None else ("да" if abs(decl - ga) < 1 else f"НЕТ ({decl})")
    print(f"{name:24}{share*100:6.0f}%{ga:9.0f}{('' if decl is None else f'{decl:.0f}'):>10}{mark:>10}")
print(f"{'ИТОГО':24}{total*100:6.0f}%{total*MAP_GA:9.0f}")
print("Сумма долей = 100%" if abs(total - 1.0) < 1e-9 else f"РАСХОЖДЕНИЕ: {total*100:.1f}%")

# ══════════════════════════════════════════ 2
h("2. Сколько карты занимает старт")
pashnya_ga = ZONES["Пашня"] * MAP_GA
luga_ga = ZONES["Луга и выпасы"] * MAP_GA
usadba_ga = ZONES["Центральная усадьба"] * MAP_GA
zastroyka = DVORY * DVOR_GA
print(f"Пашня в обороте:      {PASHNYA_START_GA:7.0f} га из {pashnya_ga:.0f} — {100*PASHNYA_START_GA/pashnya_ga:.1f}% пашни карты")
print(f"Луга в обороте:       {LUGA_START_GA:7.0f} га из {luga_ga:.0f} — {100*LUGA_START_GA/luga_ga:.1f}% лугов карты")
print(f"Застройка {DVORY} дворов:{zastroyka:7.1f} га из {usadba_ga:.0f} — {100*zastroyka/usadba_ga:.1f}% зоны усадьбы (ASSUMPTION {DVOR_GA} га/двор)")
print(f"Всё вместе:           {PASHNYA_START_GA+LUGA_START_GA+zastroyka:7.0f} га из {MAP_GA:.0f} — {100*(PASHNYA_START_GA+LUGA_START_GA+zastroyka)/MAP_GA:.1f}% карты")

# ══════════════════════════════════════════ 3
h("3. Запас на рост: хватает ли пашни до конца партии")
print(f"{'Веха':12}{'жителей':>9}{'га (2.0)':>10}{'га (2.5)':>10}{'% пашни (2.5)':>15}")
for name, n in CELI.items():
    lo, hi = n * GA_NA_EDOKA[0], n * GA_NA_EDOKA[1]
    print(f"{name:12}{n:9d}{lo:10.0f}{hi:10.0f}{100*hi/pashnya_ga:14.0f}%")
print(f"{'Зерновая спец.':12}{'—':>9}{ZERNOVAYA_SPEC_GA:10.0f}{ZERNOVAYA_SPEC_GA:10.0f}{100*ZERNOVAYA_SPEC_GA/pashnya_ga:14.0f}%")
worst = CELI["Эпоха III"] * GA_NA_EDOKA[1]
print(f"\nХудший случай (1500 жителей × 2.5 га): {worst:.0f} га = {100*worst/pashnya_ga:.0f}% пашни карты.")
print("Запас есть." if worst < pashnya_ga else "НЕ ВЛЕЗАЕТ.")

# ══════════════════════════════════════════ 4
h("4. Геометрия: где село и какие плечи оно задаёт")
otstup = sum(SELO_OTSTUP_KM) / 2          # 2.75 км, середина канонной вилки
selo = (MAP_KM / 2, otstup)               # ASSUMPTION: село на оси дороги к райцентру
krest = (MAP_KM / 2, MAP_KM / 2)          # terrain.md §1: перекрёсток в центре
dist = lambda a, b: math.hypot(a[0]-b[0], a[1]-b[1])
PLECHI = {
    "Край райцентра (сдача)": otstup,
    "Перекрёсток четырёх дорог": dist(selo, krest),
    "Граница соседа (через перекрёсток)": dist(selo, krest) + MAP_KM/2,
    "Дальний край карты": MAP_KM - otstup,
    "Дальний угол карты": dist(selo, (0.0, MAP_KM)),
    "Центр карты → угол": dist(krest, (0.0, 0.0)),
}
for name, km in PLECHI.items():
    print(f"{name:38}{km:6.2f} км")
print(f"\nКанон: отступ {SELO_OTSTUP_KM[0]}–{SELO_OTSTUP_KM[1]} км, до дальнего края {SELO_DO_DALNEGO_KM[0]}–{SELO_DO_DALNEGO_KM[1]} км.")
dal = MAP_KM - otstup
print(f"Считанное до дальнего края: {dal:.2f} км — {'сходится' if SELO_DO_DALNEGO_KM[0] <= dal <= SELO_DO_DALNEGO_KM[1] else 'РАСХОЖДЕНИЕ'}")

# ══════════════════════════════════════════ 5
h("5. Радиус стартовой пашни: как далеко лежат 160 га")
need_km2 = PASHNYA_START_GA / 100.0
print(f"160 га = {need_km2:.2f} км² чистой пашни. Земля вокруг села занята не только ею.")
print(f"{'доля пашни в кольце':22}{'кругом, км':>12}{'полукругом, км':>16}")
radii = {}
for s in ARABLE_SHARE:
    area = need_km2 / s
    r_circle = math.sqrt(area / math.pi)
    r_half = math.sqrt(2 * area / math.pi)
    radii[s] = (r_circle, r_half)
    print(f"{s*100:19.0f}%{r_circle:12.2f}{r_half:16.2f}")
r_ref = radii[0.45][1]      # ASSUMPTION: поля лежат в сторону райцентра — полукруг
print(f"\nРабочая оценка: поля в сторону райцентра (terrain.md §1) → полукруг при 45%:")
print(f"дальнее стартовое поле в {r_ref:.2f} км от села, среднее — около {r_ref*2/3:.2f} км.")

# ══════════════════════════════════════════ 6
h("6. Цена расстояния при K=12")
v_p, v_v, v_t = V_PESHKOM/K, V_LOSHAD_VERHOM/K, V_TELEGA/K
print(f"Эффективные скорости: пешком {v_p:.2f}, верхом {v_v:.2f}, телегой {v_t:.2f} км/иг.ч\n")
CASES = [
    ("Среднее стартовое поле", r_ref*2/3),
    ("Дальнее стартовое поле", r_ref),
    ("Сдача в райцентр", otstup),
    ("Дальний край карты", dal),
]
print(f"{'Плечо':26}{'км':>6}{'пешком туда-обратно':>21}{'% весеннего дня':>17}")
for name, km in CASES:
    t = 2*km/v_p
    print(f"{name:26}{km:6.2f}{t:19.1f} ч{100*t/DAY_H['весна']:16.0f}%")
print()
print(f"{'Плечо':26}{'км':>6}{'телегой туда-обратно':>22}{'верхом туда-обратно':>21}")
for name, km in CASES:
    print(f"{name:26}{km:6.2f}{2*km/v_t:20.1f} ч{2*km/v_v:19.1f} ч")

# ══════════════════════════════════════════ 7
h("7. Проверка канонных утверждений о размере карты")
print(f"«Час пешком от центра — 71% пути до угла»:")
print(f"   за 1 игровой час пешеход проходит {v_p:.2f} км — это {100*v_p/PLECHI['Центр карты → угол']:.0f}% пути до угла.")
print(f"   ВНИМАНИЕ: канон считал по реальному часу (5 км = {100*5/PLECHI['Центр карты → угол']:.0f}%), а не по игровому.")
per_day = {s: v_p*hh for s, hh in DAY_H.items()}
print(f"\n«Пешком до края — от рассвета до заката»: за световой день пешеход проходит")
print("   " + ", ".join(f"{s} {km:.1f} км" for s, km in per_day.items()))
print(f"   От села до дальнего края {dal:.2f} км — влезает только в летний день ({per_day['лето']:.1f} км).")
verhom_den = {s: v_v*hh for s, hh in DAY_H.items()}
print(f"\n«Дальний край — в полудне верхом»: верхом за полдня")
print("   " + ", ".join(f"{s} {km/2:.1f} км" for s, km in verhom_den.items()))
print(f"   До дальнего края {dal:.2f} км — {'сходится летом и весной' if verhom_den['весна']/2 >= dal else 'НЕ СХОДИТСЯ'}")

# ══════════════════════════════════════════ 8
h("8. Лошадиный бюджет на плечах стартовой карты")
FLOWS = [
    ("Урожай: поле → церковь", 68.0, r_ref*2/3, 8),      # 49-simulations §2, вал зерна
    ("Сдача: церковь → райцентр", 68.0*0.35, otstup, 12),
    ("Навоз: куча → поля", 486.0, r_ref*2/3, 24),
    ("Сено: луга → двор", 160.0, 1.2, 8),                # ASSUMPTION: луга ближе полей
    ("Дрова: лес → село", 100.0, 1.5, 16),               # ASSUMPTION: лес подходит к селу
]
resurs = HORSES * WORK_DAYS_YEAR * HORSE_WORK_H
print(f"Ресурс: {HORSES} лошадей × {WORK_DAYS_YEAR} раб.суток × {HORSE_WORK_H} ч = {resurs:.0f} лошаде-часов/год")
print(f"Телега {CART_T} т, эффективная скорость {v_t:.2f} км/иг.ч\n")
print(f"{'Поток':28}{'т':>6}{'км':>6}{'рейсов':>8}{'л-часов':>9}{'пик, лошадей':>14}")
itogo = 0.0
for name, tons, km, window in FLOWS:
    trips = tons / CART_T
    trip_h = 2*km/v_t + POGRUZKA_H
    hours = trips * trip_h
    itogo += hours
    print(f"{name:28}{tons:6.0f}{km:6.2f}{trips:8.0f}{hours:9.0f}{hours/(window*HORSE_WORK_H):14.1f}")
print(f"{'ИТОГО':28}{'':6}{'':6}{'':8}{itogo:9.0f}")
print(f"Доля годового ресурса лошадей: {100*itogo/resurs:.0f}%")
osen = FLOWS[0]; sdacha = FLOWS[1]
peak = ((osen[1]/CART_T)*(2*osen[2]/v_t+POGRUZKA_H)/(osen[3]*HORSE_WORK_H)
        + (sdacha[1]/CART_T)*(2*sdacha[2]/v_t+POGRUZKA_H)/(sdacha[3]*HORSE_WORK_H))
print(f"Осенний пик (уборка и сдача разом): {peak:.1f} лошадей из {HORSES}")

# ══════════════════════════════════════════ 9
h("9. Полный бюджет лошади: вспашка плюс логистика")
PLOUGH_HA = 120.0        # 49-simulations §2: активная пашня первого года
PLOUGHINGS = 1.6         # ASSUMPTION (v7): весенняя вспашка плюс частичная зябь
PLOUGH_HA_DAY = 0.9      # ASSUMPTION (v7): га за лошаде-день
plough_h = PLOUGH_HA * PLOUGHINGS / PLOUGH_HA_DAY * HORSE_WORK_H
print(f"Вспашка {PLOUGH_HA:.0f} га × {PLOUGHINGS} прохода: {plough_h:.0f} лошаде-часов")
print(f"Логистика по плечам карты:            {itogo:.0f} лошаде-часов")
print(f"Вместе:                               {plough_h+itogo:.0f} из {resurs:.0f} — {100*(plough_h+itogo)/resurs:.0f}% ресурса")
zapas = resurs - plough_h - itogo
print(f"Остаток: {zapas:.0f} лошаде-часов ({zapas/HORSE_WORK_H:.0f} лошаде-дней)")
if zapas < 0:
    print("НЕ СХОДИТСЯ: лошадей не хватает на собственные плечи карты.")
elif zapas / resurs < 0.15:
    print("ВПРИТЫК: запас меньше 15% — любая надбавка к плечу срывает год.")
else:
    print("Сходится с запасом.")

# ══════════════════════════════════════════ 10
h("10. Пеший подход к работе — чего он стоит за сезон")
POLEVYH_SUTOK = {"весна": 10, "лето": 11, "осень": 9}   # ASSUMPTION (v8): рабочих суток
RAB_NA_POLE = 25            # ASSUMPTION: человек в поле в пик
for season, sutok in POLEVYH_SUTOK.items():
    hod = 2 * (r_ref*2/3) / v_p
    dolya = hod / DAY_H[season]
    print(f"{season:8}{sutok:3d} суток × {hod:.1f} ч ходьбы = {sutok*hod:5.0f} ч на человека "
          f"({100*dolya:.0f}% дня), на {RAB_NA_POLE} человек — {sutok*hod*RAB_NA_POLE/DAY_H[season]:.0f} чел-суток")
poter = sum(s * (2*(r_ref*2/3)/v_p) * RAB_NA_POLE / DAY_H[se] for se, s in POLEVYH_SUTOK.items())
print()
print(f"За три сезона пеший подход съедает около {poter:.0f} чел-суток — столько же,")
print(f"сколько {poter/sum(POLEVYH_SUTOK.values()):.0f} работников, не выходящих на работу весь сезон.")
line()
