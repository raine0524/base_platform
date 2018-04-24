#pragma once

#include "crx_pch.h"

namespace crx
{
    struct server_info
    {
        int id;     //域内的每个服务都将被分配一个>0的id，非域内的外部节点id都为-1
        const char *name;   //外部节点不存在name和role，因此为空指针
        const char *role;
        const char *ip;
        uint16_t port;
    };

    struct server_cmd
    {
        uint16_t type;
        uint16_t cmd;
    };

    class scheduler;
    class CRX_SHARE simpack_server : public kobj
    {
    public:
        bool start(const char *ini);

        void stop();

        void request(int id, server_cmd& cmd, const char *data, size_t len);

        void response(int id, server_cmd& cmd, const char *data, size_t len);

        void notify(int id, server_cmd& cmd, const char *data, size_t len);

    public:
        //注册事件回调
        void reg_connect(std::function<void(server_info*, void*)> f);

        void reg_disconnect(std::function<void(server_info*, void*)> f);

        void reg_request(std::function<void(server_info*, server_cmd*, char*, size_t, void*)> f);

        void reg_response(std::function<void(server_info*, server_cmd*, char*, size_t, void*)> f);

        void reg_notify(std::function<void(server_info*, server_cmd*, char*, size_t, void*)> f);

    protected:
        simpack_server() = default;
        friend scheduler;
    };
}