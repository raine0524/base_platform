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
}

void log_server::on_request(const crx::server_info &info, const crx::server_cmd &cmd, char *data, size_t len)
{
    if (1 != cmd.cmd)       //创建远程日志请求的cmd始终为1
        return;

    auto kvs = m_seria.dump(data, len);
    auto prefix_it = kvs.find("prefix");
    std::string prefix(prefix_it->second.data, prefix_it->second.len);

    auto dir_it = kvs.find("root_dir");
    std::string root_dir = std::string(dir_it->second.data, dir_it->second.len)+'@'+info.name;

    auto idx_it = kvs.find("log_idx");
    uint32_t log_idx = ntohl(*(uint32_t*)idx_it->second.data);

    auto size_it = kvs.find("max_size");
    uint32_t max_size = ntohl(*(uint32_t*)size_it->second.data);

    int64_t log_id = ((int64_t)info.conn << 32) | log_idx;
    if (m_remote_logs.end() != m_remote_logs.find(log_id)) {
        log_error(m_local_log, "log_id=%ld[%d:%u] exist, create remote log failed\n", log_id, info.conn, log_idx);
        return;
    }
    m_remote_logs[log_id] = get_log(prefix.c_str(), root_dir.c_str(), max_size);
}

void log_server::on_notify(const crx::server_info &info, const crx::server_cmd &cmd, char *data, size_t len)
{
    if (1 != cmd.cmd)       //写远程日志推送的cmd始终为1
        return;

    auto kvs = m_seria.dump(data, len);
    auto idx_it = kvs.find("log_idx");
    uint32_t log_idx = ntohl(*(uint32_t*)idx_it->second.data);

    int64_t log_id = ((int64_t)info.conn << 32) | log_idx;
    auto log_it = m_remote_logs.find(log_id);
    if (m_remote_logs.end() == log_it) {
        log_error(m_local_log, "log_id=%ld[%d:%u] not exist, write remote log failed\n", log_id, info.conn, log_idx);
        return;
    }

    auto data_it = kvs.find("data");
    auto impl = std::dynamic_pointer_cast<crx::log_impl>(log_it->second.m_impl);
    impl->write_local_log(data_it->second.data, data_it->second.len);
}

bool log_server::init(int argc, char **argv)
{
    m_server = get_simpack_server(
            std::bind(&log_server::on_connect, this, _1),
            std::bind(&log_server::on_disconnect, this, _1),
            std::bind(&log_server::on_request, this, _1, _2, _3, _4),
            std::bind(&log_server::on_response, this, _1, _2, _3, _4),
            std::bind(&log_server::on_notify, this, _1, _2, _3, _4));
    m_local_log = get_log("log_server");
    return true;
}

int main(int argc, char *argv[])
{
    log_server svr;
    return svr.run(argc, argv);
}