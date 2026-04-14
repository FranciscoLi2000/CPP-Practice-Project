#ifndef SERVER_HPP
#define SERVER_HPP

#include <string>
#include <map>
#include <vector>
#include <poll.h>

#include "Client.hpp"
#include "Channel.hpp"

// Server 是整个程序的核心：
//   - 持有 listening socket
//   - 维护 poll() 的 fd 列表
//   - 持有所有 Client 和 Channel 对象
//   - 驱动主事件循环
class Server
{
public:
    Server(int port, const std::string& password);
    ~Server();

    void run();   // 启动主循环（阻塞直到程序退出）

    // --- 供 CommandHandler 调用的公共接口 ---

    const std::string& getPassword() const;

    // 客户端查找
    Client*  getClientByFd(int fd);
    Client*  getClientByNick(const std::string& nick);

    // 频道管理
    Channel* getOrCreateChannel(const std::string& name);
    Channel* getChannel(const std::string& name);
    void     removeChannelIfEmpty(const std::string& name);

    // 向某个 fd 的发送缓冲区追加数据
    void     sendToClient(int fd, const std::string& msg);

    // 向某频道所有成员广播（可以排除某个 fd）
    void     broadcastToChannel(const std::string& chanName,
                                const std::string& msg,
                                int exceptFd = -1);

    // 断开并清理某个客户端
    void     disconnectClient(int fd);

    // 检查 nick 是否已被占用
    bool     isNickInUse(const std::string& nick) const;

private:
    // 初始化监听 socket
    void     _setupListenSocket();

    // poll 事件处理
    void     _acceptNewClient();
    void     _handleClientRead(int fd);
    void     _handleClientWrite(int fd);

    // 从 _pollfds 中删除某个 fd
    void     _removePollFd(int fd);

    int                      _port;
    std::string              _password;
    int                      _listenFd;   // 监听 socket 的 fd

    // poll() 所需的 fd 数组（动态增减）
    std::vector<struct pollfd> _pollfds;

    // fd → Client 对象
    std::map<int, Client*>   _clients;

    // 频道名 → Channel 对象
    std::map<std::string, Channel*> _channels;
};

#endif
