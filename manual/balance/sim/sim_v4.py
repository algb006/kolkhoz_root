#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Симуляция v4. Новый лор старта:
— заброшенные поля: плодородие максимальное, вспашка тяжелее
— рыба сетями первые три года
— ЛПХ учтено
Прогон на 5 лет.
"""

TRUD_GA = {'зерно':24,'картофель':50,'лён':75,'кормовые':12,'сенокос':8}
TRUD_SKOT = {'корова':32,'лошадь':22,'свинья':8,'овца':4}
SEZON = {
 'зерно':{'весна':.28,'лето':.12,'осень':.45,'зима':.15},
 'картофель':{'весна':.22,'лето':.28,'осень':.50,'зима':.00},
 'лён':{'весна':.10,'лето':.25,'осень':.40,'зима':.25},
 'кормовые':{'весна':.40,'лето':.30,'осень':.30,'зима':.00},
 'сенокос':{'весна':.00,'лето':.85,'осень':.15,'зима':.00},
 'скот':{'весна':.25,'лето':.20,'осень':.25,'зима':.30},
 'дрова':{'весна':.10,'лето':.10,'осень':.25,'зима':.55},
 'рыба':{'весна':.25,'лето':.30,'осень':.30,'зима':.15},
 'прочее':{'весна':.25,'лето':.25,'осень':.25,'зима':.25},
}
DNEY = {'весна':60,'лето':78,'осень':68,'зима':64}
SVET = {'весна':1.00,'лето':1.15,'осень':0.85,'зима':0.55}
KKAL = {'зерно':3.3,'картофель':0.77,'овощи':0.25,'молоко':0.64,'рыба':0.9}

LYUDEY = 80
PASHNYA = 160
DVORY = 21
BAZ_UROZHAY = 0.85


def god(n, vspahano_ga, plodorodie, zapas_zerna, urozhay_mod=1.0):
    """n — номер года, начиная с 1"""
    deti_0_6=round(LYUDEY*.152); deti_7_15=round(LYUDEY*.220)
    stariki=round(LYUDEY*.136); vzroslye=LYUDEY-deti_0_6-deti_7_15-stariki
    rabotniki=(vzroslye+stariki*.45+deti_7_15*.30)*.92

    posev = vspahano_ga
    ga = {'зерно':posev*.62,'картофель':posev*.18,'лён':posev*.08,'кормовые':posev*.12}
    luga = PASHNYA*0.5

    urozhay = BAZ_UROZHAY * plodorodie * urozhay_mod
    sbor_zerno = ga['зерно']*urozhay
    sbor_kart_kolh = ga['картофель']*9.0*plodorodie
    sbor_seno = luga*1.6

    korovy=int(sbor_seno/1.8*.55); loshadi=max(6,int(PASHNYA/18))
    svini=int(LYUDEY*.18); ovcy=int(LYUDEY*.22)

    # Трудоёмкость. Первый год: целина, вспашка тяжелее в 1.6 раза
    celina = 1.6 if n==1 else (1.25 if n==2 else 1.0)
    T={}
    T['зерно']=ga['зерно']*TRUD_GA['зерно']*(celina if n<=2 else 1.0)
    T['картофель']=ga['картофель']*TRUD_GA['картофель']
    T['лён']=ga['лён']*TRUD_GA['лён']
    T['кормовые']=ga['кормовые']*TRUD_GA['кормовые']
    T['сенокос']=luga*TRUD_GA['сенокос']
    T['скот']=korovy*32+loshadi*22+svini*8+ovcy*4
    T['дрова']=(DVORY+6)*12*.55
    T['прочее']=DVORY*14+380
    # Рыба сетями — первые три года
    seti = n<=3
    T['рыба']= 3*80 if seti else 0     # 3 человека по 80 дней

    nuzhno=sum(T.values())
    sez={}
    for s_ in DNEY:
        n_=sum(T[k]*SEZON[k][s_] for k in T)
        r_=rabotniki*DNEY[s_]*SVET[s_]*.88*.90
        sez[s_]=(n_,r_,n_/r_*100)
    resurs=sum(v[1] for v in sez.values())

    # Еда
    semena=ga['зерно']*.18; plan=sbor_zerno*.28
    korma=loshadi*.45+svini*.25; eda_zerno=LYUDEY*.24
    zerno_bal = sbor_zerno+zapas_zerna-semena-plan-korma-eda_zerno

    lph_ekv = (DVORY*0.90*KKAL['картофель']+DVORY*1.20*KKAL['овощи']
               +DVORY*1.21*KKAL['молоко'])/KKAL['зерно']
    ryba_t = 9.0 if seti else 1.5
    ryba_ekv = ryba_t*KKAL['рыба']/KKAL['зерно']
    mol_kolh_ekv = korovy*1.6*KKAL['молоко']/KKAL['зерно']*0.5
    kart_kolh_ekv = sbor_kart_kolh*0.5*KKAL['картофель']/KKAL['зерно']

    pitanie = lph_ekv+ryba_ekv+mol_kolh_ekv+kart_kolh_ekv
    potrebnost = LYUDEY*0.24
    # Зерно на еду уже вычтено, остальное — сверх нормы
    sytost = (potrebnost+pitanie)/potrebnost*100

    return dict(sez=sez, zagruzka=nuzhno/resurs*100, zerno_bal=zerno_bal,
                sbor=sbor_zerno, urozhay=urozhay, sytost=sytost,
                ryba=ryba_t, seti=seti, T=T, korovy=korovy,
                plodorodie=plodorodie, vspahano=vspahano_ga)


print('═'*78)
print('СИМУЛЯЦИЯ v4 — новый лор старта, прогон 5 лет')
print('═'*78)
print(f'\n{LYUDEY} человек · {DVORY} дворов · {PASHNYA} га пашни\n')

# Год 1: успели вспахать не всё — целина тяжёлая
vspashka = [70, 100, 120, 120, 120]   # га посева по годам
plodorod = [1.30, 1.22, 1.14, 1.08, 1.03]  # заброшенная земля тратит бонус
zapas = 0.0

print(f"{'год':>4} {'посев':>6} {'плодор':>7} {'сбор':>6} {'зерно±':>8} "
      f"{'сытость':>8} {'загруз':>7} {'осень':>6} {'рыба':>6}")
print('─'*78)

for n in range(1,6):
    r = god(n, vspashka[n-1], plodorod[n-1], zapas)
    zapas = max(0, r['zerno_bal'])
    seti = 'сети' if r['seti'] else '—'
    flag=''
    if r['zerno_bal']<0: flag='  ⚠ ГОЛОД'
    elif r['sez']['осень'][2]>115: flag='  ⚠ страда'
    print(f"{n:>4} {r['vspahano']:>6.0f} {r['plodorodie']:>7.2f} "
          f"{r['sbor']:>6.1f} {r['zerno_bal']:>+8.1f} {r['sytost']:>7.0f}% "
          f"{r['zagruzka']:>6.0f}% {r['sez']['осень'][2]:>5.0f}% {seti:>6}{flag}")

print('─'*78)

print('\n' + '═'*78)
print('ДЕТАЛЬНО: ГОД 1 — целина, сети, малый посев')
print('═'*78)
r1 = god(1, 70, 1.30, 0)
print(f"  Посеяно {r1['vspahano']:.0f} га из 120 возможных — целину не осилить разом")
print(f"  Урожайность {r1['urozhay']:.2f} т/га (базовая {BAZ_UROZHAY} × плодородие 1.30)")
print(f"  Сбор зерна {r1['sbor']:.1f} т")
print(f"  Баланс зерна {r1['zerno_bal']:+.1f} т")
print(f"  Сытость {r1['sytost']:.0f}% от нормы (ЛПХ + рыба + молоко + картофель)")
print(f"\n  СЕЗОННАЯ ЗАГРУЗКА")
for s_ in ('весна','лето','осень','зима'):
    nn,rr,z = r1['sez'][s_]
    print(f"    {s_:<6} {z:>5.0f}%  " + '█'*int(z/5))
print(f"\n  ТРУД ПО РАБОТАМ")
for k,v in sorted(r1['T'].items(), key=lambda x:-x[1]):
    if v>0: print(f"    {k:<12} {v:>6.0f} чел-дней")

print('\n' + '═'*78)
print('КРИТИЧЕСКИЙ МОМЕНТ: ГОД 4 — сети запрещены')
print('═'*78)
r3 = god(3, 120, 1.14, 0); r4 = god(4, 120, 1.08, 0)
print(f"  Год 3 (сети):     рыба {r3['ryba']:.1f} т, сытость {r3['sytost']:.0f}%")
print(f"  Год 4 (запрет):   рыба {r4['ryba']:.1f} т, сытость {r4['sytost']:.0f}%")
print(f"  Падение сытости:  {r3['sytost']-r4['sytost']:.0f} процентных пункта")
if r4['sytost'] < 105:
    print("  ⚠ После запрета сетей запас прочности почти исчезает")
else:
    print("  ✓ Хозяйство успевает встать на ноги")

print('\n' + '═'*78)
print('ПРОВЕРКА: неурожай на четвёртый год, сразу после запрета сетей')
print('═'*78)
bad = god(4, 120, 1.08, 0, urozhay_mod=0.65)
print(f"  Сбор зерна {bad['sbor']:.1f} т, баланс {bad['zerno_bal']:+.1f} т")
print(f"  Сытость {bad['sytost']:.0f}%")
if bad['zerno_bal']<0:
    print(f"  ⚠ ГОЛОД. Не хватает {-bad['zerno_bal']:.1f} т")
    print("  → Без переходящего запаса или помощи района не выжить")
print('═'*78)
