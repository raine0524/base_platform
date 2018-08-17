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

        server_info() : conn(-1), port(0) {}
    };

    struct server_cmd
    {
        uint16_t type;
        uint16_t cmd;

        server_cmd() { bzero(this, sizeof(server_cmd)); }
    };

    class CRX_SHARE simp_buffer : public StringBuffer
    {
    public:
        simp_buffer();

        void append_zero();

        void reset();
    };

    class CRX_SHARE simpack_server : public kobj
    {
    public:
        void request(int conn, const server_cmd& cmd, const char *data, size_t len);

        void response(int conn, const server_cmd& cmd, const char *data, size_t len);

        void notify(int conn, const server_cmd& cmd, const char *data, size_t len);
    };
}