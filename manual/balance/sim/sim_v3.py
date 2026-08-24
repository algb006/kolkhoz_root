#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Симуляция v3. Исправлено главное: трудоёмкость ручных работ была
занижена в разы. Добавлена сезонность — годовой баланс скрывал пики.
"""

# Трудоёмкость ручного труда, чел-дней на гектар за полный цикл.
# Источник оценок: нормы конно-ручного земледелия.
TRUD_GA = {
    'зерно':     24,   # пахота, боронование, сев, жатва серпом/косой,
                       # вязка, свозка, молотьба цепами, веяние
    'картофель': 50,   # посадка, окучивание дважды, копка, переборка
    'лён':       75,   # самая трудоёмкая: теребление, расстил, мятьё, трепание
    'кормовые':  12,
    'сенокос':    8,   # косьба, ворошение, стогование, свозка
}

# Животноводство, чел-дней на голову в год
TRUD_SKOT = {'корова': 32, 'лошадь': 22, 'свинья': 8, 'овца': 4}

# Сезонное распределение: доля годовой трудоёмкости
SEZON = {
    'зерно':     {'весна': .28, 'лето': .12, 'осень': .45, 'зима': .15},
    'картофель': {'весна': .22, 'лето': .28, 'осень': .50, 'зима': .00},
    'лён':       {'весна': .10, 'лето': .25, 'осень': .40, 'зима': .25},
    'кормовые':  {'весна': .40, 'лето': .30, 'осень': .30, 'зима': .00},
    'сенокос':   {'весна': .00, 'лето': .85, 'осень': .15, 'зима': .00},
    'скот':      {'весна': .25, 'лето': .20, 'осень': .25, 'зима': .30},
    'дрова':     {'весна': .10, 'лето': .10, 'осень': .25, 'зима': .55},
    'прочее':    {'весна': .25, 'лето': .25, 'осень': .25, 'зима': .25},
}

# Рабочих дней в сезоне (за вычетом выходных, праздников, распутицы)
DNEY_SEZON = {'весна': 60, 'лето': 78, 'осень': 68, 'зима': 64}

# Длина светового дня как множитель выработки
SVET = {'весна': 1.00, 'лето': 1.15, 'осень': 0.85, 'зима': 0.55}


def model(lyudey, pashnya, urozhay=0.85, plan_dolya=0.28):
    deti_0_6  = round(lyudey*0.152)
    deti_7_15 = round(lyudey*0.220)
    stariki   = round(lyudey*0.136)
    vzroslye  = lyudey - deti_0_6 - deti_7_15 - stariki
    rabotniki = (vzroslye + stariki*0.45 + deti_7_15*0.30)*0.92

    posev = pashnya*0.75
    ga = {'зерно': posev*0.62, 'картофель': posev*0.18,
          'лён': posev*0.08, 'кормовые': posev*0.12}
    luga = pashnya*0.5

    sbor_zerno = ga['зерно']*urozhay
    sbor_seno  = luga*1.6

    korovy  = int(sbor_seno/1.8*0.55)
    loshadi = max(6, int(pashnya/18))
    svini   = int(lyudey*0.18)
    ovcy    = int(lyudey*0.22)

    T = {k: ga[k]*TRUD_GA[k] for k in ga}
    T['сенокос'] = luga*TRUD_GA['сенокос']
    T['скот'] = (korovy*TRUD_SKOT['корова'] + loshadi*TRUD_SKOT['лошадь']
                 + svini*TRUD_SKOT['свинья'] + ovcy*TRUD_SKOT['овца'])
    doma = lyudey/3.9 + 6
    T['дрова'] = doma*12*0.55
    T['прочее'] = (lyudey/3.9)*14 + 380

    # Сезонный баланс
    sez = {}
    for s in DNEY_SEZON:
        nuzhno = sum(T[k]*SEZON[k][s] for k in T)
        resurs = rabotniki*DNEY_SEZON[s]*SVET[s]*0.88*0.90
        sez[s] = (nuzhno, resurs, nuzhno/resurs*100)

    nuzhno_god = sum(T.values())
    resurs_god = sum(v[1] for v in sez.values())

    semena = ga['зерно']*0.18
    plan   = sbor_zerno*plan_dolya
    korma  = loshadi*0.45 + svini*0.25
    eda    = lyudey*0.24
    zerno_ost = sbor_zerno - semena - plan - korma - eda
    seno_ost  = sbor_seno - (korovy*1.8 + loshadi*1.8 + ovcy*0.35)

    return dict(T=T, sez=sez, nuzhno=nuzhno_god, resurs=resurs_god,
                zagruzka=nuzhno_god/resurs_god*100, zerno_ost=zerno_ost,
                seno_ost=seno_ost, sbor=sbor_zerno, korovy=korovy,
                loshadi=loshadi, rabotniki=rabotniki)


print('═'*76)
print('СИМУЛЯЦИЯ v3 — реалистичная трудоёмкость ручного земледелия')
print('═'*76)
print('\nПОИСК: где сходятся труд, хлеб и сено одновременно\n')
print(f"{'люди':>5} {'пашня':>6} {'год':>5} {'весна':>6} {'лето':>6} "
      f"{'осень':>6} {'зима':>6} {'зерно±':>8}  вердикт")
print('─'*76)

good = []
for lyudey in (80, 100, 120, 140):
    for pashnya in (60, 80, 100, 120, 150, 180):
        r = model(lyudey, pashnya)
        s = r['sez']
        peak = max(s[k][2] for k in s)
        ok = (75 <= r['zagruzka'] <= 100 and r['zerno_ost'] > 0.5
              and r['seno_ost'] > 0 and peak <= 115)
        v = []
        if r['zagruzka'] < 75: v.append('простой')
        if r['zagruzka'] > 100: v.append('НЕ ХВАТАЕТ РУК')
        if r['zerno_ost'] <= 0.5: v.append('ГОЛОД')
        if r['seno_ost'] <= 0: v.append('падёж')
        if peak > 115: v.append(f'пик {peak:.0f}%')
        verdict = ' · '.join(v) if v else '✓ СХОДИТСЯ'
        if ok: good.append((lyudey, pashnya, r))
        print(f"{lyudey:>5} {pashnya:>6} {r['zagruzka']:>4.0f}% "
              f"{s['весна'][2]:>5.0f}% {s['лето'][2]:>5.0f}% "
              f"{s['осень'][2]:>5.0f}% {s['зима'][2]:>5.0f}% "
              f"{r['zerno_ost']:>+8.1f}  {verdict}")
print('─'*76)

if good:
    print(f'\n✓ ПОДХОДЯЩИХ ВАРИАНТОВ: {len(good)}\n')
    l, p, r = good[len(good)//2]
    print('═'*76)
    print('РЕКОМЕНДУЕМЫЙ СТАРТ')
    print('═'*76)
    print(f"  Население      {l} человек, ~{l/3.9:.0f} дворов")
    print(f"  Работников     {r['rabotniki']:.0f} приведённых")
    print(f"  Пашня          {p} га  (посев {p*0.75:.0f} га)")
    print(f"  Луга           {p*0.5:.0f} га")
    print(f"  Коров          {r['korovy']}")
    print(f"  Лошадей        {r['loshadi']}")
    print(f"\n  Сбор зерна     {r['sbor']:.0f} т")
    print(f"  Остаток зерна  {r['zerno_ost']:+.1f} т "
          f"({r['zerno_ost']/r['sbor']*100:+.0f}%)")
    print(f"  Запас сена     {r['seno_ost']:+.0f} т")
    print(f"\n  СЕЗОННАЯ ЗАГРУЗКА")
    for s in ('весна','лето','осень','зима'):
        n, res, z = r['sez'][s]
        bar = '█'*int(z/5)
        flag = ' ← ПИК' if z > 95 else (' ← простой' if z < 55 else '')
        print(f"    {s:<6} {z:>5.0f}%  {bar}{flag}")

    print(f"\n  РАСПРЕДЕЛЕНИЕ ТРУДА ЗА ГОД")
    for k, v in sorted(r['T'].items(), key=lambda x: -x[1]):
        print(f"    {k:<12} {v:>6.0f} чел-дней  {v/r['nuzhno']*100:>5.1f}%")

    print('\n' + '═'*76)
    print('ПРОВЕРКИ НА ПРОЧНОСТЬ')
    print('═'*76)
    bad = model(l, p, urozhay=0.85*0.65)
    print(f"  Неурожай −35%:  остаток зерна {bad['zerno_ost']:+.1f} т", end='')
    print('  ⚠ ГОЛОД' if bad['zerno_ost'] < 0 else '  ✓')
    hard = model(l, p, plan_dolya=0.40)
    print(f"  План 40%:       остаток зерна {hard['zerno_ost']:+.1f} т", end='')
    print('  ⚠ ГОЛОД' if hard['zerno_ost'] < 0 else '  ✓')
    both = model(l, p, urozhay=0.85*0.75, plan_dolya=0.35)
    print(f"  Неурожай+план:  остаток зерна {both['zerno_ost']:+.1f} т", end='')
    print('  ⚠ ГОЛОД' if both['zerno_ost'] < 0 else '  ✓')
else:
    print('\n⚠ Окно не найдено — нужен пересмотр параметров')
print('═'*76)
