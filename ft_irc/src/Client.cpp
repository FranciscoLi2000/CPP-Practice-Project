#include "Client.hpp"
#include <cstddef>  // size_t

Client::Client(int fd)
    : _fd(fd),
      _passChecked(false),
      _registered(false)
{}

Client::~Client() {}

int  Client::getFd()         const { return _fd; }
bool Client::isRegistered()  const { return _registered; }
bool Client::isPassChecked() const { return _passChecked; }

const std::string& Client::getNickname() const { return _nickname; }
const std::string& Client::getUsername() const { return _username; }
const std::string& Client::getRealname() const { return _realname; }
const std::string& Client::getHostname() const { return _hostname; }

void Client::setNickname(const std::string& nick) { _nickname = nick; }
void Client::setUsername(const std::string& user) { _username = user; }
void Client::setRealname(const std::string& real) { _realname = real; }
void Client::setHostname(const std::string& host) { _hostname = host; }
void Client::setPassChecked(bool v)               { _passChecked = v; }

// 检查是否 NICK 和 USER 都已设置，是的话标记注册完成
void Client::tryRegister()
{
    if (!_registered && !_nickname.empty() && !_username.empty())
        _registered = true;
}

// ---------- 接收缓冲区 ----------

void Client::appendRecvBuf(const std::string& data)
{
    _recvBuf += data;
}

// 判断缓冲区中是否有完整的 IRC 消息（以 \n 结尾，兼容 \r\n 和纯 \n）
bool Client::hasCompleteMessage() const
{
    return _recvBuf.find('\n') != std::string::npos;
}

// 从缓冲区取出第一条完整命令，去除尾部的 \r\n
std::string Client::extractMessage()
{
    std::string::size_type pos = _recvBuf.find('\n');
    if (pos == std::string::npos)
        return "";

    std::string msg = _recvBuf.substr(0, pos);
    _recvBuf.erase(0, pos + 1);

    // 去掉可能的 \r
    if (!msg.empty() && msg[msg.size() - 1] == '\r')
        msg.erase(msg.size() - 1);

    return msg;
}

// ---------- 发送缓冲区 ----------

void Client::appendSendBuf(const std::string& data)
{
    _sendBuf += data;
}

bool Client::hasPendingSend() const
{
    return !_sendBuf.empty();
}

const std::string& Client::getSendBuf() const
{
    return _sendBuf;
}

// send() 成功发了 n 字节后，把这些字节从缓冲区头部删掉
void Client::consumeSendBuf(size_t n)
{
    if (n >= _sendBuf.size())
        _sendBuf.clear();
    else
        _sendBuf.erase(0, n);
}
