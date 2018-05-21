#pragma once

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
        uint32_t ses_id;    //会话id
        uint32_t req_id;    //请求id
        uint16_t type;
        uint16_t cmd;
        uint16_t result;    //请求结果 0-成功 非0-失败，这一字段通常用于response接口中

        server_cmd() { bzero(this, sizeof(server_cmd)); }
    };

    class CRX_SHARE simpack_server : public kobj
    {
    public:
        void request(int conn, const server_cmd& cmd, const char *data, size_t len);

        void response(int conn, const server_cmd& cmd, const char *data, size_t len);

        void notify(int conn, const server_cmd& cmd, const char *data, size_t len);
    };
}