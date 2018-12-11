#include "stdafx.h"

namespace crx
{
    tcp_client scheduler::get_tcp_client(std::function<void(int, const std::string&, uint16_t, char*, size_t)> f)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto& impl = sch_impl->m_util_impls[TCP_CLI];
        if (!impl) {
            auto tcp_impl = std::make_shared<tcp_client_impl>();
            tcp_impl->m_util.m_sch = this;
            tcp_impl->m_util.m_f = std::move(f);			//记录回调函数及参数
            impl = tcp_impl;
        }

        tcp_client obj;
        obj.m_impl = impl;
        return obj;
    }

    //对端关闭或发生异常的情况下，待写数据仍然缓存，因为可能尝试重连
    int tcp_event::async_write(const char *data, size_t len)
    {
        if (!is_connect) {
            cache_data.append(data, len);
            return 1;
        }

        int sts = INT_MAX;      //正常写入且缓冲未满
        if (!cache_data.empty()) {      //先写所有的缓存数据
            ssize_t ret = write(fd, cache_data.c_str(), cache_data.size());
            if (-1 == ret) {
                if (__glibc_likely(EAGAIN == errno)) {      //等待缓冲区可写
                    sts = 1;
                } else if (EPIPE == errno) {        //对端关闭
                    sts = 0;
                } else {        //发生异常
                    log_error(g_lib_log, "write failed: %s\n", strerror(errno));
                    sts = -1;
                }
            } else if (ret < cache_data.size()) {      //写了一部分，等待缓冲区可写
                cache_data.erase(0, (size_t)ret);
                sts = 1;
            } else {        //当前数据块全部写完
                cache_data.clear();
            }
        }

        if (sts != INT_MAX) {       //缓冲不可写
            if (data && len)
                cache_data.append(data, len);

            if (__glibc_unlikely(sts <= 0))       //对端关闭或发生异常
                return sts;
        } else if (data && len) {   //缓冲可写且存在待写数据
            ssize_t ret = write(fd, data, len);
            if (-1 == ret) {
                if (__glibc_likely(EAGAIN == errno)) {          //等待缓冲可写
                    sts = 1;
                } else if (EPIPE == errno) {    //对端关闭
                    sts = 0;
                } else {        //发生异常
                    log_error(g_lib_log, "write failed: %s\n", strerror(errno));
                    sts = -1;
                }

                cache_data.append(data, len);
                if (__glibc_unlikely(sts <= 0))
                    return sts;
            } else if (ret < len) {     //缓存未写入部分
                cache_data.append(data+ret, len-ret);
                sts = 1;
            } //else 全部写完
        }

        if (INT_MAX == sts && release_conn)     //已全部写完且要释放连接
            return -1;

        auto impl = sch_impl.lock();
        if (EPOLLIN == event && 1 == sts) {     //当前监听的是可读事件且写缓冲已满，同时监听可读可写
            event = EPOLLIN | EPOLLOUT;
            impl->handle_event(EPOLL_CTL_MOD, fd, event);
        } else if (EPOLLIN | EPOLLOUT == event && INT_MAX == sts) {       //当前同时监听可读可写且已全部写完，只监听可读
            event = EPOLLIN;
            impl->handle_event(EPOLL_CTL_MOD, fd, event);
        }
        return sts;
    }

    void tcp_event::release()
    {
        if (is_connect && !cache_data.empty()) {     //处于连接状态且存在待写数据
            release_conn = true;
            return;
        }

        //处于非连接状态或不存在待写数据,直接移除事件
        auto impl = sch_impl.lock();
        impl->remove_event(fd);
    }

    void tcp_client_conn::tcp_client_callback(uint32_t events)
    {
        int sts = INT_MAX;
        if (!is_connect) {
            sts = async_read(fd, stream_buffer);        //首次回调时先通过读获取当前套接字的状态
            if (__glibc_likely(sts > 0)) {      //连接成功
                tcp_impl->m_util.m_f(fd, ip_addr, conn_sock.m_port, nullptr, (size_t)last_conn);       //通知上层连接开启
                is_connect = true;
                sts = async_write(nullptr, 0);
            }
        } else {
            if (events & EPOLLIN) {
                sts = async_read(fd, stream_buffer);        //读tcp响应流
                handle_stream(fd, this);
            } else if (events & EPOLLOUT) {
                sts = async_write(nullptr, 0);
            }
        }

        if (__glibc_likely(sts > 0))
            return;

        auto impl = sch_impl.lock();        //读写操作检测到异常或发现对端已关闭连接
        if (-1 == retry || (retry > 0 && cnt < retry)) {        //若不断尝试重连或重连次数还未满足要求，则不释放资源
            if (is_connect) {       //已处于连接状态
                tcp_client client;
                client.m_impl = tcp_impl;       //创建一个新的连接
                int conn = client.connect(ip_addr.c_str(), conn_sock.m_port, retry, timeout);
                if (__glibc_likely(conn > 0)) {
                    auto tcp_conn = std::dynamic_pointer_cast<tcp_client_conn>(impl->m_ev_array[conn]);
                    tcp_conn->cache_data = std::move(cache_data);       //将当前缓存数据及扩展数据移入新的tcp_event上
                    tcp_conn->ext_data = std::move(ext_data);
                    tcp_conn->last_conn = fd;
                }
            } else {
                tcp_impl->m_util.m_wheel.add_handler((uint64_t)timeout*1000,
                        std::bind(&tcp_client_conn::retry_connect, this));
            }
        }

        if (is_connect) {       //该套接字已经成功连接过对端，此时只能移除该套接字，无法复用
            tcp_impl->m_util.m_f(fd, ip_addr, conn_sock.m_port, nullptr, 0);       //通知上层连接关闭
            impl->remove_event(fd);
        }
    }

    void tcp_client_conn::retry_connect()
    {
        if (__glibc_unlikely(-1 == connect(conn_sock.m_sock_fd, (struct sockaddr*)&conn_sock.m_addr.trans,
                sizeof(conn_sock.m_addr.trans)) && EINPROGRESS != errno))
            log_error(g_lib_log, "retry connect failed: %s\n", strerror(errno));

        cnt = retry > 0 ? (cnt+1) : cnt;
        log_info(g_lib_log, "try to connect conn=%d, ip=%s, port=%d, timeout=%d\n",
                 fd, ip_addr.c_str(), conn_sock.m_port, timeout);
    }

    int tcp_client::connect(const char *server, uint16_t port, int retry /*= 0*/, int timeout /*= 0*/)
    {
        if (!server) return -1;
        auto tcp_impl = std::dynamic_pointer_cast<tcp_client_impl>(m_impl);
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(tcp_impl->m_util.m_sch->m_impl);
        std::shared_ptr<tcp_client_conn> conn;
        switch (tcp_impl->m_util.m_app_prt) {
            case PRT_NONE:
            case PRT_SIMP:  conn = std::make_shared<tcp_client_conn>(tcp_impl->m_util.m_type);                 break;
            case PRT_HTTP:  conn = std::make_shared<http_conn_t<tcp_client_conn>>(tcp_impl->m_util.m_type);    break;
        }

        conn->tcp_impl = tcp_impl;
        conn->domain_name = server;		//记录当前连接的主机地址
        conn->retry = retry;
        conn->timeout = timeout > 60 ? 60 : timeout;

        in_addr_t ret = inet_addr(server);  //判断服务器的地址是否为点分十进制的ip地址
        if (INADDR_NONE == ret) {           //需要对域名进行解析
            bzero(&conn->req_spec, sizeof(conn->req_spec));
            conn->req_spec.ai_family = AF_INET;
            conn->req_spec.ai_socktype = SOCK_STREAM;
            conn->req_spec.ai_protocol = IPPROTO_TCP;

            auto req = conn->name_reqs[0];
            req->ar_name = conn->domain_name.c_str();
            req->ar_request = &conn->req_spec;

            bzero(&conn->sigev, sizeof(conn->sigev));
            conn->sigev.sigev_value.sival_int = sch_impl->m_running_co;
            conn->sigev.sigev_signo = SIGRTMIN+14;
            conn->sigev.sigev_notify = SIGEV_SIGNAL;
            int addr_ret = getaddrinfo_a(GAI_NOWAIT, conn->name_reqs, 1, &conn->sigev);
            if (__glibc_unlikely(addr_ret)) {
                log_error(g_lib_log, "getaddrinfo_a(%s) failed: %s\n",
                        conn->domain_name.c_str(), gai_strerror(addr_ret));
                return -1;
            }
            tcp_impl->m_util.m_sch->co_yield(0);

            if (__glibc_unlikely(!req->ar_result)) {
                log_error(g_lib_log, "name %s resolve failed\n", conn->domain_name.c_str());
                return -2;
            }

            char host[32] = {0};
            auto addr = &((sockaddr_in*)req->ar_result->ai_addr)->sin_addr;
            inet_ntop(req->ar_result->ai_family, addr, host, sizeof(host)-1);
            conn->ip_addr = host;
        } else {        //已经是ip地址
            conn->ip_addr = server;
        }

        conn->fd = conn->conn_sock.create(PRT_TCP, USR_CLIENT, conn->ip_addr.c_str(), port);
        if (-1 == conn->fd)
            return -3;

        if (!tcp_impl->m_util.m_wheel.m_impl)             //复用该秒盘
            tcp_impl->m_util.m_wheel = sch_impl->m_wheel;
        if (NORM_TRANS == tcp_impl->m_util.m_type)
            conn->conn_sock.set_keep_alive(1, 60, 5, 3);
        conn->f = std::bind(&tcp_client_conn::tcp_client_callback, conn.get(), _1);
        conn->sch_impl = sch_impl;
        conn->event = EPOLLIN | EPOLLOUT;
        sch_impl->add_event(conn, conn->event);        //套接字异步connect时可写表明与对端server已经连接成功
        return conn->fd;
    }

    void tcp_client::release(int conn)
    {
        auto tcp_impl = std::dynamic_pointer_cast<tcp_client_impl>(m_impl);
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(tcp_impl->m_util.m_sch->m_impl);
        if (conn < 0 || conn >= sch_impl->m_ev_array.size() || !sch_impl->m_ev_array[conn])
            return;

        auto cli_conn = std::dynamic_pointer_cast<tcp_client_conn>(sch_impl->m_ev_array[conn]);
        cli_conn->retry = 0;
        cli_conn->release();
    }

    void tcp_client::send_data(int conn, const char *data, size_t len)
    {
        auto tcp_impl = std::dynamic_pointer_cast<tcp_client_impl>(m_impl);
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(tcp_impl->m_util.m_sch->m_impl);

        //判断连接是否有效
        if (conn < 0 || conn >= sch_impl->m_ev_array.size() || !sch_impl->m_ev_array[conn])
            return;

        auto tcp_conn = std::dynamic_pointer_cast<tcp_client_conn>(sch_impl->m_ev_array[conn]);
        if (!tcp_conn->is_connect)      //还未建立连接
            tcp_conn->cache_data.append(data, len);
        else
            tcp_conn->async_write(data, len);
    }

    tcp_server scheduler::get_tcp_server(int port, std::function<void(int, const std::string&, uint16_t, char*, size_t)> f)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto& impl = sch_impl->m_util_impls[TCP_SVR];
        if (!impl) {
            SOCK_TYPE type = (port >= 0) ? NORM_TRANS : UNIX_DOMAIN;
            auto tcp_impl = std::make_shared<tcp_server_impl>(type);
            //创建tcp服务端的监听套接字，允许接收任意ip地址发送的服务请求，监听请求的端口为port
            tcp_impl->fd = tcp_impl->conn_sock.create(PRT_TCP, USR_SERVER, nullptr, (uint16_t)port);

            if (__glibc_likely(tcp_impl->fd > 0)) {
                tcp_impl->m_util.m_sch = this;
                tcp_impl->m_util.m_f = std::move(f);
                tcp_impl->sch_impl = sch_impl;

                tcp_impl->f = std::bind(&tcp_server_impl::tcp_server_callback, tcp_impl.get(), _1);
                sch_impl->add_event(tcp_impl);
                impl = tcp_impl;
            }
        }

        tcp_server obj;
        obj.m_impl = impl;
        return obj;
    }

    void tcp_server_impl::tcp_server_callback(uint32_t events)
    {
        bzero(&m_accept_addr, sizeof(struct sockaddr_in));
        m_addr_len = sizeof(struct sockaddr_in);
        while (true) {		//接受所有请求连接的tcp客户端
            int client_fd = -1;
            if (UNIX_DOMAIN == m_util.m_type)
                client_fd = accept(fd, nullptr, nullptr);
            else        //NORM_TRANS == m_util.m_type
                client_fd = accept(fd, (struct sockaddr*)&m_accept_addr, &m_addr_len);
            if (-1 == client_fd) {
                if (EAGAIN != errno)
                    log_error(g_lib_log, "accept socket failed: %s\n", strerror(errno));
                break;
            }

            setnonblocking(client_fd);			//将客户端连接文件描述符设为非阻塞并加入监听事件
            std::shared_ptr<tcp_server_conn> conn;
            switch (m_util.m_app_prt) {
                case PRT_NONE:
                case PRT_SIMP:      conn = std::make_shared<tcp_server_conn>(m_util.m_type);                 break;
                case PRT_HTTP:		conn = std::make_shared<http_conn_t<tcp_server_conn>>(m_util.m_type);    break;
            }

            conn->fd = client_fd;
            conn->f = std::bind(&tcp_server_conn::read_tcp_stream, conn.get(), _1);
            conn->sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_util.m_sch->m_impl);

            conn->tcp_impl = this;
            conn->is_connect = true;
            conn->conn_sock.m_sock_fd = client_fd;

            if (NORM_TRANS == m_util.m_type) {
                conn->ip_addr = inet_ntoa(m_accept_addr.sin_addr);        //将地址转换为点分十进制格式的ip地址
                conn->conn_sock.m_port = ntohs(m_accept_addr.sin_port);   //将端口由网络字节序转换为主机字节序

                /*
                 * 1分钟之后若信道上没有数据传输则发送tcp心跳包，总共发3次，每次间隔为5秒，因此若有套接字处于异常状态(TIME_WAIT/CLOSE_WAIT)
                 * 则将在75秒之后关闭该套接字，以释放系统资源提供复用能力，但是tcp本身的keepalive仍然会存在问题，即当socket处于异常状态时
                 * 应用层同样有数据需要重传时，tcp的keepalive将会无效
                 * 具体参见链接描述: https://blog.csdn.net/ctthuangcheng/article/details/8596818
                 */
                conn->conn_sock.set_keep_alive(1, 60, 5, 3);
            }

            auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_util.m_sch->m_impl);
            if (UNIX_DOMAIN == m_util.m_type) {
                auto con_impl = std::dynamic_pointer_cast<console_impl>(sch_impl->m_util_impls[EXT_DATA]);
                if (-1 != con_impl->m_conn)     //console控制台同一时刻只允许一个连接请求
                    sch_impl->remove_event(con_impl->m_conn);
                con_impl->m_conn = client_fd;
            }
            sch_impl->add_event(conn);        //将该连接加入监听事件
        }
    }

    void tcp_server_conn::read_tcp_stream(uint32_t events)
    {
        int sts = INT_MAX;
        if (events & EPOLLIN) {
            sts = async_read(fd, stream_buffer);
            handle_stream(fd, this);
        } else if (events & EPOLLOUT) {        //EPOLLOUT == event
            sts = async_write(nullptr, 0);
        }

        if (sts > 0)
            return;

        //读写操作检测到异常或发现对端已关闭连接
        tcp_impl->m_util.m_f(fd, ip_addr, conn_sock.m_port, nullptr, 0);
        sch_impl.lock()->remove_event(fd);
    }

    uint16_t tcp_server::get_port()
    {
        auto impl = std::dynamic_pointer_cast<tcp_server_impl>(m_impl);
        return impl->conn_sock.m_port;
    }

    void tcp_server::release(int conn)
    {
        auto tcp_impl = std::dynamic_pointer_cast<tcp_server_impl>(m_impl);
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(tcp_impl->m_util.m_sch->m_impl);
        if (conn < 0 || conn >= sch_impl->m_ev_array.size() || !sch_impl->m_ev_array[conn])
            return;

        auto svr_conn = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
        svr_conn->release();
    }

    void tcp_server::send_data(int conn, const char *data, size_t len)
    {
        auto tcp_impl = std::dynamic_pointer_cast<tcp_server_impl>(m_impl);
        auto sch_impl = tcp_impl->sch_impl.lock();

        //判断连接是否有效
        if (conn < 0 || conn >= sch_impl->m_ev_array.size() || !sch_impl->m_ev_array[conn])
            return;

        auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
        tcp_ev->async_write(data, len);
    }

    void scheduler::register_tcp_hook(bool client, std::function<int(int, char*, size_t)> f)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        if (client) {
            auto tcp_impl = std::dynamic_pointer_cast<tcp_client_impl>(sch_impl->m_util_impls[TCP_CLI]);
            tcp_impl->m_util.m_protocol_hook = std::move(f);
        } else {        //server
            auto tcp_impl = std::dynamic_pointer_cast<tcp_server_impl>(sch_impl->m_util_impls[TCP_SVR]);
            tcp_impl->m_util.m_protocol_hook = std::move(f);
        }
    }
}