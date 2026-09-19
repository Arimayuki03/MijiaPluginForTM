// PluginConfig.cpp - 配置管理实现（使用 Win32 INI API，多设备）
#include "pch.h"
#include "PluginConfig.h"

std::wstring ConfigManager::IniPath() const {
    return m_dir + L"\\MijiaPower.ini";
}

std::wstring ConfigManager::GetHistoryFilePathForDevice(const DeviceConfig& d) const {
    // 设计要点：路径只由设备自身身份（IP + 持久化槽位）决定，
    // 无任何“按当前配置排位”的分支——同一设备（IP+槽位）永远映射同一路径，
    // 配置增删/重排均不漂移，同 IP 多设备也不会互相覆盖。
    // historySlot 已由 NormalizeHistorySlots 保证在 1..MAX_DEVICES 且互不重复。
    std::wstring name = L"MijiaPower_history";
    if (!d.ip.empty())
        name += L"_" + d.ip;
    name += L"_s" + std::to_wstring(d.historySlot);
    return m_dir + L"\\" + name + L".json";
}

std::wstring ConfigManager::GetHistoryFilePath(int index) const {
    DeviceConfig d;   // index 越界时保持默认 DeviceConfig{} 兜底
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (index >= 0 && index < (int)m_cfg.devices.size())
            d = m_cfg.devices[index];
    }
    // 正常路径槽位已由 NormalizeHistorySlots 归一化；兜底/未分配（slot<1）时按 1 处理
    if (d.historySlot < 1) d.historySlot = 1;
    return GetHistoryFilePathForDevice(d);
}

std::wstring ConfigManager::GetIndexHistoryFilePath(int index) const {
    return m_dir + L"\\MijiaPower_history_" + std::to_wstring(index + 1) + L".json";
}

std::wstring ConfigManager::GetLegacyHistoryFilePath() const {
    return m_dir + L"\\MijiaPower_history.json";
}

// 把 v1.2.4 及之前的“按 IP(+同 IP 序号)/按索引”命名的历史文件迁移到
// 按设备身份（IP+持久化槽位）命名。规则（全部“目标不存在且源存在”才
// MoveFileW，幂等可重复执行）：
//   旧命名（按配置顺序数同 IP 前驱，重复序号从 2 起；与 v1.2.4 的
//   GetHistoryFilePathForIP 逻辑一致）：
//     同 IP 第 1 台          → MijiaPower_history_<ip>.json
//     同 IP 第 2/3 台        → MijiaPower_history_<ip>_2.json / _3.json
//     IP 为空的第 index+1 台 → MijiaPower_history_<index+1>.json
//   目标 = GetHistoryFilePathForDevice(该设备)。
// 按配置顺序 + 同 IP 出现序为每台设备算出旧源路径：同一设备的源路径
// 天然互不相同（同 IP 内序号唯一、不同 IP 前缀不同），无需额外认领逻辑。
// 注意：升级时旧 INI 无 HistorySlot 键 → Load 已按顺序分配 1..N，与升级前
// 设备顺序一致，因此每台设备的历史文件与升级前的归属关系保持一致；
// 同 IP 多设备的旧文件若已被旧版“路径漂移”bug 交叉覆盖，只能按旧序尽力还原。
void ConfigManager::MigrateLegacyHistoryFiles() const {
    if (m_dir.empty()) return;
    // 加锁快照设备列表，文件操作放锁外
    std::vector<DeviceConfig> devs;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        devs = m_cfg.devices;
    }
    for (int i = 0; i < (int)devs.size() && i < MAX_DEVICES; ++i) {
        const DeviceConfig& d = devs[i];   // 快照副本，槽位已由 Load() 归一化
        std::wstring oldName;
        if (d.ip.empty()) {
            oldName = L"MijiaPower_history_" + std::to_wstring(i + 1);
        } else {
            oldName = L"MijiaPower_history_" + d.ip;
            int dup = 0;
            for (int j = 0; j < i; ++j)
                if (devs[j].ip == d.ip) dup++;
            if (dup > 0) oldName += L"_" + std::to_wstring(dup + 1);
        }
        std::wstring src = m_dir + L"\\" + oldName + L".json";
        std::wstring dst = GetHistoryFilePathForDevice(d);
        if (dst == src) continue;   // 理论上不相遇，防御性跳过
        if (GetFileAttributesW(dst.c_str()) == INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW(src.c_str()) != INVALID_FILE_ATTRIBUTES)
            MoveFileW(src.c_str(), dst.c_str());
    }
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
    // 钳制设备数：超过 MAX_DEVICES 的设备既无法显示也无法采集，直接丢弃
    // （同时堵住 GetItem 直取 &m_items[index] 的越界防御缺口）
    if (m_cfg.devices.size() > (size_t)MAX_DEVICES)
        m_cfg.devices.resize((size_t)MAX_DEVICES);
    // 设备名净化（对话框捕获的名称可能含换行/控制字符）
    for (auto& d : m_cfg.devices)
        SanitizeDeviceName(d.name);
    // 持久化历史槽位归一化：保证每台设备持有唯一的 1..MAX_DEVICES 槽位
    NormalizeHistorySlots(m_cfg);
}

// 槽位归一化：已持有合法且互不冲突槽位（1..MAX_DEVICES）的设备保留原槽位，
// 未分配（0）、越界或与他人重复的设备按配置顺序取“最小未占用槽位”。
// 8 台设备对 8 个槽位，鸽笼原理保证必然可分配；
// 升级场景（旧 INI 无 HistorySlot 键）下按顺序分配 1..N，与升级前设备顺序一致
void ConfigManager::NormalizeHistorySlots(PluginConfig& cfg) {
    bool used[MAX_DEVICES + 1] = {};   // 槽位从 1 起，used[0] 不用
    for (auto& d : cfg.devices) {
        if (d.historySlot >= 1 && d.historySlot <= MAX_DEVICES && !used[d.historySlot])
            used[d.historySlot] = true;
        else
            d.historySlot = 0;         // 待重分配
    }
    for (auto& d : cfg.devices) {
        if (d.historySlot != 0) continue;
        for (int s = 1; s <= MAX_DEVICES; ++s) {
            if (!used[s]) { d.historySlot = s; used[s] = true; break; }
        }
    }
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
        // v1.0 格式无 HistorySlot 键：缺省 0，待 NormalizeHistorySlots 分配
        d.historySlot = ReadIniInt(L"Device", L"HistorySlot", 0, p);
        SanitizeDeviceName(d.name);
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
            // 旧版本无 InTotal/Enabled 键：缺省视为计入/启用（与升级前行为一致）
            d.inTotal = ReadIniBool(sec, L"InTotal", true, p);
            d.enabled = ReadIniBool(sec, L"Enabled", true, p);
            // 持久化历史槽位（旧版本无此键：缺省 0，待统一分配）
            d.historySlot = ReadIniInt(sec, L"HistorySlot", 0, p);
            SanitizeDeviceName(d.name);
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
    cfg.tooltipStatsHours = ReadIniInt (L"Plugin", L"TooltipStatsHours", 1,    p);

    // 约束
    if (cfg.updateIntervalSec < 1)  cfg.updateIntervalSec = 1;
    if (cfg.updateIntervalSec > 60) cfg.updateIntervalSec = 60;
    if (cfg.decimalPlaces < 0) cfg.decimalPlaces = 0;
    if (cfg.decimalPlaces > 2) cfg.decimalPlaces = 2;
    if (cfg.tooltipStatsHours < 1)  cfg.tooltipStatsHours = 1;
    if (cfg.tooltipStatsHours > 24) cfg.tooltipStatsHours = 24;
    if (cfg.devices.empty()) cfg.devices.push_back(DeviceConfig{});

    // ─── 持久化历史槽位 ───
    // 数据一致性：升级时旧 INI 无 HistorySlot 键 → 按配置顺序分配 1..N，
    // 与升级前设备顺序一致；配合 MigrateLegacyHistoryFiles 的旧命名迁移规则，
    // 每台设备的历史文件与升级前的归属关系保持一致（同 IP 多设备的旧文件
    // 若已被旧版“路径漂移”bug 交叉覆盖，只能按旧序尽力还原）
    NormalizeHistorySlots(cfg);

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
        WritePrivateProfileStringW(sec.c_str(), L"InTotal", d.inTotal ? L"1" : L"0", p.c_str());
        WritePrivateProfileStringW(sec.c_str(), L"Enabled", d.enabled ? L"1" : L"0", p.c_str());
        swprintf(buf, 32, L"%d", d.historySlot);
        WritePrivateProfileStringW(sec.c_str(), L"HistorySlot", buf, p.c_str());
    }
    // 清理多余的旧设备段（删除键即可）
    for (int i = count + 1; i <= MAX_DEVICES + 1; ++i) {
        std::wstring sec = L"Device" + std::to_wstring(i);
        WritePrivateProfileStringW(sec.c_str(), L"IP",    L"", p.c_str());
        WritePrivateProfileStringW(sec.c_str(), L"Token", L"", p.c_str());
        WritePrivateProfileStringW(sec.c_str(), L"Name",  L"", p.c_str());
        WritePrivateProfileStringW(sec.c_str(), L"InTotal", L"", p.c_str());
        WritePrivateProfileStringW(sec.c_str(), L"Enabled", L"", p.c_str());
        WritePrivateProfileStringW(sec.c_str(), L"HistorySlot", L"", p.c_str());
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
    swprintf(buf, 32, L"%d", m_cfg.tooltipStatsHours);
    WritePrivateProfileStringW(L"Plugin", L"TooltipStatsHours", buf, p.c_str());
}
