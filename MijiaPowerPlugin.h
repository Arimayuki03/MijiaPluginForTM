// MijiaPowerPlugin.h - 插件主类声明（多设备支持）
#pragma once
#include "pch.h"
#include "PluginInterface.h"
#include "MiioDevice.h"
#include "PowerHistory.h"
#include "PluginConfig.h"

// 功率格式化（负值钳为0，可选W单位）
std::wstring FormatWatts(double watts, int decimalPlaces, bool showUnit);

// ─────────────────────────────────────────────
// 单个设备的运行时状态（连接、功率、历史）
// ─────────────────────────────────────────────
struct DeviceState {
    DeviceConfig                cfg;                 // 该设备的配置快照（mtx 保护）
    std::unique_ptr<MiioDevice> device;              // miIO 连接（仅采样线程访问，无需加锁）
    PowerHistory                history;             // 每设备独立历史（内部自带锁）
    std::atomic<bool>           connected{ false };
    std::atomic<double>         watts{ 0.0 };
    std::mutex                  mtx;                 // 仅保护 cfg 的读写；网络 I/O 不持此锁
};

// ─────────────────────────────────────────────
// 功率显示项（每个插座一个）
// ─────────────────────────────────────────────
class CPowerItem : public IPluginItem {
public:
    // index 为设备序号（0 起）。对象在插件构造时全部预创建，
    // TrafficMonitor 启动时枚举并缓存 IPluginItem*，因此这些对象永不销毁。
    void Init(class CMijiaPowerPlugin* plugin, int index) { m_plugin = plugin; m_index = index; }

    const wchar_t* GetItemName()            const override;
    const wchar_t* GetItemId()              const override;
    const wchar_t* GetItemLableText()       const override;
    const wchar_t* GetItemValueText()       const override;
    const wchar_t* GetItemValueSampleText() const override { return L"9999.9W"; }

private:
    class CMijiaPowerPlugin* m_plugin = nullptr;
    int  m_index = 0;
    mutable std::wstring m_idText;
    mutable std::wstring m_nameText;
    mutable std::wstring m_valueText;
    mutable std::wstring m_labelText;
};

// ─────────────────────────────────────────────
// 总功率显示项（所有已连接插座之和）
// ─────────────────────────────────────────────
class CTotalPowerItem : public IPluginItem {
public:
    void SetPlugin(class CMijiaPowerPlugin* plugin) { m_plugin = plugin; }

    const wchar_t* GetItemName()            const override { return L"米家插座总功率"; }
    const wchar_t* GetItemId()              const override { return L"MijiaPwrTotal"; }
    const wchar_t* GetItemLableText()       const override;
    const wchar_t* GetItemValueText()       const override;
    const wchar_t* GetItemValueSampleText() const override { return L"99999.9W"; }

private:
    class CMijiaPowerPlugin* m_plugin = nullptr;
    mutable std::wstring m_valueText;
    mutable std::wstring m_labelText;
};

// ─────────────────────────────────────────────
// 插件主类
// ─────────────────────────────────────────────
class CMijiaPowerPlugin : public ITMPlugin {
public:
    CMijiaPowerPlugin();
    ~CMijiaPowerPlugin();

    // ── ITMPlugin 接口实现 ──
    // 显示项枚举：0..设备数-1 为各插座，设备数>1 且开启总功率时，
    // 索引=设备数 为总功率项，越界返回 nullptr。
    IPluginItem*   GetItem(int index)  override;
    void           DataRequired()      override;
    const wchar_t* GetInfo(PluginInfoIndex index) override;

    OptionReturn   ShowOptionsDialog(void* hParent) override;

    const wchar_t* GetTooltipInfo()    override;

    // API v7: 主程序调用此函数完成初始化，传入 ITrafficMonitor*
    void           OnInitialize(ITrafficMonitor* pApp) override;

    // 配置目录通过 OnExtenedInfo(EI_CONFIG_DIR, ...) 传入
    void           OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;

    // 安全关闭（DllMain DLL_PROCESS_DETACH 阶段调用，detach 线程避免死锁）
    void Shutdown() {
        m_stopFlag = true;
        if (m_sampleThread.joinable())
            m_sampleThread.detach();
    }

    // ── 供显示项访问 ──
    int    GetDeviceCount() const;
    bool   IsDeviceConnected(int index) const;
    double GetDeviceWatts(int index) const;

    // 清除全部功率历史（内存 + 磁盘，含旧命名文件），供设置对话框“清除历史”调用
    void   ClearAllHistory();

private:
    ITrafficMonitor* m_pTM = nullptr;
    CPowerItem       m_items[MAX_DEVICES];  // 预创建槽位，指针永不失效
    CTotalPowerItem  m_totalItem;

    // 设备状态列表，顺序与配置一致；仅主线程修改，采集线程通过快照访问
    std::vector<std::shared_ptr<DeviceState>> m_devices;
    mutable std::mutex m_devicesMutex;

    // 后台采集线程
    std::thread        m_sampleThread;
    std::atomic<bool>  m_stopFlag{ false };

    // 上次历史落盘时间（DataRequired 周期保存的节流）
    std::chrono::steady_clock::time_point m_lastHistorySave{};

    // tooltip缓存
    mutable std::wstring m_tooltipText;

    bool               m_initialized = false;  // 防止重复初始化

    void StartSampling();  // 启动采样线程（在配置目录确定后调用）
    void SampleLoop();
    void LazyInit(const std::wstring& configDir);  // 加载配置并启动采集（兼容新旧主程序）
    void InitDevices();                    // 按当前配置建立设备状态（含历史加载）
    void ApplyNewConfig();                 // 设置变更后重建设备状态（复用同 IP+Token 的连接与历史）

    std::vector<std::shared_ptr<DeviceState>> SnapshotDevices() const;
    static void PollOne(DeviceState& st, bool enableRecording);
};

// DLL 导出
extern "C" __declspec(dllexport)
ITMPlugin* TMPluginGetInstance();
