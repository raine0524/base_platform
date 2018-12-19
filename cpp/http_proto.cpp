#include "stdafx.h"

namespace crx
{
    http_client scheduler::get_http_client(std::function<void(int, int, std::map<std::string, const char*>&, char*, size_t)> f)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto& impl = sch_impl->m_util_impls[HTTP_CLI];
        if (!impl) {
            auto http_impl = std::make_shared<http_impl_t<tcp_client_impl>>(NORM_TRANS);
            http_impl->m_util.m_sch = this;
            http_impl->m_util.m_app_prt = PRT_HTTP;
            http_impl->m_util.m_protocol_hook = [this](int fd, char* data, size_t len) {
                auto this_sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
                if (fd < 0 || fd >= this_sch_impl->m_ev_array.size() || !this_sch_impl->m_ev_array[fd])
                    return -(int)len;

                auto conn = std::dynamic_pointer_cast<http_conn_t<tcp_client_conn>>(this_sch_impl->m_ev_array[fd]);
                return http_parser(true, conn, data, len);
            };

            http_impl->m_util.m_f = [this](int fd, const std::string& ip_addr, uint16_t port, char *data, size_t len) {
                auto this_sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
                auto& this_http_cli = this_sch_impl->m_util_impls[HTTP_CLI];
                auto this_http_impl = std::dynamic_pointer_cast<http_impl_t<tcp_client_impl>>(this_http_cli);
                tcp_callback_for_http<std::shared_ptr<http_impl_t<tcp_client_impl>>, tcp_client_conn>(
                        true, this_http_impl, fd, data, len);
            };
            http_impl->m_http_cli = std::move(f);		//保存回调函数
            impl = http_impl;

            auto ctl = get_sigctl();    //基于tcp_client::connect接口的连接操作在遇到名字解析时将发生协程的切换操作
            ctl.add_sig(SIGRTMIN+14, std::bind(&http_impl_t<tcp_client_impl>::name_resolve_callback, http_impl.get(), _1, _2));
        }

        http_client obj;
        obj.m_impl = impl;
        return obj;
    }

    std::unordered_map<int, std::string> g_ext_type =
            {{DST_JSON, "application/json"},
             {DST_QSTRING, "application/x-www-form-urlencoded"}};

    void http_client::request(int conn, const char *method, const char *post_page, std::map<std::string, std::string> *extra_headers,
                              const char *ext_data, size_t ext_len, EXT_DST ed /*= DST_NONE*/)
    {
        auto http_impl = std::dynamic_pointer_cast<http_impl_t<tcp_client_impl>>(m_impl);
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(http_impl->m_util.m_sch->m_impl);

        //判断当前连接是否有效
        if (conn < 0 || conn >= sch_impl->m_ev_array.size() || !sch_impl->m_ev_array[conn])
            return;

        if (__glibc_unlikely(!method || !post_page))
            return;

        auto http_conn = std::dynamic_pointer_cast<http_conn_t<tcp_client_conn>>(sch_impl->m_ev_array[conn]);
        std::string http_request = std::string(method)+" "+std::string(post_page)+" HTTP/1.1\r\n";		//构造请求行
        http_request += "Host: "+http_conn->domain_name+"\r\n";		//构造请求头中的Host字段
        http_request += "Connection: Keep-Alive\r\n";               //复用连接

        if (DST_NONE != ed)
            http_request += "Content-Type: "+g_ext_type[ed]+"; charset=utf-8\r\n";
        if (extra_headers) {
            for (auto& header : *extra_headers)		//加入额外的请求头部
                http_request += header.first+": "+header.second+"\r\n";
        }

        if (ext_data && ext_len) {		//若有则加入请求体
            http_request += "Content-Length: "+std::to_string(ext_len)+"\r\n\r\n";
            http_request.append(ext_data, ext_len);
        } else {
            http_request += "\r\n";
        }
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

    ws_client scheduler::get_ws_client(std::function<void(int, char*, size_t)> f)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto& impl = sch_impl->m_util_impls[WS_CLI];
        if (!impl) {
            auto http_cli_back = std::move(sch_impl->m_util_impls[HTTP_CLI]);
            std::function<void(int, int, std::map<std::string, const char*>&, char*, size_t)> http_stub;
            auto http_client = get_http_client(http_stub);
            auto http_impl = std::dynamic_pointer_cast<http_impl_t<tcp_client_impl>>(http_client.m_impl);
            http_impl->m_ws_cb = std::move(f);
            http_impl->m_util.m_f = [this](int fd, const std::string& ip_addr, uint16_t port, char *data, size_t len) {
                auto this_sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
                auto& this_ws_cli = this_sch_impl->m_util_impls[WS_CLI];
                auto this_ws_impl = std::dynamic_pointer_cast<http_impl_t<tcp_client_impl>>(this_ws_cli);
                tcp_callback_for_http<std::shared_ptr<http_impl_t<tcp_client_impl>>, tcp_client_conn>(
                        true, this_ws_impl, fd, data, len);
            };
            impl = std::move(http_client.m_impl);
            sch_impl->m_util_impls[HTTP_CLI] = std::move(http_cli_back);
        }

        ws_client obj;
        obj.m_impl = impl;
        return obj;
    }

    std::map<std::string, std::string> g_ws_headers =
            {{"Connection", "Upgrade"},
             {"Upgrade", "websocket"},
             {"Sec-WebSocket-Version", "13"},
             {"Sec-WebSocket-Key", "w4v7O6xFTi36lq3RNcgctw=="}};

    int ws_client::connect_with_upgrade(const char *server, uint16_t port)
    {
        int conn = connect(server, port);
        if (conn > 0)
            GET(conn, "/", &g_ws_headers);
        return conn;
    }

    void ws_client::send_data(int conn, const char *ext_data, size_t ext_len, WS_TYPE wt /*= WS_TEXT*/)
    {
        auto http_impl = std::dynamic_pointer_cast<http_impl_t<tcp_client_impl>>(m_impl);
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(http_impl->m_util.m_sch->m_impl);

        //判断当前连接是否有效
        if (conn < 0 || conn >= sch_impl->m_ev_array.size() || !sch_impl->m_ev_array[conn])
            return;

        if (__glibc_unlikely(!ext_data || !ext_len))
            return;

        auto http_conn = std::dynamic_pointer_cast<http_conn_t<tcp_client_conn>>(sch_impl->m_ev_array[conn]);
        std::string http_request(2, 0);
        if (WS_TEXT == wt)
            http_request[0] = (char)0x81;
        else if (WS_BIN == wt)
            http_request[0] = (char)0x82;

        http_request[1] = 126;
        http_request[1] |= 0x80;
        uint16_t net_len = htons((uint16_t)ext_len);
        http_request.append((const char*)&net_len, sizeof(net_len));

        uint32_t mask_key = rand();
        http_request.append((const char*)&mask_key, sizeof(mask_key));

        http_request.append(ext_data, ext_len);
        for (int i = 0; i < ext_len; i++)
            http_request[8+i] ^=  http_request[i%4+4];
        http_conn->async_write(http_request.c_str(), http_request.size());
    }

    http_server scheduler::get_http_server(int port,
            std::function<void(int, const char*, const char*, std::map<std::string, const char*>&, char*, size_t)> f)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto& impl = sch_impl->m_util_impls[HTTP_SVR];
        if (!impl) {
            SOCK_TYPE type = (port >= 0) ? NORM_TRANS : UNIX_DOMAIN;
            auto http_impl = std::make_shared<http_impl_t<tcp_server_impl>>(type);
            //创建tcp服务端的监听套接字，允许接收任意ip地址发送的服务请求，监听请求的端口为port
            http_impl->fd = http_impl->conn_sock.create(PRT_TCP, USR_SERVER, nullptr, (uint16_t)port);

            if (__glibc_likely(http_impl->fd > 0)) {
                http_impl->m_util.m_app_prt = PRT_HTTP;
                http_impl->m_util.m_sch = this;
                http_impl->m_util.m_protocol_hook = [this](int fd, char* data, size_t len) {
                    auto this_sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
                    if (fd < 0 || fd >= this_sch_impl->m_ev_array.size() || !this_sch_impl->m_ev_array[fd])
                        return -(int)len;

                    auto conn = std::dynamic_pointer_cast<http_conn_t<tcp_server_conn>>(this_sch_impl->m_ev_array[fd]);
                    return http_parser(false, conn, data, len);
                };

                http_impl->m_util.m_f = [this](int fd, const std::string& ip_addr, uint16_t port, char *data, size_t len) {
                    auto this_sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
                    auto& this_http_svr = this_sch_impl->m_util_impls[HTTP_SVR];
                    auto this_http_impl = std::dynamic_pointer_cast<http_impl_t<tcp_server_impl>>(this_http_svr);
                    tcp_callback_for_http<std::shared_ptr<http_impl_t<tcp_server_impl>>, tcp_server_conn>(
                            false, this_http_impl, fd, data, len);
                };
                http_impl->m_http_svr = std::move(f);

                http_impl->sch_impl = sch_impl;
                http_impl->f = std::bind(&http_impl_t<tcp_server_impl>::tcp_server_callback, http_impl.get(), _1);
                sch_impl->add_event(http_impl);
                impl = http_impl;
            }
        }

        http_server obj;
        obj.m_impl = impl;
        return obj;
    }

    std::unordered_map<int, std::string> g_sts_desc =
            {{100, "Continue"},                 //继续,客户端应继续其请求
             {101, "Switching Protocols"},      //切换协议.服务器根据客户端的请求切换协议,只能切换到更高级的协议,例如切换到HTTP的新版本协议

             {200, "OK"},                       //请求成功,一般用于GET与POST请求
             {201, "Created"},                  //已创建.成功请求并创建了新的资源
             {202, "Accepted"},                 //已接受.已经接受请求,但未处理完成
             {203, "Non-Authoritative Information"},    //非授权信息.请求成功.但返回的meta信息不在原始的服务器,而是一个副本
             {204, "No Content"},               //无内容.服务器成功处理,但未返回内容.在未更新网页的情况下,可确保浏览器继续显示当前文档
             {205, "Reset Content"},            //重置内容.服务器处理成功,用户终端应重置文档视图.可通过此返回码清除浏览器的表单域
             {206, "Partial Content"},          //部分内容.服务器成功处理了部分GET请求

             {300, "Multiple Choices"},         //多种选择.请求的资源可包括多个位置,相应可返回一个资源特征与地址的列表用于用户终端
             {301, "Moved Permanently"},        //永久移动.请求的资源已被永久的移动到新URI,返回信息会包括新的URI,浏览器会自动定向到新URI
             {302, "Found"},                    //临时移动.与301类似.但资源只是临时被移动.
             {303, "See Other"},                //查看其它地址.与301类似.使用GET和POST请求查看
             {304, "Not Modified"},             //未修改.所请求的资源未修改,服务器返回此状态码时,不会返回任何资源.
             {305, "Use Proxy"},                //使用代理.所请求的资源必须通过代理访问
             {306, "Unused"},                   //已经被废弃的HTTP状态码
             {307, "Temporary Redirect"},       //临时重定向.与302类似.使用GET请求重定向

             {400, "Bad Request"},              //客户端请求的语法错误,服务器无法理解
             {401, "Unauthorized"},             //请求要求用户的身份认证
             {402, "Payment Required"},         //保留,将来使用
             {403, "Forbidden"},                //服务器理解请求客户端的请求,但是拒绝执行此请求
             {404, "Not Found"},                //服务器无法根据客户端的请求找到资源
             {405, "Method Not Allowed"},       //客户端请求中的方法被禁止
             {406, "Not Acceptable"},           //服务器无法根据客户端请求的内容特性完成请求
             {407, "Proxy Authentication Required"},    //请求要求代理的身份认证,与401类似,但请求者应当使用代理进行授权
             {408, "Request Time-out"},         //服务器等待客户端发送的请求时间过长,超时
             {409, "Conflict"},                 //服务器完成客户端的PUT请求是可能返回此代码,服务器处理请求时发生了冲突
             {410, "Gone"},                     //客户端请求的资源已经不存在
             {411, "Length Required"},          //服务器无法处理客户端发送的不带Content-Length的请求信息
             {412, "Precondition Failed"},      //客户端请求信息的先决条件错误
             {413, "Request Entity Too Large"}, //由于请求的实体过大，服务器无法处理，因此拒绝请求
             {414, "Request-URI Too Large"},    //请求的URI过长(URI通常为网址),服务器无法处理
             {415, "Unsupported Media Type"},   //服务器无法处理请求附带的媒体格式
             {416, "Requested range not satisfiable"},  //客户端请求的范围无效
             {417, "Expectation Failed"},       //服务器无法满足Expect的请求头信息

             {500, "Internal Server Error"},    //服务器内部错误,无法完成请求
             {501, "Not Implemented"},          //服务器不支持请求的功能，无法完成请求
             {502, "Bad Gateway"},              //充当网关或代理的服务器,从远端服务器接收到了一个无效的请求
             {503, "Service Unavailable"},      //由于超载或系统维护,服务器暂时的无法处理客户端的请求.
             {504, "Gateway Time-out"},         //充当网关或代理的服务器,未及时从远端服务器获取请求
             {505, "HTTP Version not supported"}};      //服务器不支持请求的HTTP协议的版本，无法完成处理

    //当前的http_server只是一个基于http协议的用于和其他应用(主要是web后台)进行交互的简单模块
    void http_server::response(int conn, const char *ext_data, size_t ext_len, EXT_DST ed /*= DST_JSON*/,
            int status /*= 200*/, std::map<std::string, std::string> *extra_headers /*= nullptr*/)
    {
        auto http_impl = std::dynamic_pointer_cast<http_impl_t<tcp_server_impl>>(m_impl);
        auto sch_impl = http_impl->sch_impl.lock();
        http_response(sch_impl, conn, ext_data, ext_len, ed, status, extra_headers);
    }

    void http_response(std::shared_ptr<scheduler_impl>& sch_impl, int fd, const char *ext_data, size_t ext_len,
            EXT_DST ed, int status, std::map<std::string, std::string> *extra_headers)
    {
        //判断连接是否有效
        if (fd < 0 || fd >= sch_impl->m_ev_array.size() || !sch_impl->m_ev_array[fd])
            return;

        auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[fd]);
        std::string http_response = "HTTP/1.1 ";
        auto sts_it = g_sts_desc.find(status);
        if (g_sts_desc.end() == sts_it)
            return;

        http_response += std::to_string(status)+" "+sts_it->second+"\r\n";
        if (DST_NONE != ed)
            http_response += "Content-Type: "+g_ext_type[ed]+"; charset=utf-8\r\n";
        if (extra_headers) {
            for (auto& header : *extra_headers)
                http_response += header.first+": "+header.second+"\r\n";
        }
        http_response += "Content-Length: "+std::to_string(ext_len)+"\r\n\r\n";
        if (ext_data && ext_len)
            http_response.append(ext_data, ext_len);
        tcp_ev->async_write(http_response.c_str(), http_response.size());
    }

    ws_server scheduler::get_ws_server(int port, std::function<void(int, char*, size_t)> f)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto& impl = sch_impl->m_util_impls[WS_SVR];
        if (!impl) {
            auto http_svr_back = std::move(sch_impl->m_util_impls[HTTP_SVR]);
            std::function<void(int, const char*, const char*, std::map<std::string, const char*>&, char*, size_t)> http_stub;
            auto http_server = get_http_server(port, http_stub);
            auto http_impl = std::dynamic_pointer_cast<http_impl_t<tcp_server_impl>>(http_server.m_impl);
            http_impl->m_ws_cb = std::move(f);
            http_impl->m_util.m_f = [this](int fd, const std::string& ip_addr, uint16_t port, char *data, size_t len) {
                auto this_sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
                auto& this_ws_svr = this_sch_impl->m_util_impls[WS_SVR];
                auto this_ws_impl = std::dynamic_pointer_cast<http_impl_t<tcp_server_impl>>(this_ws_svr);
                tcp_callback_for_http<std::shared_ptr<http_impl_t<tcp_server_impl>>, tcp_server_conn>(
                        false, this_ws_impl, fd, data, len);
            };
            impl = std::move(http_server.m_impl);
            sch_impl->m_util_impls[HTTP_SVR] = std::move(http_svr_back);
        }

        ws_server obj;
        obj.m_impl = impl;
        return obj;
    }

    void ws_server::send_data(int conn, const char *ext_data, size_t ext_len, WS_TYPE wt /*= WS_TEXT*/)
    {
        auto http_impl = std::dynamic_pointer_cast<http_impl_t<tcp_server_impl>>(m_impl);
        auto sch_impl = http_impl->sch_impl.lock();

        if (conn < 0 || conn >= sch_impl->m_ev_array.size() || !sch_impl->m_ev_array[conn] || ext_len > (1<<15)-1)
            return;

        auto http_conn = std::dynamic_pointer_cast<http_conn_t<tcp_server_conn>>(sch_impl->m_ev_array[conn]);
        auto& buffer = http_conn->notify_buffer;
        buffer.resize(2);
        if (WS_TEXT == wt)
            buffer[0] = (char)0x81;
        else if (WS_BIN == wt)
            buffer[0] = (char)0x82;

        buffer[1] = 126;
        uint16_t net_len = htons((uint16_t)ext_len);
        buffer.append((const char*)&net_len, sizeof(net_len));

        buffer.append(ext_data, ext_len);     //最后放入有效载荷
        http_conn->async_write(buffer.c_str(), buffer.length());
    }
}
