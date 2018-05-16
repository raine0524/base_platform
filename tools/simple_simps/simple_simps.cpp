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
    m_server = get_simpack_server(on_connect, on_disconnect, on_request, on_response, on_notify, this);
    return true;
}

int main(int argc, char *argv[])
{
    simple_simps ss;
    return ss.run(argc, argv);
}