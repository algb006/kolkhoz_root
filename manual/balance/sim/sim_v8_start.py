#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Прогон v8: первый год после пересмотра стартовых условий.

Что изменилось против v7 (`core/manual/balance/sim/sim_v7_transport.py`):
колхозного двора на старте больше нет. Лошади, повозки и конный инвентарь
стоят по личным дворам, пока двор не построен: тягло работает, но каждый
выезд начинается в двух адресах, часть времени лошади уходит хозяину,
а возчика и маршрутной развозки нет вовсе.

Вопрос прогона: проходит ли первая весна и во что обходится каждый месяц
без двора.

Канон: manual/design/core/start.md §9-§11, manual/design/economy/farming.md §4,
core/manual/balance/49-simulations.md. Всё, что помечено ASSUMPTION, —
допущение этого прогона, а не решение дизайна.
"""

# ─── Канон ───
NASELENIE = 80                # start.md §1
PASHNYA_GA = 160.0            # start.md §1, засевается в первый год меньше
POSEV_GOD1_GA = 70.0          # sim_v6.py, posev_plan[0]
LUGA_GA = 80.0                # 49-simulations §2
HORSES = 16                   # 49-simulations §2 (после v7)
KOROVY = 39                   # 49-simulations §2
WORK_DAYS_YEAR = 41           # CLAUDE.md §9
KOEF_IGRA = 7.0               # реальные чел-дни ÷ 7 = игровые сутки
OKNO_MESYACA = 4              # farming.md §4: месяц = 4 суток
TRUD_GA = {"зерно": 24, "картофель": 50, "лён": 75, "кормовые": 12, "сенокос": 8}
TRUD_SKOT = {"корова": 32, "лошадь": 22, "овца": 4, "свинья": 8}
DAY_HOURS = {"весна": 13.0, "лето": 16.5, "осень": 11.5, "зима": 7.5}

# ─── Допущения прогона ───
RAB_SUTOK = {"весна": 10, "лето": 11, "осень": 9, "зима": 11}   # ASSUMPTION: 41 за год
DOLYA_POLYA = {"весна": 0.40, "лето": 0.15, "осень": 0.45}      # ASSUMPTION: вспашка+сев / уход / уборка
HORSE_WORK_H = 10.0           # ASSUMPTION (v7)
PLOUGH_HA_DAY = 0.9 * 7       # га за лошаде-игровые сутки (норма ÷7)
YARD_TRUDODNI = 120.0         # ASSUMPTION: трудоёмкость колхозного двора, игровых трудодней
YARD_UTEPLENIE = 25.0         # ASSUMPTION: утепление до зимы
DVA_ADRESA_H = 0.8            # ASSUMPTION: лишний конец за выезд, игровых часов
UTECHKA = 0.12                # ASSUMPTION: доля времени лошади, уходящая хозяину
BEZ_VOZCHIKA = 0.15           # ASSUMPTION: надбавка к возке без маршрутов и возчика
PEREGNOY_CHEL_DNEY = 500.0    # sim_v6.py: вывоз наследной компостной кучи, реальных чел-дней


def rabotniki(n):
    """Приведённых работников от населения (sim_v6.py)."""
    return (n * 0.492 + n * 0.136 * 0.45 + n * 0.220 * 0.30) * 0.92


RAB = rabotniki(NASELENIE)
RESURS_GOD = RAB * WORK_DAYS_YEAR * 0.88 * 0.90        # игровых чел-суток за год
RESURS = {s: RESURS_GOD * d / WORK_DAYS_YEAR for s, d in RAB_SUTOK.items()}

# ─── Труд первого года, реальные чел-дни → игровые сутки ───
ga = {"зерно": POSEV_GOD1_GA * 0.62, "картофель": POSEV_GOD1_GA * 0.18,
      "лён": POSEV_GOD1_GA * 0.08, "кормовые": POSEV_GOD1_GA * 0.12}
pole_real = sum(ga[k] * TRUD_GA[k] for k in ga)
senokos_real = LUGA_GA * TRUD_GA["сенокос"]
skot_real = KOROVY * TRUD_SKOT["корова"] + HORSES * TRUD_SKOT["лошадь"]
pole = pole_real / KOEF_IGRA
senokos = senokos_real / KOEF_IGRA
skot = skot_real / KOEF_IGRA
peregnoy = PEREGNOY_CHEL_DNEY / KOEF_IGRA

print("═" * 78)
print("ПРОГОН v8 — первая весна без колхозного двора")
print("═" * 78)
print(f"Жителей {NASELENIE}, приведённых работников {RAB:.0f}, "
      f"ресурс года {RESURS_GOD:.0f} игровых чел-суток")
print(f"Посев первого года {POSEV_GOD1_GA:.0f} га, лугов {LUGA_GA:.0f} га, "
      f"лошадей {HORSES}, коров {KOROVY}")
print(f"Полевые работы за год: {pole:.0f} чел-суток, сенокос {senokos:.0f}, "
      f"скот {skot:.0f}, перегной {peregnoy:.0f}\n")

# ─── Весна: люди ───
print("─" * 78)
print("ВЕСНА: чем заняты руки (игровых чел-суток)")
print("─" * 78)
vesna_pole = pole * DOLYA_POLYA["весна"]
vesna_skot = skot * RAB_SUTOK["весна"] / WORK_DAYS_YEAR
vesna_prochee = (NASELENIE * 3.5 / KOEF_IGRA) * RAB_SUTOK["весна"] / WORK_DAYS_YEAR

for name, val in [("Вспашка, боронование, сев", vesna_pole),
                  ("Уход за скотом", vesna_skot),
                  ("Прочее (дрова, быт, ремонт)", vesna_prochee)]:
    print(f"  {name:34}{val:8.0f}")
baza = vesna_pole + vesna_skot + vesna_prochee
svobodno = RESURS["весна"] - baza
print(f"  {'ИТОГО обязательного':34}{baza:8.0f}   из {RESURS['весна']:.0f} "
      f"({100 * baza / RESURS['весна']:.0f}%)")
print(f"  {'Свободный остаток':34}{svobodno:8.0f}\n")

print(f"  Колхозный двор стоит {YARD_TRUDODNI:.0f} трудодней "
      f"({100 * YARD_TRUDODNI / RESURS['весна']:.0f}% весеннего ресурса)")
if svobodno >= YARD_TRUDODNI:
    print(f"  ✔ Двор ставится той же весной, остаётся {svobodno - YARD_TRUDODNI:.0f} чел-суток")
else:
    doля = YARD_TRUDODNI - svobodno
    print(f"  ⚠ Весной двор целиком не встаёт: не хватает {doля:.0f} чел-суток — "
          f"хвост уходит в лето")
print(f"  Перегной ({peregnoy:.0f} чел-суток) в весну не помещается ни в одном "
      f"сценарии — это и есть развилка «возить или пахать»\n")

# ─── Весна: лошади ───
print("─" * 78)
print("ВЕСНА: тягло (игровых лошаде-часов)")
print("─" * 78)
resurs_koney = HORSES * RAB_SUTOK["весна"] * HORSE_WORK_H
vspashka_dney = POSEV_GOD1_GA / PLOUGH_HA_DAY          # лошаде-игровых суток
boronovanie = vspashka_dney * 0.5
sev = vspashka_dney * 0.4
koni_dney = vspashka_dney + boronovanie + sev
koni_h = koni_dney * HORSE_WORK_H

shtraf_adres = koni_dney * DVA_ADRESA_H                 # лишний конец за каждый выезд
shtraf_utechka = resurs_koney * UTECHKA
podvoz_h = (peregnoy * 0.0 + 40.0)                      # ASSUMPTION: подвоз семян и материалов весной
shtraf_vozka = podvoz_h * BEZ_VOZCHIKA

print(f"  Ресурс: {HORSES} лошадей x {RAB_SUTOK['весна']} суток x {HORSE_WORK_H:.0f} ч "
      f"= {resurs_koney:.0f} ч")
print(f"  Вспашка {POSEV_GOD1_GA:.0f} га: {vspashka_dney:.1f} лошаде-суток; "
      f"с боронованием и севом {koni_dney:.1f} суток = {koni_h:.0f} ч")
print(f"  Подвоз семян и материалов: {podvoz_h:.0f} ч")
print(f"  Надбавка «два адреса»: {shtraf_adres:.0f} ч   "
      f"Утечка хозяину: {shtraf_utechka:.0f} ч   Возка без возчика: {shtraf_vozka:.0f} ч")
zanyato_A = koni_h + podvoz_h
zanyato_B = zanyato_A + shtraf_adres + shtraf_utechka + shtraf_vozka
print(f"  Сценарий А (двор есть):  {zanyato_A:.0f} ч — {100 * zanyato_A / resurs_koney:.0f}% ресурса")
print(f"  Сценарий Б (двор строим): {zanyato_B:.0f} ч — {100 * zanyato_B / resurs_koney:.0f}% ресурса")
print(f"  Цена отсутствия двора весной: {zanyato_B - zanyato_A:.0f} лошаде-часов "
      f"({100 * (zanyato_B - zanyato_A) / zanyato_A:.0f}% сверху)\n")

# ─── Окно сева ───
print("─" * 78)
print("ОКНО СЕВА: влезает ли посевная в апрель и май")
print("─" * 78)
for mesyac, dolya in [("апрель (ранние яровые)", 0.62 + 0.12), ("май (картофель, лён)", 0.18 + 0.08)]:
    ga_m = POSEV_GOD1_GA * dolya
    koni_m = ga_m / PLOUGH_HA_DAY * 1.9                  # вспашка+боронование+сев
    nado_loshadey = koni_m / OKNO_MESYACA
    nado_B = nado_loshadey * (1 + DVA_ADRESA_H / HORSE_WORK_H) / (1 - UTECHKA)
    print(f"  {mesyac:26} {ga_m:5.1f} га  "
          f"нужно лошадей: А {nado_loshadey:4.1f}  Б {nado_B:4.1f}  из {HORSES}")
print("  Окно месяца — 4 суток (farming.md §4)\n")

# ─── Цена промедления ───
print("─" * 78)
print("ЦЕНА КАЖДОГО МЕСЯЦА БЕЗ ДВОРА (год, помесячно)")
print("─" * 78)
god_koni_h = HORSES * WORK_DAYS_YEAR * HORSE_WORK_H
god_zanyato = god_koni_h * 0.50                          # v7: загрузка лошадей ~50%
shtraf_god = (god_zanyato * DVA_ADRESA_H / HORSE_WORK_H
              + god_koni_h * UTECHKA
              + god_zanyato * 0.35 * BEZ_VOZCHIKA)       # ASSUMPTION: возка — 35% занятости
print(f"  Годовой ресурс лошадей: {god_koni_h:.0f} ч, занято по v7 ~50% = {god_zanyato:.0f} ч")
print(f"  Штраф за год без двора: {shtraf_god:.0f} лошаде-часов "
      f"= {shtraf_god / HORSE_WORK_H:.0f} лошаде-суток")
print(f"  В месяц (12 месяцев по 4 суток): {shtraf_god / 12:.0f} ч")
print(f"  Двор стоит {YARD_TRUDODNI:.0f} чел-суток; штраф в пересчёте на людей "
      f"(1 возчик = 1 лошадь): {shtraf_god / HORSE_WORK_H:.0f} чел-суток в год")
okup = YARD_TRUDODNI / (shtraf_god / HORSE_WORK_H / 12)
print(f"  Окупаемость двора: {okup:.1f} месяца\n")

# ─── Зима ───
print("─" * 78)
print("ЗИМА: успевает ли двор до холодов")
print("─" * 78)
do_zimy = RESURS["весна"] + RESURS["лето"] + RESURS["осень"]
nado = YARD_TRUDODNI + YARD_UTEPLENIE
print(f"  Ресурс весна+лето+осень: {do_zimy:.0f} чел-суток")
print(f"  Двор с утеплением: {nado:.0f} чел-суток — {100 * nado / do_zimy:.0f}% ресурса тёплого сезона")
print(f"  Сверх него на сезон: поле {pole * 0.85:.0f}, сенокос {senokos:.0f}, "
      f"скот {skot * 30 / 41:.0f}, перегной {peregnoy:.0f}")
itogo = pole * 0.85 + senokos + skot * 30 / 41 + nado
print(f"  Итого без перегноя: {itogo:.0f} из {do_zimy:.0f} "
      f"({100 * itogo / do_zimy:.0f}% загрузки тёплого сезона)")
print(f"  С перегноем: {itogo + peregnoy:.0f} ({100 * (itogo + peregnoy) / do_zimy:.0f}%)")
