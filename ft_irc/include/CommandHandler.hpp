#ifndef COMMANDHANDLER_HPP
#define COMMANDHANDLER_HPP

#include <string>
#include <vector>

class Server;
class Client;

// CommandHandler 负责：
//   1. 把一行原始文本（如 "JOIN #test"）解析成 命令名 + 参数列表
//   2. 根据命令名路由到对应的处理函数
//   3. 每个处理函数通过 Server 接口读写数据，不直接操作 socket
class CommandHandler
{
public:
    explicit CommandHandler(Server& server);

    // 处理来自 fd 的一条完整命令行（不含 \r\n）
    void handle(int fd, const std::string& line);

private:
    // 解析辅助：把 "PRIVMSG #chan :hello world" 拆成
    //   cmd = "PRIVMSG", params = ["#chan", "hello world"]
    struct ParsedMsg {
        std::string              prefix;   // 以 : 开头的可选前缀（客户端发的通常没有）
        std::string              command;
        std::vector<std::string> params;
    };
    static ParsedMsg parse(const std::string& line);

    // --- 注册流程命令 ---
    void cmdPass  (Client& client, const ParsedMsg& msg);
    void cmdNick  (Client& client, const ParsedMsg& msg);
    void cmdUser  (Client& client, const ParsedMsg& msg);
    void cmdQuit  (Client& client, const ParsedMsg& msg);

    // --- 频道命令 ---
    void cmdJoin  (Client& client, const ParsedMsg& msg);
    void cmdPart  (Client& client, const ParsedMsg& msg);
    void cmdPrivmsg(Client& client, const ParsedMsg& msg);
    void cmdNotice(Client& client, const ParsedMsg& msg);

    // --- 基础协议 ---
    void cmdPing  (Client& client, const ParsedMsg& msg);
    void cmdPong  (Client& client, const ParsedMsg& msg);

    // --- Operator 命令 ---
    void cmdKick  (Client& client, const ParsedMsg& msg);
    void cmdInvite(Client& client, const ParsedMsg& msg);
    void cmdTopic (Client& client, const ParsedMsg& msg);
    void cmdMode  (Client& client, const ParsedMsg& msg);

    // MODE 子处理（复杂，单独拆出来）
    void _applyChannelMode(Client& client,
                           const std::string& chanName,
                           const std::string& modeStr,
                           const std::vector<std::string>& modeParams);

    // 回复辅助：生成标准 IRC 格式回复并送入发送缓冲区
    //   例：reply(client, "001", ":Welcome to the IRC Network")
    void reply(Client& client, const std::string& code,
               const std::string& text);

    // 错误回复辅助
    void error(Client& client, const std::string& code,
               const std::string& text);

    Server& _server;
};

#endif
