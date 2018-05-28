#include "log_server.h"

void log_server::on_connect(const crx::server_info &info)
{
    log_info(m_local_log, "[%s:%d]节点 %s(%s) 上线\n", info.ip.c_str(), info.port,
             info.name.c_str(), info.role.c_str());
}

void log_server::on_disconnect(const crx::server_info &info)
{
    log_info(m_local_log, "[%s:%d]节点 %s(%s) 下线\n", info.ip.c_str(), info.port,
             info.name.c_str(), info.role.c_str());
    auto it = m_remote_logs.find(info.conn);
    if (m_remote_logs.end() == it)
        return;

    for (auto& pair : it->second)
        pair.second.detach();
    m_remote_logs.erase(it);
}

void log_server::on_request(const crx::server_info &info, const crx::server_cmd &cmd, char *data, size_t len)
{
    if (1 != cmd.cmd)       //创建远程日志请求的cmd始终为1
        return;

    auto kvs = m_seria.dump(data, len);
    auto prefix_it = kvs.find("prefix");
    std::string prefix(prefix_it->second.data, prefix_it->second.len);

    auto size_it = kvs.find("max_size");
    uint32_t max_size = ntohl(*(uint32_t*)size_it->second.data);

    auto idx_it = kvs.find("log_idx");
    uint32_t log_idx = ntohl(*(uint32_t*)idx_it->second.data);

    auto& remote_logs = m_remote_logs[info.conn];
    if (remote_logs.end() != remote_logs.find(log_idx)) {
        log_error(m_local_log, "[%d:%u] remote log exist, create failed\n", info.conn, log_idx);
        return;
    }

    std::string root_dir = m_root_dir+'@'+info.name;
    log_info(m_local_log, "[%d:%u] create remote log: prefix=%s, root_dir=%s, max_size=%u\n",
             info.conn, log_idx, prefix.c_str(), root_dir.c_str(), max_size);
    remote_logs[log_idx] = get_log(prefix.c_str(), root_dir.c_str(), max_size);
}

void log_server::on_notify(const crx::server_info &info, const crx::server_cmd &cmd, char *data, size_t len)
{
    if (1 != cmd.cmd)       //写远程日志推送的cmd始终为1
        return;

    auto kvs = m_seria.dump(data, len);
    auto idx_it = kvs.find("log_idx");
    uint32_t log_idx = ntohl(*(uint32_t*)idx_it->second.data);

    auto& remote_logs = m_remote_logs[info.conn];
    auto log_it = remote_logs.find(log_idx);
    if (remote_logs.end() == log_it) {
        log_error(m_local_log, "[%d:%u] remote log not exist, write failed\n", info.conn, log_idx);
        return;
    }

    auto data_it = kvs.find("data");
    auto impl = std::dynamic_pointer_cast<crx::log_impl>(log_it->second.m_impl);
    impl->write_local_log(data_it->second.data, data_it->second.len);
}

bool log_server::init(int argc, char **argv)
{
    crx::ini ini;
    ini.load("ini/server.ini");
    ini.set_section("log");
    m_root_dir = ini.get_str("root_dir");
    ini.set_section("registry");
    std::string this_name = ini.get_str("name");

    m_server = get_simpack_server(
            std::bind(&log_server::on_connect, this, _1),
            std::bind(&log_server::on_disconnect, this, _1),
            std::bind(&log_server::on_request, this, _1, _2, _3, _4),
            std::bind(&log_server::on_response, this, _1, _2, _3, _4),
            std::bind(&log_server::on_notify, this, _1, _2, _3, _4));

    std::string root_dir = std::string("../log_files/@")+this_name;
    m_local_log = get_log("log_server", root_dir.c_str());
    return true;
}

int main(int argc, char *argv[])
{
    log_server svr;
    return svr.run(argc, argv);
}