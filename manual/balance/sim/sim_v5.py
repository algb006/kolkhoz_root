#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Симуляция v5. Добавлен цикл удобрений:
— навоз копится помесячно от поголовья
— зреет ровно 9 месяцев
— вносится под сев, поднимает плодородие
— проверка ёмкости кучи
Прогон 10 лет, помесячно.
"""

LYUDEY=80; DVORY=21; PASHNYA=160
BAZ_UROZHAY=0.85

# Навоз от головы в год, тонн
NAVOZ = {'корова':9.0,'лошадь':7.0,'свинья':1.6,'овца':0.7}
NECHISTOTY_NA_CHELOVEKA = 0.55   # т/год с выгребных ям

# Внесение: сколько тонн удобрения на га, и что это даёт плодородию
NORMA_VNESENIYA = 18.0   # т/га — обычная норма органики
PRIBAVKA_PLODORODIYA = 0.30   # на сколько поднимает удобренный гектар

# Расход плодородия за год возделывания
RASHOD = 0.055
# Восстановление под паром
PAR_VOSST = 0.10


def prognoz(let=10, emkost_kuchi=200, dolya_para=0.25, vnosit=True):
    korovy, loshadi, svini, ovcy = 39, 8, 14, 18
    navoz_god = (korovy*NAVOZ['корова'] + loshadi*NAVOZ['лошадь']
                 + svini*NAVOZ['свинья'] + ovcy*NAVOZ['овца']
                 + LYUDEY*NECHISTOTY_NA_CHELOVEKA)
    navoz_mes = navoz_god/12

    # Куча: список порций (месяц закладки, тонн)
    kucha = []      # [(вozrast_mes, tonn)]
    gotovo = 0.0    # созревшее удобрение, т
    plodorodie = 1.30
    zapas_zerna = 0.0
    poteryano = 0.0

    rows = []
    posev_plan = [70,100,120,120,120,120,120,120,120,120]

    for god in range(1, let+1):
        posev = posev_plan[min(god-1, len(posev_plan)-1)]
        zerno_ga = posev*0.62
        udobreno_ga = 0.0

        for mes in range(1, 13):
            # Прирост навоза
            kucha.append([0, navoz_mes])
            # Старение
            for p in kucha: p[0] += 1
            # Созревшее переносим
            zrelye = [p for p in kucha if p[0] >= 9]
            gotovo += sum(p[1] for p in zrelye)
            kucha = [p for p in kucha if p[0] < 9]

            # Проверка ёмкости: свежее + созревшее
            v_kuche = sum(p[1] for p in kucha) + gotovo
            if v_kuche > emkost_kuchi:
                izbytok = v_kuche - emkost_kuchi
                # Теряем самое свежее — его некуда деть
                poteryano += izbytok
                # Убираем излишек с конца
                ost = izbytok
                while ost > 0 and kucha:
                    if kucha[-1][1] <= ost:
                        ost -= kucha[-1][1]; kucha.pop()
                    else:
                        kucha[-1][1] -= ost; ost = 0

            # Внесение: апрель (месяц 4), под сев
            if mes == 4 and vnosit and gotovo > 0:
                mozhno_ga = gotovo/NORMA_VNESENIYA
                udobreno_ga = min(mozhno_ga, posev)
                gotovo -= udobreno_ga*NORMA_VNESENIYA

        # Плодородие: расход на возделывании, прибавка от удобрений и пара
        dolya_udobr = udobreno_ga/posev if posev else 0
        plodorodie = plodorodie - RASHOD + PRIBAVKA_PLODORODIYA*dolya_udobr*0.35 + PAR_VOSST*dolya_para
        plodorodie = max(0.55, min(1.45, plodorodie))

        urozhay = BAZ_UROZHAY*plodorodie
        sbor = zerno_ga*urozhay
        semena = zerno_ga*0.18; plan = sbor*0.28
        korma = loshadi*0.45 + svini*0.25; eda = LYUDEY*0.24
        bal = sbor + zapas_zerna - semena - plan - korma - eda
        zapas_zerna = max(0, bal)

        rows.append(dict(god=god, posev=posev, udobr=udobreno_ga,
                         dolya=dolya_udobr*100, plod=plodorodie,
                         urozhay=urozhay, sbor=sbor, bal=bal,
                         gotovo=gotovo, v_kuche=sum(p[1] for p in kucha)+gotovo,
                         poteryano=poteryano))
    return rows, navoz_god


print('═'*80)
print('СИМУЛЯЦИЯ v5 — цикл удобрений, 10 лет')
print('═'*80)

rows, navoz_god = prognoz()
print(f'\nНавоза и нечистот в год: {navoz_god:.0f} т')
print(f'Норма внесения: {NORMA_VNESENIYA} т/га → хватает на {navoz_god/NORMA_VNESENIYA:.0f} га')
print(f'Посев к третьему году: 120 га\n')

print(f"{'год':>4}{'посев':>7}{'удобр га':>10}{'%поля':>7}{'плодор':>8}"
      f"{'урож т/га':>11}{'сбор':>8}{'зерно±':>9}{'в куче':>8}")
print('─'*80)
for r in rows:
    flag = ''
    if r['bal'] < 0: flag = ' ⚠'
    print(f"{r['god']:>4}{r['posev']:>7.0f}{r['udobr']:>10.0f}{r['dolya']:>6.0f}%"
          f"{r['plod']:>8.2f}{r['urozhay']:>11.2f}{r['sbor']:>8.1f}"
          f"{r['bal']:>+9.1f}{r['v_kuche']:>8.0f}{flag}")
print('─'*80)
print(f"  Потеряно навоза за 10 лет: {rows[-1]['poteryano']:.0f} т")

print('\n' + '═'*80)
print('ЧТО БУДЕТ БЕЗ УДОБРЕНИЙ')
print('═'*80)
bez, _ = prognoz(vnosit=False)
print(f"\n{'год':>4}{'плодородие':>13}{'урожай т/га':>14}{'сбор т':>10}{'зерно±':>10}")
print('─'*80)
for r in bez:
    flag = ' ⚠ ГОЛОД' if r['bal'] < 0 else ''
    print(f"{r['god']:>4}{r['plod']:>13.2f}{r['urozhay']:>14.2f}{r['sbor']:>10.1f}{r['bal']:>+10.1f}{flag}")
print('─'*80)
print(f"\n  С удобрениями к 10 году: плодородие {rows[-1]['plod']:.2f}, сбор {rows[-1]['sbor']:.1f} т")
print(f"  Без удобрений:           плодородие {bez[-1]['plod']:.2f}, сбор {bez[-1]['sbor']:.1f} т")
print(f"  Разница в сборе:         {rows[-1]['sbor']-bez[-1]['sbor']:+.1f} т "
      f"({(rows[-1]['sbor']/bez[-1]['sbor']-1)*100:+.0f}%)")

print('\n' + '═'*80)
print('ЁМКОСТЬ КУЧИ: сколько нужно')
print('═'*80)
print(f"\n{'ёмкость':>10}{'потеряно за 10 лет':>22}{'доля потерь':>14}")
print('─'*80)
for e in (80, 120, 160, 200, 250, 300):
    r2, ng = prognoz(emkost_kuchi=e)
    pot = r2[-1]['poteryano']
    print(f"{e:>8} т{pot:>20.0f} т{pot/(ng*10)*100:>13.0f}%")
print('─'*80)
print(f"\n  Годовой выход навоза: {navoz_god:.0f} т")
print(f"  Рекомендуемая ёмкость: {navoz_god*1.1:.0f}–{navoz_god*1.3:.0f} т")
print("  (в куче одновременно лежат порции девяти возрастов)")
print('═'*80)
