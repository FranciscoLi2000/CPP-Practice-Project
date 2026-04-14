#include "Channel.hpp"
#include <algorithm>  // std::find

Channel::Channel(const std::string& name)
    : _name(name),
      _inviteOnly(false),
      _topicLocked(false),
      _limit(0)
{}

Channel::~Channel() {}

const std::string& Channel::getName()  const { return _name; }
const std::string& Channel::getTopic() const { return _topic; }
void               Channel::setTopic(const std::string& topic) { _topic = topic; }

// ---------- 成员管理 ----------

void Channel::addMember(int fd, bool isOp)
{
    _members[fd] = isOp;
}

void Channel::removeMember(int fd)
{
    _members.erase(fd);
}

bool Channel::hasMember(int fd) const
{
    return _members.find(fd) != _members.end();
}

bool Channel::isOperator(int fd) const
{
    std::map<int, bool>::const_iterator it = _members.find(fd);
    return it != _members.end() && it->second;
}

void Channel::setOperator(int fd, bool op)
{
    std::map<int, bool>::iterator it = _members.find(fd);
    if (it != _members.end())
        it->second = op;
}

std::vector<int> Channel::getMemberFds() const
{
    std::vector<int> fds;
    for (std::map<int, bool>::const_iterator it = _members.begin();
         it != _members.end(); ++it)
    {
        fds.push_back(it->first);
    }
    return fds;
}

size_t Channel::getMemberCount() const
{
    return _members.size();
}

// ---------- 邀请名单 ----------

void Channel::addInvited(int fd)
{
    if (!isInvited(fd))
        _invited.push_back(fd);
}

bool Channel::isInvited(int fd) const
{
    return std::find(_invited.begin(), _invited.end(), fd) != _invited.end();
}

void Channel::removeInvited(int fd)
{
    _invited.erase(std::remove(_invited.begin(), _invited.end(), fd),
                   _invited.end());
}

// ---------- 频道模式 ----------

bool Channel::getModeInviteOnly()  const { return _inviteOnly; }
bool Channel::getModeTopicLocked() const { return _topicLocked; }
bool Channel::getModeHasKey()      const { return !_key.empty(); }
bool Channel::getModeHasLimit()    const { return _limit > 0; }

const std::string& Channel::getKey()   const { return _key; }
int                Channel::getLimit() const { return _limit; }

void Channel::setModeInviteOnly(bool v)          { _inviteOnly = v; }
void Channel::setModeTopicLocked(bool v)         { _topicLocked = v; }
void Channel::setKey(const std::string& key)     { _key = key; }
void Channel::clearKey()                         { _key.clear(); }
void Channel::setLimit(int limit)                { _limit = limit; }
void Channel::clearLimit()                       { _limit = 0; }
