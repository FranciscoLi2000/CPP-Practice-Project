#include "Server.hpp"
#include "CommandHandler.hpp"

#include <iostream>
#include <stdexcept>
#include <cstring>    // memset, strerror
#include <cerrno>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>   // close
#include <csignal>    // sig_atomic_t

// 外部信号标志（定义在 main.cpp）
extern volatile sig_atomic_t g_stop;

// ----------------------------------------------------------------
//  构造 / 析构
// ----------------------------------------------------------------

Server::Server(int port, const std::string& password)
    : _port(port), _password(password), _listenFd(-1)
{
    _setupListenSocket();
    std::cout << "Server listening on port " << _port << std::endl;
}

Server::~Server()
{
    // 关闭所有客户端连接
    for (std::map<int, Client*>::iterator it = _clients.begin();
         it != _clients.end(); ++it)
    {
        close(it->first);
        delete it->second;
    }
    // 销毁所有频道
    for (std::map<std::string, Channel*>::iterator it = _channels.begin();
         it != _channels.end(); ++it)
    {
        delete it->second;
    }
    if (_listenFd >= 0)
        close(_listenFd);
}

// ----------------------------------------------------------------
//  初始化监听 socket
// ----------------------------------------------------------------
//  知识点：
//  1. socket()  → 创建一个 TCP socket，返回 fd
//  2. setsockopt(SO_REUSEADDR) → 重启服务器时不会出现"Address already in use"
//  3. bind()    → 把 socket 绑定到指定端口
//  4. listen()  → 开始监听，允许排队 128 个连接
//  5. fcntl(O_NONBLOCK) → 设为非阻塞，accept/recv/send 不会卡住主线程
// ----------------------------------------------------------------
void Server::_setupListenSocket()
{
    _listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (_listenFd < 0)
        throw std::runtime_error(std::string("socket() failed: ") + strerror(errno));

    // 允许端口复用（重启时不必等待 TIME_WAIT 结束）
    int opt = 1;
    setsockopt(_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 设为非阻塞
    fcntl(_listenFd, F_SETFL, O_NONBLOCK);

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;   // 监听所有网卡
    addr.sin_port        = htons(static_cast<uint16_t>(_port));

    if (bind(_listenFd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
        throw std::runtime_error(std::string("bind() failed: ") + strerror(errno));

    if (listen(_listenFd, 128) < 0)
        throw std::runtime_error(std::string("listen() failed: ") + strerror(errno));

    // 把监听 fd 加入 poll 列表
    struct pollfd pfd;
    pfd.fd      = _listenFd;
    pfd.events  = POLLIN;   // 只关心"有新连接可 accept"
    pfd.revents = 0;
    _pollfds.push_back(pfd);
}

// ----------------------------------------------------------------
//  主事件循环
// ----------------------------------------------------------------
//  知识点：
//  poll() 的工作原理：
//    - 你把一组 fd 和你关心的事件(POLLIN/POLLOUT)告诉内核
//    - 内核阻塞等待，直到任意一个 fd 上发生了你关心的事件
//    - poll() 返回后，遍历数组，检查 revents 字段知道谁有事件
//
//  POLLIN  → 该 fd 可读（有数据可 recv，或者有新连接可 accept）
//  POLLOUT → 该 fd 可写（发送缓冲区有空间，可以继续 send）
// ----------------------------------------------------------------
void Server::run()
{
    CommandHandler handler(*this);

    while (!g_stop)
    {
        // 动态决定哪些 fd 需要监听写事件：
        // 只有发送缓冲区不空的客户端才需要 POLLOUT，
        // 否则 poll 会一直触发 POLLOUT 浪费 CPU
        for (size_t i = 1; i < _pollfds.size(); ++i) {
            int fd = _pollfds[i].fd;
            std::map<int, Client*>::iterator it = _clients.find(fd);
            if (it != _clients.end() && it->second->hasPendingSend())
                _pollfds[i].events = POLLIN | POLLOUT;
            else
                _pollfds[i].events = POLLIN;
        }

        // timeout = 1000ms：即使没有事件，每秒也会醒来检查一次 g_stop
        int ready = poll(_pollfds.data(), _pollfds.size(), 1000);
        if (ready < 0) {
            if (errno == EINTR) continue;  // 被信号中断，正常重试
            std::cerr << "poll() error: " << strerror(errno) << std::endl;
            break;
        }

        // 遍历所有 fd，处理已就绪的事件
        // 注意：处理过程中可能增删 _pollfds，所以用下标而非迭代器，
        // 并且用 _pollfds.size() 而不是提前保存 size
        for (size_t i = 0; i < _pollfds.size(); ++i)
        {
            struct pollfd& pfd = _pollfds[i];
            if (pfd.revents == 0)
                continue;

            if (pfd.fd == _listenFd) {
                // 监听 fd 可读 → 有新客户端连接
                if (pfd.revents & POLLIN)
                    _acceptNewClient();
            } else {
                // 客户端 fd
                if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    // 连接异常或对端关闭
                    disconnectClient(pfd.fd);
                    // disconnectClient 会修改 _pollfds，i-- 重新检查当前位置
                    if (i > 0) --i;
                    continue;
                }
                if (pfd.revents & POLLIN) {
                    _handleClientRead(pfd.fd);
                    // 读取后可能已断开（recv==0），需要检查 fd 还在不在
                    if (_clients.find(pfd.fd) == _clients.end()) {
                        if (i > 0) --i;
                        continue;
                    }
                }
                if (pfd.revents & POLLOUT) {
                    _handleClientWrite(pfd.fd);
                }
            }
            // 处理命令（读取后才处理）
            if (_clients.find(pfd.fd) == _clients.end())
                continue;
            Client* client = _clients[pfd.fd];
            while (client->hasCompleteMessage()) {
                std::string line = client->extractMessage();
                if (!line.empty())
                    handler.handle(pfd.fd, line);
                // 命令处理后客户端可能已被断开
                if (_clients.find(pfd.fd) == _clients.end())
                    break;
            }
        }
    }
}

// ----------------------------------------------------------------
//  接受新客户端连接
// ----------------------------------------------------------------
void Server::_acceptNewClient()
{
    struct sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(clientAddr);

    int clientFd = accept(_listenFd, (struct sockaddr*)&clientAddr, &addrLen);
    if (clientFd < 0) {
        // EAGAIN / EWOULDBLOCK：非阻塞 socket 暂时没有新连接，正常
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            std::cerr << "accept() error: " << strerror(errno) << std::endl;
        return;
    }

    // 新客户端 fd 也设为非阻塞
    fcntl(clientFd, F_SETFL, O_NONBLOCK);

    // 记录客户端 IP
    std::string hostname = inet_ntoa(clientAddr.sin_addr);

    Client* client = new Client(clientFd);
    client->setHostname(hostname);
    _clients[clientFd] = client;

    // 加入 poll 监听列表
    struct pollfd pfd;
    pfd.fd      = clientFd;
    pfd.events  = POLLIN;
    pfd.revents = 0;
    _pollfds.push_back(pfd);

    std::cout << "New client connected: fd=" << clientFd
              << " from " << hostname << std::endl;
}

// ----------------------------------------------------------------
//  读取客户端数据
// ----------------------------------------------------------------
void Server::_handleClientRead(int fd)
{
    char buf[4096];
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);

    if (n <= 0) {
        // n == 0：客户端主动关闭连接
        // n  < 0：读取错误（EAGAIN 例外，表示暂时无数据）
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return;
        disconnectClient(fd);
        return;
    }

    buf[n] = '\0';
    _clients[fd]->appendRecvBuf(std::string(buf, n));
}

// ----------------------------------------------------------------
//  发送缓冲区数据到客户端
// ----------------------------------------------------------------
void Server::_handleClientWrite(int fd)
{
    Client* client = _clients[fd];
    const std::string& buf = client->getSendBuf();
    if (buf.empty()) return;

    ssize_t n = send(fd, buf.c_str(), buf.size(), 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;  // 下次再发
        disconnectClient(fd);
        return;
    }
    client->consumeSendBuf(static_cast<size_t>(n));
}

// ----------------------------------------------------------------
//  断开并清理客户端
// ----------------------------------------------------------------
void Server::disconnectClient(int fd)
{
    std::map<int, Client*>::iterator it = _clients.find(fd);
    if (it == _clients.end()) return;

    Client* client = it->second;
    std::string nick = client->getNickname();
    std::cout << "Client disconnected: fd=" << fd
              << (nick.empty() ? "" : " nick=" + nick) << std::endl;

    // 从所有频道中移除
    // 注意：C++98 的 map::erase() 返回 void，不能赋值给迭代器
    // 需要在 erase 前手动推进迭代器
    for (std::map<std::string, Channel*>::iterator chIt = _channels.begin();
         chIt != _channels.end(); )
    {
        chIt->second->removeMember(fd);
        if (chIt->second->getMemberCount() == 0) {
            delete chIt->second;
            std::map<std::string, Channel*>::iterator toErase = chIt;
            ++chIt;
            _channels.erase(toErase);
        } else {
            ++chIt;
        }
    }

    close(fd);
    delete client;
    _clients.erase(it);

    _removePollFd(fd);
}

void Server::_removePollFd(int fd)
{
    for (std::vector<struct pollfd>::iterator it = _pollfds.begin();
         it != _pollfds.end(); ++it)
    {
        if (it->fd == fd) {
            _pollfds.erase(it);
            return;
        }
    }
}

// ----------------------------------------------------------------
//  公共查询接口
// ----------------------------------------------------------------

const std::string& Server::getPassword() const { return _password; }

Client* Server::getClientByFd(int fd)
{
    std::map<int, Client*>::iterator it = _clients.find(fd);
    return it != _clients.end() ? it->second : NULL;
}

Client* Server::getClientByNick(const std::string& nick)
{
    for (std::map<int, Client*>::iterator it = _clients.begin();
         it != _clients.end(); ++it)
    {
        if (it->second->getNickname() == nick)
            return it->second;
    }
    return NULL;
}

bool Server::isNickInUse(const std::string& nick) const
{
    for (std::map<int, Client*>::const_iterator it = _clients.begin();
         it != _clients.end(); ++it)
    {
        if (it->second->getNickname() == nick)
            return true;
    }
    return false;
}

Channel* Server::getChannel(const std::string& name)
{
    std::map<std::string, Channel*>::iterator it = _channels.find(name);
    return it != _channels.end() ? it->second : NULL;
}

Channel* Server::getOrCreateChannel(const std::string& name)
{
    std::map<std::string, Channel*>::iterator it = _channels.find(name);
    if (it != _channels.end())
        return it->second;
    Channel* ch = new Channel(name);
    _channels[name] = ch;
    return ch;
}

void Server::removeChannelIfEmpty(const std::string& name)
{
    std::map<std::string, Channel*>::iterator it = _channels.find(name);
    if (it != _channels.end() && it->second->getMemberCount() == 0) {
        delete it->second;
        _channels.erase(it);
    }
}

// ----------------------------------------------------------------
//  消息发送接口
// ----------------------------------------------------------------

void Server::sendToClient(int fd, const std::string& msg)
{
    std::map<int, Client*>::iterator it = _clients.find(fd);
    if (it != _clients.end())
        it->second->appendSendBuf(msg);
}

void Server::broadcastToChannel(const std::string& chanName,
                                const std::string& msg, int exceptFd)
{
    Channel* ch = getChannel(chanName);
    if (!ch) return;

    std::vector<int> fds = ch->getMemberFds();
    for (size_t i = 0; i < fds.size(); ++i) {
        if (fds[i] != exceptFd)
            sendToClient(fds[i], msg);
    }
}
