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
    registry() : m_seria(true) {}
    virtual ~registry() override = default;

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

    void register_server(int conn, const std::string& ip, uint16_t port, std::unordered_map<std::string, crx::mem_ref>& kvs);

    void notify_server_online(int conn, std::unordered_map<std::string, crx::mem_ref>& kvs);

    void notify_connect_msg(bool online, std::unordered_map<std::string, crx::mem_ref>& kvs);

    void server_offline(int conn, std::unordered_map<std::string, crx::mem_ref>& kvs);

    void setup_header(crx::mem_ref& ref, crx::simp_header *header, uint16_t cmd, uint16_t *result);

    bool check_connect_cmd(std::vector<std::string>& args);

private:
    crx::xml_parser m_xml;
    std::unordered_map<int, size_t> m_conn_node;            //在线节点的映射表
    std::vector<std::shared_ptr<node_info>> m_nodes;        //所有节点
    std::unordered_map<std::string, size_t> m_node_idx;
    std::list<size_t> m_node_uslots;        //节点数组中未使用的槽

    std::vector<std::shared_ptr<conn_info>> m_conns;        //所有连接
    std::unordered_map<std::string, size_t> m_conn_idx;
    std::list<size_t> m_conn_uslots;        //连接数组中未使用的槽

    crx::seria m_seria;
    crx::tcp_server m_tcp_server;
};
