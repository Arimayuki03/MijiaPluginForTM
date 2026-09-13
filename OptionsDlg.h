// OptionsDlg.h - 插件设置对话框（纯Win32，无MFC依赖）
#pragma once
#include "pch.h"
#include "resource.h"

// 对话框资源ID（在resource.h中定义）
// 使用独立的 Win32 对话框，通过 DialogBoxParam 显示

class CMijiaPowerPlugin;

class COptionsDlg {
public:
    // plugin：传入插件实例，“清除历史”时同步清理内存中的历史数据；可为 nullptr（仅删文件）
    // 返回 true 表示用户点击了确定
    static bool Show(HWND hParent, CMijiaPowerPlugin* plugin = nullptr);
};
