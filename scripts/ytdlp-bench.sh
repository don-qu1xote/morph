#!/bin/bash
# Сколько yt-dlp ищет поток с разными настройками и рабочая ли ссылка: по каждой
# качается первый мегабайт. По результату выбирается MORPH_YTDLP_FAST.
#   scripts/ytdlp-bench.sh              три тестовых клипа
#   scripts/ytdlp-bench.sh ID ID ...    свои клипы (id из адреса watch?v=...)
# Варианты: default это yt-dlp как есть, visionos это быстрый путь morph,
# nopage то же без загрузки страницы клипа
set -uo pipefail

ids=("$@")
[ ${#ids[@]} -eq 0 ] && ids=(dQw4w9WgXcQ kJQP7kiw5Fk 9bZkp7q19f0)
query="Rick Astley - Never Gonna Give You Up"

variants=(
    "default|"
    "visionos|youtube:player_client=visionos"
    "nopage|youtube:player_client=visionos;player_skip=webpage,configs"
)

TIMEFORMAT=%R
echo "yt-dlp $(yt-dlp --version), запуск без работы: $( { time yt-dlp --version >/dev/null; } 2>&1 ) с"
echo
printf "%-10s %-22s %8s %8s %10s\n" variant target find_s mb_s kb_per_s

# $1 вариант, $2 extractor-args, $3 цель для yt-dlp
measure() {
    local args=(-f 'bestaudio[ext=m4a]/bestaudio' --quiet --no-warnings --no-playlist -g)
    [ -n "$2" ] && args+=(--extractor-args "$2")
    local out
    out=$(mktemp)
    local found
    found=$( { time yt-dlp "${args[@]}" "$3" >"$out" 2>/dev/null; } 2>&1 )
    local url
    url=$(head -1 "$out")
    rm -f "$out"

    local label=${3#https://www.youtube.com/watch?v=}
    label=${label:0:22}
    if [ -z "$url" ]; then
        printf "%-10s %-22s %8s %8s %10s\n" "$1" "$label" "$found" "-" "no-url"
        return
    fi
    local code total speed
    read -r code total speed < <(curl -s -o /dev/null -r 0-1048575 -w '%{http_code} %{time_total} %{speed_download}\n' "$url")
    if [ "$code" != "206" ] && [ "$code" != "200" ]; then
        printf "%-10s %-22s %8s %8s %10s\n" "$1" "$label" "$found" "-" "http-$code"
        return
    fi
    printf "%-10s %-22s %8s %8.2f %10.0f\n" "$1" "$label" "$found" "$total" "$(echo "$speed / 1024" | bc -l)"
}

for v in "${variants[@]}"; do
    name=${v%%|*}
    extractor=${v#*|}
    for id in "${ids[@]}"; do
        measure "$name" "$extractor" "https://www.youtube.com/watch?v=$id"
    done
    measure "$name" "$extractor" "ytsearch1:$query"
done

echo
echo "find_s: сколько yt-dlp искал поток, mb_s: за сколько скачался первый мегабайт."
echo "no-url или http-403 у варианта значит, что он в этой сети не работает."
