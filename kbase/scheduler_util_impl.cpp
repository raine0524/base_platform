#include "stdafx.h"

namespace crx
{
    void timer::start(int64_t delay, int64_t interval)
    {
        timer_impl *impl = static_cast<timer_impl*>(m_obj);
        impl->m_delay = delay;			//设置初始延迟及时间间隔
        impl->m_interval = interval;
        reset();
    }

    void timer::reset()
    {
        auto impl = (timer_impl*)m_obj;
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
        time_setting.it_value.tv_sec = now.tv_sec+delay_nanos/nano_per_sec+add;     //设置初始延迟
        time_setting.it_value.tv_nsec = tv_nsec;
        time_setting.it_interval.tv_sec = interval_nanos/nano_per_sec;              //设置时间间隔
        time_setting.it_interval.tv_nsec = interval_nanos%nano_per_sec;
        if (-1 == timerfd_settime(impl->fd, TFD_TIMER_ABSTIME, &time_setting, nullptr))
            perror("create_timer::timerfd_settime");
    }

    void timer::release()
    {
        auto tmr_impl = (timer_impl*)m_obj;
        tmr_impl->sch_impl->remove_event(tmr_impl);       //移除该定时器相关的监听事件
        delete tmr_impl;        //释放定时器相关资源
        delete this;
    }

    void event::send_signal(const char *signal, size_t len)
    {
        event_impl *impl = static_cast<event_impl*>(m_obj);
        impl->m_signals.push_back(std::string(signal, len));    //将信号加入事件相关的信号集
        eventfd_write(impl->fd, 1);     //设置事件
    }

    void event::send_signal(std::string& signal)
    {
        event_impl *impl = static_cast<event_impl*>(m_obj);
        impl->m_signals.push_back(std::move(signal));    //将信号加入事件相关的信号集
        eventfd_write(impl->fd, 1);     //设置事件
    }

    void event::release()
    {
        auto ev_impl = (event_impl*)m_obj;
        ev_impl->sch_impl->remove_event(ev_impl);        //移除该定时器相关的监听事件
        delete ev_impl;
        delete this;
    }

    uint16_t udp_ins::get_port()
    {
        auto impl = (udp_ins_impl*)m_obj;
        return impl->m_net_sock.m_port;
    }

    void udp_ins::send_data(const char *ip_addr, uint16_t port, const char *data, size_t len)
    {
        auto impl = (udp_ins_impl*)m_obj;		//将数据发往指定主机上的指定进程
        impl->m_send_addr.sin_addr.s_addr = inet_addr(ip_addr);
        impl->m_send_addr.sin_port = htons(port);
        if (-1 == sendto(impl->fd, data, len, 0, (struct sockaddr*)&impl->m_send_addr, sizeof(struct sockaddr)))
            perror("udp_ins::send_data::sendto");
    }

    void udp_ins::release()
    {
        auto ui_impl = (udp_ins_impl*)m_obj;
        ui_impl->sch_impl->remove_event(ui_impl);        //移除该定时器相关的监听事件
        delete ui_impl;
        delete this;
    }

    int tcp_client::connect(const char *server, uint16_t port)
    {
        if (!server)
            return -1;

        int sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
        if (-1 == sock_fd)		//套接字无效直接返回
            return -1;

        auto tcp_impl = (tcp_client_impl*)m_obj;
        auto sch_impl = (scheduler_impl*)tcp_impl->m_sch->m_obj;
        tcp_client_conn *conn = nullptr;
        switch (tcp_impl->m_app_prt) {
            case PRT_NONE:      conn = new tcp_client_conn;     break;      //使用原始的tcp协议
            case PRT_HTTP:		conn = new http_client_conn;    break;      //使用http协议
        }
        conn->fd = sock_fd;
        conn->co_id = sch_impl->m_running_co;
        conn->sch_impl = sch_impl;
        conn->f = tcp_impl->tcp_client_callback;
        conn->args = conn;

        conn->domain_name = server;		//记录当前连接的主机地址
        conn->port = port;
        conn->conn_sock.m_sock_fd = sock_fd;		//保存创建的tcp套接字

        in_addr_t ret = inet_addr(server);		//判断服务器的地址是否为点分十进制的ip地址
        if (INADDR_NONE != ret) {		//已经是ip地址
            conn->ip_addr = server;
            conn->conn_sock.create(PRT_TCP, USR_CLIENT, server, port);
            sch_impl->add_event(conn, EPOLLOUT);        //套接字异步connect时其可写表明与对端server已经连接成功
        } else {        //需要对域名进行解析

        }
        return sock_fd;
    }

    void tcp_client::release(int conn)
    {
        auto tcp_impl = (tcp_client_impl*)m_obj;
        auto sch_impl = (scheduler_impl*)tcp_impl->m_sch->m_obj;
        if (conn < sch_impl->m_ev_array.size())
            sch_impl->remove_event(sch_impl->m_ev_array[conn]);
    }

    void tcp_client::send_data(int conn, const char *data, size_t len)
    {
        auto tcp_impl = (tcp_client_impl*)m_obj;
        auto sch_impl = (scheduler_impl*)tcp_impl->m_sch->m_obj;
        //判断连接是否有效
        if (conn >= sch_impl->m_ev_array.size())
            return;

        auto ev = sch_impl->m_ev_array[conn];
        if (!ev)
            return;

        auto tcp_conn = dynamic_cast<tcp_client_conn*>(ev);
        if (!tcp_conn->is_connect)      //还未连接，切换至主协程处理其他事件
            tcp_impl->m_sch->co_yield(0);
        sch_impl->async_write(ev, data, len);
    }

    uint16_t tcp_server::get_port()
    {
        tcp_server_impl *impl = static_cast<tcp_server_impl*>(m_obj);
        return impl->m_net_sock.m_port;
    }

    void tcp_server::response(int conn, const char *data, size_t len)
    {
        auto tcp_impl = (tcp_server_impl*)m_obj;
        //判断连接是否有效
        if (conn >= tcp_impl->sch_impl->m_ev_array.size())
            return;

        auto ev = tcp_impl->sch_impl->m_ev_array[conn];
        if (ev)
            tcp_impl->sch_impl->async_write(ev, data, len);
    }

    std::unordered_map<int, std::string> g_ext_type =
            {
                    {DST_NONE, "stub"},
                    {DST_JSON, "application/json"},
                    {DST_QSTRING, "application/x-www-form-urlencoded"},
            };

    void http_client::request(int conn, const char *method, const char *post_page, std::unordered_map<std::string, std::string> *extra_headers,
                              const char *ext_data, size_t ext_len, EXT_DST ed /*= DST_NONE*/)
    {
        auto http_impl = (http_client_impl*)m_obj;
        auto sch_impl = (scheduler_impl*)http_impl->m_sch->m_obj;
        //判断连接是否有效
        if (conn >= sch_impl->m_ev_array.size())
            return;

        auto ev = sch_impl->m_ev_array[conn];
        if (!ev)
            return;

        auto http_conn = dynamic_cast<http_client_conn*>(ev);
        if (!http_conn->is_connect)     //还未连接，切换至主协程处理其他事件
            http_impl->m_sch->co_yield(0);

        std::string http_request = std::string(method)+" "+std::string(post_page)+" HTTP/1.1\r\n";		//构造请求行
        http_request += "Host: "+http_conn->domain_name+"\r\n";		//构造请求头中的Host字段
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
        sch_impl->async_write(ev, http_request.c_str(), http_request.size());
    }

    void http_client::GET(int conn, const char *post_page, std::unordered_map<std::string, std::string> *extra_headers)
    {
        request(conn, "GET", post_page, extra_headers, nullptr, 0);
    }

    void http_client::POST(int conn, const char *post_page, std::unordered_map<std::string, std::string> *extra_headers,
                           const char *ext_data, size_t ext_len, EXT_DST ed /*= DST_JSON*/)
    {
        request(conn, "POST", post_page, extra_headers, ext_data, ext_len, ed);
    }

    //当前的http_server只是一个基于http协议的用于和其他应用(主要是web后台)进行交互的简单模块
    void http_server::response(int conn, const char *ext_data, size_t ext_len, EXT_DST ed /*= DST_JSON*/)
    {
        auto http_impl = (http_server_impl*)m_obj;
        //判断连接是否有效
        if (conn >= http_impl->sch_impl->m_ev_array.size())
            return;

        auto ev = http_impl->sch_impl->m_ev_array[conn];
        if (!ev)
            return;

        std::string http_response = "HTTP/1.1 200 OK\r\n";
        http_response += "Content-Type: "+g_ext_type[ed]+"; charset=utf-8\r\n";
        http_response += "Content-Length: "+std::to_string(ext_len)+"\r\n\r\n";
        http_response += std::string(ext_data, ext_len);
        http_impl->sch_impl->async_write(ev, http_response.c_str(), http_response.size());
    }

    void fs_monitor::add_watch(const char *path, uint32_t mask /*= IN_CREATE | IN_DELETE | IN_MODIFY*/, bool recursive /*= true*/)
    {
        auto impl = (fs_monitor_impl*)m_obj;
        bzero(&impl->m_st, sizeof(impl->m_st));
        stat(path, &impl->m_st);

        std::string monitor_path = path;
        if (S_ISDIR(impl->m_st.st_mode) && '/' != monitor_path.back())      //添加监控的是目录
            monitor_path.push_back('/');

        if (impl->m_path_mev.end() == impl->m_path_mev.find(path)) {
            int watch_id = inotify_add_watch(impl->fd, monitor_path.c_str(), mask);
            if (__glibc_unlikely(-1 == watch_id)) {
                perror("fs_monitor::inotify_add_watch");
                return;
            }

            impl->trigger_event(true, watch_id, monitor_path, recursive, mask);
            if (S_ISDIR(impl->m_st.st_mode) && recursive)       //对子目录递归监控
                impl->recursive_monitor(path, true, mask);
        }
    }

    void fs_monitor::rm_watch(const char *path, bool recursive /*= true*/)
    {
        auto impl = (fs_monitor_impl*)m_obj;
        bzero(&impl->m_st, sizeof(impl->m_st));
        stat(path, &impl->m_st);

        std::string monitor_path = path;
        if (S_ISDIR(impl->m_st.st_mode) && '/' != monitor_path.back())  //移除监控的是目录
            monitor_path.push_back('/');

        if (impl->m_path_mev.end() != impl->m_path_mev.find(path)) {
            impl->trigger_event(false, impl->m_path_mev[path]->watch_id, path, 0, 0);
            if (S_ISDIR(impl->m_st.st_mode) && recursive)       //对子目录递归移除
                impl->recursive_monitor(path, false, 0);
        }
    }
}