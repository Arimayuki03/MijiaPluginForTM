// MijiaPowerPlugin.cpp - 插件主类实现（多设备支持）
#include "pch.h"
#include "MijiaPowerPlugin.h"
#include "OptionsDlg.h"
#include <sstream>
#include <iomanip>

// ═══════════════════════════════════════════════
// DLL 导出入口
// ═══════════════════════════════════════════════
static CMijiaPowerPlugin* g_pluginInstance = nullptr;

extern "C" __declspec(dllexport)
ITMPlugin* TMPluginGetInstance() {
    if (!g_pluginInstance) {
        g_pluginInstance = new CMijiaPowerPlugin();
    }
    return g_pluginInstance;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        break;
    case DLL_PROCESS_DETACH:
        // 注意：不要在这里 delete 插件实例。
        // DLL_PROCESS_DETACH 阶段调用 join() 会因 loader lock 导致死锁崩溃。
        if (g_pluginInstance) {
            g_pluginInstance->Shutdown();
            g_pluginInstance = nullptr;
        }
        break;
    }
    return TRUE;
}

// ═══════════════════════════════════════════════
// 工具
// ═══════════════════════════════════════════════
std::wstring FormatWatts(double watts, int decimalPlaces, bool showUnit) {
    if (watts < 0) watts = 0;
    if (decimalPlaces < 0) decimalPlaces = 0;
    if (decimalPlaces > 2) decimalPlaces = 2;
    std::wostringstream oss;
    oss << std::fixed << std::setprecision(decimalPlaces) << watts;
    if (showUnit) oss << L"W";
    return oss.str();
}

static void ToUtf8(const std::wstring& w, char* out, size_t outLen) {
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out, (int)outLen, NULL, NULL);
}

// ═══════════════════════════════════════════════
// CPowerItem 实现
// ═══════════════════════════════════════════════
const wchar_t* CPowerItem::GetItemName() const {
    m_nameText = L"米家插座功率";
    auto cfg = ConfigManager::Instance().Get();
    if (m_index >= 0 && m_index < (int)cfg.devices.size())
        m_nameText += L"(" + cfg.devices[m_index].name + L")";
    return m_nameText.c_str();
}

const wchar_t* CPowerItem::GetItemId() const {
    // 注意：显示项 ID 不要沿用 v1.0 的 "MijiaPowerW" 系列。
    // TrafficMonitor 会把任务栏的标签文本按 ID 缓存在 config.ini 的
    // [plugin_display_str_taskbar_window] 中并覆盖插件提供的实时标签，
    // 旧 ID 下存在 v1.0 写死的"功率:"缓存，换新 ID 才能显示设备自定义名称。
    m_idText = L"MijiaPwr" + std::to_wstring(m_index + 1);
    return m_idText.c_str();
}

const wchar_t* CPowerItem::GetItemLableText() const {
    auto cfg = ConfigManager::Instance().Get();
    if (!cfg.showLabel || m_index >= (int)cfg.devices.size())
        m_labelText = L"";
    else
        m_labelText = cfg.devices[m_index].name + L":";
    return m_labelText.c_str();
}

const wchar_t* CPowerItem::GetItemValueText() const {
    auto cfg = ConfigManager::Instance().Get();
    if (m_index < 0 || m_index >= (int)cfg.devices.size()) {
        m_valueText = L"--";
        return m_valueText.c_str();
    }
    const DeviceConfig& dev = cfg.devices[m_index];
    // Token 格式非法时不可能连接成功，显示“未配置”而非永远“连接中...”
    if (dev.ip.empty() || dev.token.empty() || !IsValidToken(dev.token))
        m_valueText = L"未配置";
    else if (!m_plugin || !m_plugin->IsDeviceConnected(m_index))
        m_valueText = L"连接中...";
    else
        m_valueText = FormatWatts(m_plugin->GetDeviceWatts(m_index), cfg.decimalPlaces, cfg.showUnit);
    return m_valueText.c_str();
}

// ═══════════════════════════════════════════════
// CTotalPowerItem 实现
// ═══════════════════════════════════════════════
const wchar_t* CTotalPowerItem::GetItemLableText() const {
    auto cfg = ConfigManager::Instance().Get();
    m_labelText = cfg.showLabel ? L"总功率:" : L"";
    return m_labelText.c_str();
}

const wchar_t* CTotalPowerItem::GetItemValueText() const {
    auto cfg = ConfigManager::Instance().Get();
    int n = m_plugin ? m_plugin->GetDeviceCount() : 0;
    bool any = false;
    double sum = 0;
    for (int i = 0; i < n; ++i) {
        if (m_plugin->IsDeviceConnected(i)) {
            any = true;
            sum += m_plugin->GetDeviceWatts(i);
        }
    }
    m_valueText = any ? FormatWatts(sum, cfg.decimalPlaces, cfg.showUnit) : L"--";
    return m_valueText.c_str();
}

// ═══════════════════════════════════════════════
// CMijiaPowerPlugin 实现
// ═══════════════════════════════════════════════
CMijiaPowerPlugin::CMijiaPowerPlugin() {
    for (int i = 0; i < MAX_DEVICES; ++i)
        m_items[i].Init(this, i);
    m_totalItem.SetPlugin(this);
}

CMijiaPowerPlugin::~CMijiaPowerPlugin() {
    // 析构在普通线程上调用（不在 loader lock），join 是安全的
    m_stopFlag = true;
    if (m_sampleThread.joinable())
        m_sampleThread.join();

    // 保存历史记录（按当前设备索引）
    auto& cm = ConfigManager::Instance();
    if (cm.Get().enableRecording) {
        auto devs = SnapshotDevices();
        for (int i = 0; i < (int)devs.size(); ++i)
            devs[i]->history.SaveToFile(cm.GetHistoryFilePath(i));
    }
}

// API v7：主程序在加载插件后调用此函数，传入 ITrafficMonitor*
void CMijiaPowerPlugin::OnInitialize(ITrafficMonitor* pApp) {
    m_pTM = pApp;
    // 注意：此时配置目录可能还未通过 OnExtenedInfo 传入。
    // 不在这里加载配置，等 OnExtenedInfo(EI_CONFIG_DIR) 时再初始化。
    // 但为了兼容老版本（未调用 OnInitialize），我们在 LazyInit 里做懒加载。
}

void CMijiaPowerPlugin::LazyInit(const std::wstring& configDir) {
    auto& cm = ConfigManager::Instance();
    cm.SetConfigDir(configDir);
    cm.Load();
    auto cfg = cm.Get();

    // ── 历史文件迁移（均为“目标不存在才迁移”，可重复执行）──
    // 1) v1.0：MijiaPower_history.json → 第 1 个设备的历史文件
    std::wstring newPath = cm.GetHistoryFilePath(0);
    std::wstring oldPath = cm.GetLegacyHistoryFilePath();
    if (GetFileAttributesW(newPath.c_str()) == INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW(oldPath.c_str()) != INVALID_FILE_ATTRIBUTES)
        MoveFileW(oldPath.c_str(), newPath.c_str());

    // 2) v1.1.0/1.1.1：按索引命名的 MijiaPower_history_N.json → 按设备身份命名
    for (int i = 0; i < (int)cfg.devices.size() && i < MAX_DEVICES; ++i) {
        std::wstring newP = cm.GetHistoryFilePath(i);
        std::wstring oldP = cm.GetIndexHistoryFilePath(i);
        if (GetFileAttributesW(newP.c_str()) == INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW(oldP.c_str()) != INVALID_FILE_ATTRIBUTES)
            MoveFileW(oldP.c_str(), newP.c_str());
    }

    InitDevices();
    StartSampling();
}

// 主程序通过此函数传入扩展信息，包括配置目录（EI_CONFIG_DIR）
void CMijiaPowerPlugin::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) {
    if (index == EI_CONFIG_DIR && data && data[0] != L'\0') {
        if (!m_initialized) {
            m_initialized = true;
            LazyInit(data);
        }
    }
}

void CMijiaPowerPlugin::StartSampling() {
    m_lastHistorySave = std::chrono::steady_clock::now();
    if (!m_sampleThread.joinable()) {
        m_sampleThread = std::thread(&CMijiaPowerPlugin::SampleLoop, this);
    }
}

// 按当前配置建立设备状态（含历史加载）。
// 历史文件的加载在 m_devicesMutex 之外完成，最后仅短暂持锁换血
void CMijiaPowerPlugin::InitDevices() {
    auto& cm = ConfigManager::Instance();
    auto cfg = cm.Get();
    std::vector<std::shared_ptr<DeviceState>> devs;
    for (int i = 0; i < (int)cfg.devices.size(); ++i) {
        auto st = std::make_shared<DeviceState>();
        st->cfg = cfg.devices[i];
        if (cfg.enableRecording)
            st->history.LoadFromFile(cm.GetHistoryFilePath(i));
        devs.push_back(st);
    }
    std::lock_guard<std::mutex> lock(m_devicesMutex);
    m_devices.swap(devs);
}

// 设置变更后重建设备状态：同 IP+Token 的设备保留连接与历史，其余重建。
// 磁盘 I/O（历史加载/保存）全部在 m_devicesMutex 之外完成，
// 最后仅短暂持锁换血，避免阻塞采样线程的 SnapshotDevices
void CMijiaPowerPlugin::ApplyNewConfig() {
    auto& cm = ConfigManager::Instance();
    auto cfg = cm.Get();
    auto oldDevs = SnapshotDevices();

    std::vector<bool> used(oldDevs.size(), false);
    std::vector<std::shared_ptr<DeviceState>> newDevs;

    for (int i = 0; i < (int)cfg.devices.size(); ++i) {
        const DeviceConfig& d = cfg.devices[i];
        int found = -1;
        for (int j = 0; j < (int)oldDevs.size(); ++j) {
            if (!used[j] && oldDevs[j]->cfg.SameAs(d)) { found = j; break; }
        }
        if (found >= 0) {
            used[found] = true;
            auto st = oldDevs[found];
            {
                std::lock_guard<std::mutex> l(st->mtx);
                st->cfg = d;    // 名称等显示属性即时生效，连接保留
            }
            // 刚启用历史记录时补加载
            if (cfg.enableRecording && !st->history.HasData())
                st->history.LoadFromFile(cm.GetHistoryFilePath(i));
            newDevs.push_back(st);
        } else {
            auto st = std::make_shared<DeviceState>();
            st->cfg = d;
            if (cfg.enableRecording)
                st->history.LoadFromFile(cm.GetHistoryFilePath(i));
            newDevs.push_back(st);
        }
    }

    // 保存被移除设备的历史（按其旧身份 IP 取路径，避免索引错位）
    if (cfg.enableRecording) {
        for (int j = 0; j < (int)oldDevs.size(); ++j) {
            if (!used[j]) {
                std::wstring oldIp;
                {
                    std::lock_guard<std::mutex> l(oldDevs[j]->mtx);
                    oldIp = oldDevs[j]->cfg.ip;
                }
                oldDevs[j]->history.SaveToFile(cm.GetHistoryFilePathForIP(oldIp, j));
            }
        }
    }

    std::lock_guard<std::mutex> lock(m_devicesMutex);
    m_devices.swap(newDevs);
}

std::vector<std::shared_ptr<DeviceState>> CMijiaPowerPlugin::SnapshotDevices() const {
    std::lock_guard<std::mutex> lock(m_devicesMutex);
    return m_devices;
}

// ─── 采集线程 ───
void CMijiaPowerPlugin::SampleLoop() {
    // 首次连接所有设备（PollOne 对未连接设备执行连接）
    auto devs = SnapshotDevices();
    for (auto& st : devs) {
        if (m_stopFlag) break;
        PollOne(*st, ConfigManager::Instance().Get().enableRecording);
    }

    int elapsed = 0;
    while (!m_stopFlag) {
        Sleep(1000);
        elapsed++;

        int interval = ConfigManager::Instance().Get().updateIntervalSec;
        if (interval < 1) interval = 1;

        if (elapsed >= interval) {
            elapsed = 0;
            bool rec = ConfigManager::Instance().Get().enableRecording;
            devs = SnapshotDevices();
            for (auto& st : devs) {
                if (m_stopFlag) break;
                PollOne(*st, rec);
            }
        }
    }

    // 线程退出前保存历史
    bool rec = ConfigManager::Instance().Get().enableRecording;
    devs = SnapshotDevices();
    for (int i = 0; i < (int)devs.size(); ++i) {
        if (rec)
            devs[i]->history.SaveToFile(ConfigManager::Instance().GetHistoryFilePath(i));
    }
}

// ─── 采样线程对单台设备的一次轮询 ───
// 注意：网络 I/O 不持有 st.mtx（该锁只保护 cfg 的短暂读写），
// 否则设置对话框“确定”时的 ApplyNewConfig 会在主线程被网络超时阻塞。
// st.device 仅由采样线程访问，无需加锁。
void CMijiaPowerPlugin::PollOne(DeviceState& st, bool enableRecording) {
    // 快照配置（短暂持锁）
    DeviceConfig cfg;
    {
        std::lock_guard<std::mutex> lock(st.mtx);
        cfg = st.cfg;
    }
    if (cfg.ip.empty() || cfg.token.empty()) return;

    char ip[256], token[256];
    ToUtf8(cfg.ip, ip, 256);
    ToUtf8(cfg.token, token, 256);

    try {
        if (!st.device) {
            // 首次连接 / 断线重连
            auto dev = std::make_unique<MiioDevice>(ip, token, 5000);
            double w = 0;
            if (dev->GetPower(w)) {
                st.device = std::move(dev);
                st.connected = true;
                st.watts = w;
                if (enableRecording)
                    st.history.AddSample(w);
            }
            return;
        }
        // 已连接，读取数据
        double w = 0;
        if (st.device->GetPower(w)) {
            st.connected = true;
            st.watts = w;
            if (enableRecording)
                st.history.AddSample(w);
        } else {
            // 连接失效，丢弃连接对象，下轮重连
            st.device.reset();
            st.connected = false;
        }
    } catch (...) {
        st.connected = false;
    }
}

// ─── 供显示项访问 ───
int CMijiaPowerPlugin::GetDeviceCount() const {
    return (int)ConfigManager::Instance().Get().devices.size();
}

bool CMijiaPowerPlugin::IsDeviceConnected(int index) const {
    auto devs = SnapshotDevices();
    if (index < 0 || index >= (int)devs.size()) return false;
    return devs[index]->connected.load();
}

double CMijiaPowerPlugin::GetDeviceWatts(int index) const {
    auto devs = SnapshotDevices();
    if (index < 0 || index >= (int)devs.size()) return 0.0;
    return devs[index]->watts.load();
}

// 清除全部功率历史：先清内存（之后的周期保存只会写入空数据），
// 再按模式删除配置目录内全部历史文件（含已移除设备遗留的 IP 命名文件与旧命名文件）
void CMijiaPowerPlugin::ClearAllHistory() {
    for (auto& st : SnapshotDevices())
        st->history.Clear();
    auto& cm = ConfigManager::Instance();
    for (const auto& path : cm.GetAllHistoryFilePaths())
        DeleteFileW(path.c_str());
}

// ─── ITMPlugin 接口 ───
IPluginItem* CMijiaPowerPlugin::GetItem(int index) {
    auto cfg = ConfigManager::Instance().Get();
    int n = (int)cfg.devices.size();
    if (index >= 0 && index < n) return &m_items[index];
    if (index == n && n > 1 && cfg.showTotal) return &m_totalItem;
    return nullptr;
}

// 历史落盘间隔（秒）。进程退出时采样线程会被直接终止、来不及保存，
// 因此依赖 DataRequired（主程序定期调用、运行于主线程）周期性落盘
static const int HISTORY_SAVE_INTERVAL_SEC = 60;

void CMijiaPowerPlugin::DataRequired() {
    if (!m_initialized) {
        // 主程序未通过 OnExtenedInfo(EI_CONFIG_DIR) 传入配置目录时的兜底：
        // 优先用主程序接口（m_pTM 非空说明主程序支持 API v7，虚表完整），
        // 最后才退回当前工作目录
        std::wstring dir;
        if (m_pTM) {
            const wchar_t* d = m_pTM->GetPluginConfigDir();
            if (d && d[0]) dir = d;
        }
        if (dir.empty()) {
            wchar_t buf[MAX_PATH];
            GetCurrentDirectoryW(MAX_PATH, buf);
            dir = buf;
        }
        m_initialized = true;
        LazyInit(dir);
        return;
    }

    // 周期性保存功率历史
    auto& cm = ConfigManager::Instance();
    if (cm.Get().enableRecording) {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(
                now - m_lastHistorySave).count() >= HISTORY_SAVE_INTERVAL_SEC) {
            m_lastHistorySave = now;
            auto devs = SnapshotDevices();
            for (int i = 0; i < (int)devs.size(); ++i)
                devs[i]->history.SaveToFile(cm.GetHistoryFilePath(i));
        }
    }
}

const wchar_t* CMijiaPowerPlugin::GetInfo(PluginInfoIndex index) {
    switch (index) {
    case TMI_NAME:        return L"米家插座功率";
    case TMI_DESCRIPTION: return L"实时显示多个米家/酷控智能插座的功率，支持总功率与历史记录";
    case TMI_AUTHOR:      return L"MijiaPlug";
    case TMI_COPYRIGHT:   return L"2024 MijiaPlug";
    case TMI_URL:         return L"";
    case TMI_VERSION:     return L"1.1.3";
    default:              return L"";
    }
}

ITMPlugin::OptionReturn CMijiaPowerPlugin::ShowOptionsDialog(void* hParent) {
    int oldCount = (int)ConfigManager::Instance().Get().devices.size();
    bool changed = COptionsDlg::Show((HWND)hParent, this);
    if (changed) {
        // 配置已更新：同 IP+Token 的设备保留连接与历史，其余重建
        ApplyNewConfig();

        // 设备数量变化时提示重启（TrafficMonitor 仅在启动时枚举显示项）
        int newCount = (int)ConfigManager::Instance().Get().devices.size();
        if (newCount != oldCount && hParent) {
            MessageBoxW((HWND)hParent,
                        L"设备数量已变更。\n新增或删除的显示条目将在重启 TrafficMonitor 后生效。",
                        L"米家插座功率", MB_OK | MB_ICONINFORMATION);
        }
        return OR_OPTION_CHANGED;
    }
    return OR_OPTION_UNCHANGED;
}

const wchar_t* CMijiaPowerPlugin::GetTooltipInfo() {
    // 与 v1.0 相同的详细样式：每个插座显示当前功率和 10分钟/1小时/24小时 统计
    auto cfg = ConfigManager::Instance().Get();
    auto devs = SnapshotDevices();

    std::wostringstream oss;
    oss << std::fixed << std::setprecision(1);

    int n = (int)cfg.devices.size();
    if ((int)devs.size() < n) n = (int)devs.size();

    double total = 0;
    int connectedCount = 0;
    for (int i = 0; i < n; ++i) {
        if (i > 0) oss << L"\n";    // 设备之间空一行
        oss << L"【" << cfg.devices[i].name << L"】";

        if (!devs[i]->connected.load()) {
            oss << L"\n状态：未连接";
            if (!cfg.devices[i].ip.empty())
                oss << L"\nIP：" << cfg.devices[i].ip;
            continue;
        }

        double w = devs[i]->watts.load();
        total += w;
        ++connectedCount;
        oss << L"\n当前功率：" << w << L" W";

        if (cfg.enableRecording) {
            auto st10m = devs[i]->history.GetStats(600);
            if (st10m.valid) {
                oss << L"\n--- 最近10分钟 ---"
                    << L"\n  最大：" << st10m.maxW << L" W"
                    << L"\n  最小：" << st10m.minW << L" W"
                    << L"\n  平均：" << st10m.avgW << L" W";
            }
            auto st1h = devs[i]->history.GetLongStats(1);
            if (st1h.valid) {
                oss << L"\n--- 最近1小时 ---"
                    << L"\n  最大：" << st1h.maxW << L" W"
                    << L"\n  最小：" << st1h.minW << L" W"
                    << L"\n  平均：" << st1h.avgW << L" W";
            }
            auto st24h = devs[i]->history.GetLongStats(24);
            if (st24h.valid) {
                oss << L"\n--- 最近24小时 ---"
                    << L"\n  最大：" << st24h.maxW << L" W"
                    << L"\n  最小：" << st24h.minW << L" W"
                    << L"\n  平均：" << st24h.avgW << L" W";
            }
        }
    }

    if (n > 1 && connectedCount > 0)
        oss << L"\n合计：" << total << L" W";

    m_tooltipText = oss.str();
    return m_tooltipText.c_str();
}
