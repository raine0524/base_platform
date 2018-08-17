#pragma once

#include "stdafx.h"

struct node_info
{
    crx::server_info info;      //节点信息
    std::unordered_set<size_t> clients, servers;    //主被动连接节点
    unsigned char token[16];
};

struct conn_info
{
    size_t cli_idx;
    size_t svr_idx;
    bool online;
};

class registry : public crx::console
{
public:
    registry() : m_fp(nullptr), m_writer(m_write_buf) {}

    virtual ~registry() override
    {
        if (m_fp)
            fclose(m_fp);
    }

public:
    bool init(int argc, char **argv) override;

    void destroy() override {}

    void add_node(std::vector<std::string>& args);

    void del_node(std::vector<std::string>& args);

    void cst_conn(std::vector<std::string>& args);

    void dst_conn(std::vector<std::string>& args);

    void display_nodes(std::vector<std::string>& args);

    void display_conns(std::vector<std::string>& args);

private:
    void tcp_server_callback(int conn, const std::string& ip, uint16_t port, char *data, size_t len);

    void register_server(int conn, const std::string& ip, uint16_t port);

    void notify_server_online(int conn);

    void notify_connect_msg(bool online);

    void server_offline(int conn);

    void setup_header(size_t len, crx::simp_header *header, uint16_t cmd);

    bool check_connect_cmd(std::vector<std::string>& args);

    void flush_json();

private:
    char m_write_buffer[1024];
    Document m_doc;
    PrettyWriter<FileWriteStream> m_pretty_writer;
    FILE *m_fp;

    std::vector<std::shared_ptr<node_info>> m_nodes;    //所有节点
    std::list<size_t> m_node_uslots;                    //节点数组中未使用的槽
    std::map<int, size_t> m_conn_node;                  //在线节点的映射表
    std::map<std::string, size_t> m_node_idx;

    std::vector<std::shared_ptr<conn_info>> m_conns;    //所有连接
    std::list<size_t> m_conn_uslots;                    //连接数组中未使用的槽
    std::map<std::string, size_t> m_conn_idx;

    Document m_read_doc, m_write_doc;
    crx::simp_buffer m_write_buf;
    Writer<crx::simp_buffer> m_writer;
    crx::tcp_server m_tcp_server;
    std::string m_local_ip;
};
