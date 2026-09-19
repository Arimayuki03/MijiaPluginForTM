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
    std::atomic<bool>           noData{ false };     // 设备在线应答但不支持功率属性（显示 "--"，连接保持）
    std::atomic<double>         watts{ 0.0 };
    // 连续失败计数与退避跳轮数：仅采样线程读写，ApplyNewConfig 在配置变更时
    // 清零以便立即重试（原子变量，UI 线程写亦安全）
    std::atomic<int>            failCount{ 0 };
    std::atomic<int>            skipRounds{ 0 };
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
    // 文本缓冲不再作为 mutable 成员（std::wstring 非线程安全，且成员缓冲在
    // “返回后指针被他人失效”的问题上无解），改为各 const 方法内的函数局部
    // thread_local 缓冲：每线程独立，宿主未来多线程调用也不会互踩或产生悬垂指针
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
    // 同 CPowerItem：文本缓冲改为方法内 thread_local
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

    // 安全关闭（DllMain DLL_PROCESS_DETACH 阶段调用）。
    // - processExiting==true：ExitProcess 路径（lpvReserved 非空）。微软文档明确此时
    //   其他线程已被系统终止，等待/join 无意义（loader lock 下也禁止阻塞），直接 detach。
    // - processExiting==false：宿主在 CRT 静态析构阶段 FreeLibrary 的动态卸载路径，
    //   采样线程仍在运行：有界等待（≤1.5s）让线程走完收尾保存并退出插件代码，
    //   消除“DLL 已 unmap 而线程仍在插件映像内”的窄窗口退出竞态；随后 detach
    //   （DLL 卸载阶段禁止 join——线程可能正持有 loader lock 相关资源，会死锁）。
    void Shutdown(bool processExiting) {
        m_stopFlag = true;
        if (!processExiting) {
            for (int i = 0; i < 75 && m_sampleAlive.load(); ++i)
                Sleep(20);
        }
        if (m_sampleThread.joinable())
            m_sampleThread.detach();
    }

    // ── 供显示项访问 ──
    bool   IsDeviceConnected(int index) const;
    bool   IsDeviceNoData(int index) const;   // 已连接但设备不支持功率属性（显示 "--"）
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
    std::thread          m_sampleThread;
    std::atomic<bool>    m_stopFlag{ false };
    std::atomic<bool>    m_sampleAlive{ false };   // 采样线程存活标志，供 Shutdown 有界等待轮询

    // 上次历史落盘时间（DataRequired 周期保存的节流）
    std::chrono::steady_clock::time_point m_lastHistorySave{};

    // tooltip 文本缓冲不作为成员：改为 GetTooltipInfo 内的 thread_local 局部缓冲
    // （理由同显示项：每线程独立，宿主未来多线程调用也不会互踩或产生悬垂指针）

    bool               m_initialized = false;  // 防止重复初始化

    void StartSampling();  // 启动采样线程（在配置目录确定后调用）
    void SampleLoop();
    void LazyInit(const std::wstring& configDir);  // 加载配置并启动采集（兼容新旧主程序）
    void InitDevices();                    // 按当前配置建立设备状态（含历史加载）
    void ApplyNewConfig();                 // 设置变更后重建设备状态（复用同 IP+Token 的连接与历史）

    std::vector<std::shared_ptr<DeviceState>> SnapshotDevices() const;
    static void PollOne(DeviceState& st, bool enableRecording);
    static void BackOff(DeviceState& st);  // 采样连续失败后的指数退避（跳轮数封顶 7）
};

// DLL 导出
extern "C" __declspec(dllexport)
ITMPlugin* TMPluginGetInstance();
