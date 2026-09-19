// test_unit.cpp - 离线单元测试（纯 Win32 控制台程序，不需要真实设备/网络）
// 构建：见 build_gcc.sh（test_unit.cpp + PluginConfig.cpp + PowerHistory.cpp + OptionsDlg.cpp + pch.cpp）
// 注意：本文件内部 #include "MiioDevice.cpp" 与 "MijiaPowerPlugin.cpp"，
// 目的是访问 MiioDevice.cpp 中 static 的 HexToBytes 与插件全局的 FormatWatts；
// 因此这两个 .cpp 绝不能再被单独编译进同一目标，否则重复定义。
#include "pch.h"
#include "MiioDevice.cpp"
#include "MijiaPowerPlugin.cpp"
#include <cstdio>
#include <limits>

// ─── 极简断言 ───
static int g_pass = 0;
static int g_fail = 0;

static void Check(bool ok, const char* expr, const char* file, int line) {
    if (ok) { ++g_pass; printf("  [PASS] %s\n", expr); }
    else    { ++g_fail; printf("  [FAIL] %s:%d  %s\n", file, line, expr); }
}
#define CHECK(expr) Check(!!(expr), #expr, __FILE__, __LINE__)
#define CASE(title) do { printf("-- %s\n", title); fflush(stdout); } while (0)

// ─── 工具 ───
static double NowSec() {
    auto tp = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration<double>(tp).count();
}
static std::string F1(double v) { char b[64]; snprintf(b, sizeof(b), "%.1f", v); return b; }
static std::string ToNarrow(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, 0, 0);
    std::string s(n > 0 ? n - 1 : 0, 0);
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, 0, 0);
    return s;
}

static std::wstring g_base;   // 测试根目录（GetTempPath + PID）
static std::wstring MakeSubDir(const wchar_t* name) {
    std::wstring d = g_base + L"\\" + name;
    CreateDirectoryW(d.c_str(), NULL);
    return d;
}
static bool FileExists(const std::wstring& p) {
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}
static bool WriteAllA(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, data.data(), (DWORD)data.size(), &written, NULL);
    CloseHandle(h);
    return ok && written == (DWORD)data.size();
}
static bool ReadAllA(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    out.clear();
    char buf[8192]; DWORD n = 0;
    while (ReadFile(h, buf, sizeof(buf), &n, NULL) && n) out.append(buf, n);
    CloseHandle(h);
    return true;
}
static bool NearT(double a, double b) { return std::fabs(a - b) < 0.5; }

// ═══════════════════════════════════════════════
// A. PowerHistory
// ═══════════════════════════════════════════════
static void TestPowerHistory() {
    // A1 分钟聚合：同一分钟内两次采样只记首条
    CASE("A1 minute aggregation: same-minute samples collapse to 1 (first watts kept)");
    {
        std::unique_ptr<PowerHistory> ph;
        for (int attempt = 0; attempt < 6 && !ph; ++attempt) {
            auto p = std::make_unique<PowerHistory>();
            p->AddSample(5.0);
            p->AddSample(6.0);
            if (p->GetLongSamples(24).size() == 1) ph = std::move(p);
            else Sleep(1100);   // 极小概率跨分钟边界，等下一分钟重试
        }
        CHECK(ph != nullptr);
        if (ph) {
            auto v = ph->GetLongSamples(24);
            CHECK(v.size() == 1);
            CHECK(v.size() == 1 && v[0].watts == 5.0);
        }
    }

    // A2 空数据不落盘（P3-3）
    CASE("A2 empty history is not persisted (P3-3)");
    {
        std::wstring d = MakeSubDir(L"a2");
        PowerHistory ph;
        ph.SaveToFile(d + L"\\a.json");
        CHECK(!FileExists(d + L"\\a.json"));
    }

    // A3 保存格式与原子性（P2-3）
    CASE("A3 save format + atomic replace, no .tmp left behind (P2-3)");
    {
        std::wstring d = MakeSubDir(L"a3");
        std::wstring src = d + L"\\in.json";
        CHECK(WriteAllA(src, "[{\"t\":1726800000.0,\"w\":5.5}]"));
        PowerHistory ph;
        ph.LoadFromFile(src);
        CHECK(ph.HasData());
        std::wstring out = d + L"\\b.json";
        ph.SaveToFile(out);
        CHECK(FileExists(out));
        std::string content;
        CHECK(ReadAllA(out, content));
        bool starts = content.rfind("[{\"t\":", 0) == 0;
        bool ends   = content.size() >= 2 &&
                      content.compare(content.size() - 2, 2, "}]") == 0;
        bool hasW   = content.find("\"w\":5.50") != std::string::npos;
        CHECK(starts);
        CHECK(ends);
        CHECK(hasW);
        CHECK(!FileExists(out + L".tmp"));
    }

    // A4 往返一致：3 条不同分钟样本
    CASE("A4 roundtrip: 3 samples in distinct minutes survive save/load");
    {
        std::wstring d = MakeSubDir(L"a4");
        double t1 = std::floor(NowSec() / 60.0) * 60.0;
        double t2 = t1 + 60.0, t3 = t1 + 120.0;
        std::string js = std::string("[{\"t\":") + F1(t1) + ",\"w\":5.50},{\"t\":" +
                         F1(t2) + ",\"w\":6.50},{\"t\":" + F1(t3) + ",\"w\":7.50}]";
        std::wstring f1 = d + L"\\r1.json", f2 = d + L"\\r2.json";
        CHECK(WriteAllA(f1, js));
        PowerHistory ph;
        ph.LoadFromFile(f1);
        ph.SaveToFile(f2);
        PowerHistory ph2;
        ph2.LoadFromFile(f2);
        auto v = ph2.GetLongSamples(24);
        CHECK(v.size() == 3);
        bool ok = v.size() == 3;
        if (ok)
            ok = NearT(v[0].timestamp, t1) && v[0].watts == 5.5
              && NearT(v[1].timestamp, t2) && v[1].watts == 6.5
              && NearT(v[2].timestamp, t3) && v[2].watts == 7.5;
        CHECK(ok);
    }

    // A5 半截/坏对象文件：绝不“半截多解析出一条”（P2-3/发现2）
    //     时间戳取当前时刻附近，保证 GetLongSamples(24) 能观察计数
    CASE("A5 truncated/broken files never yield an extra record (P2-3)");
    {
        std::wstring d = MakeSubDir(L"a5");
        double t1 = std::floor(NowSec() / 60.0) * 60.0;
        double t2 = t1 + 60.0, t3 = t1 + 120.0;
        struct Case5 { std::string desc; std::string json; int expect; };
        std::vector<Case5> cases;
        cases.push_back({ "tail truncated inside w of 3rd record",
            std::string("[{\"t\":") + F1(t1) + ",\"w\":5.0},{\"t\":" + F1(t2) +
            ",\"w\":6.0},{\"t\":" + F1(t3) + ",\"w\":", 2 });
        cases.push_back({ "tail truncated inside t of 2nd record",
            std::string("[{\"t\":") + F1(t1) + ",\"w\":5.0},{\"t\":2", 1 });
        cases.push_back({ "broken object in middle skipped",
            std::string("[{\"t\":") + F1(t1) + ",\"w\":5.0},{\"bad\":1},{\"t\":" +
            F1(t3) + ",\"w\":7.0}]", 2 });
        for (size_t i = 0; i < cases.size(); ++i) {
            std::wstring f = d + L"\\half" + std::to_wstring((int)i) + L".json";
            CHECK(WriteAllA(f, cases[i].json));
            PowerHistory ph;   // 全新实例：不携带上一次载入的内存残留
            ph.LoadFromFile(f);
            auto v = ph.GetLongSamples(24);
            CHECK((int)v.size() == cases[i].expect);
            if ((int)v.size() != cases[i].expect)
                printf("      (%s: got %d, expect %d)\n",
                       cases[i].desc.c_str(), (int)v.size(), cases[i].expect);
        }
    }

    // A6 时钟回拨：回拨后必须恢复记录（P2-2）
    CASE("A6 clock rollback recovers recording (P2-2)");
    {
        std::wstring d = MakeSubDir(L"a6");
        std::unique_ptr<PowerHistory> ph;
        for (int attempt = 0; attempt < 6 && !ph; ++attempt) {
            auto p = std::make_unique<PowerHistory>();
            p->AddSample(5.0);
            p->AddSample(6.0);   // 同一分钟 → 长期 1 条
            if (p->GetLongSamples(24).size() == 1) ph = std::move(p);
            else Sleep(1100);
        }
        CHECK(ph != nullptr);
        if (ph) {
            double tsFuture = NowSec() + 7200.0;
            std::wstring f = d + L"\\future.json";
            CHECK(WriteAllA(f, std::string("[{\"t\":") + F1(tsFuture) + ",\"w\":9.50}]"));
            ph->LoadFromFile(f);
            CHECK(ph->GetLongSamples(24).size() == 1);
            ph->AddSample(8.0);   // 相对文件末样本时钟回拨 2 小时 → 必须恢复写入
            auto v = ph->GetLongSamples(24);
            CHECK(v.size() == 2);
            bool hasNew = false, hasOld = false;
            for (const auto& s : v) {
                if (s.watts == 8.0) hasNew = true;
                if (NearT(s.timestamp, tsFuture) && s.watts == 9.5) hasOld = true;
            }
            CHECK(hasNew);
            CHECK(hasOld);
        }
    }

    // A7 解析性能：23000 条样本 < 500ms（P1-2）。
    //     注意产品语义：LoadFromFile 按 MAX_LONG（10080 = 7 天分钟级上限）封顶，
    //     保留“最新的 MAX_LONG 条”；解析器仍须处理文件内全部 23000 条。
    CASE("A7 parse performance: 23000-sample file loads < 500ms (P1-2)");
    {
        std::wstring d = MakeSubDir(L"a7");
        const int N = 23000;
        const double step = 3.5;
        double t0 = NowSec() - 86399.0;   // 全部落进 24h 观察窗口
        std::string js;
        js.reserve((size_t)N * 32 + 16);
        js += "[";
        char buf[80];
        for (int i = 0; i < N; ++i) {
            if (i) js += ",";
            snprintf(buf, sizeof(buf), "{\"t\":%.1f,\"w\":%.2f}",
                     t0 + i * step, 5.0 + (i % 10));
            js += buf;
        }
        js += "]";
        std::wstring f = d + L"\\big.json";
        CHECK(WriteAllA(f, js));

        LARGE_INTEGER freq{}, c0{}, c1{};
        QueryPerformanceFrequency(&freq);
        PowerHistory ph;
        QueryPerformanceCounter(&c0);
        ph.LoadFromFile(f);
        QueryPerformanceCounter(&c1);
        double ms = (double)(c1.QuadPart - c0.QuadPart) * 1000.0 / (double)freq.QuadPart;
        printf("      loaded %d samples (%.2f MB chars) in %.1f ms\n",
               N, js.size() / 1048576.0, ms);
        CHECK(ms < 500.0);
        auto v = ph.GetLongSamples(24);
        CHECK(v.size() == PowerHistory::MAX_LONG);   // 封顶 MAX_LONG，保留最新段
        // 保留的必须恰好是“最新的 MAX_LONG 条”：证明解析器处理了全部 23000 条
        double firstKeepT = t0 + (double)(N - (int)PowerHistory::MAX_LONG) * step;
        double lastKeepT  = t0 + (double)(N - 1) * step;
        CHECK(v.size() == PowerHistory::MAX_LONG
              && NearT(v.front().timestamp, firstKeepT)
              && NearT(v.back().timestamp,  lastKeepT)
              && v.back().watts == 14.0);
    }

    // A8 Clear 后保存不创建文件
    CASE("A8 Clear() then save writes nothing");
    {
        std::wstring d = MakeSubDir(L"a8");
        std::wstring f = d + L"\\in.json";
        CHECK(WriteAllA(f, std::string("[{\"t\":") +
                        F1(std::floor(NowSec() / 60.0) * 60.0) + ",\"w\":5.00}]"));
        PowerHistory ph;
        ph.LoadFromFile(f);
        CHECK(ph.HasData());
        ph.Clear();
        CHECK(!ph.HasData());
        std::wstring out = d + L"\\out.json";
        ph.SaveToFile(out);
        CHECK(!FileExists(out));
    }
}

// ═══════════════════════════════════════════════
// B. ConfigManager（Instance() 单例 + 临时目录 INI）
// ═══════════════════════════════════════════════
static void WriteDeviceIni(const std::wstring& ini, int count,
                           const std::vector<std::wstring>& ips,
                           const std::vector<std::wstring>& slots) {
    WritePrivateProfileStringW(L"Plugin", L"DeviceCount", std::to_wstring(count).c_str(), ini.c_str());
    for (int i = 1; i <= count; ++i) {
        std::wstring sec = L"Device" + std::to_wstring(i);
        if (i <= (int)ips.size() && !ips[i - 1].empty())
            WritePrivateProfileStringW(sec.c_str(), L"IP", ips[i - 1].c_str(), ini.c_str());
        if (i <= (int)slots.size() && !slots[i - 1].empty())
            WritePrivateProfileStringW(sec.c_str(), L"HistorySlot", slots[i - 1].c_str(), ini.c_str());
    }
}

static void TestConfigManager() {
    auto& cm = ConfigManager::Instance();

    // B9 槽位分配（P1-1）：INI 无 HistorySlot → 按顺序 1/2/3
    CASE("B9 slot allocation: 3 devices without HistorySlot get 1/2/3 (P1-1)");
    {
        std::wstring d = MakeSubDir(L"b9");
        WriteDeviceIni(d + L"\\MijiaPower.ini", 3, { L"ip1", L"ip2", L"ip3" }, {});
        cm.SetConfigDir(d);
        cm.Load();
        auto cfg = cm.Get();
        CHECK(cfg.devices.size() == 3);
        CHECK(cfg.devices[0].historySlot == 1 &&
              cfg.devices[1].historySlot == 2 &&
              cfg.devices[2].historySlot == 3);
        CHECK(cm.GetHistoryFilePath(0) == d + L"\\MijiaPower_history_ip1_s1.json");
        CHECK(cm.GetHistoryFilePath(1) == d + L"\\MijiaPower_history_ip2_s2.json");
        CHECK(cm.GetHistoryFilePath(2) == d + L"\\MijiaPower_history_ip3_s3.json");
    }

    // B10 重复/越界槽位自愈
    CASE("B10 duplicate/out-of-range slots self-heal to 1/2/3");
    {
        std::wstring d = MakeSubDir(L"b10");
        WriteDeviceIni(d + L"\\MijiaPower.ini", 3, { L"ip1", L"ip2", L"ip3" },
                       { L"1", L"1", L"99" });
        cm.SetConfigDir(d);
        cm.Load();
        auto cfg = cm.Get();
        CHECK(cfg.devices.size() == 3);
        CHECK(cfg.devices[0].historySlot == 1 &&
              cfg.devices[1].historySlot == 2 &&
              cfg.devices[2].historySlot == 3);
    }

    // B11 同 IP 两台设备路径互异
    CASE("B11 two devices sharing one IP get distinct history paths");
    {
        std::wstring d = MakeSubDir(L"b11");
        WriteDeviceIni(d + L"\\MijiaPower.ini", 2, { L"10.9.9.9", L"10.9.9.9" }, {});
        cm.SetConfigDir(d);
        cm.Load();
        CHECK(cm.GetHistoryFilePath(0) == d + L"\\MijiaPower_history_10.9.9.9_s1.json");
        CHECK(cm.GetHistoryFilePath(1) == d + L"\\MijiaPower_history_10.9.9.9_s2.json");
        CHECK(cm.GetHistoryFilePath(0) != cm.GetHistoryFilePath(1));
    }

    // B12 重排稳定：槽位随设备携带，路径与排位无关
    CASE("B12 reorder keeps each device's history path");
    {
        std::wstring d = MakeSubDir(L"b12");
        cm.SetConfigDir(d);
        DeviceConfig A; A.ip = L"10.8.0.1"; A.historySlot = 1; A.name = L"A";
        DeviceConfig B; B.ip = L"10.8.0.2"; B.historySlot = 2; B.name = L"B";
        PluginConfig c1; c1.devices = { A, B };
        cm.Set(c1);
        std::wstring pA = cm.GetHistoryFilePath(0);
        std::wstring pB = cm.GetHistoryFilePath(1);
        CHECK(pA == d + L"\\MijiaPower_history_10.8.0.1_s1.json");
        CHECK(pB == d + L"\\MijiaPower_history_10.8.0.2_s2.json");
        PluginConfig c2; c2.devices = { B, A };   // Set：重排，槽位随设备携带
        cm.Set(c2);
        CHECK(cm.GetHistoryFilePath(0) == pB);
        CHECK(cm.GetHistoryFilePath(1) == pA);
    }

    // B13 增删稳定（P1-1 核心场景）：删 A/B 留 C，路径不变；新设备 D 获未占用槽位
    CASE("B13 remove/add keeps surviving device path, new device gets free slot (P1-1)");
    {
        std::wstring d = MakeSubDir(L"b13");
        cm.SetConfigDir(d);
        DeviceConfig A; A.ip = L"10.7.0.1";
        DeviceConfig B; B.ip = L"10.7.0.2";
        DeviceConfig C; C.ip = L"10.7.0.2";   // 与 B 同 IP
        PluginConfig c; c.devices = { A, B, C };
        cm.Set(c);                            // 槽位归一化 1/2/3
        std::wstring pA = cm.GetHistoryFilePath(0);
        std::wstring pB = cm.GetHistoryFilePath(1);
        std::wstring pC = cm.GetHistoryFilePath(2);
        CHECK(pC == d + L"\\MijiaPower_history_10.7.0.2_s3.json");
        // Set 归一化发生在配置副本上：本地 C 需带回其已分配槽位再参与下次 Set
        C.historySlot = cm.Get().devices[2].historySlot;
        PluginConfig c2; c2.devices = { C };  // 只留 C（携带其原槽位）
        cm.Set(c2);
        CHECK(cm.GetHistoryFilePath(0) == pC);
        DeviceConfig D; D.ip = L"10.7.0.3";   // 新设备，未占槽位
        PluginConfig c3; c3.devices = { C, D };
        cm.Set(c3);
        auto got = cm.Get();
        CHECK(got.devices.size() == 2);
        CHECK(cm.GetHistoryFilePath(0) == pC);
        std::wstring pD = cm.GetHistoryFilePath(1);
        CHECK(pD != pA && pD != pB && pD != pC);
        CHECK(got.devices[1].historySlot == 1 || got.devices[1].historySlot == 2);
    }

    // B14 旧命名迁移（P1-1）：正确 + 幂等
    CASE("B14 legacy IP-named history migration is correct and idempotent (P1-1)");
    {
        std::wstring d = MakeSubDir(L"b14");
        cm.SetConfigDir(d);
        DeviceConfig X; X.ip = L"10.6.0.2";
        DeviceConfig Y; Y.ip = L"10.6.0.2";
        PluginConfig c; c.devices = { X, Y };
        cm.Set(c);   // 槽位 1/2
        std::string body = "[{\"t\":1.0,\"w\":5.0}]";
        std::wstring src1 = d + L"\\MijiaPower_history_10.6.0.2.json";
        std::wstring src2 = d + L"\\MijiaPower_history_10.6.0.2_2.json";
        CHECK(WriteAllA(src1, body));
        CHECK(WriteAllA(src2, body));
        cm.MigrateLegacyHistoryFiles();
        std::wstring dst1 = d + L"\\MijiaPower_history_10.6.0.2_s1.json";
        std::wstring dst2 = d + L"\\MijiaPower_history_10.6.0.2_s2.json";
        CHECK(FileExists(dst1));
        CHECK(FileExists(dst2));
        std::string c1s, c2s;
        CHECK(ReadAllA(dst1, c1s) && c1s == body);
        CHECK(ReadAllA(dst2, c2s) && c2s == body);
        CHECK(!FileExists(src1));
        CHECK(!FileExists(src2));
        cm.MigrateLegacyHistoryFiles();   // 重复执行：幂等，不报错
        CHECK(FileExists(dst1));
        CHECK(FileExists(dst2));
    }

    // B15 Set 钳制（P3-4）：超过 8 台丢弃；设备名清除 \n 与 0x01
    CASE("B15 Set clamps to 8 devices and sanitizes names (P3-4)");
    {
        std::wstring d = MakeSubDir(L"b15");
        cm.SetConfigDir(d);
        PluginConfig c;
        for (int i = 0; i < 10; ++i) {
            DeviceConfig dv;
            dv.ip = L"10.5.0." + std::to_wstring(i);
            dv.name = (i == 0) ? (std::wstring(L"Na\nme") + wchar_t(0x01) + L"X") : L"dev";
            c.devices.push_back(dv);
        }
        cm.Set(c);
        auto got = cm.Get();
        CHECK(got.devices.size() == 8);
        bool sanitized = false, noCtrl = true;
        for (const auto& dv : got.devices) {
            if (dv.name == L"NameX") sanitized = true;
            for (wchar_t ch : dv.name)
                if (ch < 0x20 || ch == 0x7F) noCtrl = false;
        }
        CHECK(sanitized);
        CHECK(noCtrl);
    }
}

// ═══════════════════════════════════════════════
// C. Token / 格式化
// ═══════════════════════════════════════════════
static void TestTokenAndFormat() {
    // C16 IsValidToken
    CASE("C16 IsValidToken: exactly 32 hex chars");
    CHECK(IsValidToken(std::wstring(32, L'a')));
    CHECK(IsValidToken(std::wstring(16, L'a') + std::wstring(16, L'F')));
    CHECK(IsValidToken(L"00112233445566778899aabbccddeeff"));
    {
        std::wstring t = std::wstring(31, L'a');
        CHECK(!IsValidToken(t));
        t = std::wstring(32, L'a'); t[10] = L'g';
        CHECK(!IsValidToken(t));
        t = std::wstring(32, L'a'); t[20] = L' ';
        CHECK(!IsValidToken(t));
        t = std::wstring(33, L'a');
        CHECK(!IsValidToken(t));
    }

    // C17 HexToBytes（static，经 include 访问）：P2-1
    CASE("C17 HexToBytes strict validation (P2-1)");
    {
        unsigned char out[16] = {};
        CHECK(HexToBytes("000102030405060708090a0b0c0d0e0f", out, 16));
        bool bytesOk = true;
        for (int i = 0; i < 16; ++i)
            if (out[i] != (unsigned char)i) bytesOk = false;
        CHECK(bytesOk);
        std::string eg = std::string(32, 'a'); eg[5] = 'e'; eg[6] = 'g';
        CHECK(!HexToBytes(eg, out, 16));   // sscanf_s 时代 "eg" 会被部分放行
        CHECK(!HexToBytes(std::string(31, 'a'), out, 16));
        CHECK(!HexToBytes(std::string(33, 'a'), out, 16));
    }

    // C18 FormatWatts：NaN/±Inf/负值钳 0（P2-4）
    CASE("C18 FormatWatts clamps NaN/Inf/negative (P2-4)");
    {
        const double qnan = std::numeric_limits<double>::quiet_NaN();
        const double pinf = std::numeric_limits<double>::infinity();
        CHECK(FormatWatts(qnan, 1, true) == L"0.0W");
        CHECK(FormatWatts(pinf, 1, true) == L"0.0W");
        CHECK(FormatWatts(-5.0, 1, true) == L"0.0W");
        CHECK(FormatWatts(1234.567, 2, true) == L"1234.57W");
        CHECK(FormatWatts(1234.567, 2, false) == L"1234.57");
    }

    // C19 非法 token 设备 QueryPower 快速失败（P2-1 链路）
    CASE("C19 invalid-token device fails QueryPower instantly (P2-1 chain)");
    {
        LARGE_INTEGER freq{}, c0{}, c1{};
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&c0);
        MiioDevice dev("127.0.0.1", std::string(32, 'g'), 500);
        double w = -1.0;
        MiioQueryResult r = dev.QueryPower(w);
        QueryPerformanceCounter(&c1);
        double ms = (double)(c1.QuadPart - c0.QuadPart) * 1000.0 / (double)freq.QuadPart;
        printf("      QueryPower on invalid token returned in %.3f ms\n", ms);
        CHECK(r == MiioQueryResult::TransportError);
        CHECK(w == -1.0);    // 未被写入
        CHECK(ms < 200.0);   // Send 对 !m_tokenValid 短路：无握手、无网络流量
    }
}

int main() {
    wchar_t temp[MAX_PATH + 1] = {};
    GetTempPathW(MAX_PATH, temp);
    g_base = std::wstring(temp) + L"mijia_unit_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(g_base.c_str(), NULL);
    printf("test_unit: offline unit tests (base dir: %s)\n\n", ToNarrow(g_base).c_str());
    fflush(stdout);

    TestPowerHistory();
    TestConfigManager();
    TestTokenAndFormat();

    printf("\n==== summary ====\n");
    printf("PASS %d, FAIL %d\n", g_pass, g_fail);
    if (g_fail > 0) {
        printf("RESULT: FAIL\n");
        return 1;
    }
    printf("RESULT: ALL PASS\n");
    return 0;
}
