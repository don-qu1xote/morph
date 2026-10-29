# Среда сборки и тестов. Сам плеер в контейнере не запускается (ни окна,
# ни звука), образ нужен, чтобы сборка, тесты и проверки шли одинаково
# локально и в CI. Почему так, в docs/adr/0006.
#
#   make docker-image   собрать образ
#   make docker-ci      прогнать в нём то же, что CI
ARG BASE_IMAGE=ubuntu:24.04
FROM ${BASE_IMAGE}

ARG DEBIAN_FRONTEND=noninteractive
# та же версия, что в scripts/check-format.sh
ARG CLANG_FORMAT_VERSION=18.1.8

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        build-essential cmake git ca-certificates make python3-pip \
        qt6-base-dev qt6-declarative-dev qt6-multimedia-dev qt6-svg-dev qt6-5compat-dev \
        libgl-dev libssl-dev clang-tidy \
 && pip install --no-cache-dir --break-system-packages "clang-format==${CLANG_FORMAT_VERSION}" \
 && rm -rf /var/lib/apt/lists/*

# исходники монтируются с хоста под другим владельцем, и без этого git
# отказывается работать с репозиторием
RUN git config --system --add safe.directory '*'

WORKDIR /src
