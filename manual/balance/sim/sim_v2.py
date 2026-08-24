#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Симуляция v2: поиск жизнеспособного стартового поселения.
Перебираем население и площадь пашни, ищем окно, где
одновременно сходятся труд и хлеб.
"""

def model(lyudey, pashnya, luga_koef=0.5, plan_dolya=0.28, urozhay=0.85):
    # Возрастная структура (доли для сельского населения)
    deti_0_6  = round(lyudey * 0.152)
    deti_7_15 = round(lyudey * 0.220)
    stariki   = round(lyudey * 0.136)
    vzroslye  = lyudey - deti_0_6 - deti_7_15 - stariki

    trud = (vzroslye + stariki*0.45 + deti_7_15*0.30) * 0.92
    resurs = trud * 270 * 0.88 * 0.90

    posev = pashnya * 0.75
    zerno_ga = posev * 0.62
    kart_ga  = posev * 0.18
    len_ga   = posev * 0.08
    korm_ga  = posev * 0.12
    luga     = pashnya * luga_koef

    sbor_zerno = zerno_ga * urozhay
    sbor_kart  = kart_ga * 9.0
    sbor_seno  = luga * 1.6

    # Скот масштабируется от кормовой базы
    korovy  = int(sbor_seno / 1.8 * 0.55)
    loshadi = max(6, int(pashnya / 18))
    svini   = int(lyudey * 0.18)
    ovcy    = int(lyudey * 0.22)

    T = {}
    T['зерно']     = zerno_ga*(2.2+0.5+4.0) + sbor_zerno*3.0
    T['картофель'] = kart_ga*(6.0+5.0+14.0)
    T['лён']       = len_ga*30.0
    T['кормовые']  = korm_ga*3.5
    T['сенокос']   = luga*3.2
    T['скот']      = korovy*9.0 + loshadi*5.0 + svini*2.5 + ovcy*1.5
    doma = lyudey/3.9 + 6
    T['дрова']     = doma*12.0*0.55
    T['прочее']    = (lyudey/3.9)*14 + 380   # обслуживание дворов и юнитов

    nuzhno = sum(T.values())
    zagruzka = nuzhno / resurs * 100

    # Зерно
    semena = zerno_ga*0.18
    plan   = sbor_zerno*plan_dolya
    korma  = loshadi*0.45 + svini*0.25
    eda    = lyudey*0.24
    ostatok = sbor_zerno - semena - plan - korma - eda

    # Сено
    seno_nuzhno = korovy*1.8 + loshadi*1.8 + ovcy*0.35
    seno_ost = sbor_seno - seno_nuzhno

    # Страда
    strada_nuzhno = zerno_ga*4.0 + kart_ga*14.0 + sbor_zerno*1.5 + len_ga*15.0
    strada_resurs = trud*45*0.88
    strada_bal = strada_resurs - strada_nuzhno

    return dict(zagruzka=zagruzka, zerno_ost=ostatok, seno_ost=seno_ost,
                strada=strada_bal, korovy=korovy, sbor=sbor_zerno,
                resurs=resurs, nuzhno=nuzhno, trud=trud, loshadi=loshadi)


print('═'*74)
print('ПОИСК ЖИЗНЕСПОСОБНОГО СТАРТА')
print('═'*74)
print('\nКритерии: загрузка труда 80–100%, остаток зерна > 0, сено > 0, страда сходится\n')

hdr = f"{'люди':>5} {'пашня':>6} {'загруз':>7} {'зерно±':>8} {'сено±':>7} {'страда±':>8}  вердикт"
print(hdr)
print('─'*74)

good = []
for lyudey in (90, 110, 130, 150, 180):
    for pashnya in (150, 200, 250, 300, 350, 400):
        r = model(lyudey, pashnya)
        ok_trud  = 78 <= r['zagruzka'] <= 102
        ok_zerno = r['zerno_ost'] > 1
        ok_seno  = r['seno_ost'] > -1
        ok_str   = r['strada'] > -50
        verdict = []
        if not ok_trud:
            verdict.append('мало работы' if r['zagruzka'] < 78 else 'НЕ ХВАТАЕТ РУК')
        if not ok_zerno: verdict.append('ГОЛОД')
        if not ok_seno:  verdict.append('падёж')
        if not ok_str:   verdict.append('страда')
        v = ' · '.join(verdict) if verdict else '✓ СХОДИТСЯ'
        if not verdict:
            good.append((lyudey, pashnya, r))
        print(f"{lyudey:>5} {pashnya:>6} {r['zagruzka']:>6.0f}% "
              f"{r['zerno_ost']:>+8.1f} {r['seno_ost']:>+7.0f} {r['strada']:>+8.0f}  {v}")

print('─'*74)

if good:
    print(f"\nПОДХОДЯЩИХ СОЧЕТАНИЙ: {len(good)}\n")
    print('РЕКОМЕНДАЦИЯ — середина диапазона:\n')
    l, p, r = good[len(good)//2]
    print(f"  Население:  {l} человек")
    print(f"  Пашня:      {p} га")
    print(f"  Луга:       {p*0.5:.0f} га")
    print(f"  Коров:      {r['korovy']}")
    print(f"  Лошадей:    {r['loshadi']}")
    print(f"  Работников: {r['trud']:.0f} приведённых")
    print(f"\n  Загрузка труда:  {r['zagruzka']:.0f}%")
    print(f"  Сбор зерна:      {r['sbor']:.0f} т")
    print(f"  Остаток зерна:   {r['zerno_ost']:+.1f} т "
          f"({r['zerno_ost']/r['sbor']*100:+.0f}% от сбора)")
    print(f"  Запас сена:      {r['seno_ost']:+.0f} т")
else:
    print('\n⚠ НИ ОДНО СОЧЕТАНИЕ НЕ СХОДИТСЯ — модель или параметры требуют пересмотра')

# Проверка на неурожай
print('\n' + '═'*74)
print('ПРОВЕРКА НА ПРОЧНОСТЬ: неурожайный год (урожайность −35%)')
print('═'*74)
if good:
    l, p, _ = good[len(good)//2]
    bad = model(l, p, urozhay=0.85*0.65)
    print(f"  Остаток зерна: {bad['zerno_ost']:+.1f} т")
    if bad['zerno_ost'] < 0:
        print(f"  ⚠ Голод. Не хватает {-bad['zerno_ost']:.1f} т — "
              f"это {-bad['zerno_ost']/(l*0.24)*100:.0f}% годовой нормы питания")
        print("  → Нужен переходящий запас или помощь района")
    else:
        print("  ✓ Переживают без помощи")

    print('\n' + '═'*74)
    print('ПРОВЕРКА: жёсткий план (доля сдачи 40% вместо 28%)')
    print('═'*74)
    hard = model(l, p, plan_dolya=0.40)
    print(f"  Остаток зерна: {hard['zerno_ost']:+.1f} т")
    if hard['zerno_ost'] < 0:
        print(f"  ⚠ План выполним только за счёт голода — "
              f"не хватает {-hard['zerno_ost']:.1f} т")
    else:
        print("  ✓ Выдерживают")
print('═'*74)
