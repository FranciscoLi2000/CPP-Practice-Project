#ifndef CHANNEL_HPP
#define CHANNEL_HPP

#include <string>
#include <map>
#include <vector>

// 一个 IRC 频道（如 #general）用 Channel 对象表示
// 维护：成员列表 + 各种模式标志位 + topic + 邀请名单
class Channel
{
public:
    explicit Channel(const std::string& name);
    ~Channel();

    const std::string& getName()  const;
    const std::string& getTopic() const;
    void               setTopic(const std::string& topic);

    // --- 成员管理 ---
    void addMember(int fd, bool isOperator = false);
    void removeMember(int fd);
    bool hasMember(int fd) const;
    bool isOperator(int fd) const;
    void setOperator(int fd, bool op);

    // 返回所有成员 fd 列表（用于广播）
    std::vector<int> getMemberFds() const;
    size_t           getMemberCount() const;

    // --- 邀请名单（mode +i 时用）---
    void addInvited(int fd);
    bool isInvited(int fd) const;
    void removeInvited(int fd);

    // --- 频道模式 ---
    bool getModeInviteOnly()  const;   // +i
    bool getModeTopicLocked() const;   // +t（只有 op 能改 topic）
    bool getModeHasKey()      const;   // +k
    bool getModeHasLimit()    const;   // +l

    const std::string& getKey()   const;
    int                getLimit() const;

    void setModeInviteOnly(bool v);
    void setModeTopicLocked(bool v);
    void setKey(const std::string& key);
    void clearKey();
    void setLimit(int limit);
    void clearLimit();

private:
    std::string              _name;
    std::string              _topic;

    // fd → isOperator
    std::map<int, bool>      _members;

    // 被邀请但尚未加入的客户端 fd（+i 模式下使用）
    std::vector<int>         _invited;

    // 模式标志
    bool        _inviteOnly;    // +i
    bool        _topicLocked;   // +t
    std::string _key;           // +k（空字符串表示无密码）
    int         _limit;         // +l（0 表示无限制）
};

#endif
