#ifndef AUDIO_FEATURES_H
#define AUDIO_FEATURES_H

#include <array>
#include <vector>

// Что слышно в треке: темп, энергия и тембр. Считается по минуте звука, моно
// 22050 Гц, кадрами по 2048 отсчётов (93 мс) с шагом 512. Без нейросетей и
// внешних библиотек, классическая обработка сигнала:
//   тембр: средние и разброс MFCC 1..12 (огибающая спектра на шкале мел, как
//     её слышит ухо), яркость (центр масс спектра) и шумность (плоскость);
//   темп: автокорреляция огибающей атак с предпочтением около 120 BPM
//     (Ellis, 2007), чтобы не путать темп с его половиной и двойным;
//   энергия: громкость, частота атак и яркость, от 0 до 1
class AudioFeatures {
public:
    static constexpr int kSampleRate = 22050;
    // 12 средних MFCC, 12 разбросов, яркость, шумность
    static constexpr int kTimbreDim = 26;
    using Timbre = std::array<float, kTimbreDim>;

    struct Sound {
        Timbre timbre{};
        // 0, если ровного пульса нет: эмбиент, речь
        double bpm = 0;
        double energy = 0;
        // средняя громкость, dBFS
        double loudness = 0;
        // атак в секунду
        double onsetRate = 0;
        // false: тишина или слишком коротко, чтобы судить
        bool valid = false;
    };

    static Sound analyze(const std::vector<float>& mono);

    // Насколько далеки темпы в октавах, с точностью до удвоения: 60 и 120 BPM
    // для перехода одно и то же, под оба качает в такт. 0.2 это примерно 15 %
    static double tempoDistance(double a, double b);
};

#endif
