// PluginConfig.h - 插件配置管理（INI文件读写，支持多设备）
#pragma once
#include "pch.h"

// 最大支持设备数（与显示项一一对应，设置对话框同样受此限制）
static const int MAX_DEVICES = 8;

struct DeviceConfig {
    std::wstring ip;
    std::wstring token;
    std::wstring name    = L"米家插座";

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

    PluginConfig& Get() { return m_cfg; }
    const PluginConfig& Get() const { return m_cfg; }

    // 第 index（0起）个设备的历史文件路径；旧版单设备历史文件路径
    std::wstring GetHistoryFilePath(int index) const;
    std::wstring GetLegacyHistoryFilePath() const;

private:
    std::wstring  m_dir;
    PluginConfig  m_cfg;
    std::wstring  IniPath() const;

    static std::wstring ReadIniString(const std::wstring& section, const std::wstring& key,
                                      const std::wstring& def, const std::wstring& path);
    static int         ReadIniInt   (const std::wstring& section, const std::wstring& key,
                                      int def, const std::wstring& path);
    static bool        ReadIniBool  (const std::wstring& section, const std::wstring& key,
                                      bool def, const std::wstring& path);
};
