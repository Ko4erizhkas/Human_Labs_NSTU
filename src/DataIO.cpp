#include "dataStruct.h"
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

// Формат файла набора — числа через пробелы/переводы строк, по порядку:
//   маршруты (rows x cols, строка = маршрут, номера шагов, 0 = конец),
//   вероятности ошибочного перехода (n_pm чисел, по шагам),
//   таблица шагов (n_steps строк: "константа μ").
// В файлах прошлой версии в конце есть вероятности маршрутов (rows чисел) —
// они пропускаются.
// Всё после '#' до конца строки — комментарий

namespace {

// Строки FTXUI в UTF-8; на Windows путь надо строить из u8string,
// иначе кириллица в пути не откроется
std::filesystem::path ToPath(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

} // namespace

bool DataSet::Load(const std::string& path) {
    std::ifstream in(ToPath(path));
    if (!in) {
        file_ok     = false;
        file_status = "Не удалось открыть файл";
        return false;
    }

    std::vector<std::string> tokens;
    for (std::string line; std::getline(in, line);) {
        if (line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3);  // BOM
        line.erase(std::min(line.find('#'), line.size()));
        std::istringstream words(line);
        for (std::string t; words >> t;) tokens.push_back(t);
    }

    size_t need = P_m_i_buf.size() + 2 * t_step_buf.size();
    for (const auto& row : cells_buf) need += row.size();
    const size_t with_route_probs = need + cells_buf.size();
    if (tokens.size() != need && tokens.size() != with_route_probs) {
        file_ok     = false;
        file_status = std::format("Ожидалось {} чисел, в файле {}", need, tokens.size());
        return false;
    }

    auto it = tokens.begin();
    for (auto& row : cells_buf)
        for (auto& cell : row) cell = *it++;
    for (auto& row : P_m_i_buf) row[0] = *it++;
    for (auto& row : t_step_buf)
        for (auto& cell : row) cell = *it++;
    SyncFromBuffers();

    file_ok     = true;
    file_status = "Загружено";
    return true;
}

// Сохраняются уже разобранные значения, то есть ровно то, что пойдёт в расчёт
bool DataSet::Save(const std::string& path) {
    std::ofstream out(ToPath(path));
    if (!out) {
        file_ok     = false;
        file_status = "Не удалось создать файл";
        return false;
    }

    out << std::format("# Маршруты {}x{}: строка = маршрут, номера шагов по порядку, 0 = конец\n",
                       cells.size(), cells.empty() ? 0 : cells[0].size());
    for (const auto& row : cells) {
        for (short v : row) out << std::format("{:>3}", v);
        out << '\n';
    }

    out << "\n# Вероятности ошибочного перехода, по одной на шаг\n";
    for (float p : P_m_i) out << p << ' ';
    out << '\n';

    out << "\n# Таблица шагов: константа  μ (нормальное распределение)\n";
    for (size_t i = 0; i < t_step[0].size(); ++i)
        out << std::format("{:<6} {}\n", t_step[0][i], t_step[1][i]);

    file_ok     = static_cast<bool>(out);
    file_status = file_ok ? "Сохранено" : "Ошибка записи";
    return file_ok;
}
