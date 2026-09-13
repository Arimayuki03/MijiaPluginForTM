// PluginConfig.cpp - 配置管理实现（使用 Win32 INI API，多设备）
#include "pch.h"
#include "PluginConfig.h"

std::wstring ConfigManager::IniPath() const {
    return m_dir + L"\\MijiaPower.ini";
}

std::wstring ConfigManager::GetHistoryFilePath(int index) const {
    return m_dir + L"\\MijiaPower_history_" + std::to_wstring(index + 1) + L".json";
}

std::wstring ConfigManager::GetLegacyHistoryFilePath() const {
    return m_dir + L"\\MijiaPower_history.json";
}

std::wstring ConfigManager::ReadIniString(const std::wstring& section, const std::wstring& key,
                                           const std::wstring& def, const std::wstring& path) {
    wchar_t buf[512] = {};
    GetPrivateProfileStringW(section.c_str(), key.c_str(), def.c_str(), buf, 512, path.c_str());
    return buf;
}

int ConfigManager::ReadIniInt(const std::wstring& section, const std::wstring& key,
                               int def, const std::wstring& path) {
    return (int)GetPrivateProfileIntW(section.c_str(), key.c_str(), def, path.c_str());
}

bool ConfigManager::ReadIniBool(const std::wstring& section, const std::wstring& key,
                                 bool def, const std::wstring& path) {
    return ReadIniInt(section, key, def ? 1 : 0, path) != 0;
}

void ConfigManager::Load() {
    auto p = IniPath();
    m_cfg = PluginConfig{};

    // ─── 设备列表 ───
    // 新格式：[Plugin] DeviceCount=N + [Device1]..[DeviceN]
    // 旧格式（1.0）：[Device] 单设备，自动迁移为 Device1
    int count = ReadIniInt(L"Plugin", L"DeviceCount", 0, p);
    if (count <= 0) {
        DeviceConfig d;
        d.ip    = ReadIniString(L"Device", L"IP",    L"",        p);
        d.token = ReadIniString(L"Device", L"Token", L"",        p);
        d.name  = ReadIniString(L"Device", L"Name",  L"米家插座", p);
        if (d.name.empty()) d.name = L"米家插座";
        m_cfg.devices.push_back(d);
    } else {
        if (count > MAX_DEVICES) count = MAX_DEVICES;
        for (int i = 1; i <= count; ++i) {
            std::wstring sec = L"Device" + std::to_wstring(i);
            DeviceConfig d;
            d.ip    = ReadIniString(sec, L"IP",    L"",        p);
            d.token = ReadIniString(sec, L"Token", L"",        p);
            d.name  = ReadIniString(sec, L"Name",  L"米家插座", p);
            if (d.name.empty()) d.name = L"米家插座";
            m_cfg.devices.push_back(d);
        }
    }

    // ─── 插件选项 ───
    m_cfg.enableRecording   = ReadIniBool(L"Plugin", L"EnableRecording",   true, p);
    m_cfg.showLabel         = ReadIniBool(L"Plugin", L"ShowLabel",         true, p);
    m_cfg.showTotal         = ReadIniBool(L"Plugin", L"ShowTotal",         true, p);
    m_cfg.showUnit          = ReadIniBool(L"Plugin", L"ShowUnit",          true, p);
    m_cfg.updateIntervalSec = ReadIniInt (L"Plugin", L"UpdateIntervalSec", 3,    p);
    m_cfg.decimalPlaces     = ReadIniInt (L"Plugin", L"DecimalPlaces",     1,    p);

    // 约束
    if (m_cfg.updateIntervalSec < 1)  m_cfg.updateIntervalSec = 1;
    if (m_cfg.updateIntervalSec > 60) m_cfg.updateIntervalSec = 60;
    if (m_cfg.decimalPlaces < 0) m_cfg.decimalPlaces = 0;
    if (m_cfg.decimalPlaces > 2) m_cfg.decimalPlaces = 2;
    if (m_cfg.devices.empty()) m_cfg.devices.push_back(DeviceConfig{});
}

void ConfigManager::Save() const {
    auto p = IniPath();
    int count = (int)m_cfg.devices.size();

    wchar_t buf[32];
    swprintf(buf, 32, L"%d", count);
    WritePrivateProfileStringW(L"Plugin", L"DeviceCount", buf, p.c_str());

    for (int i = 1; i <= count; ++i) {
        const DeviceConfig& d = m_cfg.devices[i - 1];
        std::wstring sec = L"Device" + std::to_wstring(i);
        WritePrivateProfileStringW(sec.c_str(), L"IP",    d.ip.c_str(),    p.c_str());
        WritePrivateProfileStringW(sec.c_str(), L"Token", d.token.c_str(), p.c_str());
        WritePrivateProfileStringW(sec.c_str(), L"Name",  d.name.c_str(),  p.c_str());
    }
    // 清理多余的旧设备段（删除键即可）
    for (int i = count + 1; i <= MAX_DEVICES + 1; ++i) {
        std::wstring sec = L"Device" + std::to_wstring(i);
        WritePrivateProfileStringW(sec.c_str(), L"IP",    L"", p.c_str());
        WritePrivateProfileStringW(sec.c_str(), L"Token", L"", p.c_str());
        WritePrivateProfileStringW(sec.c_str(), L"Name",  L"", p.c_str());
    }

    WritePrivateProfileStringW(L"Plugin", L"EnableRecording",   m_cfg.enableRecording   ? L"1" : L"0", p.c_str());
    WritePrivateProfileStringW(L"Plugin", L"ShowLabel",         m_cfg.showLabel         ? L"1" : L"0", p.c_str());
    WritePrivateProfileStringW(L"Plugin", L"ShowTotal",         m_cfg.showTotal         ? L"1" : L"0", p.c_str());
    WritePrivateProfileStringW(L"Plugin", L"ShowUnit",          m_cfg.showUnit          ? L"1" : L"0", p.c_str());

    swprintf(buf, 32, L"%d", m_cfg.updateIntervalSec);
    WritePrivateProfileStringW(L"Plugin", L"UpdateIntervalSec", buf, p.c_str());
    swprintf(buf, 32, L"%d", m_cfg.decimalPlaces);
    WritePrivateProfileStringW(L"Plugin", L"DecimalPlaces", buf, p.c_str());
}
