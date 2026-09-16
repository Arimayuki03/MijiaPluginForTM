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

struct DeviceConfig {
    std::wstring ip;
    std::wstring token;
    std::wstring name    = L"米家插座";
    bool inTotal         = true;   // 是否计入总功率合计
    bool enabled         = true;   // 是否启用（禁用后不采集连接，数值显示“已禁用”）

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
    // 按设备身份（IP）命名，设备增删/重排后仍指向同一文件，不会错位；
    // IP 为空（未配置设备）时退回按索引命名
    std::wstring GetHistoryFilePath(int index) const;
    // 指定 IP 的历史文件路径（供配置变更时按旧身份保存被移除设备）
    std::wstring GetHistoryFilePathForIP(const std::wstring& ip, int index) const;
    // v1.1.0/1.1.1 按索引命名的旧历史文件路径（仅用于迁移与清理）
    std::wstring GetIndexHistoryFilePath(int index) const;
    // v1.0 单设备历史文件路径
    std::wstring GetLegacyHistoryFilePath() const;
    // 配置目录内全部历史文件路径（按 MijiaPower_history*.json 模式枚举，
    // 覆盖 IP 命名、索引命名与 v1.0 命名，含已删除设备遗留的文件）
    std::vector<std::wstring> GetAllHistoryFilePaths() const;

private:
    std::wstring  m_dir;
    mutable std::mutex m_mutex;
    PluginConfig  m_cfg;
    std::wstring  IniPath() const;

    static std::wstring ReadIniString(const std::wstring& section, const std::wstring& key,
                                      const std::wstring& def, const std::wstring& path);
    static int         ReadIniInt   (const std::wstring& section, const std::wstring& key,
                                      int def, const std::wstring& path);
    static bool        ReadIniBool  (const std::wstring& section, const std::wstring& key,
                                      bool def, const std::wstring& path);
};
