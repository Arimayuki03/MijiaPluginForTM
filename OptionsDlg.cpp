// OptionsDlg.cpp - 设置对话框实现（纯Win32，无RC文件，多设备）
#include "pch.h"
#include "OptionsDlg.h"
#include "PluginConfig.h"
#include "MiioDevice.h"
#include "MijiaPowerPlugin.h"
#include <commctrl.h>
#include <string>
#include <sstream>

// 控件 ID 统一定义在 resource.h（OptionsDlg.h 已包含），此处不再重复

// ─── 对话框状态（每次 Show 调用使用局部结构体传递） ───
struct DlgState {
    bool result = false;
    bool closed = false;
    std::vector<DeviceConfig> devices;   // 设备工作副本，确定时写回配置
    int curSel = -1;                     // 当前选中的设备索引
    HFONT hFont = nullptr;               // 对话框字体（关闭时销毁）
    CMijiaPowerPlugin* plugin = nullptr; // “清除历史”时同步清理内存数据
    HWND hList = nullptr;
    HWND hEditIp = nullptr, hEditToken = nullptr, hEditName = nullptr, hEditInterval = nullptr;
    HWND hCheckRecord = nullptr, hCheckLabel = nullptr, hCheckUnit = nullptr, hCheckTotal = nullptr;
    HWND hComboDecimal = nullptr, hStaticStatus = nullptr;
    HWND hBtnTest = nullptr, hBtnClearHistory = nullptr;
    HWND hBtnAdd = nullptr, hBtnDel = nullptr;
};

// ─── 编辑框内容 → 当前选中设备 ───
static void CaptureFieldsToWorking(DlgState* st) {
    if (st->curSel < 0 || st->curSel >= (int)st->devices.size()) return;
    wchar_t buf[512];
    GetWindowTextW(st->hEditName,  buf, 512); st->devices[st->curSel].name  = buf;
    GetWindowTextW(st->hEditIp,    buf, 512); st->devices[st->curSel].ip    = buf;
    GetWindowTextW(st->hEditToken, buf, 512); st->devices[st->curSel].token = buf;
}

// ─── 当前选中设备 → 编辑框 ───
static void LoadFieldsFromWorking(DlgState* st) {
    if (st->curSel < 0 || st->curSel >= (int)st->devices.size()) {
        SetWindowTextW(st->hEditName, L"");
        SetWindowTextW(st->hEditIp, L"");
        SetWindowTextW(st->hEditToken, L"");
        return;
    }
    const DeviceConfig& d = st->devices[st->curSel];
    SetWindowTextW(st->hEditName,  d.name.c_str());
    SetWindowTextW(st->hEditIp,    d.ip.c_str());
    SetWindowTextW(st->hEditToken, d.token.c_str());
}

static void RefreshDeviceList(DlgState* st) {
    SendMessageW(st->hList, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < (int)st->devices.size(); ++i) {
        const DeviceConfig& d = st->devices[i];
        wchar_t line[560];
        if (d.ip.empty())
            swprintf(line, 560, L"%d. %ls (未设置IP)", i + 1, d.name.c_str());
        else
            swprintf(line, 560, L"%d. %ls (%ls)", i + 1, d.name.c_str(), d.ip.c_str());
        SendMessageW(st->hList, LB_ADDSTRING, 0, (LPARAM)line);
    }
    if (st->curSel >= 0 && st->curSel < (int)st->devices.size())
        SendMessageW(st->hList, LB_SETCURSEL, st->curSel, 0);
}

static void CreateControls(HWND hWnd, DlgState* st) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE);

    // 固定 14px 字体（与 v1.0 一致，不随 DPI 放大，保证 4K 高缩放下窗口紧凑）
    LOGFONTW lf = {};
    lf.lfHeight = -14;
    lf.lfWeight = FW_NORMAL;
    lstrcpynW(lf.lfFaceName, L"微软雅黑", LF_FACESIZE);
    st->hFont = CreateFontIndirectW(&lf);
    HFONT hFont = st->hFont;
    if (!hFont) hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    auto addCtrl = [&](LPCWSTR cls, LPCWSTR text, DWORD style,
                       int x, int y, int w, int h, int id) -> HWND {
        HWND hw = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                                  x, y, w, h, hWnd, (HMENU)(intptr_t)id, hInst, NULL);
        if (hw && hFont) SendMessageW(hw, WM_SETFONT, (WPARAM)hFont, TRUE);
        return hw;
    };

    // ─── 设备列表分组 ───
    addCtrl(L"BUTTON", L"设备列表（米家插座，最多8个）", BS_GROUPBOX, 10, 8, 530, 116, 0);
    st->hList = addCtrl(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_HASSTRINGS,
                        24, 26, 320, 86, IDC_LIST_DEVICES);
    st->hBtnAdd = addCtrl(L"BUTTON", L"添加设备", BS_PUSHBUTTON | WS_TABSTOP, 352, 26, 150, 26, IDC_BTN_ADDDEV);
    st->hBtnDel = addCtrl(L"BUTTON", L"删除选中设备", BS_PUSHBUTTON | WS_TABSTOP, 352, 58, 150, 26, IDC_BTN_DELDEV);

    // ─── 设备设置分组 ───
    addCtrl(L"BUTTON", L"选中设备设置", BS_GROUPBOX, 10, 128, 530, 164, 0);
    addCtrl(L"STATIC", L"名称:", 0, 24, 148, 48, 20, 0);
    st->hEditName = addCtrl(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 76, 146, 150, 24, IDC_EDIT_NAME);

    addCtrl(L"STATIC", L"设备 IP:", 0, 240, 148, 60, 20, 0);
    st->hEditIp = addCtrl(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 306, 146, 210, 24, IDC_EDIT_IP);

    addCtrl(L"STATIC", L"Token:", 0, 24, 175, 48, 20, 0);
    st->hEditToken = addCtrl(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 76, 173, 440, 24, IDC_EDIT_TOKEN);

    st->hBtnTest = addCtrl(L"BUTTON", L"测试连接", BS_PUSHBUTTON | WS_TABSTOP, 76, 204, 90, 26, IDC_BTN_TEST);
    st->hStaticStatus = addCtrl(L"STATIC", L"", SS_LEFT, 176, 207, 330, 20, IDC_STATIC_STATUS);

    // ─── 插件选项分组 ───
    addCtrl(L"BUTTON", L"插件选项", BS_GROUPBOX, 10, 296, 530, 124, 0);
    st->hCheckRecord = addCtrl(L"BUTTON", L"启用功率历史记录", BS_AUTOCHECKBOX | WS_TABSTOP, 24, 314, 180, 20, IDC_CHECK_RECORD);
    st->hCheckLabel  = addCtrl(L"BUTTON", L"显示设备名称标签", BS_AUTOCHECKBOX | WS_TABSTOP, 240, 314, 180, 20, IDC_CHECK_LABEL);
    st->hCheckUnit   = addCtrl(L"BUTTON", L"显示 W 单位",   BS_AUTOCHECKBOX | WS_TABSTOP, 24, 338, 180, 20, IDC_CHECK_UNIT);
    st->hCheckTotal  = addCtrl(L"BUTTON", L"显示总功率项（多设备合计）", BS_AUTOCHECKBOX | WS_TABSTOP, 240, 338, 250, 20, IDC_CHECK_TOTAL);

    addCtrl(L"STATIC", L"采集间隔（秒）:", 0, 24, 366, 120, 20, 0);
    st->hEditInterval = addCtrl(L"EDIT", L"3", WS_BORDER | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
                                148, 364, 60, 24, IDC_EDIT_INTERVAL);

    addCtrl(L"STATIC", L"小数位数:", 0, 240, 366, 80, 20, 0);
    st->hComboDecimal = addCtrl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                                324, 364, 90, 80, IDC_COMBO_DECIMAL);
    SendMessageW(st->hComboDecimal, CB_ADDSTRING, 0, (LPARAM)L"0位");
    SendMessageW(st->hComboDecimal, CB_ADDSTRING, 0, (LPARAM)L"1位");
    SendMessageW(st->hComboDecimal, CB_ADDSTRING, 0, (LPARAM)L"2位");

    // ─── 历史数据分组 ───
    addCtrl(L"BUTTON", L"历史数据", BS_GROUPBOX, 10, 424, 530, 52, 0);
    addCtrl(L"STATIC", L"历史按设备 IP 保存为 MijiaPower_history_<IP>.json（配置目录内）",
            SS_LEFT | SS_WORDELLIPSIS, 24, 444, 300, 26, IDC_STATIC_HISTORY);
    st->hBtnClearHistory = addCtrl(L"BUTTON", L"清除历史", BS_PUSHBUTTON | WS_TABSTOP, 352, 438, 150, 26, IDC_BTN_CLEARHISTORY);

    // ─── 底部按钮 ───
    addCtrl(L"BUTTON", L"确定", BS_DEFPUSHBUTTON | WS_TABSTOP, 330, 488, 90, 28, IDC_BTN_OK);
    addCtrl(L"BUTTON", L"取消", BS_PUSHBUTTON | WS_TABSTOP,    430, 488, 90, 28, IDC_BTN_CANCEL);

    // ─── 填充现有配置 ───
    auto cfg = ConfigManager::Instance().Get();
    st->devices = cfg.devices;
    if (st->devices.empty()) st->devices.push_back(DeviceConfig{});
    st->curSel = 0;
    RefreshDeviceList(st);
    LoadFieldsFromWorking(st);

    wchar_t buf[32];
    _itow_s(cfg.updateIntervalSec, buf, 10);
    SetWindowTextW(st->hEditInterval, buf);

    SendMessageW(st->hCheckRecord, BM_SETCHECK, cfg.enableRecording ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(st->hCheckLabel,  BM_SETCHECK, cfg.showLabel       ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(st->hCheckUnit,   BM_SETCHECK, cfg.showUnit        ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(st->hCheckTotal,  BM_SETCHECK, cfg.showTotal       ? BST_CHECKED : BST_UNCHECKED, 0);

    int dec = cfg.decimalPlaces;
    if (dec < 0) dec = 0;
    if (dec > 2) dec = 2;
    SendMessageW(st->hComboDecimal, CB_SETCURSEL, dec, 0);
}

static bool SaveFromDialog(DlgState* st) {
    // 在副本上修改，再经 Set() 加锁写回（配置对象同时被采集线程读取）
    PluginConfig cfg = ConfigManager::Instance().Get();
    wchar_t buf[512];

    CaptureFieldsToWorking(st);
    cfg.devices = st->devices;

    GetWindowTextW(st->hEditInterval, buf, 32);
    try { cfg.updateIntervalSec = std::stoi(buf); } catch (...) { cfg.updateIntervalSec = 3; }
    if (cfg.updateIntervalSec < 1) cfg.updateIntervalSec = 1;
    if (cfg.updateIntervalSec > 60) cfg.updateIntervalSec = 60;

    cfg.enableRecording = (SendMessageW(st->hCheckRecord, BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg.showLabel       = (SendMessageW(st->hCheckLabel,  BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg.showUnit        = (SendMessageW(st->hCheckUnit,   BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg.showTotal       = (SendMessageW(st->hCheckTotal,  BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg.decimalPlaces   = (int)SendMessageW(st->hComboDecimal, CB_GETCURSEL, 0, 0);
    if (cfg.decimalPlaces < 0) cfg.decimalPlaces = 1;

    ConfigManager::Instance().Set(cfg);
    ConfigManager::Instance().Save();
    return true;
}

// ─── 窗口过程 ───
static LRESULT CALLBACK DlgWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // 从窗口用户数据取回 DlgState*
    DlgState* st = reinterpret_cast<DlgState*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
        // lParam 是 CREATESTRUCT*，其 lpCreateParams 就是我们传入的 DlgState*
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        st = reinterpret_cast<DlgState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)st);
        CreateControls(hWnd, st);
        return 0;
    }

    case WM_COMMAND: {
        if (!st) break;
        int id = LOWORD(wParam);

        if (id == IDC_BTN_OK) {
            if (SaveFromDialog(st)) {
                st->result = true;
                st->closed = true;
                DestroyWindow(hWnd);
            }
        } else if (id == IDC_BTN_CANCEL) {
            st->result = false;
            st->closed = true;
            DestroyWindow(hWnd);
        } else if (id == IDC_LIST_DEVICES && HIWORD(wParam) == LBN_SELCHANGE) {
            // 先把编辑框内容保存到旧选中项，再切换
            CaptureFieldsToWorking(st);
            st->curSel = (int)SendMessageW(st->hList, LB_GETCURSEL, 0, 0);
            LoadFieldsFromWorking(st);
        } else if (id == IDC_BTN_ADDDEV) {
            if ((int)st->devices.size() >= MAX_DEVICES) {
                SetWindowTextW(st->hStaticStatus, L"最多支持 8 个设备");
                break;
            }
            CaptureFieldsToWorking(st);
            DeviceConfig d;
            d.name = L"插座" + std::to_wstring(st->devices.size() + 1);
            st->devices.push_back(d);
            st->curSel = (int)st->devices.size() - 1;
            RefreshDeviceList(st);
            LoadFieldsFromWorking(st);
            SetFocus(st->hEditIp);
        } else if (id == IDC_BTN_DELDEV) {
            if ((int)st->devices.size() <= 1) {
                SetWindowTextW(st->hStaticStatus, L"至少保留一个设备，不需要的可直接删除条目");
                break;
            }
            int sel = st->curSel;
            if (sel < 0 || sel >= (int)st->devices.size()) sel = 0;
            st->devices.erase(st->devices.begin() + sel);
            if (st->curSel >= (int)st->devices.size()) st->curSel = (int)st->devices.size() - 1;
            RefreshDeviceList(st);
            LoadFieldsFromWorking(st);
        } else if (id == IDC_BTN_TEST) {
            wchar_t ipBuf[256], tokenBuf[256];
            GetWindowTextW(st->hEditIp,    ipBuf,    256);
            GetWindowTextW(st->hEditToken, tokenBuf, 256);

            if (wcslen(ipBuf) == 0 || wcslen(tokenBuf) == 0) {
                SetWindowTextW(st->hStaticStatus, L"请填写 IP 和 Token");
                break;
            }
            if (!IsValidToken(tokenBuf)) {
                SetWindowTextW(st->hStaticStatus, L"Token 格式错误：应为 32 位十六进制字符串");
                break;
            }
            SetWindowTextW(st->hStaticStatus, L"正在连接...");
            UpdateWindow(hWnd);

            char ip[256], token[256];
            WideCharToMultiByte(CP_UTF8, 0, ipBuf,    -1, ip,    256, NULL, NULL);
            WideCharToMultiByte(CP_UTF8, 0, tokenBuf, -1, token, 256, NULL, NULL);

            try {
                MiioDevice dev(ip, token, 5000);
                double watts = 0;
                if (dev.GetPower(watts)) {
                    wchar_t msg2[128];
                    swprintf(msg2, 128, L"连接成功！当前功率：%.1fW", watts);
                    SetWindowTextW(st->hStaticStatus, msg2);
                } else {
                    SetWindowTextW(st->hStaticStatus, L"连接失败，请检查 IP/Token 和网络");
                }
            } catch (...) {
                SetWindowTextW(st->hStaticStatus, L"连接异常，请检查 IP/Token");
            }
        } else if (id == IDC_BTN_CLEARHISTORY) {
            if (MessageBoxW(hWnd, L"确定要清除所有设备的功率历史记录吗？此操作不可撤销。",
                            L"确认", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                if (st->plugin) {
                    // 内存与磁盘一起清，避免采样线程稍后把旧数据写回文件
                    st->plugin->ClearAllHistory();
                } else {
                    // 无插件实例（理论上不会发生）：退化为只删文件（含已移除设备遗留的文件）
                    auto& cm = ConfigManager::Instance();
                    for (const auto& path : cm.GetAllHistoryFilePaths())
                        DeleteFileW(path.c_str());
                }
                SetWindowTextW(st->hStaticStatus, L"历史记录已清除");
            }
        }
        break;
    }

    case WM_CLOSE:
        if (st) { st->result = false; st->closed = true; }
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        // 关键：绝对不能调用 PostQuitMessage！这里什么都不做
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ─── 窗口类注册（仅主线程调用；只注册一次）───
static ATOM RegisterDlgClass(HINSTANCE hInst) {
    static ATOM atom = 0;
    if (atom == 0) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = DlgWndProc;
        wc.hInstance     = hInst;
        wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"MijiaPowerOptDlg";
        wc.hIcon         = LoadIconW(NULL, IDI_APPLICATION);
        atom = RegisterClassExW(&wc);
    }
    return atom;
}

// ─── 显示对话框（模态，不破坏主消息循环）───
bool COptionsDlg::Show(HWND hParent, CMijiaPowerPlugin* plugin) {
    HINSTANCE hInst = hParent
        ? (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE)
        : GetModuleHandleW(NULL);

    RegisterDlgClass(hInst);

    DlgState state;
    state.plugin = plugin;

    // 固定客户区尺寸（紧凑布局，与 v1.0 密度一致，不随 DPI 放大），
    // 用 AdjustWindowRectEx 反推外框尺寸，保证标题栏不挤占客户区
    RECT rc = { 0, 0, 550, 528 };
    AdjustWindowRectEx(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE,
                       WS_EX_DLGMODALFRAME | WS_EX_TOPMOST);
    int W = rc.right - rc.left;
    int H = rc.bottom - rc.top;
    int px = CW_USEDEFAULT, py = CW_USEDEFAULT;
    if (hParent) {
        RECT rcp{};
        GetWindowRect(hParent, &rcp);
        px = rcp.left + (rcp.right  - rcp.left - W) / 2;
        py = rcp.top  + (rcp.bottom - rcp.top  - H) / 2;
        if (px < 0) px = 0;
        if (py < 0) py = 0;
    }

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"MijiaPowerOptDlg",
        L"米家插座功率插件 - 设置",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        px, py, W, H,
        hParent, NULL, hInst,
        &state   // 传给 WM_CREATE 的 CREATESTRUCT::lpCreateParams
    );

    if (!hDlg) return false;

    // 禁用父窗口（实现模态效果）
    if (hParent) EnableWindow(hParent, FALSE);
    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    // 消息循环：只处理此对话框的消息，不 PostQuitMessage
    MSG msg{};
    while (!state.closed && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsWindow(hDlg)) break;
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    // 如果窗口还活着（用户关闭了消息循环之外），强制销毁
    if (IsWindow(hDlg)) DestroyWindow(hDlg);

    // 销毁对话框字体（兜底的 stock 字体除外，不应删除）
    if (state.hFont && state.hFont != (HFONT)GetStockObject(DEFAULT_GUI_FONT))
        DeleteObject(state.hFont);

    // 恢复父窗口
    if (hParent) {
        EnableWindow(hParent, TRUE);
        SetForegroundWindow(hParent);
    }

    return state.result;
}
