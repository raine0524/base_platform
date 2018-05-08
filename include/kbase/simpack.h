#pragma once

#include "crx_pch.h"

namespace crx
{
    struct server_info
    {
        int conn;           //服务连接，域内部的节点或外部的客户端都可能连接该服务
        std::string name;   //外部节点不存在name和role，因此为空
        std::string role;
        std::string ip;
        uint16_t port;
    };

    struct server_cmd
    {
        uint32_t ses_id;    //会话id
        uint32_t req_id;    //请求id
        uint16_t type;
        uint16_t cmd;
    };

    class scheduler;
    class CRX_SHARE simpack_server : public kobj
    {
    public:
        bool start(const char *ini_file);

        void stop();

        void request(int conn, const server_cmd& cmd, const char *data, size_t len);

        void response(int conn, const server_cmd& cmd, const char *data, size_t len);

        void notify(int conn, const server_cmd& cmd, const char *data, size_t len);

    public:
        //注册事件回调
        void reg_connect(std::function<void(const server_info&, void*)> f);

        void reg_disconnect(std::function<void(const server_info&, void*)> f);

        void reg_request(std::function<void(const server_info&, const server_cmd&, char*, size_t, void*)> f);

        void reg_response(std::function<void(const server_info&, const server_cmd&, char*, size_t, void*)> f);

        void reg_notify(std::function<void(const server_info&, const server_cmd&, char*, size_t, void*)> f);

    protected:
        simpack_server() = default;
        friend scheduler;
    };
}