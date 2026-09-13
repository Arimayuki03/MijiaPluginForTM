// test_harness.cpp - 模拟 TrafficMonitor 加载插件的冒烟测试
// 用法: test_harness.exe <dll路径> <配置目录>
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string>
#include "PluginInterface.h"

static std::string U8(const wchar_t* w) {
    if (!w) return "(null)";
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, 0, 0);
    std::string s(n > 0 ? n - 1 : 0, 0);
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, 0, 0);
    return s;
}

static void DumpItems(ITMPlugin* p, const char* tag) {
    printf("== %s ==\n", tag);
    for (int i = 0; i < 10; ++i) {
        IPluginItem* it = p->GetItem(i);
        if (!it) { printf("  [%d] <null>\n", i); continue; }
        printf("  [%d] id=%s | name=%s | label=\"%s\" | value=\"%s\" | sample=%s\n",
               i, U8(it->GetItemId()).c_str(), U8(it->GetItemName()).c_str(),
               U8(it->GetItemLableText()).c_str(), U8(it->GetItemValueText()).c_str(),
               U8(it->GetItemValueSampleText()).c_str());
    }
    printf("  tooltip:\n%s\n", U8(p->GetTooltipInfo()).c_str());
    fflush(stdout);
}

int main() {
    int argcW = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argcW);
    if (argcW < 3) { printf("usage: test_harness <dll> <configdir>\n"); return 1; }

    HMODULE h = LoadLibraryW(argvW[1]);
    if (!h) { printf("LoadLibrary failed: %lu\n", GetLastError()); return 1; }
    auto fn = (ITMPlugin* (*)())GetProcAddress(h, "TMPluginGetInstance");
    if (!fn) { printf("no export TMPluginGetInstance\n"); return 1; }
    ITMPlugin* p = fn();
    printf("API version: %d, name=%s ver=%s\n", p->GetAPIVersion(),
           U8(p->GetInfo(ITMPlugin::TMI_NAME)).c_str(), U8(p->GetInfo(ITMPlugin::TMI_VERSION)).c_str());
    fflush(stdout);

    p->OnExtenedInfo(ITMPlugin::EI_CONFIG_DIR, argvW[2]);
    p->DataRequired();

    DumpItems(p, "加载后(未采样)");
    Sleep(10000);   // 等采样线程连接真实设备
    DumpItems(p, "采样10秒后");

    // 注意：插件采样线程随进程生命周期存在（与 TrafficMonitor 一致），直接退出进程
    ExitProcess(0);
    return 0;
}
