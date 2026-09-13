// PluginConfig.cpp - 配置管理实现（使用 Win32 INI API，多设备）
#include "pch.h"
#include "PluginConfig.h"

std::wstring ConfigManager::IniPath() const {
    return m_dir + L"\\MijiaPower.ini";
}

std::wstring ConfigManager::GetHistoryFilePathForIP(const std::wstring& ip, int index) const {
    if (ip.empty())
        return GetIndexHistoryFilePath(index);
    std::wstring name = L"MijiaPower_history_" + ip;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // 同一 IP 配置了多个设备时追加序号，避免历史文件互相覆盖
        int dup = 0;
        for (int i = 0; i < (int)m_cfg.devices.size() && i < index; ++i)
            if (m_cfg.devices[i].ip == ip) dup++;
        if (dup > 0) name += L"_" + std::to_wstring(dup + 1);
    }
    return m_dir + L"\\" + name + L".json";
}

std::wstring ConfigManager::GetHistoryFilePath(int index) const {
    std::wstring ip;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (index >= 0 && index < (int)m_cfg.devices.size())
            ip = m_cfg.devices[index].ip;
    }
    return GetHistoryFilePathForIP(ip, index);
}

std::wstring ConfigManager::GetIndexHistoryFilePath(int index) const {
    return m_dir + L"\\MijiaPower_history_" + std::to_wstring(index + 1) + L".json";
}

std::wstring ConfigManager::GetLegacyHistoryFilePath() const {
    return m_dir + L"\\MijiaPower_history.json";
}

std::vector<std::wstring> ConfigManager::GetAllHistoryFilePaths() const {
    std::vector<std::wstring> paths;
    if (m_dir.empty()) return paths;
    std::wstring pattern = m_dir + L"\\MijiaPower_history*.json";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return paths;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            paths.push_back(m_dir + L"\\" + fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return paths;
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

PluginConfig ConfigManager::Get() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_cfg;
}

void ConfigManager::Set(const PluginConfig& cfg) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cfg = cfg;
}

void ConfigManager::Load() {
    PluginConfig cfg;
    auto p = IniPath();

    // ─── 设备列表 ───
    // 新格式：[Plugin] DeviceCount=N + [Device1]..[DeviceN]
    // 旧格式（1.0）：[Device] 单设备，保存时自动写为 Device1
    int count = ReadIniInt(L"Plugin", L"DeviceCount", 0, p);
    if (count <= 0) {
        DeviceConfig d;
        d.ip    = ReadIniString(L"Device", L"IP",    L"",        p);
        d.token = ReadIniString(L"Device", L"Token", L"",        p);
        d.name  = ReadIniString(L"Device", L"Name",  L"米家插座", p);
        if (d.name.empty()) d.name = L"米家插座";
        cfg.devices.push_back(d);
    } else {
        if (count > MAX_DEVICES) count = MAX_DEVICES;
        for (int i = 1; i <= count; ++i) {
            std::wstring sec = L"Device" + std::to_wstring(i);
            DeviceConfig d;
            d.ip    = ReadIniString(sec, L"IP",    L"",        p);
            d.token = ReadIniString(sec, L"Token", L"",        p);
            d.name  = ReadIniString(sec, L"Name",  L"米家插座", p);
            if (d.name.empty()) d.name = L"米家插座";
            cfg.devices.push_back(d);
        }
    }

    // ─── 插件选项 ───
    cfg.enableRecording   = ReadIniBool(L"Plugin", L"EnableRecording",   true, p);
    cfg.showLabel         = ReadIniBool(L"Plugin", L"ShowLabel",         true, p);
    cfg.showTotal         = ReadIniBool(L"Plugin", L"ShowTotal",         true, p);
    cfg.showUnit          = ReadIniBool(L"Plugin", L"ShowUnit",          true, p);
    cfg.updateIntervalSec = ReadIniInt (L"Plugin", L"UpdateIntervalSec", 3,    p);
    cfg.decimalPlaces     = ReadIniInt (L"Plugin", L"DecimalPlaces",     1,    p);

    // 约束
    if (cfg.updateIntervalSec < 1)  cfg.updateIntervalSec = 1;
    if (cfg.updateIntervalSec > 60) cfg.updateIntervalSec = 60;
    if (cfg.decimalPlaces < 0) cfg.decimalPlaces = 0;
    if (cfg.decimalPlaces > 2) cfg.decimalPlaces = 2;
    if (cfg.devices.empty()) cfg.devices.push_back(DeviceConfig{});

    std::lock_guard<std::mutex> lock(m_mutex);
    m_cfg = std::move(cfg);
}

void ConfigManager::Save() const {
    std::lock_guard<std::mutex> lock(m_mutex);
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
    // 清理 v1.0 遗留的单设备段（Token 不应多留一份明文）
    WritePrivateProfileStringW(L"Device", L"IP",    L"", p.c_str());
    WritePrivateProfileStringW(L"Device", L"Token", L"", p.c_str());
    WritePrivateProfileStringW(L"Device", L"Name",  L"", p.c_str());

    WritePrivateProfileStringW(L"Plugin", L"EnableRecording",   m_cfg.enableRecording   ? L"1" : L"0", p.c_str());
    WritePrivateProfileStringW(L"Plugin", L"ShowLabel",         m_cfg.showLabel         ? L"1" : L"0", p.c_str());
    WritePrivateProfileStringW(L"Plugin", L"ShowTotal",         m_cfg.showTotal         ? L"1" : L"0", p.c_str());
    WritePrivateProfileStringW(L"Plugin", L"ShowUnit",          m_cfg.showUnit          ? L"1" : L"0", p.c_str());

    swprintf(buf, 32, L"%d", m_cfg.updateIntervalSec);
    WritePrivateProfileStringW(L"Plugin", L"UpdateIntervalSec", buf, p.c_str());
    swprintf(buf, 32, L"%d", m_cfg.decimalPlaces);
    WritePrivateProfileStringW(L"Plugin", L"DecimalPlaces", buf, p.c_str());
}
