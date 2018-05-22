#include "stdafx.h"

namespace crx
{
    void sigctl::add_sig(int signo, std::function<void(uint64_t)> callback)
    {
        auto impl = std::dynamic_pointer_cast<sigctl_impl>(m_impl);
        impl->handle_sig(signo, true);
        impl->m_sig_cb[signo] = std::move(callback);
    }

    void sigctl::remove_sig(int signo)
    {
        auto impl = std::dynamic_pointer_cast<sigctl_impl>(m_impl);
        impl->handle_sig(signo, false);
        impl->m_sig_cb.erase(signo);
    }

    void timer::start(uint64_t delay, uint64_t interval)
    {
        if (!m_impl) return;
        auto impl = std::dynamic_pointer_cast<timer_impl>(m_impl);
        impl->m_delay = delay;			//设置初始延迟及时间间隔
        impl->m_interval = interval;
        reset();
    }

    void timer::reset()
    {
        if (!m_impl) return;
        auto impl = std::dynamic_pointer_cast<timer_impl>(m_impl);
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

    void timer::detach()
    {
        auto tmr_impl = std::dynamic_pointer_cast<timer_impl>(m_impl);
        if (!tmr_impl->sch_impl.expired()) {
            auto sch_impl = tmr_impl->sch_impl.lock();
            sch_impl->remove_event(tmr_impl->fd);       //移除该定时器相关的监听事件
        }
    }

    bool timer_wheel::add_handler(uint64_t delay, std::function<void()> callback)
    {
        auto tw_impl = std::dynamic_pointer_cast<timer_wheel_impl>(m_impl);
        auto tm_impl = std::dynamic_pointer_cast<timer_impl>(tw_impl->m_timer.m_impl);
        size_t tick = (size_t)std::ceil(delay/tm_impl->m_interval*1.0);

        bool ret = false;
        size_t slot_size = tw_impl->m_slots.size();
        if (tick <= slot_size) {
            ret = true;
            tw_impl->m_slots[(tw_impl->m_slot_idx+tick)%slot_size].push_back(std::move(callback));
        }
        return ret;
    }

    void event::send_signal(int signal)
    {
        auto impl = std::dynamic_pointer_cast<event_impl>(m_impl);
        impl->m_signals.push_back(signal);      //将信号加入事件相关的信号集
        eventfd_write(impl->fd, 1);             //设置事件
    }

    void event::detach()
    {
        auto ev_impl = std::dynamic_pointer_cast<event_impl>(m_impl);
        if (!ev_impl->sch_impl.expired()) {
            auto sch_impl = ev_impl->sch_impl.lock();
            sch_impl->remove_event(ev_impl->fd);    //移除该定时器相关的监听事件
        }
    }

    uint16_t udp_ins::get_port()
    {
        auto impl = std::dynamic_pointer_cast<udp_ins_impl>(m_impl);
        return impl->m_net_sock.m_port;
    }

    void udp_ins::send_data(const char *ip_addr, uint16_t port, const char *data, size_t len)
    {
        auto impl = std::dynamic_pointer_cast<udp_ins_impl>(m_impl);
        impl->m_send_addr.sin_addr.s_addr = inet_addr(ip_addr);
        impl->m_send_addr.sin_port = htons(port);
        if (-1 == sendto(impl->fd, data, len, 0, (struct sockaddr*)&impl->m_send_addr, sizeof(struct sockaddr)))
            perror("udp_ins::send_data::sendto");
    }

    void udp_ins::detach()
    {
        auto ui_impl = std::dynamic_pointer_cast<udp_ins_impl>(m_impl);
        if (!ui_impl->sch_impl.expired()) {
            auto sch_impl = ui_impl->sch_impl.lock();
            sch_impl->remove_event(ui_impl->fd);        //移除该定时器相关的监听事件
        }
    }

    int tcp_client::connect(const char *server, uint16_t port, int retry /*= 0*/, int timeout /*= 0*/)
    {
        if (!server)
            return -1;

        auto tcp_impl = std::dynamic_pointer_cast<tcp_client_impl>(m_impl);
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(tcp_impl->m_sch->m_impl);
        std::shared_ptr<tcp_client_conn> conn;
        switch (tcp_impl->m_app_prt) {
            case PRT_NONE:
            case PRT_SIMP:  conn = std::make_shared<tcp_client_conn>();                 break;
            case PRT_HTTP:  conn = std::make_shared<http_conn_t<tcp_client_conn>>();    break;
        }

        conn->tcp_impl = tcp_impl;
        conn->domain_name = server;		//记录当前连接的主机地址
        conn->retry = retry;
        conn->timeout = timeout > 60 ? 60 : timeout;

        in_addr_t ret = inet_addr(server);		//判断服务器的地址是否为点分十进制的ip地址
        if (INADDR_NONE == ret) {       //需要对域名进行解析
            auto req = conn->name_reqs[0];
            req->ar_name = conn->domain_name.c_str();

            bzero(&conn->req_spec, sizeof(conn->req_spec));
            conn->req_spec.ai_family = AF_INET;
            conn->req_spec.ai_socktype = SOCK_STREAM;
            conn->req_spec.ai_protocol = IPPROTO_TCP;
            req->ar_request = &conn->req_spec;

            bzero(&conn->sigev, sizeof(conn->sigev));
            conn->sigev.sigev_value.sival_ptr = reinterpret_cast<void*>(sch_impl->m_running_co);
            conn->sigev.sigev_signo = SIGRTMIN+14;
            conn->sigev.sigev_notify = SIGEV_SIGNAL;
            int addr_ret = getaddrinfo_a(GAI_NOWAIT, conn->name_reqs, 1, &conn->sigev);
            if (addr_ret) {
                fprintf(stderr, "getaddrinfo_a failed: %s\n", gai_strerror(addr_ret));
                return -1;
            }
            tcp_impl->m_sch->co_yield(0);

            if (!req->ar_result) {
                printf("name %s resolve failed\n", conn->domain_name.c_str());
                return -1;
            }

            char host[64] = {0};
            auto addr = &((sockaddr_in*)req->ar_result->ai_addr)->sin_addr;
            inet_ntop(req->ar_result->ai_family, addr, host, sizeof(host)-1);
            conn->ip_addr = host;
        } else {        //已经是ip地址
            conn->ip_addr = server;
        }

        if (conn->retry && !tcp_impl->m_timer_wheel.m_impl)     //创建一个秒盘
            tcp_impl->m_timer_wheel = tcp_impl->m_sch->get_timer_wheel(1000, 60);

        conn->fd = conn->conn_sock.create(PRT_TCP, USR_CLIENT, conn->ip_addr.c_str(), port);
        conn->conn_sock.set_keep_alive(1, 60, 5, 3);
        conn->f = std::bind(&tcp_client_conn::tcp_client_callback, conn.get());
        conn->sch_impl = sch_impl;
        sch_impl->add_event(conn, EPOLLOUT);        //套接字异步connect时其可写表明与对端server已经连接成功
        return conn->fd;
    }

    void tcp_client::release(int conn)
    {
        auto tcp_impl = std::dynamic_pointer_cast<tcp_client_impl>(m_impl);
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(tcp_impl->m_sch->m_impl);
        sch_impl->remove_event(conn);
    }

    void tcp_client::send_data(int conn, const char *data, size_t len)
    {
        auto tcp_impl = std::dynamic_pointer_cast<tcp_client_impl>(m_impl);
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(tcp_impl->m_sch->m_impl);

        //判断连接是否有效
        if (conn < 0 || conn >= sch_impl->m_ev_array.size())
            return;

        auto tcp_conn = std::dynamic_pointer_cast<tcp_client_conn>(sch_impl->m_ev_array[conn]);
        if (!tcp_conn->is_connect)      //还未建立连接
            tcp_conn->cache_data.push_back(std::string(data, len));
        else
            tcp_conn->async_write(data, len);
    }

    uint16_t tcp_server::get_port()
    {
        auto impl = std::dynamic_pointer_cast<tcp_server_impl>(m_impl);
        return impl->m_net_sock.m_port;
    }

    void tcp_server::release(int conn)
    {
        auto tcp_impl = std::dynamic_pointer_cast<tcp_server_impl>(m_impl);
        auto sch_impl = tcp_impl->sch_impl.lock();
        sch_impl->remove_event(conn);
    }

    void tcp_server::send_data(int conn, const char *data, size_t len)
    {
        auto tcp_impl = std::dynamic_pointer_cast<tcp_server_impl>(m_impl);
        auto sch_impl = tcp_impl->sch_impl.lock();

        //判断连接是否有效
        if (conn < 0 || conn >= sch_impl->m_ev_array.size())
            return;

        auto ev = sch_impl->m_ev_array[conn];
        if (ev)
            ev->async_write(data, len);
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
        auto http_impl = std::dynamic_pointer_cast<http_impl_t<tcp_client_impl>>(m_impl);
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(http_impl->m_sch->m_impl);

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
        auto http_impl = std::dynamic_pointer_cast<http_impl_t<tcp_server_impl>>(m_impl);
        auto sch_impl = http_impl->sch_impl.lock();
        //判断连接是否有效
        if (conn < 0 || conn >= sch_impl->m_ev_array.size())
            return;

        auto ev = sch_impl->m_ev_array[conn];
        if (!ev)
            return;

        std::string http_response = "HTTP/1.1 200 OK\r\n";
        http_response += "Content-Type: "+g_ext_type[ed]+"; charset=utf-8\r\n";
        http_response += "Content-Length: "+std::to_string(ext_len)+"\r\n\r\n";
        http_response += std::string(ext_data, ext_len);
        ev->async_write(http_response.c_str(), http_response.size());
    }

    void simpack_server::request(int conn, const server_cmd& cmd, const char *data, size_t len)
    {
        auto simp_impl = std::dynamic_pointer_cast<simpack_server_impl>(m_impl);
        simp_impl->send_data(1, conn, cmd, data, len);
    }

    void simpack_server::response(int conn, const server_cmd& cmd, const char *data, size_t len)
    {
        auto simp_impl = std::dynamic_pointer_cast<simpack_server_impl>(m_impl);
        simp_impl->send_data(2, conn, cmd, data, len);
    }

    void simpack_server::notify(int conn, const server_cmd& cmd, const char *data, size_t len)
    {
        auto simp_impl = std::dynamic_pointer_cast<simpack_server_impl>(m_impl);
        simp_impl->send_data(3, conn, cmd, data, len);
    }

    void fs_monitor::add_watch(const char *path, uint32_t mask /*= IN_CREATE | IN_DELETE | IN_MODIFY*/, bool recursive /*= true*/)
    {
        auto impl = std::dynamic_pointer_cast<fs_monitor_impl>(m_impl);
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
        auto impl = std::dynamic_pointer_cast<fs_monitor_impl>(m_impl);
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