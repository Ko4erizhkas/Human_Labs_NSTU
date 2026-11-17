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

// Маршрут — номера шагов строки матрицы до первого 0; пустые строки пропускаются.
// Если маршрутов нет или шаг вне диапазона, возвращается пустой список и
// причина пишется в error
Routes DataHuman::ParseRoutes(const DataSet& set, std::string& error) {
    const size_t n_steps = set.t_step[0].size();
    Routes routes;
    for (size_t r = 0; r < set.cells.size(); ++r) {
        std::vector<size_t> route;
        for (short step : set.cells[r]) {
            if (step == 0) break;
            if (step < 0 || static_cast<size_t>(step) > n_steps) {
                error = std::format("Маршрут {}: шаг {} вне диапазона 1..{}", r + 1, step, n_steps);
                return {};
            }
            route.push_back(static_cast<size_t>(step) - 1);
        }
        if (!route.empty()) routes.push_back(std::move(route));
    }
    if (routes.empty()) error = "Не задано ни одного маршрута";
    return routes;
}

// Одна реализация: маршрут выбирается случайно, все непустые маршруты
// равновероятны; время решения = сумма времён его шагов. На каждом шаге с вероятностью
// P_m_i пользователь ошибается, и время этого шага добавляется ещё раз
float DataHuman::Realization(const DataSet& set, const Routes& routes) {
    std::uniform_int_distribution<size_t> pick_route(0, routes.size() - 1);
    std::uniform_real_distribution<float> uniform(0.f, 1.f);

    float total = 0.f;
    for (size_t step : routes[pick_route(rng)]) {
        const float t = StepTime(set, step);
        total += t;
        if (step < set.P_m_i.size() && uniform(rng) < set.P_m_i[step])
            total += t;
    }
    return total;
}

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
    const Routes routes = ParseRoutes(set, status);
    if (routes.empty()) return;

    hist = Histogram{};
    hist.set_index = hist_set;
    hist.sample.reserve(sample_n);
    for (int i = 0; i < sample_n; ++i) hist.sample.push_back(Realization(set, routes));

    const auto [mn, mx] = std::minmax_element(hist.sample.begin(), hist.sample.end());
    hist.min = *mn;
    hist.max = *mx;
    double sum = 0;
    for (float x : hist.sample) sum += x;
    hist.t_sum = sum;
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

// ЛР2: N циклов «работа до отказа → восстановление».
//   В цикле: T_отказа ~ Exp(λ2). Заявки — пуассоновский поток с интенсивностью
//   λ1: t_i = t_{i-1} + Exp(λ1), начиная с 0, пока t_i < T_отказа. Оператор
//   обрабатывает их по очереди, обработка одной заявки t_пр — реализация
//   диалога набора 1 (Регулирование). В момент отказа очередь аннулируется,
//   прерванная заявка засчитывается до момента отказа. Затем восстановление
//   τ_вос — реализация диалога набора 2 (Восстановление).
//   F = Σ (ρ1 · Σ t_пр + ρ2 · τ_вос) / Σ (T_отказа + τ_вос); режим определяет,
//   какие слагаемые входят в числитель
void DataHuman::CalcLr2() {
    lr2 = Lr2Result{};
    if (lr2_n <= 0 || lr2_n > kMaxCycles) {
        lr2_status = std::format("N должно быть от 1 до {}", kMaxCycles);
        return;
    }
    if (lambda_1 <= 0.f || lambda_2 <= 0.f) {
        lr2_status = "λ1 и λ2 должны быть больше 0";
        return;
    }

    std::string error;
    const Routes reg_routes = ParseRoutes(sets[0], error);
    if (reg_routes.empty()) {
        lr2_status = "Набор 1: " + error;
        return;
    }
    const Routes rec_routes = ParseRoutes(sets[1], error);
    if (rec_routes.empty()) {
        lr2_status = "Набор 2: " + error;
        return;
    }

    std::exponential_distribution<double> fail_time(lambda_2);
    std::exponential_distribution<double> interval(lambda_1);
    lr2.cycles.reserve(lr2_n);
    for (int c = 0; c < lr2_n; ++c) {
        Lr2Cycle cycle;
        cycle.t_fail = fail_time(rng);

        double arrival = 0;
        double free_at = 0;  // Когда оператор освободится
        for (;;) {
            arrival += interval(rng);
            if (arrival >= cycle.t_fail) break;
            if (++lr2.arrived > kMaxRequests) {
                lr2        = Lr2Result{};
                lr2_status = std::format("Больше {} заявок — уменьшите N или λ1, "
                                         "или увеличьте λ2", kMaxRequests);
                return;
            }
            ++cycle.arrived;

            const double start = std::max(arrival, free_at);
            if (start >= cycle.t_fail) continue;  // Ждала в очереди — аннулирована
            const double t = Realization(sets[0], reg_routes);
            free_at = start + t;
            if (free_at <= cycle.t_fail) {
                ++cycle.processed;
                cycle.t_proc += t;
            } else {
                cycle.t_proc += cycle.t_fail - start;  // Прервана отказом
            }
        }
        cycle.t_rec = Realization(sets[1], rec_routes);

        lr2.sum_fail  += cycle.t_fail;
        lr2.sum_proc  += cycle.t_proc;
        lr2.sum_rec   += cycle.t_rec;
        lr2.processed += cycle.processed;
        lr2.cycles.push_back(cycle);
    }

    const double numerator = (lr2_mode != 2 ? rho_1 * lr2.sum_proc : 0.0)
                           + (lr2_mode != 1 ? rho_2 * lr2.sum_rec : 0.0);
    lr2.F    = numerator / (lr2.sum_fail + lr2.sum_rec);
    lr2.done = true;
    lr2_row  = 0;
    lr2_status.clear();
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
