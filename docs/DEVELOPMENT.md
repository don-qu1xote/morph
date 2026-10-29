# Разработка

## Что поставить

macOS:

```bash
brew install qt cmake openssl yt-dlp ffmpeg deno pipx
pipx install clang-format==18.1.8
```

`deno` нужен yt-dlp: без него он больше не открывает YouTube. clang-format
закреплён на 18.1.8, потому что разные версии форматируют по-разному, и то,
что прошло локально, упало бы в CI.

Linux (Ubuntu 24.04): пакеты из `docker/Dockerfile` плюс
`pip install --user clang-format==18.1.8`.

## Каждый день

```bash
make run    # Debug-сборка и запуск
make test   # сборка и все тесты
make dmg    # Release и build-release/morph.dmg (macOS)
make help   # остальное
```

Один раз после клонирования включить хуки:

```bash
make hooks
```

pre-commit проверяет подпись коммита и формат изменённых строк.

## Формат и clang-tidy

Проверяются только строки, которые форк поменял относительно upstream
(коммит в файле `.upstream`). Весь upstream под `.clang-format` не
переписываем, почему так, в [ADR-0001](adr/0001-format-only-changed-lines.md).

```bash
make fmt        # отформатировать свои изменения
make fmt-check  # то же, что проверяет CI
make tidy       # clang-tidy по изменённым строкам
```

## Docker

Образ это среда сборки, плеер в нём не запускается
([ADR-0006](adr/0006-docker-as-build-environment.md)).

```bash
make docker-image
make docker-ci      # формат, сборка, тесты и tidy, как в CI
```

## Тесты

Лежат в `tests/`, каждый тест отдельный бинарник на QtTest. Сервисы ходят не
в настоящие API, а в локальный `FakeHttpServer` (`tests/support/`): адреса
подменяются переменными окружения.

| Переменная | По умолчанию |
|---|---|
| `MORPH_YANDEX_API` | `https://api.music.yandex.net` |
| `MORPH_SOUNDCLOUD_API` | `https://api-v2.soundcloud.com` |

yt-dlp подменяется shell-скриптом через `FakeTools`, так тесты не зависят от
сети и от того, что установлено на машине.

Те же переменные годятся, чтобы посмотреть трафик приложения через свой
прокси: `MORPH_YANDEX_API=http://127.0.0.1:8080 ./build/morph`.

## Обновление с upstream

```bash
git remote add upstream https://github.com/lizapropanol/morph.git   # один раз
git fetch upstream
git merge upstream/main
git rev-parse upstream/main > .upstream
git commit -am "[MORPH-SYNC-NN] Слияние с upstream"
```

После слияния `.upstream` должен указывать на свежий коммит upstream, иначе
проверки формата начнут считать чужие изменения своими.

## Коммиты

Формат в [commits.md](commits.md).
