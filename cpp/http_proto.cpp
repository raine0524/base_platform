#include "stdafx.h"

namespace crx
{
    http_client scheduler::get_http_client(std::function<void(int, int, std::map<std::string, const char*>&, char*, size_t)> f)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto& impl = sch_impl->m_util_impls[HTTP_CLI];
        if (!impl) {
            auto http_impl = std::make_shared<http_impl_t<tcp_client_impl>>(NORM_TRANS);
            impl = http_impl;
            http_impl->m_util.m_sch = this;
            http_impl->m_util.m_app_prt = PRT_HTTP;
            http_impl->m_util.m_protocol_hook = [this](int fd, char* data, size_t len) {
                auto this_sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
                auto conn = std::dynamic_pointer_cast<http_conn_t<tcp_client_conn>>(this_sch_impl->m_ev_array[fd]);
                return http_parser(true, conn, data, len);
            };
            http_impl->m_util.m_f = [this](int fd, const std::string& ip_addr, uint16_t port, char *data, size_t len) {
                auto this_sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
                auto this_http_impl = std::dynamic_pointer_cast<http_impl_t<tcp_client_impl>>(this_sch_impl->m_util_impls[HTTP_CLI]);
                tcp_callback_for_http<std::shared_ptr<http_impl_t<tcp_client_impl>>,
                        http_conn_t<tcp_client_conn>>(true, this_http_impl, fd, data, len);
            };
            http_impl->funcs.m_http_cli = std::move(f);		//保存回调函数

            auto ctl = get_sigctl();
            ctl.add_sig(SIGRTMIN+14, std::bind(&http_impl_t<tcp_client_impl>::name_resolve_callback,
                                               http_impl.get(), _1));
        }

        http_client obj;
        obj.m_impl = impl;
        return obj;
    }

    std::unordered_map<int, std::string> g_ext_type =
            {{DST_NONE, "stub"},
             {DST_JSON, "application/json"},
             {DST_QSTRING, "application/x-www-form-urlencoded"}};

    void http_client::request(int conn, const char *method, const char *post_page, std::map<std::string, std::string> *extra_headers,
                              const char *ext_data, size_t ext_len, EXT_DST ed /*= DST_NONE*/)
    {
        auto http_impl = std::dynamic_pointer_cast<http_impl_t<tcp_client_impl>>(m_impl);
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(http_impl->m_util.m_sch->m_impl);

        //判断当前连接是否已经加入监听
        if (conn < 0 || conn >= sch_impl->m_ev_array.size())
            return;

        auto http_conn = std::dynamic_pointer_cast<http_conn_t<tcp_client_conn>>(sch_impl->m_ev_array[conn]);
        std::string http_request = std::string(method)+" "+std::string(post_page)+" HTTP/1.1\r\n";		//构造请求行
        http_request += "Host: "+http_conn->domain_name+"\r\n";		//构造请求头中的Host字段

        if (DST_NONE != ed)
            http_request += "Content-Type: "+g_ext_type[ed]+"; charset=utf-8\r\n";
        if (extra_headers) {
            for (auto& header : *extra_headers)		//加入额外的请求头部
                http_request += header.first+": "+header.second+"\r\n";
        }

        if (ext_data) {		//若有则加入请求体
            http_request += "Content-Length: "+std::to_string(ext_len)+"\r\n\r\n";
            http_request.append(ext_data, ext_len);
        } else {
            http_request += "\r\n";
        }

        if (!http_conn->is_connect)     //还未建立连接
            http_conn->cache_data.push_back(std::move(http_request));
        else
            http_conn->async_write(http_request.c_str(), http_request.size());
    }

    void http_client::GET(int conn, const char *post_page, std::map<std::string, std::string> *extra_headers)
    {
        request(conn, "GET", post_page, extra_headers, nullptr, 0);
    }

    void http_client::POST(int conn, const char *post_page, std::map<std::string, std::string> *extra_headers,
                           const char *ext_data, size_t ext_len, EXT_DST ed /*= DST_JSON*/)
    {
        request(conn, "POST", post_page, extra_headers, ext_data, ext_len, ed);
    }

    http_server scheduler::get_http_server(int port,
            std::function<void(int, const char*, const char*, std::map<std::string, const char*>&, char*, size_t)> f)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto& impl = sch_impl->m_util_impls[HTTP_SVR];
        if (!impl) {
            SOCK_TYPE type = (port >= 0) ? NORM_TRANS : UNIX_DOMAIN;
            auto http_impl = std::make_shared<http_impl_t<tcp_server_impl>>(type);
            impl = http_impl;
            http_impl->m_util.m_app_prt = PRT_HTTP;
            http_impl->m_util.m_sch = this;
            http_impl->m_util.m_protocol_hook = [this](int fd, char* data, size_t len) {
                auto this_sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
                auto conn = std::dynamic_pointer_cast<http_conn_t<tcp_client_conn>>(this_sch_impl->m_ev_array[fd]);
                return http_parser(false, conn, data, len);
            };
            http_impl->m_util.m_f = [this](int fd, const std::string& ip_addr, uint16_t port, char *data, size_t len) {
                auto this_sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
                auto this_http_impl = std::dynamic_pointer_cast<http_impl_t<tcp_server_impl>>(this_sch_impl->m_util_impls[HTTP_SVR]);
                tcp_callback_for_http<std::shared_ptr<http_impl_t<tcp_server_impl>>,
                        http_conn_t<tcp_server_conn>>(true, this_http_impl, fd, data, len);
            };
            http_impl->funcs.m_http_svr = std::move(f);

            //创建tcp服务端的监听套接字，允许接收任意ip地址发送的服务请求，监听请求的端口为port
            http_impl->fd = http_impl->conn_sock.create(PRT_TCP, USR_SERVER, nullptr, (uint16_t)port);
            http_impl->sch_impl = sch_impl;
            http_impl->f = std::bind(&http_impl_t<tcp_server_impl>::tcp_server_callback, http_impl.get(), _1);
            sch_impl->add_event(http_impl);
        }

        http_server obj;
        obj.m_impl = impl;
        return obj;
    }

    //当前的http_server只是一个基于http协议的用于和其他应用(主要是web后台)进行交互的简单模块
    void http_server::response(int conn, const char *ext_data, size_t ext_len, EXT_DST ed /*= DST_JSON*/)
    {
        auto http_impl = std::dynamic_pointer_cast<http_impl_t<tcp_server_impl>>(m_impl);
        auto sch_impl = http_impl->sch_impl.lock();
        //判断连接是否有效
        if (conn < 0 || conn >= sch_impl->m_ev_array.size())
            return;

        auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
        if (!tcp_ev)
            return;

        std::string http_response = "HTTP/1.1 200 OK\r\n";
        http_response += "Content-Type: "+g_ext_type[ed]+"; charset=utf-8\r\n";
        http_response += "Content-Length: "+std::to_string(ext_len)+"\r\n\r\n";
        http_response += std::string(ext_data, ext_len);
        tcp_ev->async_write(http_response.c_str(), http_response.size());
    }
}