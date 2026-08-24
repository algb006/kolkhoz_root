#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Симуляция v6. Главное изменение: население РАСТЁТ.
Прежние прогоны считали статичные 80 человек.
С ускорением ×4 к седьмому году их 199, к четырнадцатому 500.

Вопрос: успевает ли производство за ростом едоков?
"""
import statistics

# ─── Демография (из 52-demography.md) ───
NASELENIE = {1:93,2:109,3:128,4:149,5:173,6:199,7:227,8:258,9:291,10:329,
             11:370,12:417,13:468,14:520,15:571,16:625,17:682,18:749,19:820,20:895}

# ─── Труд ───
TRUD_GA = {'зерно':24,'картофель':50,'лён':75,'кормовые':12,'сенокос':8}
TRUD_SKOT = {'корова':32,'лошадь':22,'свинья':8,'овца':4}
KOEF_IGRA = 7.0     # делитель: реальные чел-дни → игровые сутки

def rabotniki(n):
    """Приведённых работников от населения"""
    return (n*0.492 + n*0.136*0.45 + n*0.220*0.30)*0.92

def god(n, naselenie, pashnya, plodorodie, zapas):
    rab = rabotniki(naselenie)
    # Ресурс в игровых рабочих сутках
    resurs = rab * 41 * 0.88 * 0.90

    posev = pashnya
    ga = {'зерно':posev*0.62,'картофель':posev*0.18,'лён':posev*0.08,'кормовые':posev*0.12}
    luga = pashnya*0.55

    urozhay = 0.85*plodorodie
    sbor = ga['зерно']*urozhay
    seno = luga*1.6

    korovy = int(seno/1.8*0.55)
    loshadi = max(6, int(pashnya/16))
    svini = int(naselenie*0.18); ovcy = int(naselenie*0.22)

    # Труд, в реальных чел-днях → делим на коэффициент
    T = {k: ga[k]*TRUD_GA[k] for k in ga}
    T['сенокос'] = luga*TRUD_GA['сенокос']
    T['скот'] = korovy*32+loshadi*22+svini*8+ovcy*4
    T['дрова'] = (naselenie/4.5+8)*12*0.55
    T['добыча'] = 180 if n<=6 else 90        # камень, глина — активнее в первые годы
    T['лес'] = 220 if n<=8 else 140          # порубка и вывоз своих деревьев
    T['стройка'] = 420 if n<=10 else 260     # дома под растущее население
    T['прочее'] = naselenie*3.5 + 300
    if n<=1: T['наследство'] = 500/0.5*0.5   # вывоз перегноя: 1000 рейсов
    nuzhno_real = sum(T.values())
    nuzhno = nuzhno_real/KOEF_IGRA

    zagruzka = nuzhno/resurs*100

    semena = ga['зерно']*0.18; plan = sbor*0.28
    korma = loshadi*0.45+svini*0.25; eda = naselenie*0.24
    bal = sbor+zapas-semena-plan-korma-eda

    # Жильё
    dvorov_nuzhno = naselenie/4.5
    return dict(n=n, nas=naselenie, rab=rab, resurs=resurs, nuzhno=nuzhno,
                zagruzka=zagruzka, sbor=sbor, bal=bal, korovy=korovy,
                dvory=dvorov_nuzhno, posev=posev, plod=plodorodie, T=T)


print('═'*80)
print('СИМУЛЯЦИЯ v6 — растущее население, игровые сутки')
print('═'*80)
print(f'\nКоэффициент пересчёта: реальные чел-дни ÷ {KOEF_IGRA} = игровые сутки')
print('Рабочих суток в году: 41\n')

# Посев растёт вслед за руками
posev_plan = [70,100,130,160,190,220,250,280,310,340,370,400,430,460,490,520,550,580,610,640]
plod = 1.30
zapas = 0.0
rows=[]
print(f"{'год':>4}{'жителей':>9}{'работн':>8}{'посев':>7}{'загруз':>8}"
      f"{'сбор т':>9}{'зерно±':>9}{'дворов':>8}")
print('─'*80)
for n in range(1,21):
    nas = NASELENIE[n]
    posev = posev_plan[n-1]
    r = god(n, nas, posev, plod, zapas)
    zapas = max(0, r['bal'])
    # Плодородие: тратится, чуть восстанавливается навозом
    udobr_ga = min(posev, (r['korovy']*9+r['dvory']*0.55*4)/18)
    plod = max(0.6, plod - 0.055 + 0.28*(udobr_ga/posev)*0.35 + 0.10*0.25)
    rows.append(r)
    flag=''
    if r['zagruzka']>105: flag=' ⚠ НЕТ РУК'
    elif r['bal']<0: flag=' ⚠ ГОЛОД'
    if n<=14 or n%2==1:
        print(f"{n:>4}{nas:>9}{r['rab']:>8.0f}{posev:>7.0f}{r['zagruzka']:>7.0f}%"
              f"{r['sbor']:>9.1f}{r['bal']:>+9.1f}{r['dvory']:>8.0f}{flag}")
print('─'*80)

print('\n' + '═'*80)
print('ГЛАВНЫЙ ВОПРОС: успевает ли посев за ростом едоков?')
print('═'*80)
print(f"\n{'год':>4}{'жителей':>9}{'посев га':>10}{'га/едока':>10}{'нужно га*':>11}{'дефицит':>10}")
print('─'*80)
for i,r in enumerate(rows[:14]):
    nuzhno_ga = r['nas']*2.0
    print(f"{r['n']:>4}{r['nas']:>9}{r['posev']:>10.0f}{r['posev']/r['nas']:>10.1f}"
          f"{nuzhno_ga:>11.0f}{r['posev']-nuzhno_ga:>+10.0f}")
print('─'*80)
print('  * норма 2 га пашни на едока (`49-simulation-v1.md` §1)')
