#!/usr/bin/env python3
"""Transport load simulation for the single-clock model (KCD-style, clock x K).

Checks whether starting-farm logistics survive when effective speeds in game
hours drop by K. Inputs from core/manual/balance/49-simulations.md and the
start-location canon; cart capacity and inner distances are assumptions
marked ASSUMPTION below.
"""

K_VARIANTS = [1, 8, 12]

# --- canon ---
HORSES = 8                    # 49-simulations §2
WORK_DAYS_YEAR = 41           # CLAUDE.md §9
DAY_HOURS = {"весна": 13.0, "лето": 16.5, "осень": 11.5, "зима": 7.5}  # daylight, game h
GRAIN_GROSS_T = 68.0          # 49-simulations §2 (сбор при нормальном плодородии)
MANURE_T = 486.0              # 49-simulations §2
DELIVERY_SHOULDER_KM = 2.5    # terrain.md §1
PLOUGH_HA = 120.0             # активная пашня, 49-simulations §2

# --- assumptions ---
CART_T = 0.5                  # ASSUMPTION: конная телега ~0.5 т
CART_KMH = 9.0                # ASSUMPTION: гружёная телега, реальная скорость
LOAD_UNLOAD_H = 0.5           # ASSUMPTION: фикс. погрузка+разгрузка за рейс, game h
HORSE_WORK_H = 10.0           # ASSUMPTION: часов лошади в рабочий день (лето)
PLOUGH_HA_PER_DAY = 0.9       # ASSUMPTION: конная вспашка га/лошаде-день
PLOUGHINGS = 1.6              # ASSUMPTION: весенняя + частичная зябь
GRAIN_PLAN_SHARE = 0.35       # ASSUMPTION: доля вала на сдачу

FLOWS = [
    # name, tonnes/year, one-way km (ASSUMPTION for inner), window in game days
    ("Урожай: поле → ток/склад", GRAIN_GROSS_T, 0.7, 8),     # уборочное окно
    ("Сдача: склад → райцентр",  GRAIN_GROSS_T*GRAIN_PLAN_SHARE, DELIVERY_SHOULDER_KM, 12),
    ("Навоз: куча → поля",       MANURE_T, 0.5, 24),         # растянуто на 2 сезона
    ("Сено: луга → двор",        160.0, 1.2, 8),             # 80 га x 2 т/га
    ("Дрова: лес → село",        100.0, 1.5, 16),
    ("Семена, стройка, прочее",  60.0, 1.0, 24),
]

plough_days = PLOUGH_HA*PLOUGHINGS/PLOUGH_HA_PER_DAY
horse_hours_year = HORSES*WORK_DAYS_YEAR*HORSE_WORK_H
print(f"Ресурс: {HORSES} лошадей x {WORK_DAYS_YEAR} раб.суток x {HORSE_WORK_H} ч = {horse_hours_year:.0f} лошаде-часов/год")
print(f"Вспашка {PLOUGH_HA:.0f} га x {PLOUGHINGS} прохода: {plough_days:.0f} лошаде-дней = {plough_days*HORSE_WORK_H:.0f} ч")
print(f"Остаток на логистику: {horse_hours_year-plough_days*HORSE_WORK_H:.0f} ч\n")

for K in K_VARIANTS:
    v_eff = CART_KMH/K
    print(f"=== K={K} (эфф. скорость телеги {v_eff:.2f} км/иг.ч) ===")
    total_h = 0.0
    print(f"{'Поток':28}{'т':>6}{'рейсов':>8}{'ч/рейс':>8}{'л-часов':>9}{'л-дней':>8}{'пик лошадей':>12}")
    for name, tons, km, window in FLOWS:
        trips = tons/CART_T
        trip_h = 2*km/v_eff + LOAD_UNLOAD_H
        hours = trips*trip_h
        days = hours/HORSE_WORK_H
        peak = hours/(window*HORSE_WORK_H)   # horses needed simultaneously in window
        total_h += hours
        print(f"{name:28}{tons:6.0f}{trips:8.0f}{trip_h:8.2f}{hours:9.0f}{days:8.1f}{peak:12.1f}")
    logistics_days = total_h/HORSE_WORK_H
    budget = horse_hours_year - plough_days*HORSE_WORK_H
    print(f"{'ИТОГО логистика':28}{'':6}{'':8}{'':8}{total_h:9.0f}{logistics_days:8.1f}")
    print(f"Доля от годового ресурса лошадей: {100*total_h/horse_hours_year:.0f}%  "
          f"(бюджет после вспашки: {100*total_h/budget:.0f}%)")
    # harvest peak check: уборка+сдача одновременно осенью
    harv = FLOWS[0]; deliv = FLOWS[1]
    peak_h = (harv[1]/CART_T)*(2*harv[2]/v_eff+LOAD_UNLOAD_H)/(harv[3]*HORSE_WORK_H) \
           + (deliv[1]/CART_T)*(2*deliv[2]/v_eff+LOAD_UNLOAD_H)/(deliv[3]*HORSE_WORK_H)
    print(f"Осенний пик (уборка+сдача разом): {peak_h:.1f} лошадей из {HORSES}\n")

print("Чувствительность: сдача 25 т при K=12, число лошадей на плече по дням окна")
for window in [6, 12, 18]:
    trips = GRAIN_GROSS_T*GRAIN_PLAN_SHARE/CART_T
    trip_h = 2*DELIVERY_SHOULDER_KM/(CART_KMH/12) + LOAD_UNLOAD_H
    print(f"  окно {window:>2} суток: {trips*trip_h/(window*HORSE_WORK_H):.1f} лошади постоянно")

# --- Scenario B: fixes applied (K=12) ---
print("\n=== Сценарий Б: K=12, телега 0.75 т, 16 лошадей, навоз и дрова зимой санями ===")
CART_B = 0.75; HORSES_B = 16
v_eff = CART_KMH/12
winter = {"Навоз: куча → поля", "Дрова: лес → село"}
tot_summer = tot_winter = 0.0
for name, tons, km, window in FLOWS:
    trips = tons/CART_B
    trip_h = 2*km/v_eff + LOAD_UNLOAD_H
    hours = trips*trip_h
    if name in winter: tot_winter += hours
    else: tot_summer += hours
    peak = hours/(window*HORSE_WORK_H)
    print(f"  {name:28}{hours:7.0f} ч  пик {peak:4.1f} лошадей {'(зимой)' if name in winter else ''}")
res = HORSES_B*WORK_DAYS_YEAR*HORSE_WORK_H
print(f"  Летне-осенняя логистика: {tot_summer:.0f} ч; зимняя: {tot_winter:.0f} ч; итого {(tot_summer+tot_winter):.0f} ч")
print(f"  Ресурс {HORSES_B} лошадей: {res:.0f} ч -> загрузка {100*(tot_summer+tot_winter)/res:.0f}%")
# people: 1 возчик на телегу, часы людей = часы лошадей
workers = 52
season_days = 30   # раб. суток вне зимы
print(f"  Возчики в сезон: {tot_summer/(season_days*HORSE_WORK_H):.1f} чел-экв из {workers} трудоспособных"
      f" ({100*tot_summer/(season_days*HORSE_WORK_H)/workers:.0f}% сезонного труда)")
print(f"  Возчики зимой:  {tot_winter/(11*7.5):.1f} чел-экв (зимних раб.суток ~11, день 7.5 ч) — зима и так свободна")
# autumn peak with B params
harv, deliv = FLOWS[0], FLOWS[1]
peak_b = (harv[1]/CART_B)*(2*harv[2]/v_eff+LOAD_UNLOAD_H)/(harv[3]*HORSE_WORK_H) \
       + (deliv[1]/CART_B)*(2*deliv[2]/v_eff+LOAD_UNLOAD_H)/(deliv[3]*HORSE_WORK_H)
print(f"  Осенний пик (уборка+сдача): {peak_b:.1f} лошадей из {HORSES_B}")
# ploughing with /7 norm compression
plough_game_days = PLOUGH_HA*PLOUGHINGS/(PLOUGH_HA_PER_DAY*7)
print(f"  Вспашка по норме ÷7: {plough_game_days:.0f} лошаде-игровых-суток -> {plough_game_days/6:.1f} лошади в окно 6 суток")
