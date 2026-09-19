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

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpvReserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        break;
    case DLL_PROCESS_DETACH:
        // 注意：不要在这里 delete 插件实例，也不要在 loader lock 下 join/阻塞。
        // lpvReserved 非空 = 进程整体退出（ExitProcess）路径：其他线程已被系统终止，
        // 等待采样线程没有意义，直接 detach；
        // lpvReserved 为空 = 动态卸载（FreeLibrary）路径：采样线程仍在运行，
        // Shutdown 内部做有界等待（≤1.5s）让线程退出插件代码后再 detach，
        // 避免“DLL 已 unmap 而线程仍在插件映像内”的退出竞态。
        if (g_pluginInstance) {
            g_pluginInstance->Shutdown(lpvReserved != nullptr);
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
    // NaN/±Inf 不得进入任务栏（会显示 "nanW"/"infW"），与负值一并钳为 0
    if (!std::isfinite(watts) || watts < 0) watts = 0;
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
// 以下显示方法的文本缓冲均为函数局部 thread_local：每线程独立缓冲，
// 宿主未来多线程调用也不会互踩或产生悬垂指针（返回后同线程再次调用
// 会覆盖旧值，与原 mutable 成员缓存的语义一致）
const wchar_t* CPowerItem::GetItemName() const {
    thread_local std::wstring nameText;
    nameText = L"米家插座功率";
    auto cfg = ConfigManager::Instance().Get();
    if (m_index >= 0 && m_index < (int)cfg.devices.size())
        nameText += L"(" + cfg.devices[m_index].name + L")";
    return nameText.c_str();
}

const wchar_t* CPowerItem::GetItemId() const {
    // 注意：显示项 ID 不要沿用 v1.0 的 "MijiaPowerW" 系列。
    // TrafficMonitor 会把任务栏的标签文本按 ID 缓存在 config.ini 的
    // [plugin_display_str_taskbar_window] 中并覆盖插件提供的实时标签，
    // 旧 ID 下存在 v1.0 写死的"功率:"缓存，换新 ID 才能显示设备自定义名称。
    thread_local std::wstring idText;
    idText = L"MijiaPwr" + std::to_wstring(m_index + 1);
    return idText.c_str();
}

const wchar_t* CPowerItem::GetItemLableText() const {
    thread_local std::wstring labelText;
    auto cfg = ConfigManager::Instance().Get();
    if (!cfg.showLabel || m_index >= (int)cfg.devices.size())
        labelText = L"";
    else
        labelText = cfg.devices[m_index].name + L":";
    return labelText.c_str();
}

const wchar_t* CPowerItem::GetItemValueText() const {
    thread_local std::wstring valueText;
    auto cfg = ConfigManager::Instance().Get();
    if (m_index < 0 || m_index >= (int)cfg.devices.size()) {
        valueText = L"--";
        return valueText.c_str();
    }
    const DeviceConfig& dev = cfg.devices[m_index];
    // 禁用的设备不采集连接，显示“已禁用”而非“连接中...”
    if (!dev.enabled)
        valueText = L"已禁用";
    // Token 格式非法时不可能连接成功，显示“未配置”而非永远“连接中...”
    else if (dev.ip.empty() || dev.token.empty() || !IsValidToken(dev.token))
        valueText = L"未配置";
    else if (!m_plugin || !m_plugin->IsDeviceConnected(m_index))
        valueText = L"连接中...";
    // 已连接但设备不支持功率属性（应答但无 "value" 字段），显示 "--" 而非反复重连
    else if (m_plugin->IsDeviceNoData(m_index))
        valueText = L"--";
    else
        valueText = FormatWatts(m_plugin->GetDeviceWatts(m_index), cfg.decimalPlaces, cfg.showUnit);
    return valueText.c_str();
}

// ═══════════════════════════════════════════════
// CTotalPowerItem 实现
// ═══════════════════════════════════════════════
const wchar_t* CTotalPowerItem::GetItemLableText() const {
    thread_local std::wstring labelText;   // 每线程独立缓冲，见 CPowerItem 处说明
    auto cfg = ConfigManager::Instance().Get();
    labelText = cfg.showLabel ? L"总功率:" : L"";
    return labelText.c_str();
}

const wchar_t* CTotalPowerItem::GetItemValueText() const {
    thread_local std::wstring valueText;   // 每线程独立缓冲，见 CPowerItem 处说明
    auto cfg = ConfigManager::Instance().Get();
    // 直接用配置快照自身的设备数，禁止“一份快照的 vector 配另一份快照的 size”
    // 的混搭（旧实现两次 Get()，配置变更瞬间会越界读）
    int n = (int)cfg.devices.size();
    bool any = false;
    double sum = 0;
    for (int i = 0; i < n && m_plugin; ++i) {
        // 只合计勾选“计入总功率”且已启用的设备
        if (!cfg.devices[i].inTotal || !cfg.devices[i].enabled) continue;
        if (m_plugin->IsDeviceConnected(i)) {
            any = true;
            sum += m_plugin->GetDeviceWatts(i);
        }
    }
    valueText = any ? FormatWatts(sum, cfg.decimalPlaces, cfg.showUnit) : L"--";
    return valueText.c_str();
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
    // 当前宿主不会 delete 插件实例（TrafficMonitor 启动时缓存 ITMPlugin* 直至进程
    // 退出），此析构是防御路径：仅做停线程兜底；历史收尾保存由 SampleLoop 退出时
    // 完成，不在此重复（原先这里的保存代码与 SampleLoop 的收尾保存重复且不可达）
    m_stopFlag = true;
    if (m_sampleThread.joinable())
        m_sampleThread.join();
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
    //    （目标为按设备身份的新命名 MijiaPower_history_<ip>_s<槽位>.json，
    //     空 IP 设备为 MijiaPower_history_s<槽位>.json，下同）
    std::wstring newPath = cm.GetHistoryFilePath(0);
    std::wstring oldPath = cm.GetLegacyHistoryFilePath();
    if (GetFileAttributesW(newPath.c_str()) == INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW(oldPath.c_str()) != INVALID_FILE_ATTRIBUTES)
        MoveFileW(oldPath.c_str(), newPath.c_str());

    // 2) v1.1.0/1.1.1：按索引命名的 MijiaPower_history_N.json → 按设备身份命名
    //    （GetHistoryFilePath 现返回 MijiaPower_history_<ip>_s<槽位>.json /
    //     空 IP 设备为 MijiaPower_history_s<槽位>.json）
    for (int i = 0; i < (int)cfg.devices.size() && i < MAX_DEVICES; ++i) {
        std::wstring newP = cm.GetHistoryFilePath(i);
        std::wstring oldP = cm.GetIndexHistoryFilePath(i);
        if (GetFileAttributesW(newP.c_str()) == INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW(oldP.c_str()) != INVALID_FILE_ATTRIBUTES)
            MoveFileW(oldP.c_str(), newP.c_str());
    }

    // 3) v1.2.4 及更早：按 IP(+同 IP 序号) 命名的 MijiaPower_history_<ip>[_N].json
    //    → 按设备身份命名（IP+持久化槽位），详见 ConfigManager 内说明
    cm.MigrateLegacyHistoryFiles();

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
            // 配置变更后立即重试：清零退避计数（原子变量，UI 线程写安全）
            st->failCount.store(0, std::memory_order_relaxed);
            st->skipRounds.store(0, std::memory_order_relaxed);
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

    // 保存被移除设备的历史（按其自身身份 IP+持久化槽位取路径，与设备在
    // 配置中的排位无关，避免增删/重排导致的索引错位与文件覆盖）
    if (cfg.enableRecording) {
        for (int j = 0; j < (int)oldDevs.size(); ++j) {
            if (!used[j]) {
                DeviceConfig oldCfg;
                {
                    std::lock_guard<std::mutex> l(oldDevs[j]->mtx);
                    oldCfg = oldDevs[j]->cfg;   // 锁内拷贝完整配置，锁外取路径与写盘
                }
                oldDevs[j]->history.SaveToFile(cm.GetHistoryFilePathForDevice(oldCfg));
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
    // 置存活标志：供 Shutdown（动态卸载路径）的有界等待轮询
    m_sampleAlive = true;

    // 首次连接所有设备（PollOne 对未连接设备执行连接）
    auto devs = SnapshotDevices();
    for (auto& st : devs) {
        if (m_stopFlag) break;
        PollOne(*st, ConfigManager::Instance().Get().enableRecording);
    }

    int elapsed = 0;
    while (!m_stopFlag) {
        // 拆成 10×100ms：停止信号最长 100ms 内被观察到（原 Sleep(1000) 会导致
        // 退出最长迟滞 1 秒）；轮次/间隔逻辑不变
        for (int i = 0; i < 10 && !m_stopFlag; ++i)
            Sleep(100);
        if (m_stopFlag) break;
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

    // 最后一刻才清存活标志，保证 Shutdown 的有界等待覆盖整个收尾保存过程
    m_sampleAlive = false;
}

// ─── 采样连续失败后的指数退避 ───
// 第 f 次连续失败后跳过 (1<<min(f,3))-1 = 1/3/7/7... 轮并封顶 7：
// 避免离线设备每轮都空等 5 秒超时（8 台全离线一轮阻塞 40 秒），
// 也让 m_stopFlag 在退避轮次间有更多被观察的机会
void CMijiaPowerPlugin::BackOff(DeviceState& st) {
    int f = st.failCount.fetch_add(1, std::memory_order_relaxed) + 1;
    st.skipRounds.store(std::min(7, (1 << std::min(f, 3)) - 1), std::memory_order_relaxed);
}

// ─── 采样线程对单台设备的一次轮询 ───
// 注意：网络 I/O 不持有 st.mtx（该锁只保护 cfg 的短暂读写），
// 否则设置对话框“确定”时的 ApplyNewConfig 会在主线程被网络超时阻塞。
// st.device 仅由采样线程访问，无需加锁。
void CMijiaPowerPlugin::PollOne(DeviceState& st, bool enableRecording) {
    // 连续失败退避中：本轮跳过（BackOff 设置的跳轮数，逐轮递减）
    if (st.skipRounds.load(std::memory_order_relaxed) > 0) {
        st.skipRounds.fetch_sub(1, std::memory_order_relaxed);
        return;
    }

    // 快照配置（短暂持锁）
    DeviceConfig cfg;
    {
        std::lock_guard<std::mutex> lock(st.mtx);
        cfg = st.cfg;
    }
    // 禁用的设备不采集；若此前已连接（配置刚改为禁用），主动断开
    if (!cfg.enabled) {
        if (st.device) st.device.reset();
        st.connected = false;
        st.noData = false;
        return;
    }
    // Token 格式非法时密钥必然错误，不可能连接成功，直接短路
    // （与显示路径“未配置”的判定一致，避免无谓的握手流量）
    if (cfg.ip.empty() || cfg.token.empty() || !IsValidToken(cfg.token)) return;

    char ip[256], token[256];
    ToUtf8(cfg.ip, ip, 256);
    ToUtf8(cfg.token, token, 256);

    try {
        if (!st.device) {
            // 首次连接 / 断线重连：任何失败都不保留半初始化连接对象
            //（dev 在失败路径由 unique_ptr 自然释放）
            auto dev = std::make_unique<MiioDevice>(ip, token, 5000);
            double w = 0;
            if (dev->QueryPower(w) == MiioQueryResult::Ok) {
                st.device = std::move(dev);
                st.connected = true;
                st.noData = false;
                st.watts = w;
                st.failCount.store(0, std::memory_order_relaxed);
                st.skipRounds.store(0, std::memory_order_relaxed);
                if (enableRecording)
                    st.history.AddSample(w);
            } else {
                // NoData（设备应答但不支持功率属性）或 TransportError（握手/网络失败）：
                // 下轮退避后重试
                BackOff(st);
            }
            return;
        }
        // 已连接，读取数据
        double w = 0;
        switch (st.device->QueryPower(w)) {
        case MiioQueryResult::Ok:
            st.connected = true;
            st.noData = false;
            st.watts = w;
            st.failCount.store(0, std::memory_order_relaxed);
            st.skipRounds.store(0, std::memory_order_relaxed);
            if (enableRecording)
                st.history.AddSample(w);
            break;
        case MiioQueryResult::NoData:
            // 设备在线应答但不支持功率属性：保持连接，界面显示 "--"；
            // 若按传输失败处理会触发无限重连（下轮握手同样无 "value"，循环往复）
            st.connected = true;
            st.noData = true;
            BackOff(st);
            break;
        case MiioQueryResult::TransportError:
        default:
            // 连接失效，丢弃连接对象，退避后下轮重连
            st.device.reset();
            st.connected = false;
            st.noData = false;
            BackOff(st);
            break;
        }
    } catch (...) {
        st.connected = false;
        st.noData = false;
        BackOff(st);
    }
}

// ─── 供显示项访问 ───
bool CMijiaPowerPlugin::IsDeviceConnected(int index) const {
    auto devs = SnapshotDevices();
    if (index < 0 || index >= (int)devs.size()) return false;
    return devs[index]->connected.load();
}

// 已连接但设备不支持功率属性（显示 "--"，连接保持）
bool CMijiaPowerPlugin::IsDeviceNoData(int index) const {
    auto devs = SnapshotDevices();
    if (index < 0 || index >= (int)devs.size()) return false;
    return devs[index]->noData.load();
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
// 因此依赖 DataRequired 周期性落盘。
// 并发说明（此前注释有误，勿再据此评估）：DataRequired 并非运行于主线程——
// 宿主由监控工作线程调用（TrafficMonitorDlg 的 MonitorThreadCallback →
// DoMonitorAcquisition → plugin->DataRequired），与 UI 线程及本插件的采样线程并发
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
    case TMI_VERSION:     return L"1.3.0";
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
    // thread_local 文本缓冲：每线程独立缓冲，宿主未来多线程调用也不会互踩
    // 或产生悬垂指针（同线程两次调用覆盖旧值，与原成员缓存语义一致）
    thread_local std::wstring tooltipText;

    // 紧凑样式：每台设备一行（当前功率 + N 小时内最高/最低/平均），末行合计。
    // N 由设置中的“悬浮统计时段”指定（1-24 小时），基于分钟级历史数据。
    // 约束：TM 把所有插件的 tooltip 拼成一条喂给 MFC CToolTipCtrl::UpdateTipText，
    // 后者对超过 1024 字符的文本抛 CInvalidArgException（宿主弹"遇到不适当的参数。"）。
    // 本插件曾用每设备 15 行的详细样式（3 设备即 700+ 字符），与其他插件同载时会越界，
    // 故总长收在 ~400 字符内；完整统计仍在设置窗口查看。
    auto cfg = ConfigManager::Instance().Get();
    auto devs = SnapshotDevices();

    std::wostringstream oss;
    oss << std::fixed << std::setprecision(1);

    int n = (int)cfg.devices.size();
    if ((int)devs.size() < n) n = (int)devs.size();

    double total = 0;
    int connectedCount = 0;
    for (int i = 0; i < n; ++i) {
        if (i > 0) oss << L"\n";
        // 设备名截断，防超长自定义名
        std::wstring name = cfg.devices[i].name;
        if (name.size() > 16) name.assign(name, 0, 15).append(L"…");
        oss << L"【" << name << L"】";

        if (!devs[i]->connected.load()) {
            oss << (cfg.devices[i].enabled ? L" 未连接" : L" 已禁用");
            continue;
        }

        double w = devs[i]->watts.load();
        if (cfg.devices[i].inTotal && cfg.devices[i].enabled) {   // 与总功率项一致
            total += w;
            ++connectedCount;
        }
        oss << L" " << w << L" W";

        // N 小时内最高/最低/平均（分钟级历史，需开启历史记录且已有数据）
        if (cfg.enableRecording) {
            auto st = devs[i]->history.GetLongStats(cfg.tooltipStatsHours);
            if (st.valid)
                oss << L"（" << cfg.tooltipStatsHours << L"h内 最高" << st.maxW
                    << L" 最低" << st.minW << L" 均" << st.avgW << L"）";
        }
    }

    if (n > 1 && connectedCount > 0)
        oss << L"\n合计：" << total << L" W";

    tooltipText = oss.str();
    // 兜底护栏：极端情况下（多设备+超长名）也不越过 MFC 上限
    if (tooltipText.size() > 400) {
        std::wstring clipped = tooltipText.substr(0, 399);
        // 截断可能落在 UTF-16 代理对中间：末字符若是高位代理（0xD800..0xDBFF），
        // 丢掉这半个代理对，避免后续追加文本把它拼成非法序列
        //（MinGW 的 wchar_t 为 16 位，直接按 unsigned short 数值比较）
        if (!clipped.empty()) {
            unsigned short last = (unsigned short)clipped.back();
            if (last >= 0xD800u && last <= 0xDBFFu)
                clipped.pop_back();
        }
        // 避免截断在半个换行处影响观感
        size_t lastNl = clipped.rfind(L'\n');
        if (lastNl != std::wstring::npos && lastNl > 100) clipped.resize(lastNl);
        clipped += L"\n…（已折叠，详情见设置）";
        tooltipText = clipped;
    }
    return tooltipText.c_str();
}
