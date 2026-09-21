#include "app.hpp"
#include <iostream>

#if defined(_WIN32) && !defined(_CONSOLE)
#include <windows.h>
#endif

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    obsidian::desktop::AppConfig config;
    config.title = L"Obsidian Terminal";
    config.width = 1440;
    config.height = 900;

    obsidian::desktop::App app;
    if (!app.initialize(config)) {
        return 1;
    }

    return app.run();
}

#if defined(_WIN32) && !defined(_CONSOLE)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    return main(0, nullptr);
}
#endif
