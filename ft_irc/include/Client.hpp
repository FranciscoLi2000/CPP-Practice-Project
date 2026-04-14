#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>
#include <vector>

// 每一个连接到服务器的 IRC 客户端都用一个 Client 对象来表示
// 核心数据：文件描述符(fd) + 身份信息 + 收发缓冲区 + 注册状态
class Client
{
public:
    explicit Client(int fd);
    ~Client();

    int         getFd()       const;
    bool        isRegistered() const;

    const std::string& getNickname() const;
    const std::string& getUsername() const;
    const std::string& getRealname() const;
    const std::string& getHostname() const;

    void setNickname(const std::string& nick);
    void setUsername(const std::string& user);
    void setRealname(const std::string& real);
    void setHostname(const std::string& host);

    // 密码握手
    bool        isPassChecked() const;
    void        setPassChecked(bool v);

    // nick 和 user 都设置完才算注册成功
    void        tryRegister();

    // 接收缓冲区：recv() 的数据先追加到这里，再按 \r\n 切割成完整命令
    void        appendRecvBuf(const std::string& data);
    bool        hasCompleteMessage() const;
    std::string extractMessage();    // 取出第一条完整命令（不含 \r\n）

    // 发送缓冲区：想发的数据先放这里，poll 说 fd 可写时再 send()
    void        appendSendBuf(const std::string& data);
    bool        hasPendingSend()  const;
    const std::string& getSendBuf() const;
    void        consumeSendBuf(size_t n);  // send() 发了 n 字节后调用

private:
    int         _fd;
    std::string _nickname;
    std::string _username;
    std::string _realname;
    std::string _hostname;

    bool        _passChecked;   // 是否已发送正确密码
    bool        _registered;    // 是否完成注册（PASS+NICK+USER 都完成）

    std::string _recvBuf;       // 接收缓冲区（未处理的原始字节）
    std::string _sendBuf;       // 发送缓冲区（待发出的数据）
};

#endif
