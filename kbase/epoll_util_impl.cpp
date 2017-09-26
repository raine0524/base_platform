#include "stdafx.h"

namespace crx
{
    timer::timer()
    {
        m_obj = new timer_impl;
    }

    void timer::start(int64_t delay, int64_t interval)
    {
        timer_impl *impl = static_cast<timer_impl*>(m_obj);
        impl->m_delay = delay;			//设置初始延迟及时间间隔
        impl->m_interval = interval;
        reset();
    }

    void timer::reset()
    {
        timer_impl *impl = static_cast<timer_impl*>(m_obj);
        int64_t delay_nanos = impl->m_delay*1000*1000;     //计算延迟及间隔对应的纳秒值
        int64_t interval_nanos = impl->m_interval*1000*1000;

        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);		//获取当前时间
        __syscall_slong_t tv_nsec = now.tv_nsec+(delay_nanos%nano_per_sec), add = 0;
        if (tv_nsec >= nano_per_sec) {		//该字段大于1秒，进行进位操作
            tv_nsec -= nano_per_sec;
            add = 1;
        }

        struct itimerspec time_setting;
        memset(&time_setting, 0, sizeof(time_setting));
        time_setting.it_value.tv_sec = now.tv_sec+delay_nanos/nano_per_sec+add;		//设置初始延迟
        time_setting.it_value.tv_nsec = tv_nsec;
        time_setting.it_interval.tv_sec = interval_nanos/nano_per_sec;			//设置时间间隔
        time_setting.it_interval.tv_nsec = interval_nanos%nano_per_sec;
        if (-1 == timerfd_settime(impl->fd, TFD_TIMER_ABSTIME, &time_setting, nullptr))
            perror("create_timer::timerfd_settime");
    }

    void timer::release()
    {
        timer_impl *impl = static_cast<timer_impl*>(m_obj);
        eth_sig sig;
        sig.ev = impl;
        sig.type = 0;
        sig.op = 0;     //移除该定时器相关的监听事件
        impl->eth_impl->m_event->send_signal((const char*)&sig, sizeof(sig));
        delete this;		//释放定时器相关资源
    }

    event::event()
    {
        m_obj = new event_impl;
    }

    void event::send_signal(const char *signal, int len)
    {
        event_impl *impl = static_cast<event_impl*>(m_obj);
        {
            std::lock_guard<std::mutex> lck(impl->m_mtx);
            impl->m_signals.push_back(std::string(signal, len));    //将信号加入事件相关的信号集
        }
        eventfd_write(impl->fd, 1);     //设置事件
    }

    void event::send_signal(std::string& signal)
    {
        event_impl *impl = static_cast<event_impl*>(m_obj);
        {
            std::lock_guard<std::mutex> lck(impl->m_mtx);
            impl->m_signals.push_back(std::move(signal));    //将信号加入事件相关的信号集
        }
        eventfd_write(impl->fd, 1);     //设置事件
    }

    void event::release()
    {
        event_impl *impl = static_cast<event_impl*>(m_obj);
        eth_sig sig;
        sig.ev = impl;
        sig.type = 0;
        sig.op = 0;     //移除该定时器相关的监听事件
        impl->eth_impl->m_event->send_signal((const char*)&sig, sizeof(sig));
        delete this;
    }

    udp_ins::udp_ins()
    {
        m_obj = new udp_ins_impl;
    }

    uint16_t udp_ins::get_port()
    {
        udp_ins_impl *impl = static_cast<udp_ins_impl*>(m_obj);
        return impl->m_net_sock.m_port;
    }

    void udp_ins::send_data(const char *ip_addr, uint16_t port, const char *data, int len)
    {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));		//根据ip地址和端口构造sockaddr_in结构体
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr(ip_addr);
        addr.sin_port = htons(port);

        udp_ins_impl *impl = static_cast<udp_ins_impl*>(m_obj);		//将数据发往指定主机上的指定进程
        if (-1 == sendto(impl->fd, data, len, 0, (struct sockaddr*)&addr, sizeof(struct sockaddr)))
            perror("udp_ins::send_data::sendto");
    }

    void udp_ins::release()
    {
        udp_ins_impl *impl = static_cast<udp_ins_impl*>(m_obj);
        eth_sig sig;
        sig.ev = impl;
        sig.type = 0;
        sig.op = 0;     //移除该定时器相关的监听事件
        impl->eth_impl->m_event->send_signal((const char*)&sig, sizeof(sig));
        delete this;
    }

    tcp_client::tcp_client(bool expose)
    {
        if (expose) {
            tcp_client_impl *impl = new tcp_client_impl;
            impl->m_expose = true;
            m_obj = impl;
        }
    }

    tcp_client::~tcp_client()
    {
        tcp_client_impl *impl = static_cast<tcp_client_impl*>(m_obj);
        if (impl->m_expose)
            delete impl;
    }

    int tcp_client::connect(const char *server, uint16_t port)
    {
        if (!server)
            return -1;

        int sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
        if (-1 == sock_fd)		//套接字无效直接返回
            return -1;

        tcp_client_impl *impl = static_cast<tcp_client_impl*>(m_obj);
        tcp_client_conn *conn = nullptr;
        if (impl->m_expose) {		//此时使用的是基本的tcp协议
            conn = new tcp_client_conn;
        } else {
            switch (impl->m_app_prt) {
                case PRT_HTTP:		conn = new http_client_conn;    break;
            }
        }
        conn->fd = sock_fd;
        conn->conn_sock.m_sock_fd = sock_fd;		//保存创建的tcp套接字
        conn->tcp_impl = impl;
        conn->domain_name = server;		//记录当前连接的主机地址
        conn->port = port;

        in_addr_t ret = inet_addr(server);		//判断服务器的地址是否为点分十进制的ip地址
        if (INADDR_NONE != ret) {		//已经是ip地址
            conn->ip_addr = server;
            impl->m_resolve_ev->send_signal((const char*)&conn, sizeof(conn));
        } else {        //需要对域名进行解析
            conn->ctx = ub_ctx_create();
            if (!conn->ctx) {
                printf("[tcp_client::connect] could not create unbound context\n");
                close(sock_fd);
                delete conn;
                return -1;
            }

            int retval = ub_resolve_async(conn->ctx, server,
                                          1 /* TYPE A (IPv4 address) */,
                                          1 /* CLASS IN (internet) */,
                                          conn, impl->name_resolve_callback, nullptr);
            if (retval) {
                printf("[tcp_client::connect] resolve error: %s\n", ub_strerror(retval));
                close(sock_fd);
                delete conn;
                return -1;
            }

            std::lock_guard<std::mutex> lck(impl->m_mtx);
            impl->m_resolve_list.push_back(conn);
        }
        return sock_fd;
    }

    void tcp_client::release(int conn)
    {
        tcp_client_impl *tc_impl = static_cast<tcp_client_impl*>(m_obj);
        eth_sig sig;
        sig.fd = conn;
        sig.type = 1;
        sig.op = 0;
        tc_impl->m_eth_impl->m_event->send_signal((const char*)&sig, sizeof(sig));
    }

    void tcp_client::send_data(int conn, const char *data, int len)
    {
        write_sig sig;
        sig.data = new std::string(data, len);
        sig.fd = conn;
        tcp_client_impl *impl = static_cast<tcp_client_impl*>(m_obj);
        impl->m_write_ev->send_signal((const char*)&sig, sizeof(sig));
    }

    void tcp_client::send_data(int conn, std::string& data)
    {
        write_sig sig;
        sig.data = new std::string(std::move(data));
        sig.fd = conn;
        tcp_client_impl *impl = static_cast<tcp_client_impl*>(m_obj);
        impl->m_write_ev->send_signal((const char*)&sig, sizeof(sig));
    }

    tcp_server::tcp_server(bool expose)
    {
        if (expose) {
            tcp_server_impl *impl = new tcp_server_impl;
            impl->m_expose = true;
            m_obj = impl;
        }
    }

    uint16_t tcp_server::get_port()
    {
        tcp_server_impl *impl = static_cast<tcp_server_impl*>(m_obj);
        return impl->m_net_sock.m_port;
    }

    void tcp_server::response(int conn, const char *data, int len)
    {
        write_sig sig;
        sig.data = new std::string(data, len);
        sig.fd = conn;
        tcp_server_impl *impl = static_cast<tcp_server_impl*>(m_obj);
        impl->m_write_ev->send_signal((const char*)&sig, sizeof(sig));
    }

    void tcp_server::response(int conn, std::string& data)
    {
        write_sig sig;
        sig.data = new std::string(std::move(data));
        sig.fd = conn;
        tcp_server_impl *impl = static_cast<tcp_server_impl*>(m_obj);
        impl->m_write_ev->send_signal((const char*)&sig, sizeof(sig));
    }

    std::map<EXT_DST, std::string> g_ext_type =
            {
                    {DST_NONE, "stub"},
                    {DST_JSON, "application/json"},
                    {DST_QSTRING, "application/x-www-form-urlencoded"},
            };

    http_client::http_client()
            :tcp_client(false)
    {
        http_client_impl *impl = new http_client_impl;
        impl->m_app_prt = PRT_HTTP;
        m_obj = impl;
    }

    http_client::~http_client()
    {
        delete (http_client_impl*)m_obj;
    }

    void http_client::request(int conn, const char *method, const char *post_page, std::map<std::string, std::string> *extra_headers,
                              const char *ext_data, int ext_len, EXT_DST ed /*= DST_NONE*/)
    {
        std::string http_request = std::string(method)+" "+std::string(post_page)+" HTTP/1.1\r\n";		//构造请求行
        http_request += "Host: \r\n";		//构造请求头中的Host字段
        http_request += "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "		//User-Agent字段
                "(KHTML, like Gecko) Chrome/46.0.2490.71 Safari/537.36\r\n";

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

        http_client_impl *impl = static_cast<http_client_impl*>(m_obj);
        write_sig sig;
        sig.data = new std::string(std::move(http_request));
        sig.fd = conn;
        impl->m_write_ev->send_signal((const char*)&sig, sizeof(sig));
    }

    void http_client::GET(int conn, const char *post_page, std::map<std::string, std::string> *extra_headers)
    {
        request(conn, "GET", post_page, extra_headers, nullptr, 0);
    }

    void http_client::POST(int conn, const char *post_page, std::map<std::string, std::string> *extra_headers,
                           const char *ext_data, int ext_len, EXT_DST ed /*= DST_JSON*/)
    {
        request(conn, "POST", post_page, extra_headers, ext_data, ext_len, ed);
    }

    http_server::http_server()
            :tcp_server(false)
    {
        http_server_impl *impl = new http_server_impl;
        impl->m_app_prt = PRT_HTTP;
        m_obj = impl;
    }

    //当前的http_server只是一个基于http协议的用于和其他应用(主要是web后台)进行交互的简单模块
    void http_server::response(int conn, const char *ext_data, int ext_len, EXT_DST ed /*= DST_JSON*/)
    {
        std::string http_response = "HTTP/1.1 200 OK\r\n";
        http_response += "Content-Type: "+g_ext_type[ed]+"; charset=utf-8\r\n";
        http_response += "Content-Length: "+std::to_string(ext_len)+"\r\n\r\n";
        http_response += std::string(ext_data, ext_len);

        http_server_impl *impl = static_cast<http_server_impl*>(m_obj);
        write_sig sig;
        sig.data = new std::string(std::move(http_response));
        sig.fd = conn;
        impl->m_write_ev->send_signal((const char*)&sig, sizeof(sig));
    }

    fs_monitor::fs_monitor()
    {
        m_obj = new fs_monitor_impl;
    }

    void fs_monitor::add_watch(const char *path, int mask /*= IN_CREATE | IN_DELETE | IN_MODIFY*/, bool recursive /*= true*/)
    {
        fs_monitor_impl *impl = static_cast<fs_monitor_impl*>(m_obj);
        std::string add_req = "1";
        int recur_flag = recursive ? 1 : 0;
        add_req.append((const char*)&recur_flag, sizeof(recur_flag));
        add_req.append((const char*)&mask, sizeof(mask));
        add_req += path;
        impl->m_monitor_ev->send_signal(add_req);
    }

    void fs_monitor::rm_watch(const char *path, bool recursive /*= true*/)
    {
        fs_monitor_impl *impl = static_cast<fs_monitor_impl*>(m_obj);
        std::string rm_req = "0";
        int recur_flag = recursive ? 1 : 0;
        rm_req.append((const char*)&recur_flag, sizeof(recur_flag));
        rm_req += path;
        impl->m_monitor_ev->send_signal(rm_req);
    }
}