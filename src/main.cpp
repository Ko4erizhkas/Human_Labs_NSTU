#include "dataStruct.h"
#include <iostream>

using namespace ftxui;

int main() {
    // Если программа всё же упадёт с исключением, после закрытия
    // полноэкранного режима в консоли останется причина
    try {
        auto screen = App::Fullscreen();
        DataHuman data(10, 10, 10, 10);
        screen.Loop(data.RenderAllTables(screen.ExitLoopClosure()));
    } catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
