#include "AudioFeatures.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>
#include <utility>

namespace {

const int kFrame = 2048;
const int kHop = 512;
const int kBins = kFrame / 2 + 1;
const int kMelBands = 40;
const int kCoefficients = 12;
// кадры тише этого в тембр не идут: паузы и затухания
const double kSilenceDb = -60;
// без пяти секунд звука о треке судить нечего
const double kMinSoundSeconds = 5;
// темп ищется от 60 до 200 BPM, предпочтение у 120 с разбросом в октаву
const double kMinBpm = 60;
const double kMaxBpm = 200;
const double kPreferredBpm = 120;
const double kPreferenceOctaves = 1.0;
// пульс слабее этой доли от общей изменчивости атак пульсом не считается
const double kMinClarity = 0.1;
// Огибающая атак ровнее этого (в дБ) значит, что атак нет вовсе: у ровного
// тона или шума она дрожит на сотые доли, у бочки под аккордами на десятые
const double kMinOnsetSpread = 0.12;
// ниже этого уровня полоса считается тишиной: переход из цифрового нуля не атака
const double kFloorDb = -80;
const double kPi = 3.14159265358979323846;
const double kFps = double(AudioFeatures::kSampleRate) / kHop;

using Complex = std::complex<float>;

// Быстрое преобразование Фурье на месте для kFrame точек. Поворотные
// множители считаются один раз в double: накопление в float даёт заметную
// ошибку на верхних частотах
void fft(std::vector<Complex>& a) {
    static const std::vector<Complex> twiddle = [] {
        std::vector<Complex> t(kFrame / 2);
        for (int k = 0; k < kFrame / 2; ++k) {
            double angle = -2 * kPi * k / kFrame;
            t[k] = Complex(float(std::cos(angle)), float(std::sin(angle)));
        }
        return t;
    }();
    const int n = int(a.size());
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        const int stride = n / len;
        for (int i = 0; i < n; i += len) {
            for (int k = 0; k < len / 2; ++k) {
                Complex u = a[i + k];
                Complex v = a[i + k + len / 2] * twiddle[k * stride];
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
}

double hzToMel(double hz) {
    return 2595 * std::log10(1 + hz / 700);
}

double melToHz(double mel) {
    return 700 * (std::pow(10, mel / 2595) - 1);
}

// Треугольные фильтры от 20 Гц до Найквиста, равные по шкале мел. У каждого
// хотя бы одна полоса спектра, иначе нижние фильтры были бы пустыми
using Filter = std::vector<std::pair<int, float>>;
const std::vector<Filter>& melFilters() {
    static const std::vector<Filter> filters = [] {
        std::vector<double> edges(kMelBands + 2);
        double lo = hzToMel(20);
        double hi = hzToMel(AudioFeatures::kSampleRate / 2.0);
        for (int i = 0; i < kMelBands + 2; ++i)
            edges[i] = melToHz(lo + (hi - lo) * i / (kMelBands + 1)) * kFrame / AudioFeatures::kSampleRate;
        std::vector<Filter> out(kMelBands);
        for (int m = 0; m < kMelBands; ++m) {
            double left = edges[m];
            double center = edges[m + 1];
            double right = edges[m + 2];
            for (int b = int(std::ceil(left)); b <= int(std::floor(right)) && b < kBins; ++b) {
                double w = b < center ? (b - left) / (center - left) : (right - b) / (right - center);
                if (w > 0) out[m].emplace_back(b, float(w));
            }
            if (out[m].empty()) out[m].emplace_back(std::clamp(int(std::lround(center)), 0, kBins - 1), 1.0F);
        }
        return out;
    }();
    return filters;
}

// Окно Ханна: края кадра без щелчков, иначе в спектре ложные высокие частоты
const std::vector<float>& hann() {
    static const std::vector<float> window = [] {
        std::vector<float> w(kFrame);
        for (int i = 0; i < kFrame; ++i) w[i] = float(0.5 - 0.5 * std::cos(2 * kPi * i / kFrame));
        return w;
    }();
    return window;
}

double clamp01(double x) {
    return std::clamp(x, 0.0, 1.0);
}

// Темп по огибающей атак. Автокорреляция показывает, через сколько кадров
// атаки повторяются; к ней прибавляется половина корреляции на двойном
// периоде (такт из двух долей подтверждает долю) и всё умножается на
// предпочтение около 120 BPM. Вершина уточняется параболой между соседними
// кадрами: один кадр это 23 мс, около 5 % темпа
double tempoOf(const std::vector<double>& onset) {
    const int minLag = int(std::floor(kFps * 60 / kMaxBpm));
    const int maxLag = int(std::ceil(kFps * 60 / kMinBpm));
    const int n = int(onset.size());
    if (n < 4 * maxLag) return 0;

    double mean = std::accumulate(onset.begin(), onset.end(), 0.0) / n;
    std::vector<double> e(n);
    double spread = 0;
    for (int t = 0; t < n; ++t) {
        e[t] = onset[t] - mean;
        spread += e[t] * e[t];
    }
    if (std::sqrt(spread / n) < kMinOnsetSpread) return 0;
    std::vector<double> r(2 * maxLag + 2, 0);
    for (int lag = 0; lag < int(r.size()); ++lag) {
        double sum = 0;
        for (int t = 0; t + lag < n; ++t) sum += e[t] * e[t + lag];
        r[lag] = sum / (n - lag);
    }
    if (r[0] <= 0) return 0;

    int best = -1;
    double bestScore = 0;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        double bpm = kFps * 60 / lag;
        double octaves = std::log2(bpm / kPreferredBpm) / kPreferenceOctaves;
        double score = (r[lag] + 0.5 * r[2 * lag]) * std::exp(-0.5 * octaves * octaves);
        if (best < 0 || score > bestScore) {
            best = lag;
            bestScore = score;
        }
    }
    if (r[best] / r[0] < kMinClarity) return 0;

    double lag = best;
    double denominator = r[best - 1] - 2 * r[best] + r[best + 1];
    if (denominator < 0) lag += std::clamp(0.5 * (r[best - 1] - r[best + 1]) / denominator, -0.5, 0.5);
    return kFps * 60 / lag;
}

} // namespace

AudioFeatures::Sound AudioFeatures::analyze(const std::vector<float>& mono) {
    Sound sound;
    const int frames = mono.size() < size_t(kFrame) ? 0 : int((mono.size() - kFrame) / kHop) + 1;
    if (frames == 0) return sound;

    const std::vector<Filter>& filters = melFilters();
    const std::vector<float>& window = hann();
    std::vector<Complex> buffer(kFrame);
    std::vector<double> power(kBins);
    std::vector<double> logMel(kMelBands);
    std::vector<double> previous(kMelBands, 0);
    std::vector<double> onset;
    onset.reserve(frames);

    std::array<double, kCoefficients> mfccSum{};
    std::array<double, kCoefficients> mfccSquares{};
    double centroidSum = 0;
    double flatnessSum = 0;
    int sounding = 0;

    for (int f = 0; f < frames; ++f) {
        const float* x = mono.data() + size_t(f) * kHop;
        double energy = 0;
        for (int i = 0; i < kFrame; ++i) {
            energy += double(x[i]) * x[i];
            buffer[i] = Complex(x[i] * window[i], 0);
        }
        double db = 10 * std::log10(energy / kFrame + 1e-12);
        fft(buffer);

        double total = 0;
        double weighted = 0;
        double logSum = 0;
        for (int b = 0; b < kBins; ++b) {
            power[b] = std::norm(buffer[b]);
            total += power[b];
            weighted += power[b] * b;
            logSum += std::log(power[b] + 1e-12);
        }
        for (int m = 0; m < kMelBands; ++m) {
            double sum = 0;
            for (const auto& [bin, w] : filters[m]) sum += w * power[bin];
            logMel[m] = std::max(kFloorDb, 10 * std::log10(sum + 1e-10));
        }

        // атака это рост громкости по полосам мел; первый кадр не с чем сравнить
        double flux = 0;
        for (int m = 0; m < kMelBands; ++m) flux += std::max(0.0, logMel[m] - previous[m]);
        if (f > 0) onset.push_back(flux / kMelBands);
        previous = logMel;

        if (db < kSilenceDb) continue;
        ++sounding;
        centroidSum += total > 0 ? weighted / total / (kBins - 1) : 0;
        flatnessSum += std::exp(logSum / kBins) / (total / kBins + 1e-12);
        for (int k = 1; k <= kCoefficients; ++k) {
            double c = 0;
            for (int m = 0; m < kMelBands; ++m) c += logMel[m] * std::cos(kPi * k * (m + 0.5) / kMelBands);
            mfccSum[k - 1] += c;
            mfccSquares[k - 1] += c * c;
        }
    }
    if (sounding < kMinSoundSeconds * kFps) return sound;

    for (int k = 0; k < kCoefficients; ++k) {
        double mean = mfccSum[k] / sounding;
        sound.timbre[k] = float(mean);
        sound.timbre[kCoefficients + k] = float(std::sqrt(std::max(0.0, mfccSquares[k] / sounding - mean * mean)));
    }
    const double centroid = centroidSum / sounding;
    sound.timbre[2 * kCoefficients] = float(centroid);
    sound.timbre[2 * kCoefficients + 1] = float(flatnessSum / sounding);

    double squares = 0;
    for (float v : mono) squares += double(v) * v;
    sound.loudness = 10 * std::log10(squares / mono.size() + 1e-12);

    // Огибающая сглаживается на пару кадров: острые пики между кадрами
    // размазали бы автокорреляцию по соседним задержкам
    static const std::array<double, 5> kernel{1, 4, 6, 4, 1};
    const int length = int(onset.size());
    std::vector<double> smooth(length);
    for (int t = 0; t < length; ++t) {
        double sum = 0;
        double weight = 0;
        for (int d = -2; d <= 2; ++d) {
            if (t + d < 0 || t + d >= length) continue;
            sum += kernel[d + 2] * onset[t + d];
            weight += kernel[d + 2];
        }
        smooth[t] = sum / weight;
    }
    sound.bpm = tempoOf(smooth);

    // атаки: местные вершины огибающей выше среднего на половину разброса, не чаще раза в 116 мс
    double mean = std::accumulate(smooth.begin(), smooth.end(), 0.0) / smooth.size();
    double spread = 0;
    for (double v : smooth) spread += (v - mean) * (v - mean);
    spread = std::sqrt(spread / smooth.size());
    int onsets = 0;
    int last = -100;
    for (int t = 1; spread >= kMinOnsetSpread && t + 1 < int(smooth.size()); ++t) {
        if (smooth[t] > mean + 0.5 * spread && smooth[t] >= smooth[t - 1] && smooth[t] > smooth[t + 1] &&
            t - last >= 5) {
            ++onsets;
            last = t;
        }
    }
    sound.onsetRate = onsets / (smooth.size() / kFps);

    // Энергия: громкость (от -35 до -10 dBFS), темп атак (до пяти в секунду) и
    // яркость. Громкость весит больше всего: тихое не бывает энергичным
    double loud = clamp01((sound.loudness + 35) / 25);
    double pace = clamp01(sound.onsetRate / 5);
    double bright = clamp01(centroid / 0.3);
    sound.energy = 0.5 * loud + 0.3 * pace + 0.2 * bright;
    sound.valid = true;
    return sound;
}

double AudioFeatures::tempoDistance(double a, double b) {
    if (a <= 0 || b <= 0) return 0.5;
    double octaves = std::log2(a / b);
    return std::abs(octaves - std::round(octaves));
}
