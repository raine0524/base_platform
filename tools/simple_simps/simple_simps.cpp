#include "simple_simps.h"

void simple_simps::on_connect(const crx::server_info &info, void *arg)
{
    printf("[on_connect] name=%s, role=%s (%s:%d)\n", info.name.c_str(),
           info.role.c_str(), info.ip.c_str(), info.port);
}

void simple_simps::on_disconnect(const crx::server_info &info, void *arg)
{
    printf("[on_disconnect] name=%s, role=%s (%s:%d)\n", info.name.c_str(),
           info.role.c_str(), info.ip.c_str(), info.port);
}

void simple_simps::on_request(const crx::server_info &info, const crx::server_cmd &cmd, char *data, size_t len, void *arg)
{

}

void simple_simps::on_response(const crx::server_info &info, const crx::server_cmd &cmd, char *data, size_t len, void *arg)
{

}

void simple_simps::on_notify(const crx::server_info &info, const crx::server_cmd &cmd, char *data, size_t len, void *arg)
{

}

bool simple_simps::init(int argc, char **argv)
{
    m_server = get_simpack_server(this);
    m_server->reg_connect(on_connect);
    m_server->reg_disconnect(on_disconnect);
    m_server->reg_request(on_request);
    m_server->reg_response(on_response);
    m_server->reg_notify(on_notify);
    m_server->start("ini/server.ini");
    return true;
}

void simple_simps::destroy()
{
    m_server->stop();
}

int main(int argc, char *argv[])
{
    simple_simps ss;
    return ss.run(argc, argv);
}