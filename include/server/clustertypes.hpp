#ifndef CLUSTERTYPES_H
#define CLUSTERTYPES_H

#include <cstdint>
#include <string>

struct NodeMeta
{
    std::string nodeId;
    std::string instanceId;
    std::string clientIp;
    uint16_t clientPort;
    std::string interIp;
    uint16_t interPort;
    long long heartbeatMs;

    NodeMeta()
        : clientPort(0)
        , interPort(0)
        , heartbeatMs(0)
    {
    }

    bool valid() const
    {
        return !nodeId.empty() && !interIp.empty() && interPort != 0;
    }
};

#endif /* CLUSTERTYPES_H */
