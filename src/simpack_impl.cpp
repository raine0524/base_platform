#include "stdafx.h"

namespace crx
{
    bool simpack_server::start(const char *ini_file)
    {
        auto impl = (simpack_server_impl*)m_obj;
        ini ini_conf;
        ini_conf.load(ini_file);
        ini_conf.set_section("registry");
        impl->m_conf.ip = ini_conf.get_str("ip");
        impl->m_conf.port = ini_conf.get_int("port");
        impl->m_conf.name = ini_conf.get_str("name");
        impl->m_conf.listen = ini_conf.get_int("listen");

        auto client_impl = (tcp_client_impl*)impl->m_client->m_obj;
        size_t co_id = client_impl->m_sch->co_create([&](scheduler *sch, void *arg) {
            int conn = impl->m_client->connect(impl->m_conf.ip.c_str(), (uint16_t)impl->m_conf.port);
//            impl->m_client->send_data(conn);
        }, nullptr, true);
        client_impl->m_sch->co_yield(co_id);
        return true;
    }

    void simpack_server::stop()
    {
        auto impl = (simpack_server_impl*)m_obj;
    }

    void simpack_server::request(int id, server_cmd& cmd, const char *data, size_t len)
    {
        auto impl = (simpack_server_impl*)m_obj;
    }

    void simpack_server::response(int id, server_cmd& cmd, const char *data, size_t len)
    {
        auto impl = (simpack_server_impl*)m_obj;
    }

    void simpack_server::notify(int id, server_cmd& cmd, const char *data, size_t len)
    {
        auto impl = (simpack_server_impl*)m_obj;
    }

    void simpack_server::reg_connect(std::function<void(server_info*, void*)> f)
    {
        auto impl = (simpack_server_impl*)m_obj;
        impl->m_on_connect = std::move(f);
    }

    void simpack_server::reg_disconnect(std::function<void(server_info*, void*)> f)
    {
        auto impl = (simpack_server_impl*)m_obj;
        impl->m_on_disconnect = std::move(f);
    }

    void simpack_server::reg_request(std::function<void(server_info*, server_cmd*, char*, size_t, void*)> f)
    {
        auto impl = (simpack_server_impl*)m_obj;
        impl->m_on_request = std::move(f);
    }

    void simpack_server::reg_response(std::function<void(server_info*, server_cmd*, char*, size_t, void*)> f)
    {
        auto impl = (simpack_server_impl*)m_obj;
        impl->m_on_response = std::move(f);
    }

    void simpack_server::reg_notify(std::function<void(server_info*, server_cmd*, char*, size_t, void*)> f)
    {
        auto impl = (simpack_server_impl*)m_obj;
        impl->m_on_notify = std::move(f);
    }
}