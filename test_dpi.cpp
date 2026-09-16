// test_dpi.cpp - PMv2 宿主模拟器：加载插件打开设置对话框，
// 可选先把对话框窗口移动到指定显示器（模拟跨屏拖动后重新打开/移动）。
// 用法: test_dpi.exe <dll> <configdir> [move|--screenshot]
//   move    创建对话框后把它移到副屏并等 1 秒（触发 WM_DPICHANGED），再截图
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include "PluginInterface.h"

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

struct MoveCtx { int x, y; HANDLE done; };

int main(int argc, char** argv) {
    int argcW = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argcW);
    if (argcW < 3) { printf("usage: test_dpi <dll> <configdir> [move]\n"); return 1; }
    // 模拟 TrafficMonitor：其 manifest 声明 PerMonitorV2 DPI 感知。
    // 必须在任何窗口/DC 创建之前设置，否则无效
    if (auto u32 = GetModuleHandleW(L"user32.dll")) {
        auto fn2 = (BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT))GetProcAddress(u32, "SetProcessDpiAwarenessContext");
        if (fn2) printf("SetProcessDpiAwarenessContext(PMv2) -> %d\n", fn2((DPI_AWARENESS_CONTEXT)-4));
        else printf("SetProcessDpiAwarenessContext not available\n");
    }
    HMODULE h = LoadLibraryW(argvW[1]);
    if (!h) { printf("LoadLibrary failed\n"); return 1; }
    auto fn = (ITMPlugin* (*)())GetProcAddress(h, "TMPluginGetInstance");
    if (!fn) { printf("no export\n"); return 1; }
    ITMPlugin* p = fn();
    p->OnExtenedInfo(ITMPlugin::EI_CONFIG_DIR, argvW[2]);

    // 后台线程：等对话框窗口出现 →（可选）移到副屏 → 截图 → 关闭
    struct Ctx { const wchar_t* dll; bool move; } ctx = { argvW[1], argcW > 3 };
    CreateThread(NULL, 0, [](LPVOID pv) -> DWORD {
        Ctx* c = (Ctx*)pv;
        HWND dlg = NULL;
        for (int i = 0; i < 200 && !dlg; ++i) {
            Sleep(50);
            dlg = FindWindowW(L"MijiaPowerOptDlg", NULL);
        }
        if (!dlg) { printf("[thread] dialog not found\n"); return 0; }
        RECT r; GetWindowRect(dlg, &r);
        HMONITOR mon = MonitorFromWindow(dlg, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{}; mi.cbSize = sizeof(mi);
        GetMonitorInfoW(mon, &mi);
        UINT dpi = 96;
        if (auto sh = GetModuleHandleW(L"Shcore.dll")) {
            auto f = (HRESULT(WINAPI*)(HMONITOR,int,UINT*,UINT*))GetProcAddress(sh, "GetDpiForMonitor");
            if (f) f(mon, 0, &dpi, &dpi);
        }
        printf("[thread] opened at (%ld,%ld) %ldx%ld dpi=%u\n", r.left, r.top, r.right-r.left, r.bottom-r.top, dpi);
        if (c->move) {
            // 移到副屏（主屏宽度外）
            SetWindowPos(dlg, NULL, -1500, 1400, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            Sleep(800);
            GetWindowRect(dlg, &r);
            mon = MonitorFromWindow(dlg, MONITOR_DEFAULTTONEAREST);
            GetMonitorInfoW(mon, &mi);
            dpi = 96;
            if (auto sh = GetModuleHandleW(L"Shcore.dll")) {
                auto f = (HRESULT(WINAPI*)(HMONITOR,int,UINT*,UINT*))GetProcAddress(sh, "GetDpiForMonitor");
                if (f) f(mon, 0, &dpi, &dpi);
            }
            printf("[thread] after move: (%ld,%ld) %ldx%ld dpi=%u\n", r.left, r.top, r.right-r.left, r.bottom-r.top, dpi);
            // 在副屏上先截一张（96 DPI 状态），再移回主屏观察恢复
            {
                GetWindowRect(dlg, &r);
                int w2 = r.right - r.left, h2 = r.bottom - r.top;
                HDC s2 = GetDC(NULL), m2 = CreateCompatibleDC(s2);
                HBITMAP b2 = CreateCompatibleBitmap(s2, w2, h2);
                HGDIOBJ o2 = SelectObject(m2, b2);
                PrintWindow(dlg, m2, PW_RENDERFULLCONTENT);
                BITMAPINFO bi2{}; bi2.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bi2.bmiHeader.biWidth = w2; bi2.bmiHeader.biHeight = -h2; bi2.bmiHeader.biPlanes = 1;
                bi2.bmiHeader.biBitCount = 32; bi2.bmiHeader.biCompression = BI_RGB;
                GetDIBits(m2, b2, 0, h2, NULL, &bi2, DIB_RGB_COLORS);
                void* p2 = malloc(bi2.bmiHeader.biSizeImage);
                GetDIBits(m2, b2, 0, h2, p2, &bi2, DIB_RGB_COLORS);
                BITMAPFILEHEADER f2{}; f2.bfType = 0x4D42;
                f2.bfOffBits = sizeof(f2) + sizeof(bi2.bmiHeader);
                f2.bfSize = f2.bfOffBits + bi2.bmiHeader.biSizeImage;
                FILE* fp2 = _wfopen(L"dialog_secondary.bmp", L"wb");
                fwrite(&f2, 1, sizeof(f2), fp2);
                fwrite(&bi2.bmiHeader, 1, sizeof(bi2.bmiHeader), fp2);
                fwrite(p2, 1, bi2.bmiHeader.biSizeImage, fp2);
                fclose(fp2);
                free(p2);
                SelectObject(m2, o2); DeleteObject(b2); DeleteDC(m2); ReleaseDC(NULL, s2);
                printf("[thread] secondary screenshot saved (96dpi state)\n");
            }
            // 移回主屏，观察窗口是否恢复
            SetWindowPos(dlg, NULL, 400, 300, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            Sleep(800);
            GetWindowRect(dlg, &r);
            printf("[thread] back on primary: (%ld,%ld) %ldx%ld\n", r.left, r.top, r.right-r.left, r.bottom-r.top);
        }
        // 截图保存（先等一轮 WM_PAINT 完成，避免 PrintWindow 拿到残帧）
        Sleep(500);
        GetWindowRect(dlg, &r);
        int w = r.right - r.left, hgt = r.bottom - r.top;
        HDC hdcScreen = GetDC(NULL), hdcMem = CreateCompatibleDC(hdcScreen);
        HBITMAP hbmp = CreateCompatibleBitmap(hdcScreen, w, hgt);
        HGDIOBJ old = SelectObject(hdcMem, hbmp);
        PrintWindow(dlg, hdcMem, PW_RENDERFULLCONTENT);
        BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -hgt; bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        // 先取 DIB 再写 PNG（简单起见写 BMP）
        GetDIBits(hdcMem, hbmp, 0, hgt, NULL, &bi, DIB_RGB_COLORS);
        bits = malloc(bi.bmiHeader.biSizeImage);
        GetDIBits(hdcMem, hbmp, 0, hgt, bits, &bi, DIB_RGB_COLORS);
        BITMAPFILEHEADER fh{};
        fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(bi.bmiHeader);
        fh.bfSize = fh.bfOffBits + bi.bmiHeader.biSizeImage;
        FILE* fp = _wfopen(L"dialog_shot.bmp", L"wb");
        fwrite(&fh, 1, sizeof(fh), fp);
        fwrite(&bi.bmiHeader, 1, sizeof(bi.bmiHeader), fp);
        fwrite(bits, 1, bi.bmiHeader.biSizeImage, fp);
        fclose(fp);
        free(bits);
        SelectObject(hdcMem, old);
        DeleteObject(hbmp); DeleteDC(hdcMem); ReleaseDC(NULL, hdcScreen);
        printf("[thread] screenshot saved: dialog_shot.bmp (%dx%d)\n", w, hgt);
        // 关闭对话框（发送取消）
        PostMessageW(dlg, WM_COMMAND, 1014 /*IDC_BTN_CANCEL*/, 0);
        return 0;
    }, &ctx, 0, NULL);

    p->ShowOptionsDialog(NULL);
    printf("dialog closed\n");
    ExitProcess(0);
    return 0;
}
