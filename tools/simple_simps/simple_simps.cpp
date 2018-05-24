#include "simple_simps.h"

void simple_simps::on_connect(const crx::server_info &info)
{
    printf("[on_connect] name=%s, role=%s (%s:%d)\n", info.name.c_str(),
           info.role.c_str(), info.ip.c_str(), info.port);
}

void simple_simps::on_disconnect(const crx::server_info &info)
{
    printf("[on_disconnect] name=%s, role=%s (%s:%d)\n", info.name.c_str(),
           info.role.c_str(), info.ip.c_str(), info.port);
}

void simple_simps::on_request(const crx::server_info &info, const crx::server_cmd &cmd, char *data, size_t len)
{

}

void simple_simps::on_response(const crx::server_info &info, const crx::server_cmd &cmd, char *data, size_t len)
{

}

void simple_simps::on_notify(const crx::server_info &info, const crx::server_cmd &cmd, char *data, size_t len)
{

}

bool simple_simps::init(int argc, char **argv)
{
    m_server = get_simpack_server(
            std::bind(&simple_simps::on_connect, this, _1),
            std::bind(&simple_simps::on_disconnect, this, _1),
            std::bind(&simple_simps::on_request, this, _1, _2, _3, _4),
            std::bind(&simple_simps::on_response, this, _1, _2, _3, _4),
            std::bind(&simple_simps::on_notify, this, _1, _2, _3, _4));
    m_log = get_log("simps", "~/workspace/base_platform/tools/log_files");
    return true;
}

void simple_simps::test_remote_log(std::vector<std::string>& args)
{
    if (args.empty()) {
        printf("[test_remote_log] 测试写远程日志的次数\n");
        return;
    }

    for (int i = 0; i < std::stoi(args.front()); ++i)
        log_info(m_log, "Hello 0123456789 abcdefghijklmnopqrstuvwxyz %d\n", i);
}

int main(int argc, char *argv[])
{
    simple_simps ss;
    ss.add_cmd("trl", std::bind(&simple_simps::test_remote_log, &ss, _1), "测试写远程日志 usage@ trl {cnt}");
    return ss.run(argc, argv);
}