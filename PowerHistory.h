// PowerHistory.h - 功率历史记录与统计管理
#pragma once
#include "pch.h"

struct PowerSample {
    double   timestamp; // Unix 时间戳（秒，浮点）
    double   watts;     // 功率（W）
};

struct PowerStats {
    double maxW   = 0.0;
    double minW   = 0.0;
    double avgW   = 0.0;
    double lastW  = 0.0;
    size_t count  = 0;
    bool   valid  = false;
};

class PowerHistory {
public:
    static const size_t MAX_LONG     = 10080; // 7天（1分钟一次）

    PowerHistory();
    ~PowerHistory() = default;

    // 添加一个采样点（按分钟聚合写入长期队列：同一分钟只记首条）
    void AddSample(double watts);

    // 清空全部历史（内存）；供“清除历史”使用，避免旧数据之后被写回文件
    void Clear();

    // 长期（按分钟聚合）样本
    std::vector<PowerSample> GetLongSamples(int hours = 1) const;

    // 是否有历史数据（长期队列非空）
    bool HasData() const;

    // 统计
    PowerStats GetLongStats(int hours) const;     // 长期段

    // 持久化（保存/加载长期历史到JSON文件）
    void SaveToFile(const std::wstring& filePath) const;
    void LoadFromFile(const std::wstring& filePath);

private:
    mutable std::mutex       m_mutex;
    std::deque<PowerSample>  m_longterm;  // 长期（分钟级）
    double                   m_lastMinTs = 0.0; // 最后长期采样的分钟时间戳

    static double Now();
    PowerStats CalcStats(const std::vector<PowerSample>& pts) const;
};
