// PowerHistory.cpp - 功率历史记录实现
#include "pch.h"
#include "PowerHistory.h"
#include <chrono>
#include <sstream>
#include <fstream>
#include <cwchar>
#include <cmath>

double PowerHistory::Now() {
    auto tp = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration<double>(tp).count();
}

PowerHistory::PowerHistory() = default;

void PowerHistory::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_longterm.clear();
    m_lastMinTs = 0.0;
}

void PowerHistory::AddSample(double watts) {
    std::lock_guard<std::mutex> lock(m_mutex);
    double ts = Now();

    // 按分钟聚合长期记录：同一分钟（curMin == m_lastMinTs）只记首条
    double curMin = std::floor(ts / 60.0);
    bool record = false;
    if (curMin < m_lastMinTs) {
        // 时钟回拨恢复：重置分钟基线并照常记录该样本，
        // 否则长期队列会停记直到系统时钟追平回拨前的时刻
        m_lastMinTs = curMin;
        record = true;
    } else if (curMin > m_lastMinTs) {
        m_lastMinTs = curMin;
        record = true;
    }
    if (record) {
        m_longterm.push_back({ ts, watts });
        while (m_longterm.size() > MAX_LONG)
            m_longterm.pop_front();
    }
}

bool PowerHistory::HasData() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_longterm.empty();
}

std::vector<PowerSample> PowerHistory::GetLongSamples(int hours) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    double cutoff = Now() - hours * 3600.0;
    std::vector<PowerSample> result;
    for (const auto& s : m_longterm)
        if (s.timestamp >= cutoff) result.push_back(s);
    return result;
}

PowerStats PowerHistory::CalcStats(const std::vector<PowerSample>& pts) const {
    PowerStats st;
    if (pts.empty()) return st;
    st.valid = true;
    st.count = pts.size();
    st.maxW  = pts[0].watts;
    st.minW  = pts[0].watts;
    double sum = 0;
    for (const auto& p : pts) {
        if (p.watts > st.maxW) st.maxW = p.watts;
        if (p.watts < st.minW) st.minW = p.watts;
        sum += p.watts;
    }
    st.avgW  = sum / pts.size();
    st.lastW = pts.back().watts;
    return st;
}

PowerStats PowerHistory::GetLongStats(int hours) const {
    return CalcStats(GetLongSamples(hours));
}

// ─── 持久化（极简 JSON，无第三方库）────
void PowerHistory::SaveToFile(const std::wstring& filePath) const {
    // 锁内序列化到局部缓冲，锁外写盘：磁盘慢时不阻塞采样线程的 AddSample
    std::wstring json;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_longterm.empty())
            return; // 无数据不落盘：避免“清除历史”后周期落盘重建空文件
        std::wostringstream oss;
        oss << L"[";
        bool first = true;
        for (const auto& s : m_longterm) {
            if (!first) oss << L",";
            oss << L"{\"t\":" << std::fixed << std::setprecision(1) << s.timestamp
                << L",\"w\":" << std::setprecision(2) << s.watts << L"}";
            first = false;
        }
        oss << L"]";
        json = oss.str();
    }

    // 先写临时文件，成功后原子替换：避免写盘中途崩溃/断电留下半截文件被下次加载静默接受
    const std::wstring tmpPath = filePath + L".tmp";
    {
        std::wofstream f(tmpPath.c_str());
        if (!f.is_open()) return;
        f << json;
        f.close();
        if (!f.good()) { // 写入或缓冲刷写失败
            DeleteFileW(tmpPath.c_str());
            return;
        }
    }
    if (!MoveFileExW(tmpPath.c_str(), filePath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmpPath.c_str());
    }
}

// 解析单个 JSON 对象 [begin, end) 内的 "t":<数字> 与 "w":<数字>（end 为对象 '}' 的位置）
static bool ParseSampleJson(const std::wstring& content, size_t begin, size_t end, double& t, double& w) {
    if (begin >= end || end > content.size()) return false;
    const size_t ts = content.find(L"\"t\":", begin);
    if (ts == std::wstring::npos || ts + 4 > end) return false;
    const size_t ws = content.find(L"\"w\":", begin);
    if (ws == std::wstring::npos || ws + 4 > end) return false;
    // content 经 c_str() 保证 NUL 结尾，wcstod 基于“值起点”指针直读，
    // 遇到 '}'、',' 等非数字字符自动停止，无需 substr 拷贝大段内容（避免 O(n²)）
    const wchar_t* base = content.c_str();
    const wchar_t* tp   = base + ts + 4;
    const wchar_t* wp   = base + ws + 4;
    wchar_t* tEnd = nullptr;
    wchar_t* wEnd = nullptr;
    const double tv = wcstod(tp, &tEnd);
    const double wv = wcstod(wp, &wEnd);
    if (tEnd == tp || wEnd == wp) return false; // 未解析出任何数字
    if (!std::isfinite(tv) || !std::isfinite(wv)) return false; // 拒绝 inf/nan 等非有限值
    t = tv;
    w = wv;
    return true;
}

void PowerHistory::LoadFromFile(const std::wstring& filePath) {
    std::wifstream f(filePath.c_str());
    if (!f.is_open()) return;
    // 防御异常膨胀的文件：最多读取 1M 字符。
    // 7 天满载数据（10080 样本）约 43 万字符，1MB 足够；异常膨胀文件的解析耗时也随之封顶。
    static constexpr size_t MAX_LOAD_CHARS = 1024 * 1024;
    std::wstring content;
    content.reserve(MAX_LOAD_CHARS + 1);
    std::istreambuf_iterator<wchar_t> it(f), end;
    for (size_t i = 0; i < MAX_LOAD_CHARS && it != end; ++i, ++it)
        content.push_back(*it);

    // 锁外按对象解析到局部缓冲，全部完成后才加锁换入：避免持锁解析放大 UI 卡顿。
    // 只接受完整对象：找不到 '}'（文件半截，含截断在 "w":<数字> 中间）即丢弃尾部，
    // 绝不多解析出一条记录。
    std::deque<PowerSample> loaded;
    size_t pos = 0;
    while (true) {
        const size_t begin = content.find(L'{', pos);
        if (begin == std::wstring::npos) break;
        const size_t objEnd = content.find(L'}', begin);
        if (objEnd == std::wstring::npos) break; // 文件半截：丢弃尾部不完整记录
        double t = 0.0, w = 0.0;
        if (ParseSampleJson(content, begin, objEnd, t, w)) {
            loaded.push_back({ t, w });
            if (loaded.size() > MAX_LONG)
                loaded.pop_front();
        }
        pos = objEnd + 1;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_longterm.swap(loaded);
    m_lastMinTs = m_longterm.empty() ? 0.0 : std::floor(m_longterm.back().timestamp / 60.0);
}
