#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Председатель колхоза — симуляция стартового поселения, версия 1.
Цель: проверить, сходится ли баланс труда, еды и топлива в Эпохе I.
Все параметры — гипотезы, помеченные как ГИПОТЕЗА.
"""

# ═══════════════════════════════════════════════════════════
# 1. НАСЕЛЕНИЕ
# ═══════════════════════════════════════════════════════════

DVORY = 30                    # ГИПОТЕЗА: дворов на старте
LYUDEY = 118                  # ГИПОТЕЗА: жителей

# Возрастная структура сельского населения
DETI_0_6   = 18   # дошкольники
DETI_7_15  = 26   # школьники (частично работают летом)
VZROSLYE   = 58   # 16-59, основная рабочая сила
STARIKI    = 16   # 60+, работают ограниченно

assert DETI_0_6 + DETI_7_15 + VZROSLYE + STARIKI == LYUDEY

# Коэффициенты трудоспособности
K_VZROSLY  = 1.00
K_STARIK   = 0.45   # ГИПОТЕЗА: посильные работы
K_PODROSTOK= 0.30   # ГИПОТЕЗА: только в страду и каникулы
K_BOLEZN   = 0.92   # ГИПОТЕЗА: потери на болезни, декрет, прочее

trudosposobnye = (VZROSLYE * K_VZROSLY
                  + STARIKI * K_STARIK
                  + DETI_7_15 * K_PODROSTOK) * K_BOLEZN

# ═══════════════════════════════════════════════════════════
# 2. РАБОЧЕЕ ВРЕМЯ ЗА ГОД
# ═══════════════════════════════════════════════════════════
# Рабочий день по солнцу, выходные, праздники, распутица

RABOCHIH_DNEY = 270           # ГИПОТЕЗА: за вычетом выходных, праздников, распутицы
POTERI_DOROGA = 0.88          # ГИПОТЕЗА: потери на дорогу до места работы
POTERI_POGODA = 0.90          # ГИПОТЕЗА: дожди, срывы

chelovekodni_god = trudosposobnye * RABOCHIH_DNEY * POTERI_DOROGA * POTERI_POGODA

# ═══════════════════════════════════════════════════════════
# 3. ЗЕМЛЯ И ПОСЕВЫ
# ═══════════════════════════════════════════════════════════

PASHNYA = 180.0   # ГИПОТЕЗА: га пашни на старте (маленькое хозяйство)
DOLYA_PARA = 0.25 # четверть под паром

posev = PASHNYA * (1 - DOLYA_PARA)

# Структура посева
ZERNO_GA     = posev * 0.62
KARTOFEL_GA  = posev * 0.18
LEN_GA       = posev * 0.08
KORMOVYE_GA  = posev * 0.12

# Урожайность, Эпоха I, без удобрений, Нечерноземье
U_ZERNO    = 0.85   # т/га  ГИПОТЕЗА
U_KARTOFEL = 9.0    # т/га  ГИПОТЕЗА
U_LEN      = 0.35   # т/га волокна+семя ГИПОТЕЗА
U_SENO     = 1.6    # т/га с луга ГИПОТЕЗА

LUGA_GA = 90.0      # ГИПОТЕЗА: сенокосы

sbor_zerno    = ZERNO_GA * U_ZERNO
sbor_kartofel = KARTOFEL_GA * U_KARTOFEL
sbor_len      = LEN_GA * U_LEN
sbor_seno     = LUGA_GA * U_SENO

# ═══════════════════════════════════════════════════════════
# 4. ТРУДОЁМКОСТЬ (человеко-дни)
# ═══════════════════════════════════════════════════════════
# Ручной труд + конная тяга

T = {}
# Зерновые: пахота, сев, уборка серпом/косой, вязка, свозка, молотьба цепами
T['зерно_пахота']   = ZERNO_GA * 2.2    # ГИПОТЕЗА чел-дней/га
T['зерно_сев']      = ZERNO_GA * 0.5
T['зерно_уборка']   = ZERNO_GA * 4.0
T['зерно_молотьба'] = sbor_zerno * 3.0  # чел-дней на тонну

# Картофель: очень трудоёмок
T['картофель_посадка'] = KARTOFEL_GA * 6.0
T['картофель_уход']    = KARTOFEL_GA * 5.0
T['картофель_уборка']  = KARTOFEL_GA * 14.0

# Лён: самая трудоёмкая культура
T['лён'] = LEN_GA * 30.0

# Кормовые
T['кормовые'] = KORMOVYE_GA * 3.5

# Сенокос: косьба, сушка, стогование, свозка
T['сенокос'] = LUGA_GA * 3.2

# Животноводство
KOROVY = 34    # ГИПОТЕЗА: сданных коров
LOSHADI = 12   # ГИПОТЕЗА
SVINI = 20
OVCY = 25

T['скот'] = (KOROVY * 9.0 + LOSHADI * 5.0 + SVINI * 2.5 + OVCY * 1.5)  # чел-дней/голову в год

# Дрова: заготовка и колка
DOMA_OTAPLIVAT = DVORY + 6   # дворы плюс юниты
DROVA_M3 = DOMA_OTAPLIVAT * 12.0   # ГИПОТЕЗА: м³ на объект за зиму
T['дрова'] = DROVA_M3 * 0.55       # ГИПОТЕЗА: чел-дней на м³ (рубка, колка, свозка)

# Прочее: ремонт, стройка, обслуживание, подвоз
T['прочее'] = chelovekodni_god * 0.14

vsego_trud = sum(T.values())

# ═══════════════════════════════════════════════════════════
# 5. ПОТРЕБЛЕНИЕ
# ═══════════════════════════════════════════════════════════

ZERNO_NA_CHELOVEKA = 0.24   # т/год ГИПОТЕЗА: хлеб, крупа
KARTOFEL_NA_CHELOVEKA = 0.35

potreb_zerno = LYUDEY * ZERNO_NA_CHELOVEKA
potreb_kartofel = LYUDEY * KARTOFEL_NA_CHELOVEKA

# Семенной фонд
semena_zerno = ZERNO_GA * 0.18      # т/га ГИПОТЕЗА
semena_kartofel = KARTOFEL_GA * 2.5

# Корма
korm_zerno = LOSHADI * 0.45 + SVINI * 0.25   # овёс лошадям, концентраты свиньям
seno_nuzhno = KOROVY * 1.8 + LOSHADI * 1.8 + OVCY * 0.35

# План сдачи государству
PLAN_ZERNO_DOLYA = 0.28   # ГИПОТЕЗА: доля валового сбора
plan_zerno = sbor_zerno * PLAN_ZERNO_DOLYA

# ═══════════════════════════════════════════════════════════
# ВЫВОД
# ═══════════════════════════════════════════════════════════

def line(c='─', n=62): return c*n

print(line('═'))
print('СИМУЛЯЦИЯ СТАРТОВОГО ПОСЕЛЕНИЯ — ЭПОХА I, первый год')
print(line('═'))

print(f"\nНАСЕЛЕНИЕ: {LYUDEY} человек, {DVORY} дворов")
print(f"  дошкольники {DETI_0_6} · школьники {DETI_7_15} · "
      f"взрослые {VZROSLYE} · старики {STARIKI}")
print(f"  Приведённых работников: {trudosposobnye:.1f}")

print(f"\nТРУДОВОЙ РЕСУРС: {chelovekodni_god:,.0f} человеко-дней в год".replace(',', ' '))

print(f"\nЗЕМЛЯ: пашня {PASHNYA:.0f} га (посев {posev:.0f}), луга {LUGA_GA:.0f} га")
print(f"  зерно {ZERNO_GA:.0f} · картофель {KARTOFEL_GA:.0f} · "
      f"лён {LEN_GA:.0f} · кормовые {KORMOVYE_GA:.0f}")

print(f"\n{line()}")
print("ПОТРЕБНОСТЬ В ТРУДЕ")
print(line())
for k, v in sorted(T.items(), key=lambda x: -x[1]):
    dolya = v / vsego_trud * 100
    bar = '█' * int(dolya / 2)
    print(f"  {k:<22} {v:7.0f} чел-дней  {dolya:5.1f}%  {bar}")
print(f"  {'ИТОГО':<22} {vsego_trud:7.0f} чел-дней")

balans = chelovekodni_god - vsego_trud
zagruzka = vsego_trud / chelovekodni_god * 100

print(f"\n{line()}")
print("БАЛАНС ТРУДА")
print(line())
print(f"  Ресурс:      {chelovekodni_god:8.0f} чел-дней")
print(f"  Потребность: {vsego_trud:8.0f} чел-дней")
print(f"  Баланс:      {balans:+8.0f} чел-дней   ЗАГРУЗКА {zagruzka:.0f}%")

if zagruzka > 100:
    print(f"  ⚠ ДЕФИЦИТ РУК: не хватает {-balans:.0f} чел-дней")
elif zagruzka > 92:
    print("  ⚠ На пределе: любой сбой сорвёт работы")
elif zagruzka < 70:
    print("  ⚠ Слишком свободно: людям нечем заняться")
else:
    print("  ✓ Напряжённо, но выполнимо")

print(f"\n{line()}")
print("БАЛАНС ЗЕРНА (тонн)")
print(line())
print(f"  Валовой сбор:        {sbor_zerno:7.1f}")
print(f"  − семенной фонд:     {semena_zerno:7.1f}")
print(f"  − план сдачи:        {plan_zerno:7.1f}")
print(f"  − корма:             {korm_zerno:7.1f}")
print(f"  − питание людей:     {potreb_zerno:7.1f}")
ost_zerno = sbor_zerno - semena_zerno - plan_zerno - korm_zerno - potreb_zerno
print(f"  {'ОСТАТОК':<20} {ost_zerno:+7.1f}")
if ost_zerno < 0:
    print(f"  ⚠ ГОЛОД: не хватает {-ost_zerno:.1f} т")
elif ost_zerno < sbor_zerno * 0.05:
    print("  ⚠ Впритык: неурожай станет катастрофой")
else:
    print("  ✓ Запас есть")

print(f"\n{line()}")
print("БАЛАНС КАРТОФЕЛЯ И СЕНА (тонн)")
print(line())
ost_kart = sbor_kartofel - semena_kartofel - potreb_kartofel
print(f"  Картофель: сбор {sbor_kartofel:.0f}, семена {semena_kartofel:.0f}, "
      f"еда {potreb_kartofel:.0f} → остаток {ost_kart:+.0f}")
ost_seno = sbor_seno - seno_nuzhno
print(f"  Сено:      сбор {sbor_seno:.0f}, нужно {seno_nuzhno:.0f} "
      f"→ остаток {ost_seno:+.0f}")
if ost_seno < 0:
    print(f"  ⚠ СКОТ НЕ ПЕРЕЗИМУЕТ: не хватает {-ost_seno:.0f} т сена")

print(f"\n{line()}")
print("ПИКОВАЯ НАГРУЗКА — СТРАДА (август–сентябрь, ~45 дней)")
print(line())
strada = (T['зерно_уборка'] + T['картофель_уборка'] +
          T['зерно_молотьба'] * 0.5 + T['лён'] * 0.5)
resurs_strada = trudosposobnye * 45 * POTERI_DOROGA
print(f"  Нужно в страду:  {strada:7.0f} чел-дней")
print(f"  Есть за 45 дней: {resurs_strada:7.0f} чел-дней")
defic = strada - resurs_strada
if defic > 0:
    print(f"  ⚠ ДЕФИЦИТ {defic:.0f} чел-дней — часть урожая не убрать")
    print(f"    Нужно людей дополнительно: {defic/45:.0f} человек ежедневно")
else:
    print(f"  ✓ Успевают, запас {-defic:.0f} чел-дней")

print(f"\n{line('═')}")
