// MiioDevice.h - 纯C++ miIO协议实现（AES-128-CBC + UDP）
// 参考 Python 原版 miio_proto.py
#pragma once
#include "pch.h"

// ─── MD5 简易实现 ───
namespace MiioMD5 {
    struct MD5Context {
        unsigned int state[4];
        unsigned int count[2];
        unsigned char buffer[64];
    };
    void Init(MD5Context* ctx);
    void Update(MD5Context* ctx, const unsigned char* data, unsigned int len);
    void Final(unsigned char digest[16], MD5Context* ctx);
    void Compute(const unsigned char* data, size_t len, unsigned char out[16]);
}

// ─── AES-128-CBC 简易实现 ───
namespace MiioAES {
    // 加密（PKCS7 Padding，输入明文，输出密文，长度必须是16的倍数+padding后）
    bool Encrypt(const unsigned char key[16], const unsigned char iv[16],
                 const unsigned char* plaintext, size_t plainLen,
                 std::vector<unsigned char>& ciphertext);
    bool Decrypt(const unsigned char key[16], const unsigned char iv[16],
                 const unsigned char* ciphertext, size_t cipherLen,
                 std::vector<unsigned char>& plaintext);
}

// ─── miIO 查询结果（三态） ───
// Ok：拿到功率值；
// NoData：设备在线并已应答，但不支持目标属性/无该数据（保持连接，上层显示 "--"，
//         与"连接失效须重连"区分开——否则不支持该属性的设备会无限重连）；
// TransportError：握手/网络/协议层失败（上层应丢弃连接对象，下轮重连）
enum class MiioQueryResult {
    Ok,
    NoData,
    TransportError
};

// ─── miIO 设备类 ───
class MiioDevice {
public:
    static const int PORT = 54321;

    explicit MiioDevice(const std::string& ip, const std::string& token, int timeoutMs = 5000);
    ~MiioDevice();

    bool Handshake();

    // 查询功率 (W)。取代旧 GetPower(double&)：设备应答错误（无 "value" 字段）
    // 不再与网络失败混为一谈，调用方可区分"重连"与"保持连接显示 --"
    MiioQueryResult QueryPower(double& outWatts);

private:
    // 发送命令并取回响应 JSON（错误应答时返回整个响应且返回 true，
    // 由调用方按找不到期望字段处理）
    bool Send(const std::string& method, const std::string& paramsJson, std::string& outResult);

    std::string  m_ip;
    unsigned char m_token[16];
    bool         m_tokenValid = false;  // token 是否为合法的 32 位十六进制
    int          m_timeoutMs;
    unsigned char m_key[16];
    unsigned char m_iv[16];
    unsigned int  m_deviceId  = 0;
    unsigned int  m_serverStamp = 0;
    long long     m_stampDelta  = 0;
    int           m_msgId      = 1;
    bool          m_handshaked = false;

    std::vector<unsigned char> Encrypt(const std::string& plaintext);
    std::string Decrypt(const std::vector<unsigned char>& ciphertext);
    std::vector<unsigned char> BuildPacket(const std::string& payloadJson);
    std::string ParsePacket(const std::vector<unsigned char>& data);
    bool UdpSendRecv(const std::vector<unsigned char>& sendBuf,
                     std::vector<unsigned char>& recvBuf, int recvMax = 4096);
    unsigned int CurrentStamp() const;
};
