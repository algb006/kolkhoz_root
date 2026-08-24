#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Демографическая модель по когортам.
Проверяем: какое ускорение жизни и какая рождаемость дают
500 жителей к Эпохе II и 1500 к Эпохе III за разумное время.
"""

def simulate(uskorenie=3.0, migraciya=8, let=40, start=80,
             rozhd_ep1=6.5, rozhd_ep2=4.0, rozhd_ep3=2.2,
             det_smert_ep1=0.28, det_smert_ep2=0.12, det_smert_ep3=0.04,
             ottok_ep3=0.015, verbose=False):
    """
    uskorenie — во сколько раз быстрее идёт жизненный цикл.
    Женщина рожает не за 20 лет фертильности, а за 20/uskorenie игровых лет.
    """
    # Когорты по возрасту, шаг 1 игровой год = uskorenie лет жизни
    # Структура: список численностей по «возрастным ступеням»
    STUPENEY = int(60/uskorenie)     # от 0 до 60 лет
    FERT_OT = int(18/uskorenie)      # начало фертильности
    FERT_DO = int(40/uskorenie)      # конец
    TRUD_OT = int(16/uskorenie)
    TRUD_DO = int(60/uskorenie)

    koh = [0.0]*(STUPENEY+1)
    # Стартовое распределение
    for i in range(STUPENEY+1):
        if i < int(7/uskorenie): koh[i]=start*0.152/max(1,int(7/uskorenie))
        elif i < int(16/uskorenie): koh[i]=start*0.220/max(1,int(9/uskorenie))
        elif i < TRUD_DO: koh[i]=start*0.492/max(1,TRUD_DO-int(16/uskorenie))
        else: koh[i]=start*0.136/max(1,STUPENEY+1-TRUD_DO)

    rows=[]
    ep=1
    for god in range(1, let+1):
        n=sum(koh)
        # Эпоха по населению
        if n>=500 and ep==1: ep=2
        if n>=1200 and ep==2: ep=3
        rozhd = {1:rozhd_ep1,2:rozhd_ep2,3:rozhd_ep3}[ep]
        smert = {1:det_smert_ep1,2:det_smert_ep2,3:det_smert_ep3}[ep]

        # Женщины фертильного возраста
        zhen_fert = sum(koh[FERT_OT:FERT_DO])*0.5
        fert_stupeney = max(1, FERT_DO-FERT_OT)
        # Рождений за игровой год: полный набор детей размазан на фертильный период
        rozhdeniy = zhen_fert * rozhd / fert_stupeney
        vyzhilo = rozhdeniy*(1-smert)

        # Сдвиг когорт
        koh = [vyzhilo] + koh[:-1]
        # Смертность взрослых
        for i in range(len(koh)):
            vozrast = i*uskorenie
            if vozrast>=60: koh[i]*=0.80
            elif vozrast>=45: koh[i]*=0.985
            else: koh[i]*=0.997
        # Отток в третьей эпохе
        if ep==3:
            for i in range(TRUD_OT, TRUD_DO):
                koh[i]*=(1-ottok_ep3)
        # Миграция — во взрослые когорты
        if migraciya:
            mig_na_stupen = migraciya/max(1,(TRUD_DO-TRUD_OT))
            for i in range(TRUD_OT, TRUD_DO): koh[i]+=mig_na_stupen

        n_new=sum(koh)
        deti = sum(koh[:int(16/uskorenie)])
        trud = sum(koh[TRUD_OT:TRUD_DO])
        stariki = sum(koh[TRUD_DO:])
        rows.append(dict(god=god,n=n_new,ep=ep,deti=deti,trud=trud,
                         stariki=stariki,rozhd=rozhdeniy,
                         semya=rozhd))
    return rows


print('═'*80)
print('ДЕМОГРАФИЧЕСКАЯ МОДЕЛЬ: подбор ускорения')
print('═'*80)
print('\nЦели: 500 к Эпохе II, 1500 к Эпохе III\n')

print(f"{'ускор':>6}{'мигр':>6}{'год→500':>10}{'год→1000':>11}{'год→1500':>11}{'нас. к 40 г':>13}")
print('─'*80)
for u in (2,3,4,5):
    for m in (0,8,15):
        r=simulate(uskorenie=u, migraciya=m, let=60)
        g500=next((x['god'] for x in r if x['n']>=500), None)
        g1000=next((x['god'] for x in r if x['n']>=1000), None)
        g1500=next((x['god'] for x in r if x['n']>=1500), None)
        n40=r[39]['n']
        print(f"{u:>5}×{m:>6}{str(g500 or '—'):>10}{str(g1000 or '—'):>11}"
              f"{str(g1500 or '—'):>11}{n40:>13.0f}")
print('─'*80)
