#include "App.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    App app;
    return app.run(hInstance, nCmdShow);
}
