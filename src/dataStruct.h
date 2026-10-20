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
    double mean_theory = 0;      // t_ср, посчитанное аналитически по исходным данным
    size_t routes      = 0;      // Число непустых (равновероятных) маршрутов
};

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
    ftxui::Component RenderAllTables(std::function<void()> on_quit);

    // K = int(1 + 3.22 lg N)
    static int AutoK(int n);

    static constexpr int kMaxSampleN = 10'000'000;

private:
    // Моделирование (Simulation.cpp)
    float StepTime(const DataSet& set, size_t step);
    double StepMean(const DataSet& set, size_t step) const;
    void  BuildHistogram();
    void  Rebin();

    // Отрисовка результатов (UI.cpp)
    ftxui::Element HistogramChart() const;
    ftxui::Element FrequencyTable() const;
    ftxui::Element Stats() const;

    std::array<DataSet, 2> sets;

    std::vector<std::string> page_names{"Наборы данных", "Гистограмма"};
    std::vector<std::string> set_names{"Набор 1", "Набор 2"};
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

    Histogram hist;
    std::mt19937 rng{std::random_device{}()};
};
