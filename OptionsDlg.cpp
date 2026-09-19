// OptionsDlg.cpp - 设置对话框实现（纯Win32，无RC文件，多设备）
// 布局：左侧设备列表（行内复选框：启用 / 计入总功率），右侧选中设备设置，
//       下方插件选项 / 历史数据分区，底部确定/取消
// DPI：所有控件坐标、字体、自绘行高均按 96 DPI 设计值 × 当前 DPI 等比缩放；
//       跨显示器拖动收到 WM_DPICHANGED 时重建字体并按同一设计坐标全量重排，
//       修复混合 DPI 多屏下窗口与内容比例失调、右列/底部被裁切的问题
#include "pch.h"
#include "OptionsDlg.h"
#include "PluginConfig.h"
#include "MiioDevice.h"
#include "MijiaPowerPlugin.h"
#include <commctrl.h>
#include <string>
#include <sstream>

// 控件 ID 统一定义在 resource.h（OptionsDlg.h 已包含），此处不再重复

// ─── 设备列表自绘参数（均为 96 DPI 设计值，运行时按 DPI 等比缩放） ───
static const int LIST_ITEM_H = 26;   // 行高（14px 微软雅黑行距约 20px，26px 保证不裁切）
static const int CB_SIZE = 13;       // 行内复选框边长
// 复选框点击热区（相对行矩形左/右边缘）
static const int CB_LEFT_HOT = 22;           // 左复选框热区 [0, CB_LEFT_HOT]
static const int CB_RIGHT_HOT = 28;          // 右复选框热区 [rc.right-CB_RIGHT_HOT, rc.right-4]

// ─── DPI 缩放 ───
// 对话框按所在显示器的 DPI 整体等比缩放（字体与控件坐标用同一因子），
// 100% 缩放下与设计值逐像素一致；跨屏拖动时经 WM_DPICHANGED 全量重排
static int ScaleByDpi(int v, UINT dpi) { return MulDiv(v, (int)dpi, 96); }

static UINT GetDialogDpi(HWND hParent) {
    // 优先取对话框所在显示器的每显示器 DPI（Win8.1+，经 Shcore.dll 动态加载），
    // 混合 DPI 多屏下也能取对；不可用时退回父窗口 DPI，再退回系统 DPI
    HMONITOR hMon = hParent ? MonitorFromWindow(hParent, MONITOR_DEFAULTTONEAREST) : NULL;
    if (!hMon) {
        POINT pt{};
        if (GetCursorPos(&pt)) hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    }
    if (hMon) {
        // 模块句柄提为函数局部 static 缓存：Shcore.dll 为系统 DLL，有意保留至
        // 进程结束（不 FreeLibrary），避免每次打开设置对话框重复加载/释放
        static HMODULE s_hShcore = nullptr;
        if (!s_hShcore) {
            s_hShcore = GetModuleHandleW(L"Shcore.dll");
            if (!s_hShcore) s_hShcore = LoadLibraryW(L"Shcore.dll");
        }
        HMODULE hShcore = s_hShcore;
        if (hShcore) {
            auto pfn = reinterpret_cast<HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*)>(
                GetProcAddress(hShcore, "GetDpiForMonitor"));
            UINT dx = 0, dy = 0;
            if (pfn && SUCCEEDED(pfn(hMon, 0 /*MDT_EFFECTIVE_DPI*/, &dx, &dy)) && dx >= 96)
                return dx;
        }
    }
    if (hParent) {
        HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
        if (hUser32) {
            auto pfn = reinterpret_cast<UINT(WINAPI*)(HWND)>(
                GetProcAddress(hUser32, "GetDpiForWindow"));
            if (pfn) {
                UINT dpi = pfn(hParent);
                if (dpi >= 96) return dpi;
            }
        }
    }
    HDC hdc = GetDC(NULL);
    UINT dpi = 96;
    if (hdc) {
        int v = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(NULL, hdc);
        if (v >= 96) dpi = (UINT)v;
    }
    return dpi;
}

// ─── 对话框状态（每次 Show 调用使用局部结构体传递） ───
struct DlgState {
    struct CtlRec {                  // 子控件及其 96 DPI 设计矩形（重排时的唯一布局来源）
        HWND hw; int x, y, w, h;
    };
    bool result = false;
    bool closed = false;
    std::vector<DeviceConfig> devices;   // 设备工作副本，确定时写回配置
    int curSel = -1;                     // 当前选中的设备索引
    HFONT hFont = nullptr;               // 对话框字体（关闭时销毁）
    std::vector<HFONT> oldFonts;         // DPI 变更换下的旧字体（销毁后统一删除）
    CMijiaPowerPlugin* plugin = nullptr; // “清除历史”时同步清理内存数据
    HWND hList = nullptr;
    WNDPROC oldListProc = nullptr;       // 设备列表原始窗口过程（随 DlgState 生命周期，本次 Show 有效）
    HWND hEditIp = nullptr, hEditToken = nullptr, hEditName = nullptr, hEditInterval = nullptr;
    HWND hCheckRecord = nullptr, hCheckLabel = nullptr, hCheckUnit = nullptr, hCheckTotal = nullptr;
    HWND hComboDecimal = nullptr, hComboTtStats = nullptr, hStaticStatus = nullptr;
    HWND hBtnTest = nullptr, hBtnClearHistory = nullptr;
    HWND hBtnAdd = nullptr, hBtnDel = nullptr;
    std::vector<CtlRec> ctls;            // 全部子控件（含分组框/标签）
    int ttHours[6] = { 1, 2, 3, 6, 12, 24 }; // 下拉项对应的小时数
    UINT dpi = 96;                       // 对话框所在显示器的 DPI（布局/自绘/热区共用）
};

// ─── 编辑框内容 → 当前选中设备 ───
static void CaptureFieldsToWorking(DlgState* st) {
    if (st->curSel < 0 || st->curSel >= (int)st->devices.size()) return;
    wchar_t buf[512];
    GetWindowTextW(st->hEditName,  buf, 512); st->devices[st->curSel].name  = buf;
    GetWindowTextW(st->hEditIp,    buf, 512); st->devices[st->curSel].ip    = buf;
    GetWindowTextW(st->hEditToken, buf, 512); st->devices[st->curSel].token = buf;
    // 净化设备名：剔除换行/制表符等控制字符，防止破坏任务栏标签排版并干扰
    // 400 字符护栏计算；IP/Token 不做净化
    SanitizeDeviceName(st->devices[st->curSel].name);
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
        // 名称/IP 过长时先钳制到固定上限（超出取前 63/199 字符 + 省略号），
        // 避免 swprintf 静默截断整行；用局部副本，不改动 st->devices 原数据
        auto clamp = [](const std::wstring& s, size_t keep) -> std::wstring {
            if (s.size() <= keep) return s;
            return s.substr(0, keep) + L"…";
        };
        std::wstring name = clamp(d.name, 63);
        std::wstring ip   = clamp(d.ip, 199);
        wchar_t line[560];
        if (ip.empty())
            swprintf(line, 560, L"%d. %ls (未设置IP)", i + 1, name.c_str());
        else
            swprintf(line, 560, L"%d. %ls (%ls)", i + 1, name.c_str(), ip.c_str());
        SendMessageW(st->hList, LB_ADDSTRING, 0, (LPARAM)line);
    }
    if (st->curSel >= 0 && st->curSel < (int)st->devices.size())
        SendMessageW(st->hList, LB_SETCURSEL, st->curSel, 0);
    InvalidateRect(st->hList, NULL, TRUE);
}

// ─── 切换行内复选框：col = 0 启用 / 1 计入总功率 ───
static void ToggleDeviceFlag(DlgState* st, int index, int col) {
    if (!st || index < 0 || index >= (int)st->devices.size()) return;
    if (col == 0) st->devices[index].enabled = !st->devices[index].enabled;
    else          st->devices[index].inTotal = !st->devices[index].inTotal;
    if (st->hList) InvalidateRect(st->hList, NULL, TRUE);
}

// 自绘行高/复选框/热区按 DPI 缩放后的实际像素
static int DpiItemH(const DlgState* st)   { return ScaleByDpi(LIST_ITEM_H,   st ? st->dpi : 96); }
static int DpiCbSize(const DlgState* st)  { return ScaleByDpi(CB_SIZE,      st ? st->dpi : 96); }
static int DpiLeftHot(const DlgState* st) { return ScaleByDpi(CB_LEFT_HOT,  st ? st->dpi : 96); }
static int DpiRightHot(const DlgState* st){ return ScaleByDpi(CB_RIGHT_HOT, st ? st->dpi : 96); }

// ─── 设备列表子类化：点击行首/行尾复选框切换，空格切换“启用” ───
static LRESULT CALLBACK ListSubProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    DlgState* st = reinterpret_cast<DlgState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (msg) {
    case WM_LBUTTONDOWN: {
        DWORD hit = (DWORD)SendMessageW(h, LB_ITEMFROMPOINT, 0, lp);
        int x = (short)LOWORD(lp);
        if (HIWORD(hit) == 0) {
            RECT rc{};
            GetClientRect(h, &rc);
            int leftHot = DpiLeftHot(st), rightHot = DpiRightHot(st);
            if (x >= 2 && x <= leftHot)
                ToggleDeviceFlag(st, (int)LOWORD(hit), 0);
            else if (x >= rc.right - rightHot && x <= rc.right - 4)
                ToggleDeviceFlag(st, (int)LOWORD(hit), 1);
        }
        break; // 继续默认处理以完成行选中
    }
    case WM_KEYDOWN:
        if (wp == VK_SPACE) {
            ToggleDeviceFlag(st, (int)SendMessageW(h, LB_GETCURSEL, 0, 0), 0);
            return 0;
        }
        break;
    }
    return CallWindowProcW(st ? st->oldListProc : DefWindowProcW, h, msg, wp, lp);
}

// ─── 自绘设备列表行：[启用□] 文本 …… [计入□] ───
static void DrawDeviceItem(const DRAWITEMSTRUCT* dis, DlgState* st) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    UINT dpi = st ? st->dpi : 96;
    bool selected = (dis->itemState & ODS_SELECTED) != 0;

    int id = (int)dis->itemID;
    bool enabled = st && id >= 0 && id < (int)st->devices.size() && st->devices[id].enabled;
    bool checkedTotal = st && id >= 0 && id < (int)st->devices.size() && st->devices[id].inTotal;

    COLORREF bg = selected ? GetSysColor(COLOR_HIGHLIGHT)     : GetSysColor(COLOR_WINDOW);
    COLORREF fg = selected ? GetSysColor(COLOR_HIGHLIGHTTEXT) : GetSysColor(COLOR_WINDOWTEXT);
    if (!enabled && !selected) fg = GetSysColor(COLOR_GRAYTEXT);
    HBRUSH hbr = CreateSolidBrush(bg);
    FillRect(hdc, &rc, hbr);
    DeleteObject(hbr);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, fg);

    // 绘制文本必须显式选入对话框字体，否则用 DC 默认字体度量导致基线偏移
    HFONT hFont = (HFONT)SendMessageW(dis->hwndItem, WM_GETFONT, 0, 0);
    HFONT hOldFont = hFont ? (HFONT)SelectObject(hdc, hFont) : nullptr;

    // 左复选框：启用
    int cb = DpiCbSize(st);
    int h = rc.bottom - rc.top;
    int cy = rc.top + (h - cb) / 2;
    RECT rcBoxL = { rc.left + ScaleByDpi(5, dpi), cy,
                    rc.left + ScaleByDpi(5, dpi) + cb, cy + cb };
    DrawFrameControl(hdc, &rcBoxL, DFC_BUTTON,
                     DFCS_BUTTONCHECK | (enabled ? DFCS_CHECKED : 0));

    // 行文本
    int len = (int)SendMessageW(dis->hwndItem, LB_GETTEXTLEN, id, 0);
    std::wstring text;
    if (len > 0) {
        text.resize(len + 1);
        SendMessageW(dis->hwndItem, LB_GETTEXT, id, (LPARAM)text.data());
        text.resize(len);
    }
    RECT rcText = { rc.left + DpiLeftHot(st) + ScaleByDpi(4, dpi), rc.top,
                    rc.right - DpiRightHot(st), rc.bottom };
    DrawTextW(hdc, text.c_str(), (int)text.size(), &rcText,
              DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    // 右复选框：计入总功率（禁用设备时灰显，仍可点击修改存值）
    RECT rcBoxR = { rc.right - DpiRightHot(st) + ScaleByDpi(2, dpi), cy,
                    rc.right - DpiRightHot(st) + ScaleByDpi(2, dpi) + cb, cy + cb };
    DrawFrameControl(hdc, &rcBoxR, DFC_BUTTON,
                     DFCS_BUTTONCHECK | (checkedTotal ? DFCS_CHECKED : 0) |
                     (enabled ? 0 : DFCS_INACTIVE));

    if (hOldFont) SelectObject(hdc, hOldFont);
    if (dis->itemState & ODS_FOCUS)
        DrawFocusRect(hdc, &rc);
}

// 对话框字体：14px 微软雅黑按 DPI 等比缩放（96 DPI 下即 14px）
static HFONT CreateDlgFont(UINT dpi) {
    LOGFONTW lf = {};
    lf.lfHeight = -MulDiv(14, (int)dpi, 96);
    lf.lfWeight = FW_NORMAL;
    lstrcpynW(lf.lfFaceName, L"微软雅黑", LF_FACESIZE);
    return CreateFontIndirectW(&lf);
}

// 按 DlgState::ctls 记录的设计坐标 × 当前 DPI 全量重排子控件。
// 创建时与 WM_DPICHANGED 时共用，保证窗口尺寸与内容永远同比例
static void RelayoutAll(HWND hWnd, DlgState* st) {
    UINT dpi = st->dpi;
    for (const auto& c : st->ctls) {
        if (!c.hw) continue;
        SetWindowPos(c.hw, NULL,
                     ScaleByDpi(c.x, dpi), ScaleByDpi(c.y, dpi),
                     ScaleByDpi(c.w, dpi), ScaleByDpi(c.h, dpi),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (st->hList) {
        SendMessageW(st->hList, LB_SETITEMHEIGHT, 0, DpiItemH(st));
        InvalidateRect(st->hList, NULL, TRUE);
    }
    InvalidateRect(hWnd, NULL, TRUE);
}

static void CreateControls(HWND hWnd, DlgState* st, UINT dpi) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE);

    st->hFont = CreateDlgFont(dpi);
    HFONT hFont = st->hFont;
    if (!hFont) hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    // 所有控件坐标按 DPI 整体缩放（S=scale helper），并把设计矩形记入 st->ctls 供重排
    auto S = [dpi](int v) -> int { return ScaleByDpi(v, dpi); };

    auto addCtrl = [&](LPCWSTR cls, LPCWSTR text, DWORD style,
                       int x, int y, int w, int h, int id) -> HWND {
        HWND hw = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                                  S(x), S(y), S(w), S(h), hWnd, (HMENU)(intptr_t)id, hInst, NULL);
        if (hw && hFont) SendMessageW(hw, WM_SETFONT, (WPARAM)hFont, TRUE);
        if (hw) st->ctls.push_back({ hw, x, y, w, h });
        return hw;
    };

    // ─── 设备列表分组（左列）：行首“启用”、行尾“计入总功率”复选框 ───
    addCtrl(L"BUTTON", L"设备列表（最多8个）", BS_GROUPBOX, 10, 8, 302, 236, 0);
    addCtrl(L"STATIC", L"启用", 0, 24, 26, 36, 18, 0);
    addCtrl(L"STATIC", L"计入总功率", SS_RIGHT, 210, 26, 84, 18, 0);
    st->hList = addCtrl(L"LISTBOX", L"",
                        WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_HASSTRINGS | LBS_OWNERDRAWFIXED,
                        22, 44, 278, 140, IDC_LIST_DEVICES);
    SendMessageW(st->hList, LB_SETITEMHEIGHT, 0, DpiItemH(st));
    SetWindowLongPtrW(st->hList, GWLP_USERDATA, (LONG_PTR)st);
    st->oldListProc = (WNDPROC)SetWindowLongPtrW(st->hList, GWLP_WNDPROC, (LONG_PTR)ListSubProc);
    st->hBtnAdd = addCtrl(L"BUTTON", L"添加设备", BS_PUSHBUTTON | WS_TABSTOP, 22, 192, 132, 26, IDC_BTN_ADDDEV);
    st->hBtnDel = addCtrl(L"BUTTON", L"删除选中设备", BS_PUSHBUTTON | WS_TABSTOP, 168, 192, 132, 26, IDC_BTN_DELDEV);

    // ─── 选中设备设置分组（右列）：标签在上、输入框通栏 ───
    // 状态文字独占按钮下方通栏一行（单行省略号），避免长提示被截断或折行溢出
    addCtrl(L"BUTTON", L"选中设备设置", BS_GROUPBOX, 322, 8, 288, 236, 0);
    addCtrl(L"STATIC", L"名称", 0, 334, 26, 120, 18, 0);
    st->hEditName = addCtrl(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 334, 46, 264, 24, IDC_EDIT_NAME);

    addCtrl(L"STATIC", L"设备 IP", 0, 334, 74, 120, 18, 0);
    st->hEditIp = addCtrl(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 334, 94, 264, 24, IDC_EDIT_IP);

    addCtrl(L"STATIC", L"Token", 0, 334, 122, 120, 18, 0);
    st->hEditToken = addCtrl(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 334, 142, 264, 24, IDC_EDIT_TOKEN);

    st->hBtnTest = addCtrl(L"BUTTON", L"测试连接", BS_PUSHBUTTON | WS_TABSTOP, 334, 176, 90, 26, IDC_BTN_TEST);
    st->hStaticStatus = addCtrl(L"STATIC", L"",
                                SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, 334, 208, 264, 20, IDC_STATIC_STATUS);

    // ─── 插件选项分组：两行对齐网格 ───
    // 第一行四个复选框等距分布；第二行“标签右对齐 + 值控件”三组成一条基线
    addCtrl(L"BUTTON", L"插件选项", BS_GROUPBOX, 10, 252, 600, 84, 0);
    st->hCheckRecord = addCtrl(L"BUTTON", L"启用功率历史记录", BS_AUTOCHECKBOX | WS_TABSTOP, 24, 270, 140, 20, IDC_CHECK_RECORD);
    st->hCheckLabel  = addCtrl(L"BUTTON", L"显示设备名称标签", BS_AUTOCHECKBOX | WS_TABSTOP, 172, 270, 148, 20, IDC_CHECK_LABEL);
    st->hCheckUnit   = addCtrl(L"BUTTON", L"显示 W 单位", BS_AUTOCHECKBOX | WS_TABSTOP, 328, 270, 100, 20, IDC_CHECK_UNIT);
    st->hCheckTotal  = addCtrl(L"BUTTON", L"显示总功率项", BS_AUTOCHECKBOX | WS_TABSTOP, 436, 270, 120, 20, IDC_CHECK_TOTAL);

    addCtrl(L"STATIC", L"采集间隔（秒）:", SS_RIGHT, 24, 302, 110, 20, 0);
    st->hEditInterval = addCtrl(L"EDIT", L"3", WS_BORDER | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
                                138, 300, 52, 24, IDC_EDIT_INTERVAL);

    addCtrl(L"STATIC", L"小数位数:", SS_RIGHT, 214, 302, 76, 20, 0);
    st->hComboDecimal = addCtrl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                                294, 298, 76, 100, IDC_COMBO_DECIMAL);
    SendMessageW(st->hComboDecimal, CB_ADDSTRING, 0, (LPARAM)L"0位");
    SendMessageW(st->hComboDecimal, CB_ADDSTRING, 0, (LPARAM)L"1位");
    SendMessageW(st->hComboDecimal, CB_ADDSTRING, 0, (LPARAM)L"2位");

    addCtrl(L"STATIC", L"悬浮统计时段:", SS_RIGHT, 400, 302, 104, 20, 0);
    st->hComboTtStats = addCtrl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                                508, 298, 90, 130, IDC_COMBO_TTSTATS);
    struct { int h; const wchar_t* label; } ttOpts[] = {
        { 1, L"1小时" }, { 2, L"2小时" }, { 3, L"3小时" },
        { 6, L"6小时" }, { 12, L"12小时" }, { 24, L"24小时" },
    };
    for (int i = 0; i < 6; ++i)
        SendMessageW(st->hComboTtStats, CB_ADDSTRING, 0, (LPARAM)ttOpts[i].label);
    st->ttHours[0] = 1; st->ttHours[1] = 2; st->ttHours[2] = 3;
    st->ttHours[3] = 6; st->ttHours[4] = 12; st->ttHours[5] = 24;

    // ─── 历史数据分组 ───
    addCtrl(L"BUTTON", L"历史数据", BS_GROUPBOX, 10, 348, 600, 52, 0);
    addCtrl(L"STATIC", L"历史按设备 IP+槽位命名，保存在插件配置目录中",
            SS_LEFT | SS_WORDELLIPSIS, 24, 368, 380, 20, IDC_STATIC_HISTORY);
    st->hBtnClearHistory = addCtrl(L"BUTTON", L"清除历史", BS_PUSHBUTTON | WS_TABSTOP, 440, 362, 156, 26, IDC_BTN_CLEARHISTORY);

    // ─── 底部按钮 ───
    addCtrl(L"BUTTON", L"确定", BS_DEFPUSHBUTTON | WS_TABSTOP, 420, 416, 90, 28, IDC_BTN_OK);
    addCtrl(L"BUTTON", L"取消", BS_PUSHBUTTON | WS_TABSTOP,    520, 416, 90, 28, IDC_BTN_CANCEL);

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

    // 悬浮统计时段：选与配置值匹配的项，无精确匹配时选最接近且不大于它的项
    int hours = cfg.tooltipStatsHours;
    if (hours < 1) hours = 1;
    if (hours > 24) hours = 24;
    int ttSel = 0;
    for (int i = 0; i < 6; ++i) {
        if (st->ttHours[i] == hours) { ttSel = i; break; }
        if (st->ttHours[i] < hours) ttSel = i;
    }
    SendMessageW(st->hComboTtStats, CB_SETCURSEL, ttSel, 0);
}

static bool SaveFromDialog(DlgState* st) {
    // 在副本上修改，再经 Set() 加锁写回（配置对象同时被采集线程读取）
    PluginConfig cfg = ConfigManager::Instance().Get();
    wchar_t buf[512];

    CaptureFieldsToWorking(st);

    // 保存前校验各设备 Token（N 为 1 起序号）：非空但格式非法时提示并返回 false
    // 保持对话框打开，避免非法 token 设备每轮采集都白等 5 秒超时；
    // 空 Token 视为“未配置”，允许保存（与既有 UX 一致）
    for (int i = 0; i < (int)st->devices.size(); ++i) {
        const std::wstring& tok = st->devices[i].token;
        if (!tok.empty() && !IsValidToken(tok)) {
            wchar_t msg[128];
            swprintf(msg, 128, L"设备 %d 的 Token 格式错误：应为 32 位十六进制字符串", i + 1);
            SetWindowTextW(st->hStaticStatus, msg);
            return false;
        }
    }

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

    int ttSel = (int)SendMessageW(st->hComboTtStats, CB_GETCURSEL, 0, 0);
    cfg.tooltipStatsHours = (ttSel >= 0 && ttSel < 6) ? st->ttHours[ttSel] : 1;

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
        CreateControls(hWnd, st, st->dpi);
        return 0;
    }

    case WM_DPICHANGED: {
        // 跨显示器拖动（宿主为 PerMonitorV2 感知进程时）：
        // 系统按新旧 DPI 比例给出建议矩形，仅缩放窗口外框；
        // 字体与子控件必须按新 DPI 自行重建/重排，否则内容与窗口比例失调被裁切
        if (!st) break;
        UINT newDpi = HIWORD(wParam);
        bool dpiChanged = (newDpi >= 96 && newDpi != st->dpi);
        if (dpiChanged) {
            st->dpi = newDpi;
            // 旧字体可能仍被未完成的重绘引用，延后到窗口销毁后再删除
            if (st->hFont) st->oldFonts.push_back(st->hFont);
            st->hFont = CreateDlgFont(newDpi);
            for (const auto& c : st->ctls)
                if (c.hw) SendMessageW(c.hw, WM_SETFONT, (WPARAM)st->hFont, TRUE);
        }
        RECT* prc = reinterpret_cast<RECT*>(lParam);
        if (prc)
            SetWindowPos(hWnd, NULL, prc->left, prc->top,
                         prc->right - prc->left, prc->bottom - prc->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        if (dpiChanged)
            RelayoutAll(hWnd, st);   // 先定外框再重排，子控件按新 DPI 与设计坐标重新布局
        return 0;
    }

    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (dis && dis->CtlID == IDC_LIST_DEVICES &&
            dis->itemAction & (ODA_DRAWENTIRE | ODA_SELECT | ODA_FOCUS)) {
            DrawDeviceItem(dis, st);
            return TRUE;
        }
        break;
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
                auto r = dev.QueryPower(watts);
                if (r == MiioQueryResult::Ok) {
                    wchar_t msg2[128];
                    swprintf(msg2, 128, L"连接成功！当前功率：%.1fW", watts);
                    SetWindowTextW(st->hStaticStatus, msg2);
                } else if (r == MiioQueryResult::NoData) {
                    // 设备在线并已应答，只是不支持功率属性——与连接失败区分开
                    SetWindowTextW(st->hStaticStatus, L"设备在线，但未返回功率属性（该型号可能不支持功率查询）");
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
    // DPI 只在此处查询一次，窗口尺寸与控件布局共用同一个值，
    // 避免两处分别查询在多显示器/混合 DPI 下取到不同结果导致窗口与控件错位
    state.dpi = GetDialogDpi(hParent);
    UINT dpi = state.dpi;

    // 客户区尺寸按 DPI 等比缩放（96 DPI 下即设计值 620x460），
    // 用 AdjustWindowRectEx 反推外框尺寸，保证标题栏不挤占客户区
    RECT rc = { 0, 0, ScaleByDpi(620, dpi), ScaleByDpi(460, dpi) };
    AdjustWindowRectEx(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE,
                       WS_EX_DLGMODALFRAME | WS_EX_TOPMOST);
    int W = rc.right - rc.left;
    int H = rc.bottom - rc.top;
    int px = CW_USEDEFAULT, py = CW_USEDEFAULT;
    // 计算父窗口/鼠标所在显示器的工作区，窗口完整落在屏幕内（多显示器也适用）
    HMONITOR hMon;
    if (hParent) hMon = MonitorFromWindow(hParent, MONITOR_DEFAULTTONEAREST);
    else {
        POINT pt{};
        GetCursorPos(&pt);
        hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    }
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    bool haveMon = GetMonitorInfoW(hMon, &mi) != 0;
    if (hParent) {
        RECT rcp{};
        GetWindowRect(hParent, &rcp);
        px = rcp.left + (rcp.right  - rcp.left - W) / 2;
        py = rcp.top  + (rcp.bottom - rcp.top  - H) / 2;
    } else if (haveMon) {
        px = mi.rcWork.left + ((mi.rcWork.right  - mi.rcWork.left) - W) / 2;
        py = mi.rcWork.top  + ((mi.rcWork.bottom - mi.rcWork.top ) - H) / 2;
    }
    if (haveMon) {
        // 左右/上下居中后整体夹入工作区，避免越界被裁切
        px = (int)std::max<LONG>(mi.rcWork.left, std::min<LONG>(px, mi.rcWork.right  - W));
        py = (int)std::max<LONG>(mi.rcWork.top,  std::min<LONG>(py, mi.rcWork.bottom - H));
        // 窗口比工作区还大（极小屏/超高缩放）时对齐左上角
        if (mi.rcWork.right  - mi.rcWork.left  < W) px = mi.rcWork.left;
        if (mi.rcWork.bottom - mi.rcWork.top   < H) py = mi.rcWork.top;
    } else {
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

    // 子控件创建完成后，按其实际占用的范围自适应校正窗口尺寸：
    // 兜底“窗口尺寸与控件布局缩放因子不一致”的情况，保证窗口永远完整包住所有控件
    RECT rcNeed = { 0, 0, 0, 0 };
    EnumChildWindows(hDlg, [](HWND h, LPARAM lp) -> BOOL {
        RECT* pNeed = reinterpret_cast<RECT*>(lp);
        RECT rc{};
        if (GetWindowRect(h, &rc)) {
            MapWindowPoints(NULL, GetAncestor(h, GA_PARENT), (POINT*)&rc, 2);
            pNeed->right  = std::max<LONG>(pNeed->right,  rc.right);
            pNeed->bottom = std::max<LONG>(pNeed->bottom, rc.bottom);
        }
        return TRUE;
    }, (LPARAM)&rcNeed);
    if (rcNeed.right > 0 && rcNeed.bottom > 0) {
        int cw = rcNeed.right  + ScaleByDpi(10, dpi);
        int ch = rcNeed.bottom + ScaleByDpi(10, dpi);
        RECT rcFit = { 0, 0, cw, ch };
        AdjustWindowRectEx(&rcFit, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE,
                           WS_EX_DLGMODALFRAME | WS_EX_TOPMOST);
        int W2 = rcFit.right - rcFit.left;
        int H2 = rcFit.bottom - rcFit.top;
        if (W2 != W || H2 != H)
            SetWindowPos(hDlg, NULL, 0, 0, W2, H2, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        // 尺寸可能变大，重新夹入工作区
        if (haveMon) {
            RECT rw{};
            GetWindowRect(hDlg, &rw);
            int nx = (int)std::max<LONG>(mi.rcWork.left, std::min<LONG>(rw.left, mi.rcWork.right  - W2));
            int ny = (int)std::max<LONG>(mi.rcWork.top,  std::min<LONG>(rw.top,  mi.rcWork.bottom - H2));
            if (nx != rw.left || ny != rw.top)
                SetWindowPos(hDlg, NULL, nx, ny, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    // 禁用父窗口（实现模态效果）
    if (hParent) EnableWindow(hParent, FALSE);
    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    // 消息循环：只处理此对话框的消息，不 PostQuitMessage
    MSG msg{};
    while (!state.closed && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    // 如果窗口还活着（用户关闭了消息循环之外），强制销毁
    if (IsWindow(hDlg)) DestroyWindow(hDlg);

    // 销毁对话框字体（兜底的 stock 字体除外，不应删除；含 DPI 变更换下的旧字体）
    if (state.hFont && state.hFont != (HFONT)GetStockObject(DEFAULT_GUI_FONT))
        DeleteObject(state.hFont);
    for (HFONT f : state.oldFonts)
        if (f && f != (HFONT)GetStockObject(DEFAULT_GUI_FONT))
            DeleteObject(f);

    // 恢复父窗口
    if (hParent) {
        EnableWindow(hParent, TRUE);
        SetForegroundWindow(hParent);
    }

    return state.result;
}
