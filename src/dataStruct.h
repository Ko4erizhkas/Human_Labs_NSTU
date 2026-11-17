#pragma once
#include <array>
#include <cstddef>
#include <functional>
#include <random>
#include <string>
#include <vector>
#include <ftxui/ftxui.hpp>

// Один набор исходных данных (подтема): маршруты и их вероятности, таблица
// ошибочного перехода, таблица шагов и строковые буферы для их Input
struct DataSet
{
    DataSet(size_t rows, size_t cols, size_t n_pm, size_t n_steps);

    // Файл набора (DataIO.cpp). Результат пишется в file_status / file_ok
    bool Load(const std::string& path);
    bool Save(const std::string& path);

    // Пересчитать данные из строковых буферов (UI.cpp)
    void SyncFromBuffers();

    // Данные
    std::vector<std::vector<short>> cells;   // Маршруты: строка = маршрут, номера шагов 1..n, 0 = конец
    std::vector<float> P_m_i;                // Вероятность ошибки на шаге i
    std::vector<std::vector<float>> t_step;  // Таблица шагов: [0] константа, [1] μ норм. распределения

    // Строковые буферы для Input, по одному на каждую ячейку
    std::vector<std::vector<std::string>> cells_buf;
    std::vector<std::vector<std::string>> P_m_i_buf;  // 1 столбец
    std::vector<std::vector<std::string>> t_step_buf; // 2 столбца

    std::string file_buf;     // Путь к файлу набора
    std::string file_status;
    bool        file_ok = true;

    int left_size  = 16;
    int right_size = 26;
};

// Результат моделирования и гистограмма по нему
struct Histogram
{
    std::vector<float> sample;   // Время диалога в каждой реализации
    std::vector<int> counts;     // Число попаданий в интервалы
    int    set_index = 0;        // По какому набору построена выборка
    int    k         = 0;        // Количество интервалов
    bool   k_auto    = true;     // K посчитано по формуле
    double lo        = 0;        // Левая граница первого интервала
    double width     = 0;        // Ширина интервала
    double mean = 0, sd = 0, min = 0, max = 0;
    double t_sum       = 0;      // Суммарное время всех N реализаций
    double mean_theory = 0;      // t_ср, посчитанное аналитически по исходным данным
    size_t routes      = 0;      // Число непустых (равновероятных) маршрутов
};

// Один цикл ЛР2: работа до отказа, затем восстановление
struct Lr2Cycle
{
    double t_fail    = 0;  // T_отказа ~ Exp(λ2) — работа до отказа
    int    arrived   = 0;  // Заявок пришло до отказа
    int    processed = 0;  // Обработано полностью
    double t_proc    = 0;  // Σ t_пр — время обработки (прерванная заявка — до отказа)
    double t_rec     = 0;  // τ_вос — диалог набора 2 (Восстановление)
};

// Результат расчёта ЛР2 по N циклам:
// F = Σ (ρ1 · Σ t_пр + ρ2 · τ_вос) / Σ (T_отказа + τ_вос)
struct Lr2Result
{
    bool   done      = false;
    double sum_fail  = 0;  // Σ T_отказа
    double sum_proc  = 0;  // Σ t_пр
    double sum_rec   = 0;  // Σ τ_вос
    long long arrived   = 0;
    long long processed = 0;
    double F = 0;
    std::vector<Lr2Cycle> cycles;
};

// Маршрут — номера шагов (с 0) по порядку
using Routes = std::vector<std::vector<size_t>>;

class DataHuman
{
public:
    DataHuman(size_t rows, size_t cols, size_t n_pm, size_t n_steps);

    // Компоненты хранят указатели на буферы и this, поэтому объект нельзя
    // копировать или перемещать после построения интерфейса
    DataHuman(const DataHuman&) = delete;
    DataHuman& operator=(const DataHuman&) = delete;

    ftxui::Component TableMatrixSteps(DataSet& set);
    ftxui::Component TableErrorSteps(DataSet& set);
    ftxui::Component TableSteps(DataSet& set);
    ftxui::Component SetPanel(DataSet& set, std::string title);
    ftxui::Component HistogramPanel();
    ftxui::Component Lr2Panel();
    ftxui::Component RenderAllTables(std::function<void()> on_quit);

    // K = int(1 + 3.22 lg N)
    static int AutoK(int n);

    static constexpr int kMaxSampleN = 10'000'000;
    // ЛР2: ограничения на число циклов и на число заявок за весь расчёт
    // (каждая заявка — отдельная реализация диалога набора 1)
    static constexpr int       kMaxCycles   = 1'000'000;
    static constexpr long long kMaxRequests = 10'000'000;

private:
    // Моделирование (Simulation.cpp)
    float StepTime(const DataSet& set, size_t step);
    double StepMean(const DataSet& set, size_t step) const;
    static Routes ParseRoutes(const DataSet& set, std::string& error);
    float  Realization(const DataSet& set, const Routes& routes);
    void  BuildHistogram();
    void  Rebin();
    void  CalcLr2();

    // Отрисовка результатов (UI.cpp)
    ftxui::Element HistogramChart() const;
    ftxui::Element FrequencyTable(bool focused) const;
    ftxui::Element Stats() const;
    ftxui::Element Lr2Stats() const;
    ftxui::Element Lr2Table(bool focused) const;

    std::array<DataSet, 2> sets;

    std::vector<std::string> page_names{"Наборы данных", "Гистограмма", "ЛР2"};
    std::vector<std::string> set_names{"Набор 1\nРегулирования", "Набор 2\nВосстановления"};
    int page = 0;

    // Параметры моделирования
    int   hist_set   = 0;     // Для какого набора строить гистограмму
    int   sample_n   = 1000;  // Объём выборки N
    int   user_k     = 0;     // K, заданное пользователем; 0 — по формуле
    float sigma_coef = 0.1f;  // Для нормальных шагов σ = sigma_coef · μ
    std::string n_buf     = "1000";
    std::string k_buf;
    std::string sigma_buf = "0.1";
    std::string status;

    int freq_row = 0;  // Выбранная строка таблицы частот (для прокрутки)

    // Параметры ЛР2
    std::vector<std::string> lr2_mode_names{"Регул + восст", "Регул", "Восстановления"};
    int   lr2_mode = 0;       // Индекс в lr2_mode_names: какие слагаемые входят в числитель F
    float rho_1    = 1.f;     // Вес времени регулирования в F
    float rho_2    = 1.f;     // Вес времени восстановления в F
    float lambda_1 = 0.01f;   // Интенсивность потока заявок, 1/с
    float lambda_2 = 0.001f;  // Интенсивность потока отказов (1/MTBF), 1/с
    int   lr2_n    = 1000;    // Число циклов N
    std::string rho_1_buf    = "1";
    std::string rho_2_buf    = "1";
    std::string lambda_1_buf = "0.01";
    std::string lambda_2_buf = "0.001";
    std::string lr2_n_buf    = "1000";
    std::string lr2_status;
    int lr2_row = 0;  // Выбранная строка таблицы циклов (для прокрутки)
    Lr2Result lr2;

    Histogram hist;
    std::mt19937 rng{std::random_device{}()};
};
