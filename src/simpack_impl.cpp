#include "stdafx.h"

namespace crx
{
    bool simpack_server::start(const char *ini_file)
    {
        auto simp_impl = (simpack_server_impl*)m_obj;
        ini ini_conf;
        ini_conf.load(ini_file);

        //使用simpack_server组件的应用必须配置registry节区以及ip,port,name这三个字段
        ini_conf.set_section("registry");
        simp_impl->m_conf.ip = ini_conf.get_str("ip");
        simp_impl->m_conf.port = ini_conf.get_int("port");
        simp_impl->m_conf.name = ini_conf.get_str("name");
        if (ini_conf.has_key("listen"))
            simp_impl->m_conf.listen = ini_conf.get_int("listen");
        else
            simp_impl->m_conf.listen = 0;

        auto cli_impl = (tcp_client_impl*)simp_impl->m_client->m_obj;
        auto sch_impl = (scheduler_impl*)cli_impl->m_sch->m_obj;
        simp_impl->m_server = cli_impl->m_sch->get_tcp_server(simp_impl->m_conf.listen, simp_impl->tcp_server_callback, simp_impl);
        cli_impl->m_sch->register_tcp_hook(false, simp_impl->server_protohook, simp_impl);
        simp_impl->m_conf.listen = simp_impl->m_server->get_port();
        sch_impl->m_tcp_server = nullptr;

        size_t co_id = cli_impl->m_sch->co_create([&](scheduler *sch, void *arg) {
            int conn = simp_impl->m_client->connect(simp_impl->m_conf.ip.c_str(), (uint16_t)simp_impl->m_conf.port);
            simp_impl->m_seria.insert("ip", simp_impl->m_conf.ip.c_str(), simp_impl->m_conf.ip.size());
            uint16_t net_port = htons((uint16_t)simp_impl->m_conf.port);
            simp_impl->m_seria.insert("port", (const char*)&net_port, sizeof(net_port));
            simp_impl->m_seria.insert("name", simp_impl->m_conf.name.c_str(), simp_impl->m_conf.name.size());
            auto ref = simp_impl->m_seria.get_string();

            //setup header
            auto header = (simp_header*)ref.data;
            header->length = htonl((uint32_t)(ref.len-sizeof(simp_header)));
            header->cmd = CMD_REG_NAME;
            SET_BIT(header->ctl_flag, 0);

            simp_impl->m_client->send_data(conn, ref.data, ref.len);
            simp_impl->m_seria.reset();
        }, nullptr, true);
        cli_impl->m_sch->co_yield(co_id);
        return true;
    }

    void simpack_server::stop()
    {
        auto impl = (simpack_server_impl*)m_obj;
    }

    void simpack_server::request(int conn, const server_cmd& cmd, const char *data, size_t len)
    {
        auto impl = (simpack_server_impl*)m_obj;
        impl->send_package(1, conn, cmd, data, len);
    }

    void simpack_server::response(int conn, const server_cmd& cmd, const char *data, size_t len)
    {
        auto impl = (simpack_server_impl*)m_obj;
        impl->send_package(2, conn, cmd, data, len);
    }

    void simpack_server::notify(int conn, const server_cmd& cmd, const char *data, size_t len)
    {
        auto impl = (simpack_server_impl*)m_obj;
        impl->send_package(3, conn, cmd, data, len);
    }

    void simpack_server::reg_connect(std::function<void(const server_info&, void*)> f)
    {
        auto impl = (simpack_server_impl*)m_obj;
        impl->m_on_connect = std::move(f);
    }

    void simpack_server::reg_disconnect(std::function<void(const server_info&, void*)> f)
    {
        auto impl = (simpack_server_impl*)m_obj;
        impl->m_on_disconnect = std::move(f);
    }

    void simpack_server::reg_request(std::function<void(const server_info&, const server_cmd&, char*, size_t, void*)> f)
    {
        auto impl = (simpack_server_impl*)m_obj;
        impl->m_on_request = std::move(f);
    }

    void simpack_server::reg_response(std::function<void(const server_info&, const server_cmd&, char*, size_t, void*)> f)
    {
        auto impl = (simpack_server_impl*)m_obj;
        impl->m_on_response = std::move(f);
    }

    void simpack_server::reg_notify(std::function<void(const server_info&, const server_cmd&, char*, size_t, void*)> f)
    {
        auto impl = (simpack_server_impl*)m_obj;
        impl->m_on_notify = std::move(f);
    }
}