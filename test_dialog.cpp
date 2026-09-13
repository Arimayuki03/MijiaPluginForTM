// test_dialog.cpp - 打开插件设置对话框供视觉验证
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include "PluginInterface.h"
int main(int argc, char**) {
    int argcW = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argcW);
    if (argcW < 3) { printf("usage: test_dialog <dll> <configdir>\n"); return 1; }
    HMODULE h = LoadLibraryW(argvW[1]);
    if (!h) { printf("LoadLibrary failed\n"); return 1; }
    auto fn = (ITMPlugin* (*)())GetProcAddress(h, "TMPluginGetInstance");
    if (!fn) { printf("no export TMPluginGetInstance\n"); return 1; }
    ITMPlugin* p = fn();
    p->OnExtenedInfo(ITMPlugin::EI_CONFIG_DIR, argvW[2]);
    p->ShowOptionsDialog(NULL);   // 阻塞在对话框消息循环
    ExitProcess(0);
    return 0;
}
