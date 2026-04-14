#include "CommandHandler.hpp"
#include "Server.hpp"
#include "Client.hpp"
#include "Channel.hpp"

#include <sstream>
#include <iostream>
#include <cstdlib>   // atoi

CommandHandler::CommandHandler(Server& server) : _server(server) {}

// ================================================================
//  解析 IRC 消息格式
//
//  IRC 消息格式（RFC 1459）：
//    [':' prefix SPACE] command [SPACE params] [SPACE ':' trailing]
//
//  例：":nick!user@host PRIVMSG #chan :hello world"
//    prefix  = "nick!user@host"
//    command = "PRIVMSG"
//    params  = ["#chan", "hello world"]
//
//  例："JOIN #test"
//    command = "JOIN"
//    params  = ["#test"]
// ================================================================
CommandHandler::ParsedMsg CommandHandler::parse(const std::string& line)
{
    ParsedMsg msg;
    std::istringstream ss(line);
    std::string token;

    // 可选前缀
    if (!line.empty() && line[0] == ':') {
        ss >> msg.prefix;
        msg.prefix = msg.prefix.substr(1);  // 去掉开头的 ':'
    }

    // 命令（转大写）
    ss >> msg.command;
    for (size_t i = 0; i < msg.command.size(); ++i)
        msg.command[i] = static_cast<char>(std::toupper(
            static_cast<unsigned char>(msg.command[i])));

    // 参数：遇到以 ':' 开头的 token，剩余所有内容都是最后一个参数（trailing）
    while (ss >> token) {
        if (token[0] == ':') {
            // trailing：把 ':' 去掉，再把剩余行内容拼进来
            std::string rest;
            std::getline(ss, rest);
            token = token.substr(1) + rest;
            msg.params.push_back(token);
            break;
        }
        msg.params.push_back(token);
    }

    return msg;
}

// ================================================================
//  派发入口：根据 command 字段调用对应处理函数
// ================================================================
void CommandHandler::handle(int fd, const std::string& line)
{
    Client* client = _server.getClientByFd(fd);
    if (!client) return;

    ParsedMsg msg = parse(line);
    if (msg.command.empty()) return;

    // 调试输出（可在发布时关闭）
    std::cout << "[fd=" << fd << "] " << line << std::endl;

    // 注册前只允许 PASS / NICK / USER / QUIT
    if (!client->isRegistered()) {
        if (msg.command == "PASS")  { cmdPass(*client, msg); return; }
        if (msg.command == "NICK")  { cmdNick(*client, msg); return; }
        if (msg.command == "USER")  { cmdUser(*client, msg); return; }
        if (msg.command == "QUIT")  { cmdQuit(*client, msg); return; }
        // 其他命令：尚未注册，返回 451
        _server.sendToClient(fd, ":server 451 * :You have not registered\r\n");
        return;
    }

    if      (msg.command == "NICK")    cmdNick(*client, msg);
    else if (msg.command == "USER")    cmdUser(*client, msg);
    else if (msg.command == "QUIT")    cmdQuit(*client, msg);
    else if (msg.command == "JOIN")    cmdJoin(*client, msg);
    else if (msg.command == "PART")    cmdPart(*client, msg);
    else if (msg.command == "PRIVMSG") cmdPrivmsg(*client, msg);
    else if (msg.command == "NOTICE")  cmdNotice(*client, msg);
    else if (msg.command == "PING")    cmdPing(*client, msg);
    else if (msg.command == "PONG")    cmdPong(*client, msg);
    else if (msg.command == "KICK")    cmdKick(*client, msg);
    else if (msg.command == "INVITE")  cmdInvite(*client, msg);
    else if (msg.command == "TOPIC")   cmdTopic(*client, msg);
    else if (msg.command == "MODE")    cmdMode(*client, msg);
    else {
        // 未知命令：421
        _server.sendToClient(client->getFd(),
            ":server 421 " + client->getNickname() +
            " " + msg.command + " :Unknown command\r\n");
    }
}

// ================================================================
//  辅助函数：构造 IRC 回复行
//
//  IRC 回复格式：":server <code> <target> <text>\r\n"
//  所有 IRC 消息必须以 \r\n 结尾
// ================================================================
void CommandHandler::reply(Client& client, const std::string& code,
                           const std::string& text)
{
    std::string msg = ":server " + code + " " +
                      (client.getNickname().empty() ? "*" : client.getNickname()) +
                      " " + text + "\r\n";
    _server.sendToClient(client.getFd(), msg);
}

void CommandHandler::error(Client& client, const std::string& code,
                           const std::string& text)
{
    reply(client, code, text);
}

// ================================================================
//  PASS <password>
//
//  必须是第一条命令，验证密码
// ================================================================
void CommandHandler::cmdPass(Client& client, const ParsedMsg& msg)
{
    if (client.isRegistered()) {
        error(client, "462", ":You may not reregister");
        return;
    }
    if (msg.params.empty()) {
        error(client, "461", "PASS :Not enough parameters");
        return;
    }
    if (msg.params[0] != _server.getPassword()) {
        error(client, "464", ":Password incorrect");
        _server.disconnectClient(client.getFd());
        return;
    }
    client.setPassChecked(true);
}

// ================================================================
//  NICK <nickname>
//
//  设置或更改昵称
// ================================================================
void CommandHandler::cmdNick(Client& client, const ParsedMsg& msg)
{
    if (msg.params.empty()) {
        error(client, "431", ":No nickname given");
        return;
    }

    const std::string& newNick = msg.params[0];

    // 简单校验：只允许字母、数字、下划线、连字符，且不以数字开头
    if (newNick.empty() || std::isdigit(static_cast<unsigned char>(newNick[0]))) {
        error(client, "432", newNick + " :Erroneous nickname");
        return;
    }
    for (size_t i = 0; i < newNick.size(); ++i) {
        char c = newNick[i];
        if (!std::isalnum(static_cast<unsigned char>(c)) &&
            c != '-' && c != '_' && c != '[' && c != ']' && c != '\\' && c != '`') {
            error(client, "432", newNick + " :Erroneous nickname");
            return;
        }
    }

    if (_server.isNickInUse(newNick) && newNick != client.getNickname()) {
        error(client, "433", newNick + " :Nickname is already in use");
        return;
    }

    std::string oldNick = client.getNickname();
    client.setNickname(newNick);

    // 注册完成后广播 NICK 变更
    if (client.isRegistered()) {
        std::string notice = ":" + oldNick + "!" + client.getUsername() +
                             "@" + client.getHostname() +
                             " NICK :" + newNick + "\r\n";
        _server.sendToClient(client.getFd(), notice);
    }

    client.tryRegister();

    // 如果刚完成注册，发送欢迎序列
    if (client.isRegistered() && client.isPassChecked()) {
        reply(client, "001", ":Welcome to the IRC Network " + client.getNickname());
        reply(client, "002", ":Your host is ircserv");
        reply(client, "003", ":This server was created today");
        reply(client, "004", client.getNickname() + " ircserv 1.0 o itkol");
    }
}

// ================================================================
//  USER <username> <mode> <unused> :<realname>
// ================================================================
void CommandHandler::cmdUser(Client& client, const ParsedMsg& msg)
{
    if (client.isRegistered()) {
        error(client, "462", ":You may not reregister");
        return;
    }
    if (msg.params.size() < 4) {
        error(client, "461", "USER :Not enough parameters");
        return;
    }
    client.setUsername(msg.params[0]);
    client.setRealname(msg.params[3]);
    client.tryRegister();

    // 如果 NICK 先到了，现在可以发欢迎消息
    if (client.isRegistered() && client.isPassChecked()) {
        reply(client, "001", ":Welcome to the IRC Network " + client.getNickname());
        reply(client, "002", ":Your host is ircserv");
        reply(client, "003", ":This server was created today");
        reply(client, "004", client.getNickname() + " ircserv 1.0 o itkol");
    }
}

// ================================================================
//  QUIT [:<message>]
// ================================================================
void CommandHandler::cmdQuit(Client& client, const ParsedMsg& msg)
{
    std::string reason = msg.params.empty() ? "Client quit" : msg.params[0];
    std::string quitMsg = ":" + client.getNickname() + "!" +
                          client.getUsername() + "@" + client.getHostname() +
                          " QUIT :" + reason + "\r\n";

    // 通知同频道的其他人
    // （Server::disconnectClient 会把他从频道里移除，这里先广播）
    // 为了避免重复，我们手动找出所有相关频道的成员
    // 这里简单实现：直接断开，让对方收到连接关闭
    _server.sendToClient(client.getFd(), "ERROR :Closing connection\r\n");
    _server.disconnectClient(client.getFd());
    (void)quitMsg;
}

// ================================================================
//  PING <token>
//
//  客户端定期发 PING 来检测连接，服务器必须用 PONG 回应
//  否则客户端会认为超时并断开
// ================================================================
void CommandHandler::cmdPing(Client& client, const ParsedMsg& msg)
{
    std::string token = msg.params.empty() ? "server" : msg.params[0];
    _server.sendToClient(client.getFd(), ":server PONG server :" + token + "\r\n");
}

void CommandHandler::cmdPong(Client& client, const ParsedMsg& msg)
{
    // 客户端回应我们发出的 PING，不需要处理，忽略即可
    (void)client;
    (void)msg;
}

// ================================================================
//  JOIN <#channel> [key]
//
//  加入一个频道
// ================================================================
void CommandHandler::cmdJoin(Client& client, const ParsedMsg& msg)
{
    if (msg.params.empty()) {
        error(client, "461", "JOIN :Not enough parameters");
        return;
    }

    const std::string& chanName = msg.params[0];
    std::string key = msg.params.size() > 1 ? msg.params[1] : "";

    // 频道名必须以 # 或 & 开头
    if (chanName.empty() || (chanName[0] != '#' && chanName[0] != '&')) {
        error(client, "403", chanName + " :No such channel");
        return;
    }

    Channel* ch = _server.getOrCreateChannel(chanName);

    // 已在频道中则忽略
    if (ch->hasMember(client.getFd()))
        return;

    // 检查邀请制
    if (ch->getModeInviteOnly() && !ch->isInvited(client.getFd())) {
        error(client, "473", chanName + " :Cannot join channel (+i)");
        return;
    }

    // 检查密码
    if (ch->getModeHasKey() && key != ch->getKey()) {
        error(client, "475", chanName + " :Cannot join channel (+k)");
        return;
    }

    // 检查人数限制
    if (ch->getModeHasLimit() &&
        static_cast<int>(ch->getMemberCount()) >= ch->getLimit())
    {
        error(client, "471", chanName + " :Cannot join channel (+l)");
        return;
    }

    // 第一个加入者自动成为 operator
    bool isFirstMember = ch->getMemberCount() == 0;
    ch->addMember(client.getFd(), isFirstMember);
    ch->removeInvited(client.getFd());

    // 向频道所有人（包括新成员自己）广播 JOIN 消息
    std::string joinMsg = ":" + client.getNickname() + "!" +
                          client.getUsername() + "@" + client.getHostname() +
                          " JOIN " + chanName + "\r\n";
    _server.broadcastToChannel(chanName, joinMsg);

    // 向新成员发送 TOPIC（353 成员列表 + 366 结束）
    if (!ch->getTopic().empty())
        reply(client, "332", chanName + " :" + ch->getTopic());

    // 发送成员列表
    std::string namesList = "= " + chanName + " :";
    std::vector<int> fds = ch->getMemberFds();
    for (size_t i = 0; i < fds.size(); ++i) {
        Client* member = _server.getClientByFd(fds[i]);
        if (!member) continue;
        if (ch->isOperator(fds[i]))
            namesList += "@";
        namesList += member->getNickname();
        if (i + 1 < fds.size())
            namesList += " ";
    }
    reply(client, "353", namesList);
    reply(client, "366", chanName + " :End of /NAMES list");
}

// ================================================================
//  PART <#channel> [:<reason>]
//
//  离开一个频道
// ================================================================
void CommandHandler::cmdPart(Client& client, const ParsedMsg& msg)
{
    if (msg.params.empty()) {
        error(client, "461", "PART :Not enough parameters");
        return;
    }

    const std::string& chanName = msg.params[0];
    std::string reason = msg.params.size() > 1 ? msg.params[1] : "Leaving";

    Channel* ch = _server.getChannel(chanName);
    if (!ch || !ch->hasMember(client.getFd())) {
        error(client, "442", chanName + " :You're not on that channel");
        return;
    }

    std::string partMsg = ":" + client.getNickname() + "!" +
                          client.getUsername() + "@" + client.getHostname() +
                          " PART " + chanName + " :" + reason + "\r\n";
    _server.broadcastToChannel(chanName, partMsg);

    ch->removeMember(client.getFd());
    _server.removeChannelIfEmpty(chanName);
}

// ================================================================
//  PRIVMSG <target> :<message>
//
//  发送消息给用户或频道
// ================================================================
void CommandHandler::cmdPrivmsg(Client& client, const ParsedMsg& msg)
{
    if (msg.params.size() < 2) {
        error(client, "412", ":No text to send");
        return;
    }

    const std::string& target = msg.params[0];
    const std::string& text   = msg.params[1];

    std::string fullMsg = ":" + client.getNickname() + "!" +
                          client.getUsername() + "@" + client.getHostname() +
                          " PRIVMSG " + target + " :" + text + "\r\n";

    if (target[0] == '#' || target[0] == '&') {
        // 频道消息
        Channel* ch = _server.getChannel(target);
        if (!ch) { error(client, "403", target + " :No such channel"); return; }
        if (!ch->hasMember(client.getFd())) {
            error(client, "404", target + " :Cannot send to channel"); return;
        }
        // 转发给频道内除发送者之外的所有人
        _server.broadcastToChannel(target, fullMsg, client.getFd());
    } else {
        // 私信
        Client* dest = _server.getClientByNick(target);
        if (!dest) { error(client, "401", target + " :No such nick"); return; }
        _server.sendToClient(dest->getFd(), fullMsg);
    }
}

// ================================================================
//  NOTICE <target> :<message>
//
//  与 PRIVMSG 类似，但不触发自动回复（机器人用）
// ================================================================
void CommandHandler::cmdNotice(Client& client, const ParsedMsg& msg)
{
    if (msg.params.size() < 2) return;  // NOTICE 不回错误

    const std::string& target = msg.params[0];
    const std::string& text   = msg.params[1];

    std::string fullMsg = ":" + client.getNickname() + "!" +
                          client.getUsername() + "@" + client.getHostname() +
                          " NOTICE " + target + " :" + text + "\r\n";

    if (target[0] == '#' || target[0] == '&') {
        Channel* ch = _server.getChannel(target);
        if (ch && ch->hasMember(client.getFd()))
            _server.broadcastToChannel(target, fullMsg, client.getFd());
    } else {
        Client* dest = _server.getClientByNick(target);
        if (dest)
            _server.sendToClient(dest->getFd(), fullMsg);
    }
}

// ================================================================
//  KICK <#channel> <user> [:<reason>]
//
//  将用户踢出频道（仅 operator 可用）
// ================================================================
void CommandHandler::cmdKick(Client& client, const ParsedMsg& msg)
{
    if (msg.params.size() < 2) {
        error(client, "461", "KICK :Not enough parameters");
        return;
    }

    const std::string& chanName   = msg.params[0];
    const std::string& targetNick = msg.params[1];
    std::string reason = msg.params.size() > 2 ? msg.params[2] : client.getNickname();

    Channel* ch = _server.getChannel(chanName);
    if (!ch) { error(client, "403", chanName + " :No such channel"); return; }
    if (!ch->hasMember(client.getFd())) {
        error(client, "442", chanName + " :You're not on that channel"); return;
    }
    if (!ch->isOperator(client.getFd())) {
        error(client, "482", chanName + " :You're not channel operator"); return;
    }

    Client* target = _server.getClientByNick(targetNick);
    if (!target || !ch->hasMember(target->getFd())) {
        error(client, "441", targetNick + " " + chanName + " :They aren't on that channel");
        return;
    }

    std::string kickMsg = ":" + client.getNickname() + "!" +
                          client.getUsername() + "@" + client.getHostname() +
                          " KICK " + chanName + " " + targetNick +
                          " :" + reason + "\r\n";
    _server.broadcastToChannel(chanName, kickMsg);

    ch->removeMember(target->getFd());
    _server.removeChannelIfEmpty(chanName);
}

// ================================================================
//  INVITE <user> <#channel>
//
//  邀请用户加入频道（仅 operator 可用，如果频道是邀请制）
// ================================================================
void CommandHandler::cmdInvite(Client& client, const ParsedMsg& msg)
{
    if (msg.params.size() < 2) {
        error(client, "461", "INVITE :Not enough parameters");
        return;
    }

    const std::string& targetNick = msg.params[0];
    const std::string& chanName   = msg.params[1];

    Channel* ch = _server.getChannel(chanName);
    if (!ch) { error(client, "403", chanName + " :No such channel"); return; }
    if (!ch->hasMember(client.getFd())) {
        error(client, "442", chanName + " :You're not on that channel"); return;
    }
    if (ch->getModeInviteOnly() && !ch->isOperator(client.getFd())) {
        error(client, "482", chanName + " :You're not channel operator"); return;
    }

    Client* target = _server.getClientByNick(targetNick);
    if (!target) { error(client, "401", targetNick + " :No such nick"); return; }
    if (ch->hasMember(target->getFd())) {
        error(client, "443", targetNick + " " + chanName + " :is already on channel");
        return;
    }

    ch->addInvited(target->getFd());

    reply(client, "341", targetNick + " " + chanName);

    std::string inviteMsg = ":" + client.getNickname() + "!" +
                            client.getUsername() + "@" + client.getHostname() +
                            " INVITE " + targetNick + " " + chanName + "\r\n";
    _server.sendToClient(target->getFd(), inviteMsg);
}

// ================================================================
//  TOPIC <#channel> [:<newtopic>]
//
//  查看或设置频道主题
// ================================================================
void CommandHandler::cmdTopic(Client& client, const ParsedMsg& msg)
{
    if (msg.params.empty()) {
        error(client, "461", "TOPIC :Not enough parameters");
        return;
    }

    const std::string& chanName = msg.params[0];
    Channel* ch = _server.getChannel(chanName);
    if (!ch) { error(client, "403", chanName + " :No such channel"); return; }
    if (!ch->hasMember(client.getFd())) {
        error(client, "442", chanName + " :You're not on that channel"); return;
    }

    if (msg.params.size() == 1) {
        // 只查看 topic
        if (ch->getTopic().empty())
            reply(client, "331", chanName + " :No topic is set");
        else
            reply(client, "332", chanName + " :" + ch->getTopic());
        return;
    }

    // 设置 topic
    if (ch->getModeTopicLocked() && !ch->isOperator(client.getFd())) {
        error(client, "482", chanName + " :You're not channel operator");
        return;
    }

    ch->setTopic(msg.params[1]);

    std::string topicMsg = ":" + client.getNickname() + "!" +
                           client.getUsername() + "@" + client.getHostname() +
                           " TOPIC " + chanName + " :" + msg.params[1] + "\r\n";
    _server.broadcastToChannel(chanName, topicMsg);
}

// ================================================================
//  MODE <#channel> <modestring> [params...]
//
//  修改频道模式
//
//  modestring 格式：+/-flag [param]
//  例：
//    MODE #test +i           → 设置邀请制
//    MODE #test -i           → 取消邀请制
//    MODE #test +k secret    → 设置密码
//    MODE #test -k           → 移除密码
//    MODE #test +o alice     → 给 alice operator 权限
//    MODE #test -o alice     → 撤销 alice operator 权限
//    MODE #test +l 10        → 限制最多10人
//    MODE #test -l           → 移除人数限制
//    MODE #test +t           → 只有 op 能改 topic
// ================================================================
void CommandHandler::cmdMode(Client& client, const ParsedMsg& msg)
{
    if (msg.params.empty()) {
        error(client, "461", "MODE :Not enough parameters");
        return;
    }

    const std::string& chanName = msg.params[0];
    Channel* ch = _server.getChannel(chanName);
    if (!ch) { error(client, "403", chanName + " :No such channel"); return; }
    if (!ch->hasMember(client.getFd())) {
        error(client, "442", chanName + " :You're not on that channel"); return;
    }

    // 只查询当前模式
    if (msg.params.size() == 1) {
        std::string modeStr = "+";
        if (ch->getModeInviteOnly())  modeStr += "i";
        if (ch->getModeTopicLocked()) modeStr += "t";
        if (ch->getModeHasKey())      modeStr += "k";
        if (ch->getModeHasLimit())    modeStr += "l";
        reply(client, "324", chanName + " " + modeStr);
        return;
    }

    if (!ch->isOperator(client.getFd())) {
        error(client, "482", chanName + " :You're not channel operator");
        return;
    }

    const std::string& modeStr = msg.params[1];
    std::vector<std::string> modeParams(msg.params.begin() + 2, msg.params.end());
    _applyChannelMode(client, chanName, modeStr, modeParams);
}

void CommandHandler::_applyChannelMode(Client& client,
                                       const std::string& chanName,
                                       const std::string& modeStr,
                                       const std::vector<std::string>& modeParams)
{
    Channel* ch = _server.getChannel(chanName);
    if (!ch) return;

    bool adding = true;          // '+' → true, '-' → false
    size_t paramIdx = 0;         // 当前消耗到哪个参数
    std::string appliedModes;    // 用来广播确认消息

    for (size_t i = 0; i < modeStr.size(); ++i)
    {
        char c = modeStr[i];
        if (c == '+') { adding = true;  continue; }
        if (c == '-') { adding = false; continue; }

        switch (c)
        {
        case 'i':
            ch->setModeInviteOnly(adding);
            appliedModes += adding ? "+i" : "-i";
            break;

        case 't':
            ch->setModeTopicLocked(adding);
            appliedModes += adding ? "+t" : "-t";
            break;

        case 'k':
            if (adding) {
                if (paramIdx >= modeParams.size()) {
                    error(client, "461", "MODE :Not enough parameters");
                    return;
                }
                ch->setKey(modeParams[paramIdx++]);
                appliedModes += "+k";
            } else {
                ch->clearKey();
                appliedModes += "-k";
            }
            break;

        case 'o':
        {
            if (paramIdx >= modeParams.size()) {
                error(client, "461", "MODE :Not enough parameters");
                return;
            }
            const std::string& targetNick = modeParams[paramIdx++];
            Client* target = _server.getClientByNick(targetNick);
            if (!target || !ch->hasMember(target->getFd())) {
                error(client, "441", targetNick + " " + chanName +
                      " :They aren't on that channel");
                break;
            }
            ch->setOperator(target->getFd(), adding);
            appliedModes += adding ? "+o" : "-o";
            break;
        }

        case 'l':
            if (adding) {
                if (paramIdx >= modeParams.size()) {
                    error(client, "461", "MODE :Not enough parameters");
                    return;
                }
                int lim = std::atoi(modeParams[paramIdx++].c_str());
                if (lim > 0) {
                    ch->setLimit(lim);
                    appliedModes += "+l";
                }
            } else {
                ch->clearLimit();
                appliedModes += "-l";
            }
            break;

        default:
            error(client, "472", std::string(1, c) + " :is unknown mode char to me");
            break;
        }
    }

    // 广播模式变更通知
    if (!appliedModes.empty()) {
        std::string modeMsg = ":" + client.getNickname() + "!" +
                              client.getUsername() + "@" + client.getHostname() +
                              " MODE " + chanName + " " + appliedModes + "\r\n";
        _server.broadcastToChannel(chanName, modeMsg);
    }
}
