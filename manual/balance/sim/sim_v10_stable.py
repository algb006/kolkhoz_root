#!/usr/bin/env python3
"""ПРОГОН v10: вместимость колхозного двора по ступеням.

Вопрос: сколько лошадей держит каждая ступень юнита «колхозный двор» и какой
площадью пашни этот табун управляется. Вместимость конюшни — потолок развития
в Эпохе I: приплод прекращается, когда ставить некуда.

Модель тягла взята из прогонов v7/v9 без изменений; новое здесь только то, что
плечо логистики растёт с площадью хозяйства как корень из неё.
"""

# ─── Канон ───
HORSES_START = 16          # epochs.md §3
PASHNYA_START_GA = 160.0   # CLAUDE.md §9
AKTIVNAYA_GA = 120.0       # 49-simulations §2: активная пашня первого года
WORK_DAYS_YEAR = 41        # CLAUDE.md §9
GA_NA_EDOKA = (2.0, 2.5)   # 49-simulations §2: норма зрелого хозяйства
# Норма зависит от эпохи: чем больше стол закрывает ЛПХ, тем меньше нужно
# колхозной пашни на едока. ЛПХ закрывает 59% потребности, с молоком 90%
# (household-farming.md §1), и доля эта падает от эпохи к эпохе.
NORMA_PO_EPOHAM = {"I": 1.1, "II": 1.75, "III": 2.25}   # решение человека, v10
VEHI = {"старт": 80, "село не пустое": 200, "год 11": 370, "Эпоха II": 500}

# ─── Допущения, унаследованные от v7/v9 ───
HORSE_WORK_H = 10.0        # ASSUMPTION (v7): рабочих часов лошади в сутки
PLOUGHINGS = 1.6           # ASSUMPTION (v7): весенняя вспашка плюс частичная зябь
PLOUGH_HA_DAY = 0.9        # ASSUMPTION (v7): га за лошаде-день
LOGISTIKA_START_H = 3983.0 # v9 §8: годовая логистика на стартовых плечах
ZAPAS = 0.85               # ASSUMPTION: рабочая загрузка табуна, выше — год без запаса

RESURS_LOSHADI = WORK_DAYS_YEAR * HORSE_WORK_H          # лошаде-часов в год
AKTIV_SHARE = AKTIVNAYA_GA / PASHNYA_START_GA
PLOUGH_H_GA = PLOUGHINGS / PLOUGH_HA_DAY * HORSE_WORK_H  # лошаде-часов на га активной

line = lambda: print("-" * 78)
def h(t): print("\n" + t + "\n" + "-" * 78)

print("ПРОГОН v10: ВМЕСТИМОСТЬ КОЛХОЗНОГО ДВОРА ПО СТУПЕНЯМ")
print("Ресурс лошади: %d раб.суток x %.0f ч = %.0f лошаде-часов в год"
      % (WORK_DAYS_YEAR, HORSE_WORK_H, RESURS_LOSHADI))


def chasy(ga):
    """Лошаде-часов в год на хозяйство с пашней ga."""
    vspashka = PLOUGH_H_GA * AKTIV_SHARE * ga
    # тоннаж линеен по площади, плечо растёт как корень из неё
    logistika = LOGISTIKA_START_H * (ga / PASHNYA_START_GA) ** 1.5
    return vspashka, logistika


def loshadey(ga):
    v, l = chasy(ga)
    return (v + l) / RESURS_LOSHADI


def ga_po_loshadyam(n, zapas=ZAPAS):
    """Обратная задача: какую пашню тянет табун из n голов при загрузке zapas."""
    lo, hi = 1.0, 20000.0
    for _ in range(200):
        mid = (lo + hi) / 2
        if loshadey(mid) > n * zapas:
            hi = mid
        else:
            lo = mid
    return (lo + hi) / 2


# ══════════════════════════════════════════ 1
h("1. Сверка модели со стартом")
v, l = chasy(PASHNYA_START_GA)
print("Вспашка %.0f га x %.1f прохода: %.0f лошаде-часов" % (AKTIVNAYA_GA, PLOUGHINGS, v))
print("Логистика на стартовых плечах:  %.0f лошаде-часов" % l)
print("Итого %.0f, это %.1f лошади из %d — загрузка %.0f%%"
      % (v + l, loshadey(PASHNYA_START_GA), HORSES_START,
         100 * loshadey(PASHNYA_START_GA) / HORSES_START))
print("Сверка с v9: там вышло 93%%. Расхождение %.0f п.п."
      % abs(93 - 100 * loshadey(PASHNYA_START_GA) / HORSES_START))

# ══════════════════════════════════════════ 2
h("2. Сколько лошадей требует хозяйство по мере роста")
print("%-10s%10s%12s%12s%12s%12s" % ("га пашни", "едоков*", "вспашка", "логистика",
                                     "лошадей", "мест при 85%"))
for ga in (160, 250, 400, 550, 700, 1000, 1250):
    v, l = chasy(ga)
    n = loshadey(ga)
    print("%-10d%10d%12.0f%12.0f%12.1f%12.0f"
          % (ga, ga / GA_NA_EDOKA[0], v, l, n, n / ZAPAS + 0.5))
print("* едоков при норме %.1f га на едока" % GA_NA_EDOKA[0])

# ══════════════════════════════════════════ 3
h("3. Демографические вехи против тягла")
print("%-22s%8s%10s%14s%14s" % ("веха", "жителей", "га (2.0)", "лошадей", "мест при 85%"))
for name, ppl in VEHI.items():
    ga = ppl * GA_NA_EDOKA[0]
    n = loshadey(ga)
    print("%-22s%8d%10.0f%14.1f%14.0f" % (name, ppl, ga, n, n / ZAPAS + 0.5))

# ══════════════════════════════════════════ 4
h("4. Что тянет каждая предлагаемая ступень")
STUPENI = [
    ("1. Летний двор (старт)", 20),
    ("2. Конюшня, утеплённая (Эпоха I)", 60),
    ("3. Конюшня, Эпоха II", 120),
]
print("%-36s%7s%10s%10s%12s" % ("ступень", "мест", "га", "едоков", "загрузка"))
for name, mest in STUPENI:
    ga = ga_po_loshadyam(mest)
    print("%-36s%7d%10.0f%10.0f%11.0f%%"
          % (name, mest, ga, ga / GA_NA_EDOKA[0], 100 * ZAPAS))

# ══════════════════════════════════════════ 5
h("5. Проверка первой ступени: помещается ли стартовый табун")
for mest in (18, 20, 24):
    zapas_golov = mest - HORSES_START
    ga = ga_po_loshadyam(mest)
    print("%d мест: запас на приплод %d голов, тянет %.0f га (старт %.0f га)"
          % (mest, zapas_golov, ga, PASHNYA_START_GA))

# ══════════════════════════════════════════ 6
h("6. Шаг между ступенями")
prev = None
for name, mest in STUPENI:
    ga = ga_po_loshadyam(mest)
    if prev is None:
        print("%-36s%7d мест%10.0f га" % (name, mest, ga))
    else:
        print("%-36s%7d мест%10.0f га   x%.1f по местам, x%.1f по пашне"
              % (name, mest, ga, mest / prev[0], ga / prev[1]))
    prev = (mest, ga)

# ══════════════════════════════════════════ 7
h("7. Где барьер бьёт")
ga2 = ga_po_loshadyam(60)
ga3 = ga_po_loshadyam(120)
print("Ступень 2 (60 мест) держит %.0f га — это %.0f жителей при 2.0 га на едока."
      % (ga2, ga2 / GA_NA_EDOKA[0]))
print("Веха «село не пустое» — 200 жителей, 7-й год: %.0f га, нужно %.0f мест."
      % (200 * GA_NA_EDOKA[0], loshadey(200 * GA_NA_EDOKA[0]) / ZAPAS + 0.5))
print("Веха «Эпоха II» — 500 жителей: %.0f га, нужно %.0f мест."
      % (500 * GA_NA_EDOKA[0], loshadey(500 * GA_NA_EDOKA[0]) / ZAPAS + 0.5))
print()
print("Вывод: на одном тягле до 500 жителей не дойти — потребовалось бы")
print("%.0f мест в одной конюшне. Барьер снимается не конюшней, а техникой."
      % (loshadey(500 * GA_NA_EDOKA[0]) / ZAPAS + 0.5))
print("Ступень 3 в %d мест держит %.0f га; остальное в Эпохе II делают трактора."
      % (120, ga3))

# ══════════════════════════════════════════ 8
h("8. Норма га на едока зависит от эпохи")
print("Решено: в Эпохе I норма ниже, потому что стол закрывает ЛПХ.")
print("ЛПХ даёт 59% пищевой потребности, с колхозным молоком 90% (household-farming.md §1).")
print()
print("%-8s%12s%14s%16s" % ("эпоха", "га на едока", "доля ЛПХ", "чем задана"))
print("%-8s%12.2f%14s%16s" % ("I", NORMA_PO_EPOHAM["I"], "наибольшая", "ЛПХ кормит"))
print("%-8s%12.2f%14s%16s" % ("II", NORMA_PO_EPOHAM["II"], "падает", "дрейф в колхоз"))
print("%-8s%12.2f%14s%16s" % ("III", NORMA_PO_EPOHAM["III"], "наименьшая", "налог на ЛПХ"))

h("9. Ступени против нормы своей эпохи")
print("%-36s%7s%9s%12s%12s" % ("ступень", "мест", "га", "норма", "жителей"))
for (name, mest), epoha in zip(STUPENI, ("I", "I", "II")):
    ga = ga_po_loshadyam(mest)
    norma = NORMA_PO_EPOHAM[epoha]
    print("%-36s%7d%9.0f%12.2f%12.0f" % (name, mest, ga, norma, ga / norma))
print()
print("Третья ступень кормит не больше второй — норма выросла вместе с ней.")
print("Это и правильно: в Эпохе II пашню поднимают трактора, а конюшня перестаёт")
print("быть тем, во что упирается хозяйство.")

h("10. Куда теперь попадает потолок Эпохи I")
ga2 = ga_po_loshadyam(60)
zhiteley = ga2 / NORMA_PO_EPOHAM["I"]
print("Ступень 2: %.0f га при норме %.2f — это %.0f жителей."
      % (ga2, NORMA_PO_EPOHAM["I"], zhiteley))
print("Демография: год 11 — 370 жителей, последний год Эпохи I.")
print("Расхождение: %.0f человек (%.0f%%)." % (abs(zhiteley - 370), 100 * abs(zhiteley - 370) / 370))
print()
print("Потолок конюшни приходится ровно на конец первой эпохи: табун упирается")
print("в стойла тогда же, когда хозяйство готово менять эпоху. Дальше — трактора.")

h("11. Проверка третьей эпохи не сдвинулась")
for celi, norma in ((1500, NORMA_PO_EPOHAM["III"]), (1500, GA_NA_EDOKA[1])):
    print("%d жителей x %.2f га = %.0f га пашни (%.0f%% пашни карты 4500 га)"
          % (celi, norma, celi * norma, 100 * celi * norma / 4500))
