#include "log_server.h"

void log_server::on_connect(const crx::server_info &info)
{
    printf("[on_connect %s:%d] 节点 %s(%s) 上线\n", info.ip.c_str(), info.port, info.name.c_str(), info.role.c_str());
}

void log_server::on_disconnect(const crx::server_info &info)
{
    printf("[on_connect %s:%d] 节点 %s(%s) 下线\n", info.ip.c_str(), info.port, info.name.c_str(), info.role.c_str());
}

void log_server::on_request(const crx::server_info &info, const crx::server_cmd &cmd, char *data, size_t len)
{
    if (1 != cmd.cmd)       //
        return;

    auto kvs = m_seria.dump(data, len);
}

void log_server::on_notify(const crx::server_info &info, const crx::server_cmd &cmd, char *data, size_t len)
{
    if (1 != cmd.cmd)
        return;
}

bool log_server::init(int argc, char **argv)
{
    m_server = get_simpack_server(
            std::bind(&log_server::on_connect, this, _1),
            std::bind(&log_server::on_disconnect, this, _1),
            std::bind(&log_server::on_request, this, _1, _2, _3, _4),
            std::bind(&log_server::on_response, this, _1, _2, _3, _4),
            std::bind(&log_server::on_notify, this, _1, _2, _3, _4));
    return true;
}

int main(int argc, char *argv[])
{
    log_server svr;
    return svr.run(argc, argv);
}