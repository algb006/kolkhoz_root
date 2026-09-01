# Kolkhoz Chairman — simulation core.
#
# Thin driver over CMake + Ninja. The build system is CMake; this file only
# spells the commands so nobody has to remember them. See README.txt.

.DEFAULT_GOAL := help
.PHONY: help configure build release rebuild asan clean distclean test unit \
        check-tests format format-check tidy docs sync win win-release win-clean \
        win-setup hooks deps info version bump-patch bump-minor

# --- Settings ---------------------------------------------------------------

BUILD_DIR   ?= build
TYPE        ?= Debug
GENERATOR   ?= Ninja
CXX_COMPILER?= clang++
C_COMPILER  ?= clang
JOBS        ?= $(shell nproc 2>/dev/null || echo 4)
SANITIZERS  ?=

# Windows host for the MSVC build (see CLAUDE.md section 12).
WIN_HOST    ?= win
WIN_DIR     ?= /c/MyGames/Kolkhoz/core-msvc

CMAKE_FLAGS = -S . -B $(BUILD_DIR) -G $(GENERATOR) \
              -DCMAKE_BUILD_TYPE=$(TYPE) \
              -DCMAKE_C_COMPILER=$(C_COMPILER) \
              -DCMAKE_CXX_COMPILER=$(CXX_COMPILER) \
              -DKOLKHOZ_SANITIZERS="$(SANITIZERS)"

SOURCE_GLOBS = include src subprojects tests

# --- Targets ----------------------------------------------------------------

help:
	@echo 'Ядро симуляции — сборка и обслуживание проекта.'
	@echo ''
	@echo '  make build          собрать (Debug), настроив при необходимости'
	@echo '  make release        собрать Release'
	@echo '  make rebuild        собрать с нуля — при странном поведении первым делом'
	@echo '  make test           прогнать все тесты через ctest'
	@echo '  make unit           только обязательные unit-тесты модулей'
	@echo '  make check-tests    проверить, что на каждый модуль есть unit-тест'
	@echo '  make version        версии ядра и формата сохранений'
	@echo '  make bump-patch     поднять версию: сдан модуль'
	@echo '  make bump-minor     поднять версию: сдан этап плана либо сломана граница с UE'
	@echo '  make asan           собрать с санитайзерами address+undefined'
	@echo '  make clean          удалить объектные файлы, конфигурацию оставить'
	@echo '  make distclean      удалить каталог сборки целиком'
	@echo ''
	@echo '  make format         применить .clang-format ко всем исходникам'
	@echo '  make format-check   проверить форматирование, ничего не меняя'
	@echo '  make tidy           статический анализ clang-tidy'
	@echo '  make docs           выжимка Doxygen в $(BUILD_DIR)/doc'
	@echo ''
	@echo '  make sync           отправить исходники на Windows-хост ($(WIN_HOST))'
	@echo '  make win            собрать и опубликовать на хосте под MSVC (Debug)'
	@echo '  make win-release    то же, Release — это берёт слой графики'
	@echo '  make win-clean      снести каталог сборки Debug на хосте и собрать заново'
	@echo '  make win-setup      что и как настроить на хосте в первый раз'
	@echo ''
	@echo '  make hooks          включить git-хуки из scripts/git-hooks'
	@echo '  make deps           показать, что доустановить в системе'
	@echo '  make info           текущие настройки сборки'
	@echo ''
	@echo 'Переменные: TYPE=Debug|Release  BUILD_DIR=$(BUILD_DIR)  JOBS=$(JOBS)'

configure:
	cmake $(CMAKE_FLAGS)

build: configure
	cmake --build $(BUILD_DIR) -j $(JOBS)

release:
	@$(MAKE) --no-print-directory TYPE=Release build

rebuild: distclean build

asan:
	@$(MAKE) --no-print-directory SANITIZERS='address;undefined' rebuild

clean:
	@test -d $(BUILD_DIR) && cmake --build $(BUILD_DIR) --target clean || \
	  echo 'Каталог сборки отсутствует — чистить нечего.'

distclean:
	rm -rf $(BUILD_DIR)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure -j $(JOBS)

# Only the obligatory per-module tests, without the long simulation runs.
unit: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure -j $(JOBS) -L unit

check-tests:
	@./scripts/check_module_tests.sh
	@python3 scripts/check_includes.py

# Bumping is the closing step of a delivery cycle, run in the same commit as the
# work being delivered. VERSION_SAVE is not bumped here — see 57-versioning.md.
bump-patch:
	@./scripts/bump_version.sh patch

bump-minor:
	@./scripts/bump_version.sh minor

# There is no bump-major target: the major is frozen at zero until the game
# ships (manual/setup/57-versioning.md §2). A broken boundary is a minor.

version:
	@echo 'Версия ядра            : '$$(cat VERSION)'   (файл VERSION)'
	@echo 'Формат сохранений      : '$$(cat VERSION_SAVE)'       (файл VERSION_SAVE)'
	@echo 'Заголовок для кода     : core_common/version.h, генерируется при настройке'
	@echo 'Когда что поднимать    : manual/setup/57-versioning.md'

# --- Code hygiene -----------------------------------------------------------

format:
	@files=$$(find $(SOURCE_GLOBS) -type f \( -name '*.h' -o -name '*.cpp' \
	  -o -name '*.inl' \) 2>/dev/null); \
	if [ -z "$$files" ]; then echo 'Исходников пока нет.'; else \
	  echo "$$files" | xargs clang-format -i --style=file && echo 'Отформатировано.'; fi

format-check:
	@files=$$(find $(SOURCE_GLOBS) -type f \( -name '*.h' -o -name '*.cpp' \
	  -o -name '*.inl' \) 2>/dev/null); \
	if [ -z "$$files" ]; then echo 'Исходников пока нет.'; else \
	  echo "$$files" | xargs clang-format --dry-run --Werror --style=file && \
	  echo 'Форматирование в порядке.'; fi

tidy: configure
	@files=$$(find $(SOURCE_GLOBS) -type f -name '*.cpp' 2>/dev/null); \
	if [ -z "$$files" ]; then echo 'Исходников пока нет.'; else \
	  echo "$$files" | xargs clang-tidy -p $(BUILD_DIR); fi

docs:
	@command -v doxygen >/dev/null || { echo 'doxygen не установлен: make deps'; exit 1; }
	@mkdir -p $(BUILD_DIR)/doc
	doxygen Doxyfile
	@echo 'Готово: $(BUILD_DIR)/doc/html/index.html'

# --- Windows host -----------------------------------------------------------

# All four go through one script: the rsync flags and the remote invocation are
# fiddly enough that a second copy of them would drift.
#
# Each configuration has its own build directory on the host and its own
# publish/<Config>/ folder — include, lib/core.lib (enkiTS merged in) and
# VERSION beside it. The graphics layer takes Release; Debug is for our runs.
# manual/setup/60-windows-host.md §6а.
WIN_ENV = WIN_HOST=$(WIN_HOST) WIN_DIR=$(WIN_DIR)

sync:
	@$(WIN_ENV) ./scripts/win-build.sh --sync-only

win:
	@$(WIN_ENV) ./scripts/win-build.sh

win-release:
	@$(WIN_ENV) ./scripts/win-build.sh --release

win-clean:
	@$(WIN_ENV) ./scripts/win-build.sh --clean

win-setup:
	@echo 'Первичная настройка Windows-хоста — manual/setup/60-windows-host.md.'
	@echo ''
	@echo 'Скрипт: scripts/win-setup.ps1, запускается на хосте из PowerShell'
	@echo 'от администратора. Переносится так — на виртуалке:'
	@echo ''
	@echo '  python3 -m http.server 8000 --directory scripts'
	@echo ''
	@echo 'на хосте, в PowerShell от администратора:'
	@echo ''
	@echo '  irm http://$(shell hostname -I 2>/dev/null | awk "{print \$$1}"):8000/win-setup.ps1 -OutFile $$env:TEMP\win-setup.ps1'
	@echo '  powershell -NoProfile -ExecutionPolicy Bypass -File $$env:TEMP\win-setup.ps1 -PublicKey "<ключ ниже>"'
	@echo ''
	@echo 'Публичный ключ этой машины:'
	@if [ -f ~/.ssh/id_ed25519_win.pub ]; then cat ~/.ssh/id_ed25519_win.pub; \
	 else echo '  НЕТ — создать: ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519_win -N ""'; fi

# --- Environment ------------------------------------------------------------

hooks:
	git config core.hooksPath scripts/git-hooks
	@chmod +x scripts/git-hooks/* 2>/dev/null || true
	@echo 'Git-хуки включены: scripts/git-hooks'

deps:
	@echo 'AlmaLinux 10 — подключить EPEL и CRB, затем:'
	@echo ''
	@echo '  sudo dnf install -y epel-release'
	@echo '  sudo dnf config-manager --set-enabled crb'
	@echo '  sudo dnf install -y clang clang-tools-extra lld cmake ninja-build \'
	@echo '                      ccache doxygen git rsync'
	@echo ''
	@echo 'Проверка после установки: make info'

info:
	@echo 'Версия ядра    : '$$(cat VERSION)' (сохранения: '$$(cat VERSION_SAVE)')'
	@echo 'Каталог сборки : $(BUILD_DIR)'
	@echo 'Тип сборки     : $(TYPE)'
	@echo 'Генератор      : $(GENERATOR)'
	@echo 'Компилятор     : $(CXX_COMPILER)'
	@echo 'Параллельность : $(JOBS)'
	@echo 'Санитайзеры    : $(if $(SANITIZERS),$(SANITIZERS),нет)'
	@echo 'Windows-хост   : $(WIN_HOST):$(WIN_DIR)'
	@echo ''
	@for tool in $(CXX_COMPILER) cmake ninja ccache clang-format clang-tidy doxygen; do \
	  if command -v $$tool >/dev/null; then printf '  %-14s есть\n' $$tool; \
	  else printf '  %-14s НЕТ\n' $$tool; fi; done
