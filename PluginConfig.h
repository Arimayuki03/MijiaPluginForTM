// PluginConfig.h - 插件配置管理（INI文件读写，支持多设备）
#pragma once
#include "pch.h"

// 最大支持设备数（与显示项一一对应，设置对话框同样受此限制）
inline constexpr int MAX_DEVICES = 8;

// Token 格式校验：32 位十六进制（设置对话框与任务栏显示共用）
inline bool IsValidToken(const std::wstring& token) {
    if (token.size() != 32) return false;
    for (wchar_t c : token) {
        bool hex = (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F');
        if (!hex) return false;
    }
    return true;
}

// 设备名净化：剔除控制字符（< 0x20 与 0x7F，含 \n \r \t）。
// 供配置载入（Load/Set）与设置对话框捕获共同使用，防止设备名里的
// 换行/制表符等破坏 INI 键值格式或任务栏标签排版
inline void SanitizeDeviceName(std::wstring& name) {
    std::wstring out;
    out.reserve(name.size());
    for (wchar_t c : name) {
        if (c >= 0x20 && c != 0x7F)
            out.push_back(c);
    }
    name.swap(out);
}

struct DeviceConfig {
    std::wstring ip;
    std::wstring token;
    std::wstring name    = L"米家插座";
    bool inTotal         = true;   // 是否计入总功率合计
    bool enabled         = true;   // 是否启用（禁用后不采集连接，数值显示“已禁用”）

    // 持久化历史槽位（1..MAX_DEVICES）：历史文件路径按“IP+槽位”命名，该槽位
    // 随配置保存（INI 的 HistorySlot 键），设备增删/重排不改变其取值，从而保证
    // 同一设备的历史文件路径永不漂移；0 表示尚未分配（由
    // ConfigManager::NormalizeHistorySlots 在 Load/Set 时统一分配）
    int historySlot      = 0;

    // 按 IP+Token 判断是否同一设备（用于配置变更时保留连接与历史）
    bool SameAs(const DeviceConfig& o) const { return ip == o.ip && token == o.token; }
};

struct PluginConfig {
    // 设备列表（多设备支持）
    std::vector<DeviceConfig> devices;

    // 功能开关
    bool enableRecording   = true;    // 是否记录功率历史
    bool showLabel         = true;    // 是否显示标签（设备名称）
    bool showTotal         = true;    // 是否显示总功率项（2个及以上设备时）
    int  updateIntervalSec = 3;       // 采集间隔（秒）
    // 显示格式
    bool showUnit          = true;    // 是否显示 W 单位
    int  decimalPlaces     = 1;       // 小数位数（0/1/2）
    int  tooltipStatsHours = 1;       // 悬停提示统计窗口（小时，1-24）
};

class ConfigManager {
public:
    static ConfigManager& Instance() {
        static ConfigManager inst;
        return inst;
    }

    void  SetConfigDir(const std::wstring& dir) { m_dir = dir; }
    void  Load();
    void  Save() const;

    // 配置对象同时被 UI 线程（设置对话框写）与采集线程（采样循环读）访问，
    // 统一通过副本读写，避免数据竞争
    PluginConfig Get() const;
    void         Set(const PluginConfig& cfg);

    // 第 index（0起）个设备的历史文件路径。
    // 按设备身份（IP+持久化槽位）命名，设备增删/重排后仍指向同一文件，不会错位；
    // IP 为空（未配置设备）时退回仅按槽位命名；index 越界时用默认设备兜底
    std::wstring GetHistoryFilePath(int index) const;
    // 按设备自身身份（IP+持久化槽位 historySlot）取历史文件路径。
    // 路径与设备在当前配置中的排位完全无关：无任何“按当前配置排位”的分支，
    // 同一设备（IP+槽位）永远映射同一路径，配置增删/重排均不漂移。
    // 调用前 d.historySlot 应已由 NormalizeHistorySlots 归一化
    std::wstring GetHistoryFilePathForDevice(const DeviceConfig& d) const;
    // v1.1.0/1.1.1 按索引命名的旧历史文件路径（仅用于迁移与清理）
    std::wstring GetIndexHistoryFilePath(int index) const;
    // v1.0 单设备历史文件路径
    std::wstring GetLegacyHistoryFilePath() const;
    // 把 v1.2.4 及更早的 IP(+同 IP 序号)/索引命名历史文件迁移到按设备身份
    // （IP+槽位）命名。仅“目标不存在且源存在”才迁移，幂等可重复执行
    void MigrateLegacyHistoryFiles() const;
    // 配置目录内全部历史文件路径（按 MijiaPower_history*.json 模式枚举，
    // 覆盖 IP+槽位命名、IP 命名、索引命名与 v1.0 命名，含已删除设备遗留的文件）
    std::vector<std::wstring> GetAllHistoryFilePaths() const;

private:
    std::wstring  m_dir;
    mutable std::mutex m_mutex;
    PluginConfig  m_cfg;
    std::wstring  IniPath() const;

    // 为各设备分配唯一的持久化历史槽位（1..MAX_DEVICES）：
    // 已有合法且未被前序设备占用的槽位者保留原槽位（保证升级/增删后路径稳定），
    // 其余（slot<1、>MAX_DEVICES 或与他人重复）按配置顺序分配“最小未占用槽位”。
    // 8 台设备对 8 个槽位，鸽笼原理保证总能分配成功
    static void NormalizeHistorySlots(PluginConfig& cfg);

    static std::wstring ReadIniString(const std::wstring& section, const std::wstring& key,
                                      const std::wstring& def, const std::wstring& path);
    static int         ReadIniInt   (const std::wstring& section, const std::wstring& key,
                                      int def, const std::wstring& path);
    static bool        ReadIniBool  (const std::wstring& section, const std::wstring& key,
                                      bool def, const std::wstring& path);
};
