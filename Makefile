# Всё, что делается руками больше одного раза. Список целей: make help

BUILD_DIR ?= build
BUILD_TYPE ?= Debug
JOBS ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu)
UPSTREAM := $(shell cat .upstream)
DOCKER_IMAGE ?= morph-dev

# на маке Qt и OpenSSL из brew сами не находятся
QT_PREFIX ?= $(shell brew --prefix qt 2>/dev/null)
OPENSSL_PREFIX ?= $(shell brew --prefix openssl@3 2>/dev/null)
CMAKE_FLAGS := -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
ifneq ($(QT_PREFIX),)
CMAKE_FLAGS += -DCMAKE_PREFIX_PATH=$(QT_PREFIX)
endif
ifneq ($(OPENSSL_PREFIX),)
CMAKE_FLAGS += -DOPENSSL_ROOT_DIR=$(OPENSSL_PREFIX)
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

.PHONY: ci
ci: ## всё, что гоняет CI: формат, сборка, тесты, tidy
	scripts/check-format.sh $(UPSTREAM)
	$(MAKE) test BUILD_DIR=build-ci
	scripts/tidy-changed.sh build-ci $(UPSTREAM)

.PHONY: docker-image
docker-image: ## собрать образ среды сборки
	docker build -t $(DOCKER_IMAGE) -f docker/Dockerfile $(if $(DOCKER_BASE),--build-arg BASE_IMAGE=$(DOCKER_BASE)) .

.PHONY: docker-ci
docker-ci: ## make ci внутри образа, как в CI
	docker run --rm -u $$(id -u):$$(id -g) -e HOME=/tmp -v "$(CURDIR)":/src $(DOCKER_IMAGE) make ci

.PHONY: hooks
hooks: ## включить git-хуки из .githooks
	git config core.hooksPath .githooks

.PHONY: clean
clean: ## удалить сборки
	rm -rf $(BUILD_DIR) build-release build-ci
