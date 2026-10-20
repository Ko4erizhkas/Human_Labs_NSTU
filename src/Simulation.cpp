#include "dataStruct.h"
#include <algorithm>
#include <cmath>
#include <format>

int DataHuman::AutoK(int n) {
    if (n <= 1) return 1;
    return std::max(1, static_cast<int>(1 + 3.22 * std::log10(static_cast<double>(n))));
}

// Время шага: константа из первого столбца, иначе N(μ, σ) с μ из второго
// столбца и σ = sigma_coef · μ. Отрицательные значения отсекаются в 0
float DataHuman::StepTime(const DataSet& set, size_t step) {
    if (step >= set.t_step[0].size()) return 0.f;

    const float constant = set.t_step[0][step];
    if (constant != 0.f) return std::max(constant, 0.f);

    const float mu    = set.t_step[1][step];
    const float sigma = std::abs(mu) * sigma_coef;
    if (sigma <= 0.f) return std::max(mu, 0.f);

    std::normal_distribution<float> dist(mu, sigma);
    return std::max(dist(rng), 0.f);
}

// Матожидание времени шага с учётом отсечения отрицательных значений в 0:
// E[max(X, 0)] = μΦ(μ/σ) + σφ(μ/σ) для X ~ N(μ, σ)
double DataHuman::StepMean(const DataSet& set, size_t step) const {
    if (step >= set.t_step[0].size()) return 0.0;

    const double constant = set.t_step[0][step];
    if (constant != 0.0) return std::max(constant, 0.0);

    const double mu    = set.t_step[1][step];
    const double sigma = std::abs(mu) * sigma_coef;
    if (sigma <= 0.0) return std::max(mu, 0.0);

    const double z   = mu / sigma;
    const double cdf = 0.5 * std::erfc(-z / std::sqrt(2.0));
    const double pdf = std::exp(-0.5 * z * z) / std::sqrt(2.0 * 3.14159265358979323846);
    return mu * cdf + sigma * pdf;
}

// Одна реализация: маршрут выбирается случайно, все непустые маршруты
// равновероятны; время решения = сумма времён его шагов. На каждом шаге с вероятностью
// P_m_i пользователь ошибается, и время этого шага добавляется ещё раз
void DataHuman::BuildHistogram() {
    if (sample_n <= 0) {
        status = "N должно быть больше 0";
        return;
    }
    // Сборка 32-битная: выборка из 10^8+ значений может не поместиться в память
    if (sample_n > kMaxSampleN) {
        status = std::format("N не больше {}", kMaxSampleN);
        return;
    }

    const DataSet& set = sets[hist_set];
    const size_t n_steps = set.t_step[0].size();

    // Маршрут — номера шагов строки матрицы до первого 0; пустые строки пропускаются
    std::vector<std::vector<size_t>> routes;
    for (size_t r = 0; r < set.cells.size(); ++r) {
        std::vector<size_t> route;
        for (short step : set.cells[r]) {
            if (step == 0) break;
            if (step < 0 || static_cast<size_t>(step) > n_steps) {
                status = std::format("Маршрут {}: шаг {} вне диапазона 1..{}", r + 1, step, n_steps);
                return;
            }
            route.push_back(static_cast<size_t>(step) - 1);
        }
        if (!route.empty()) routes.push_back(std::move(route));
    }
    if (routes.empty()) {
        status = "Не задано ни одного маршрута";
        return;
    }
    std::uniform_int_distribution<size_t> pick_route(0, routes.size() - 1);

    std::uniform_real_distribution<float> uniform(0.f, 1.f);
    hist = Histogram{};
    hist.set_index = hist_set;
    hist.sample.reserve(sample_n);

    for (int i = 0; i < sample_n; ++i) {
        float total = 0.f;
        for (size_t step : routes[pick_route(rng)]) {
            const float t = StepTime(set, step);
            total += t;
            if (step < set.P_m_i.size() && uniform(rng) < set.P_m_i[step])
                total += t;
        }
        hist.sample.push_back(total);
    }

    const auto [mn, mx] = std::minmax_element(hist.sample.begin(), hist.sample.end());
    hist.min = *mn;
    hist.max = *mx;
    double sum = 0;
    for (float x : hist.sample) sum += x;
    hist.mean = sum / hist.sample.size();
    double sq = 0;
    for (float x : hist.sample) sq += (x - hist.mean) * (x - hist.mean);
    hist.sd = hist.sample.size() > 1 ? std::sqrt(sq / (hist.sample.size() - 1)) : 0.0;

    // Теоретическое t_ср = (1/R) · Σ_маршруты Σ_шаги E[t_s] · (1 + P_ошибки_s),
    // R — число непустых маршрутов
    hist.mean_theory = 0;
    for (const auto& route : routes)
        for (size_t step : route) {
            const double p_err = step < set.P_m_i.size() ? set.P_m_i[step] : 0.0;
            hist.mean_theory += StepMean(set, step) * (1 + p_err);
        }
    hist.mean_theory /= routes.size();
    hist.routes = routes.size();

    status.clear();
    Rebin();
}

// Разбиение уже полученной выборки на K интервалов (без повторного моделирования)
void DataHuman::Rebin() {
    if (hist.sample.empty()) return;

    hist.k_auto = user_k <= 0;
    hist.k      = hist.k_auto ? AutoK(static_cast<int>(hist.sample.size())) : user_k;

    hist.lo = hist.min;
    double span = hist.max - hist.min;
    if (span <= 0) {  // все значения одинаковы
        hist.lo -= 0.5;
        span = 1;
    }
    hist.width = span / hist.k;

    hist.counts.assign(hist.k, 0);
    for (float x : hist.sample) {
        int i = static_cast<int>((x - hist.lo) / hist.width);
        ++hist.counts[std::clamp(i, 0, hist.k - 1)];
    }
}
