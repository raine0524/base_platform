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
    m_log = get_log("simps");
    return true;
}

void simple_simps::test_remote_log(std::vector<std::string>& args)
{
    if (args.empty()) {
        printf("[test_remote_log] 测试写远程日志的次数\n");
        return;
    }

    int cnt = std::stoi(args.front());
    int64_t time_consume = crx::measure_time([&]() {
        for (int i = 0; i < cnt; ++i)
            log_info(m_log, "Hello 0123456789 abcdefghijklmnopqrstuvwxyz %d\n", i);
    });
    printf("每条日志平均耗时 %lf us\n", time_consume*1.0/cnt);
}

int main(int argc, char *argv[])
{
    simple_simps ss;
    ss.add_cmd("trl", std::bind(&simple_simps::test_remote_log, &ss, _1), "测试写远程日志 usage@ trl {cnt}");
    ss.add_cmd("ca", [&](std::vector<std::string>& args) {
        int fd = std::stoi(args[0]);
        auto impl = std::dynamic_pointer_cast<crx::scheduler_impl>(ss.m_impl);
        auto tcp_ev = std::dynamic_pointer_cast<crx::tcp_event>(impl->m_ev_array[fd]);
        std::cout<<"指定描述符 "<<fd<<" 中的cache_data数量为"<<tcp_ev->cache_data.size()<<std::endl;
    }, "查看指定文件描述符中的cache_data数量");
    ss.add_cmd("bom", [](std::vector<std::string>& args) {
        malloc_stats();
        malloc_trim(0);
        malloc_stats();
    }, "强制glibc中的内存返回给操作系统");
    return ss.run(argc, argv);
}