# Всё, что делается руками больше одного раза. Список целей: make help

BUILD_DIR ?= build
BUILD_TYPE ?= Debug
JOBS ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu)
UPSTREAM := $(shell cat .upstream)

# на маке Qt из brew сам не находится
QT_PREFIX ?= $(shell brew --prefix qt 2>/dev/null)
CMAKE_FLAGS := -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
ifneq ($(QT_PREFIX),)
CMAKE_FLAGS += -DCMAKE_PREFIX_PATH=$(QT_PREFIX)
endif

ifeq ($(shell uname),Darwin)
RUN := open $(BUILD_DIR)/morph.app
else
RUN := ./$(BUILD_DIR)/morph
endif

.DEFAULT_GOAL := help

.PHONY: help
help: ## эта справка
	@grep -E '^[a-z-]+:.*## ' $(MAKEFILE_LIST) | awk -F':.*## ' '{printf "  %-12s %s\n", $$1, $$2}'

.PHONY: configure
configure: ## сгенерировать проект сборки
	cmake -S . -B $(BUILD_DIR) $(CMAKE_FLAGS)

.PHONY: build
build: configure ## собрать
	cmake --build $(BUILD_DIR) -j$(JOBS)

.PHONY: run
run: build ## собрать и запустить
	$(RUN)

.PHONY: test
test: build ## собрать и прогнать тесты
	ctest --test-dir $(BUILD_DIR) --output-on-failure

.PHONY: dmg
dmg: ## релизная сборка и morph.dmg (macOS)
	$(MAKE) build BUILD_DIR=build-release BUILD_TYPE=Release
	cmake --build build-release --target dmg

.PHONY: fmt
fmt: ## отформатировать строки, изменённые относительно upstream
	git clang-format --extensions cpp,h,mm $(UPSTREAM)

.PHONY: fmt-check
fmt-check: ## то же, что проверяет CI
	scripts/check-format.sh $(UPSTREAM)

.PHONY: tidy
tidy: configure ## clang-tidy по изменённым файлам
	scripts/tidy-changed.sh $(BUILD_DIR) $(UPSTREAM)

.PHONY: hooks
hooks: ## включить git-хуки из .githooks
	git config core.hooksPath .githooks

.PHONY: clean
clean: ## удалить сборки
	rm -rf $(BUILD_DIR) build-release
