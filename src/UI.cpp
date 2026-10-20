#include "dataStruct.h"
#include <algorithm>
#include <charconv>
#include <format>
#include <sstream>

#ifndef DATA_DIR
#define DATA_DIR "data"
#endif

using namespace ftxui;

namespace {

// Ограничение на K, заданное вручную
constexpr int kMaxK = 200;

// Подписи строк таблицы: prefix1, prefix2, ... Высота 3 — как у ячейки с рамкой
Element RowLabels(const std::string& prefix, size_t rows) {
    Elements labels;
    for (size_t r = 0; r < rows; ++r)
        labels.push_back(text(prefix + std::to_string(r + 1)) | vcenter
                         | size(HEIGHT, EQUAL, 3) | size(WIDTH, EQUAL, 4));
    return vbox(std::move(labels));
}

// Аналог Wrap из gallery.cpp: заголовок и рамка вокруг компонента.
// frame даёт прокрутку, если таблица не влезает в панель.
// Заголовок может быть многострочным ('\n'); если задан row_prefix,
// слева выводятся подписи строк
Component Section(std::string title, Component content,
                  std::string row_prefix = "", size_t rows = 0) {
    Elements header;
    std::istringstream lines(title);
    for (std::string line; std::getline(lines, line);)
        header.push_back(text(line) | bold);
    Element labels = RowLabels(row_prefix, row_prefix.empty() ? 0 : rows);
    return Renderer(content, [header, labels, content] {
        return vbox({
            vbox(header),
            separator(),
            hbox({labels, content->Render()}) | frame | flex,
        }) | border;
    });
}


// Одна числовая ячейка
Component NumberCell(std::string* buffer, std::function<void()> on_change,
               std::string placeholder = "0") {
    InputOption opt;
    opt.multiline = false;
    opt.on_change = std::move(on_change);
    return Input(buffer, std::move(placeholder), opt) | size(WIDTH, EQUAL, 8) | border;
}

// Таблица из ячеек; on_change(row, col) вызывается при правке ячейки
Component Grid(std::vector<std::vector<std::string>>& buffers,
               std::function<void(size_t, size_t)> on_change) {
    auto rows = Container::Vertical({});
    for (size_t r = 0; r < buffers.size(); ++r) {
        Components cols;
        for (size_t c = 0; c < buffers[r].size(); ++c)
            cols.push_back(NumberCell(&buffers[r][c], [on_change, r, c] { on_change(r, c); }));
        rows->Add(Container::Horizontal(std::move(cols)));
    }
    return rows;
}

// Невалидный ввод или выход за диапазон дают 0. Запятая считается
// десятичным разделителем наравне с точкой
template <class T>
T Parse(std::string s) {
    std::replace(s.begin(), s.end(), ',', '.');
    T v{};
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || ptr != s.data() + s.size()) return T{};
    return v;
}

float ParseProbability(const std::string& s) {
    return std::clamp(Parse<float>(s), 0.f, 1.f);
}

// Однострочная кнопка вида [Текст], чтобы строка файла не занимала 3 строки
Component SmallButton(std::string label, std::function<void()> on_click) {
    return Button(std::move(label), std::move(on_click), ButtonOption::Ascii());
}

} // namespace

DataSet::DataSet(size_t rows, size_t cols, size_t n_pm, size_t n_steps)
    : cells(rows, std::vector<short>(cols, 0)),
      P_m_i(n_pm, 0.f),
      t_step(2, std::vector<float>(n_steps, 0.f)),
      cells_buf(rows, std::vector<std::string>(cols, "0")),
      P_m_i_buf(n_pm, std::vector<std::string>(1, "0")),
      t_step_buf(n_steps, std::vector<std::string>(2, "0")) {}

void DataSet::SyncFromBuffers() {
    for (size_t r = 0; r < cells.size(); ++r)
        for (size_t c = 0; c < cells[r].size(); ++c)
            cells[r][c] = Parse<short>(cells_buf[r][c]);
    for (size_t r = 0; r < P_m_i.size(); ++r)
        P_m_i[r] = ParseProbability(P_m_i_buf[r][0]);
    for (size_t r = 0; r < t_step_buf.size(); ++r)
        for (size_t c = 0; c < 2; ++c)
            t_step[c][r] = Parse<float>(t_step_buf[r][c]);
}

// Файлы наборов по умолчанию лежат в data/ проекта (DATA_DIR задаётся
// в CMakeLists.txt) и подгружаются при запуске, если существуют
DataHuman::DataHuman(size_t rows, size_t cols, size_t n_pm, size_t n_steps)
    : sets{DataSet(rows, cols, n_pm, n_steps), DataSet(rows, cols, n_pm, n_steps)} {
    for (size_t i = 0; i < sets.size(); ++i) {
        sets[i].file_buf = std::format("{}/set{}.txt", DATA_DIR, i + 1);
        sets[i].Load(sets[i].file_buf);
        if (!sets[i].file_ok) sets[i].file_status.clear();  // файла может не быть, это не ошибка
    }
}

// Строка матрицы — маршрут: номера шагов по порядку
Component DataHuman::TableMatrixSteps(DataSet& set) {
    return Section("Маршруты\nномера шагов по порядку, 0 = конец маршрута",
                   Grid(set.cells_buf, [&set](size_t r, size_t c) {
        set.cells[r][c] = Parse<short>(set.cells_buf[r][c]);
    }), "М", set.cells_buf.size());
}

Component DataHuman::TableErrorSteps(DataSet& set) {
    return Section("P ошибки\nна шаге", Grid(set.P_m_i_buf, [&set](size_t r, size_t) {
        set.P_m_i[r] = ParseProbability(set.P_m_i_buf[r][0]);
    }), "ш", set.P_m_i_buf.size());
}

Component DataHuman::TableSteps(DataSet& set) {
    return Section("Таблица шагов\nконст. | μ норм.", Grid(set.t_step_buf, [&set](size_t r, size_t c) {
        set.t_step[c][r] = Parse<float>(set.t_step_buf[r][c]);
    }), "ш", set.t_step_buf.size());
}

// Один набор таблиц в отдельном vbox: слева errors, справа steps,
// посередине matrix (занимает остаток)
Component DataHuman::SetPanel(DataSet& set, std::string title) {
    auto matrix = TableMatrixSteps(set);
    auto errors = TableErrorSteps(set);
    auto steps  = TableSteps(set);

    // ResizableSplit сам создаёт контейнер навигации, отдельный
    // Container::Horizontal для таблиц не нужен
    auto middle_right = ResizableSplitRight(steps, matrix, &set.right_size);
    auto tables = ResizableSplit({
        .main      = errors,
        .back      = middle_right,
        .direction = Direction::Left,
        .main_size = &set.left_size,
        .min       = 12,
    });

    // Строка файла: путь, загрузка, сохранение
    auto file_input = Input(&set.file_buf, "путь к файлу набора");
    auto load = SmallButton("Загрузить", [&set] { set.Load(set.file_buf); });
    auto save = SmallButton("Сохранить", [&set] { set.Save(set.file_buf); });
    auto file_row = Container::Horizontal({file_input, load, save});

    auto layout = Container::Vertical({file_row, tables});

    return Renderer(layout, [=, &set] {
        return vbox({
            text(title) | bold | hcenter,
            hbox({
                text("Файл: "),
                file_input->Render() | flex,
                text(" "), load->Render(),
                text(" "), save->Render(),
                text(" "),
                text(set.file_status) | color(set.file_ok ? Color::Green : Color::Red),
            }),
            separator(),
            tables->Render() | flex,
        }) | border;
    });
}

Element DataHuman::Stats() const {
    if (hist.sample.empty())
        return status.empty() ? text("Выборка не построена") | dim
                              : paragraph(status) | color(Color::Red);

    Elements lines{
        text(std::format("{}, N = {}", set_names[hist.set_index], hist.sample.size())),
        text(std::format("K = {} ({})", hist.k, hist.k_auto ? "по формуле" : "задано")),
        text(std::format("Маршрутов {} (равновероятны)", hist.routes)),
        text(std::format("t_ср     {:.3f}", hist.mean)) | bold | color(Color::Yellow),
        text(std::format("t_ср теор. {:.3f}", hist.mean_theory)),
        text(std::format("СКО      {:.3f}", hist.sd)),
        text(std::format("Мин      {:.3f}", hist.min)),
        text(std::format("Макс     {:.3f}", hist.max)),
    };
    if (!status.empty())
        lines.push_back(paragraph(status) | color(Color::Red));
    return vbox(std::move(lines));
}

// Столбцы гистограммы: сверху частота, снизу середина интервала.
// Все столбцы получают одинаковую минимальную ширину, чтобы flex
// распределил место поровну
Element DataHuman::HistogramChart() const {
    if (hist.counts.empty())
        return text("Гистограмма появится после «Построить»") | center;

    const int max_count = *std::max_element(hist.counts.begin(), hist.counts.end());

    std::vector<std::string> labels;
    size_t col_width = 0;
    for (int i = 0; i < hist.k; ++i) {
        labels.push_back(std::format("{:.2f}", hist.lo + (i + 0.5) * hist.width));
        col_width = std::max({col_width, labels.back().size(),
                              std::to_string(hist.counts[i]).size()});
    }

    // Интервал, в который попадает t_ср, выделяется цветом
    const int mean_bin = std::clamp(static_cast<int>((hist.mean - hist.lo) / hist.width), 0, hist.k - 1);

    Elements cols;
    for (int i = 0; i < hist.k; ++i) {
        const float ratio = max_count ? static_cast<float>(hist.counts[i]) / max_count : 0.f;
        const Color bar_color = i == mean_bin ? Color::Yellow : Color::Cyan;
        // Пробел справа отделяет и столбец, и подпись от соседнего
        cols.push_back(hbox({
            vbox({
                text(std::to_string(hist.counts[i])) | hcenter,
                gaugeUp(ratio) | color(bar_color) | yflex,
                text(labels[i]) | hcenter,
            }) | size(WIDTH, GREATER_THAN, static_cast<int>(col_width)) | flex,
            text(" "),
        }) | flex);
    }

    return vbox({
        text("Гистограмма: " + set_names[hist.set_index]) | bold | hcenter,
        hbox({
            text("t_ср = ") | bold,
            text(std::format("{:.3f}", hist.mean)) | bold | color(Color::Yellow),
            text(std::format("   (теор. {:.3f})", hist.mean_theory)) | dim,
        }) | hcenter,
        separator(),
        hbox(std::move(cols)) | flex,
        text("время диалога (середины интервалов)") | dim | hcenter,
    });
}

Element DataHuman::FrequencyTable() const {
    Elements rows{
        text(" N      интервал         n       p") | bold,
        separator(),
    };
    const double n = static_cast<double>(hist.sample.size());
    for (int i = 0; i < hist.k && !hist.counts.empty(); ++i) {
        const double a = hist.lo + i * hist.width;
        rows.push_back(text(std::format("{:>2} [{:>8.2f};{:>8.2f}) {:>6} {:.3f}",
                                        i + 1, a, a + hist.width, hist.counts[i],
                                        hist.counts[i] / n)));
    }
    return vbox({
        text("Частоты") | bold | hcenter,
        separator(),
        vbox(std::move(rows)) | yframe | flex,
    });
}

Component DataHuman::HistogramPanel() {
    auto set_toggle  = Toggle(&set_names, &hist_set);
    auto n_input     = NumberCell(&n_buf, [this] { sample_n = Parse<int>(n_buf); }, "1000");
    auto k_input     = NumberCell(&k_buf, [this] {
        user_k = std::clamp(Parse<int>(k_buf), 0, kMaxK);
        Rebin();  // меняем только разбиение, выборка остаётся прежней
    }, "авто");
    auto sigma_input = NumberCell(&sigma_buf, [this] {
        sigma_coef = std::max(Parse<float>(sigma_buf), 0.f);
    }, "0.1");
    // Ошибка при расчёте не должна закрывать программу — показываем её в панели
    auto build = Button("Построить", [this] {
        try {
            BuildHistogram();
        } catch (const std::exception& e) {
            hist   = Histogram{};
            status = std::string("Ошибка расчёта: ") + e.what();
        }
    });

    auto controls = Container::Vertical({set_toggle, n_input, k_input, sigma_input, build});

    return Renderer(controls, [=, this] {
        auto field = [](std::string label, Element input) {
            return hbox({text(std::move(label)) | vcenter | flex, input});
        };
        auto params = vbox({
            text("Параметры") | bold | hcenter,
            separator(),
            set_toggle->Render() | hcenter,
            field("Объём выборки N", n_input->Render()),
            field("Интервалов K", k_input->Render()),
            text(std::format("пусто: K = int(1+3.22 lg N) = {}", AutoK(sample_n))) | dim,
            field("σ/μ норм. шагов", sigma_input->Render()),
            build->Render() | hcenter,
            separator(),
            Stats(),
        }) | size(WIDTH, EQUAL, 36) | border;

        return hbox({
            params,
            HistogramChart() | flex | border,
            FrequencyTable() | border,
        });
    });
}

Component DataHuman::RenderAllTables(std::function<void()> on_quit) {
    auto set1 = SetPanel(sets[0], set_names[0]);
    auto set2 = SetPanel(sets[1], set_names[1]);
    auto sets_page = Renderer(Container::Vertical({set1, set2}), [=] {
        return vbox({
            set1->Render() | flex,
            set2->Render() | flex,
        });
    });

    auto page_toggle = Toggle(&page_names, &page);
    auto pages = Container::Tab({sets_page, HistogramPanel()}, &page);
    auto quit  = Button("Выход", std::move(on_quit));

    auto layout = Container::Vertical({page_toggle, pages, quit});

    return Renderer(layout, [=] {
        return vbox({
            page_toggle->Render() | hcenter,
            separator(),
            pages->Render() | flex,
            hbox({
                filler(),
                quit->Render(),
                filler(),
            }) | border,
        });
    });
}
