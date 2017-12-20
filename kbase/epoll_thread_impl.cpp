#include "stdafx.h"

namespace crx
{
    static const int epoll_size = 512;

    epoll_thread::epoll_thread()
    {
        epoll_thread_impl *impl = new epoll_thread_impl;
        m_obj = impl;

        //create epoll
        impl->m_epoll_fd = epoll_create(epoll_size);
        if (__glibc_unlikely(-1 == impl->m_epoll_fd))
            perror("epoll_thread::epoll_create");

        //create event
        impl->m_event = get_event(impl->handle_request, impl);
    }

    epoll_thread::~epoll_thread()
    {
        epoll_thread_impl *impl = (epoll_thread_impl*)m_obj;
        if (impl->m_http_client)
            delete impl->m_http_client;
        if (impl->m_http_server)
            delete impl->m_http_server;

        if (impl->m_tcp_client)
            delete impl->m_tcp_client;
        if (impl->m_tcp_server)
            delete impl->m_tcp_server;

        if (impl->m_fs_monitor)
            delete impl->m_fs_monitor;

        if (-1 != impl->m_epoll_fd)
            close(impl->m_epoll_fd);      //close epoll fd
        delete impl;
    }

    void epoll_thread::start()
    {
        epoll_thread_impl *impl = (epoll_thread_impl*)m_obj;
        if (!impl->m_cover)
            impl->m_thread = std::thread(epoll_thread_impl::thread_proc, impl);
        else		//覆盖当前线程方式运行，直接调用thread_proc函数
            impl->thread_proc(impl);
    }

    void epoll_thread::stop()
    {
        epoll_thread_impl *impl = (epoll_thread_impl*)m_obj;
        impl->join_thread();
    }

    void epoll_thread_impl::join_thread()
    {
        eth_sig sig;
        sig.type = -1;
        m_event->send_signal((const char*)&sig, sizeof(sig));

        if (!m_cover)       //回收线程释放的资源
            m_thread.join();
    }

    void epoll_thread_impl::handle_event(int op, eth_event *eth_ev)
    {
        struct epoll_event ev;
        ev.events = eth_ev->event | EPOLLET;    //每个读/写事件都采用edge-trigger(边沿触发)的模式
        ev.data.fd = eth_ev->fd;

        if (-1 == epoll_ctl(m_epoll_fd, op, eth_ev->fd, &ev))
            perror("handle_epoll_event::epoll_ctl");
    }

    void epoll_thread_impl::add_event(eth_event *ev)
    {
        if (ev) {
            if (ev->fd+1 > m_ev_array.size())
                m_ev_array.resize(ev->fd+1, 0);
            m_ev_array[ev->fd] = ev;
            handle_event(EPOLL_CTL_ADD, ev);
        }
    }

    void epoll_thread_impl::remove_event(eth_event *ev)
    {
        if (ev) {
            handle_event(EPOLL_CTL_DEL, ev);
            m_ev_array[ev->fd] = 0;
            delete ev;
        }
    }

    void epoll_thread_impl::handle_request(std::string& signal, void *args)
    {
        eth_sig *sig = (eth_sig*)signal.data();
        epoll_thread_impl *impl = static_cast<epoll_thread_impl*>(args);
        switch (sig->type) {
            case -1: {      //退出主循环
                impl->m_go_done = false;
                break;
            }

            case 0: {       //eth_event对象
                if (1 == sig->op)       //新增
                    impl->add_event(sig->ev);
                else if (0 == sig->op)        //删除
                    impl->remove_event(sig->ev);
                break;
            }

            case 1: {       //文件描述符
                if (0 == sig->op)       //删除
                    impl->remove_event(impl->m_ev_array[sig->fd]);
                break;
            }
        }
    }

    //异步读的一个原则是只要可读就一直读，直到errno被置为EAGAIN提示等待更多数据可读
    int epoll_thread_impl::async_read(int fd, std::string& read_str)
    {
        int flag = 0;
        char buffer[256] = {0};
        while (true) {
            int ret = read(fd, buffer, 256);
            if (ret > 0) {
                read_str.append(buffer, ret);
                continue;
            }

            if (-1 == ret) {
                if (EAGAIN == errno) {		//等待更多数据可读
                    flag = 1;
                } else {		//异常状态
                    perror("epoll_thread::async_read");
                    flag = -1;
                }
            }
            break;
        }
        return flag;
    }

    void epoll_thread_impl::async_write(eth_event *ev, std::string *data)
    {
        if (EPOLLOUT == ev->event) {       //当前文件描述符正在监听可写事件
            ev->data_list.push_back(data);
            return;
        }

        int sts = write(ev->fd, data->c_str(), data->size());
        if (sts == data->size()) {      //数据已全部写完
            delete data;
            return;
        }

        if (-1 == sts && EAGAIN != errno) {      //写数据出现异常，不再监听该文件描述符上的事件
            perror("epoll_thread_impl::async_write");
            remove_event(ev);
            delete data;
            return;
        }

        if (sts > 0)
            data->erase(0, sts);
        ev->f_bk = std::move(ev->f);
        ev->f = switch_write;
        ev->event = EPOLLOUT;
        handle_event(EPOLL_CTL_MOD, ev);
    }

    void epoll_thread_impl::switch_write(void *args)
    {
        bool occur_excep = false;
        eth_event *ev = static_cast<eth_event*>(args);
        for (auto it = ev->data_list.begin(); it != ev->data_list.end(); ) {
            int sts = write(ev->fd, (*it)->c_str(), (*it)->size());
            if (-1 == sts) {
                if (EAGAIN != errno) {
                    perror("epoll_thread_impl::switch_write");
                    occur_excep = true;
                }
                break;
            } else if (sts < (*it)->size()) {
                (*it)->erase(0, sts);
                break;
            } else {
                delete *it;
                it = ev->data_list.erase(it);
            }
        }

        if (occur_excep) {
            for (auto& data : ev->data_list)
                delete data;
            ev->data_list.clear();
            ev->eth_impl->remove_event(ev);
        } else if (ev->data_list.empty()){
            ev->f = std::move(ev->f_bk);
            ev->event = EPOLLIN;
            ev->eth_impl->handle_event(EPOLL_CTL_MOD, ev);
        }
    }

    void epoll_thread_impl::thread_proc(epoll_thread_impl *this_ptr)
    {
        epoll_event *events = new epoll_event[epoll_size];
        this_ptr->m_go_done = true;
        while (this_ptr->m_go_done) {
            int cnt = epoll_wait(this_ptr->m_epoll_fd, events, epoll_size, -1);		//block indefinitely

            if (-1 == cnt) {			//epoll_wait有可能因为中断操作而返回，然而此时并没有任何监听事件触发
                perror("thread_proc::epoll_wait");
                continue;
            }

            if (cnt == epoll_size)
                printf("[epoll_thread::thread_proc WARN] 当前待处理的事件达到监听队列的上限 (%d)！\n", cnt);

            for (int i = 0; i < cnt; ++i) {     //处理已触发的事件
                int fd = events[i].data.fd;
                if (!this_ptr->m_ev_array[fd])
                    continue;

                eth_event *ev = this_ptr->m_ev_array[fd];
                ev->f(ev->args);
            }
        }

        for (auto ev : this_ptr->m_ev_array)
            this_ptr->remove_event(ev);
        this_ptr->m_ev_array.clear();
        delete []events;
    }

    timer* epoll_thread::get_timer(std::function<void(void*)> f, void *args /*= nullptr*/)
    {
        timer *tmr = new timer;
        timer_impl *tmr_impl = static_cast<timer_impl*>(tmr->m_obj);
        tmr_impl->fd = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK);    //创建一个非阻塞的定时器资源
        if (-1 != tmr_impl->fd) {
            epoll_thread_impl *epoll_impl = static_cast<epoll_thread_impl*>(m_obj);
            tmr_impl->m_f = std::move(f);
            tmr_impl->m_args = args;

            tmr_impl->eth_impl = epoll_impl;
            tmr_impl->f = tmr_impl->timer_callback;
            tmr_impl->args = tmr_impl;

            eth_sig sig;
            sig.ev = tmr_impl;
            sig.type = 0;
            sig.op = 1;     //加入epoll监听事件
            epoll_impl->m_event->send_signal((const char*)&sig, sizeof(sig));
        } else {
            perror("epoll_thread::get_timer");
            delete tmr;
            delete tmr_impl;
            tmr = nullptr;
        }
        return tmr;
    }

    /**
     * 触发定时器回调时首先读文件描述符 fd，读操作将定时器状态切换为已读，若不执行读操作，
     * 则由于epoll采用edge-trigger边沿触发模式，定时器事件再次触发时将不再回调该函数
     */
    void timer_impl::timer_callback(eth_event *args)
    {
        timer_impl *impl = dynamic_cast<timer_impl*>(args);
        uint64_t cnt;
        read(impl->fd, &cnt, sizeof(cnt));
        impl->m_f(impl->m_args);
    }

    event* epoll_thread::get_event(std::function<void(std::string&, void*)> f, void *args /*= nullptr*/)
    {
        event *ev = new event;
        event_impl *ev_impl = static_cast<event_impl*>(ev->m_obj);
        ev_impl->fd = eventfd(0, EFD_NONBLOCK);			//创建一个非阻塞的事件资源
        if (-1 != ev_impl->fd) {
            epoll_thread_impl *epoll_impl = static_cast<epoll_thread_impl*>(m_obj);
            ev_impl->m_f = std::move(f);
            ev_impl->m_args = args;

            ev_impl->eth_impl = epoll_impl;
            ev_impl->f = ev_impl->event_callback;
            ev_impl->args = ev_impl;

            if (epoll_impl->m_event) {      //已经创建内部事件
                eth_sig sig;
                sig.ev = ev_impl;
                sig.type = 0;
                sig.op = 1;
                epoll_impl->m_event->send_signal((const char*)&sig, sizeof(sig));
            } else {
                epoll_impl->add_event(ev_impl);
            }
        } else {
            perror("epoll_thread::get_event");
            delete ev;
            delete ev_impl;
            ev = nullptr;
        }
        return ev;
    }

    void event_impl::event_callback(eth_event *args)
    {
        std::list<std::string> signals;
        event_impl *impl = dynamic_cast<event_impl*>(args);

        eventfd_t val;
        eventfd_read(impl->fd, &val);       //读操作将事件重置
        {
            std::lock_guard<std::mutex> lck(impl->m_mtx);
            signals = std::move(impl->m_signals);
        }

        for (auto& signal : signals)
            impl->m_f(signal, impl->m_args);			//执行事件回调函数
    }

    udp_ins* epoll_thread::get_udp_ins(bool is_server, uint16_t port,
                                       std::function<void(const std::string&, uint16_t, const char*, size_t, void*)> f, void *args /*= nullptr*/)
    {
        udp_ins *ui = new udp_ins;
        udp_ins_impl *ui_impl = static_cast<udp_ins_impl*>(ui->m_obj);
        if (is_server)		//创建server端的udp套接字不需要指明ip地址，若port设置为0，则系统将随机绑定一个可用端口
            ui_impl->fd = ui_impl->m_net_sock.create(PRT_UDP, USR_SERVER, nullptr, port);
        else		//创建client端的udp套接字时不会使用ip地址和端口
            ui_impl->fd = ui_impl->m_net_sock.create(PRT_UDP, USR_CLIENT, "127.0.0.1", port);

        if (-1 != ui_impl->fd) {
            epoll_thread_impl *epoll_impl = static_cast<epoll_thread_impl*>(m_obj);
            ui_impl->m_f = std::move(f);
            ui_impl->m_args = args;

            ui_impl->eth_impl = epoll_impl;
            ui_impl->f = ui_impl->udp_ins_callback;
            ui_impl->args = ui_impl;

            eth_sig sig;
            sig.ev = ui_impl;
            sig.type = 0;
            sig.op = 1;     //同样加入epoll监听事件
            epoll_impl->m_event->send_signal((const char*)&sig, sizeof(sig));
        } else {
            delete ui;
            delete ui_impl;
            ui = nullptr;
        }
        return ui;
    }

    void udp_ins_impl::udp_ins_callback(eth_event *args)
    {
        udp_ins_impl *impl = dynamic_cast<udp_ins_impl*>(args);
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        while (true) {
            /**
             * 一个udp包的最大长度为65536个字节，因此在获取数据包的时候将应用层缓冲区大小设置为65536个字节，
             * 一次即可获取一个完整的udp包，同时使用udp传输时不需要考虑粘包的问题
             */
            int ret = recvfrom(impl->fd, &impl->m_recv_buffer[0], impl->m_recv_buffer.size(),
                               0, (struct sockaddr*)&addr, &addr_len);
            if (-1 == ret) {
                if (EAGAIN != errno)
                    perror("udp_ins_callback::recvfrom");
                break;
            }

            impl->m_recv_buffer[ret] = 0;		//字符串以0结尾
            std::string ip_addr = inet_ntoa(addr.sin_addr);		//将地址转换为点分十进制格式的ip地址
            uint16_t port = ntohs(addr.sin_port);
            //执行回调，将完整的udp数据包传给应用层
            impl->m_f(ip_addr, port, impl->m_recv_buffer.data(), ret, impl->m_args);
        }
    }

    tcp_client* epoll_thread::get_tcp_client(std::function<void(int, std::string&, void*)> f,
                                             void *args /*= nullptr*/)
    {
        epoll_thread_impl *epoll_impl = static_cast<epoll_thread_impl*>(m_obj);
        if (!epoll_impl->m_tcp_client) {
            epoll_impl->m_tcp_client = new tcp_client(true);		//创建一个新的tcp_client
            tcp_client_impl *tcp_impl = static_cast<tcp_client_impl*>(epoll_impl->m_tcp_client->m_obj);
//            tcp_impl->m_timer = get_timer(tcp_impl->name_resolve_process, tcp_impl);
//            tcp_impl->m_timer->start(10, 10);

            tcp_impl->m_resolve_ev = get_event(tcp_impl->resolve_comp_callback, tcp_impl);
            tcp_impl->m_write_ev = get_event(tcp_impl->accept_request, epoll_impl);

            tcp_impl->m_eth_impl = epoll_impl;
            tcp_impl->m_tcp_f = std::move(f);			//记录回调函数及参数
            tcp_impl->m_tcp_args = args;
        }
        return epoll_impl->m_tcp_client;
    }

    void tcp_client_impl::name_resolve_process(void *args)
    {
        std::list<tcp_client_conn*> resolve_list;
        tcp_client_impl *impl = static_cast<tcp_client_impl*>(args);
        {
            std::lock_guard<std::mutex> lck(impl->m_mtx);
            resolve_list = std::move(impl->m_resolve_list);
        }
        for (auto it = resolve_list.begin(); it != resolve_list.end(); ) {
            int retval = ub_process((*it)->ctx);
            if (retval) {
                printf("resolve error: %s\n", ub_strerror(retval));
                ub_ctx_delete((*it)->ctx);
                delete *it;
                it = resolve_list.erase(it);
                continue;
            }

            if ((*it)->resolve_succ) {
                ub_ctx_delete((*it)->ctx);
                (*it)->ctx = nullptr;
                it = resolve_list.erase(it);
            } else {
                ++it;
            }
        }
        {
            std::lock_guard<std::mutex> lck(impl->m_mtx);
            impl->m_resolve_list.splice(impl->m_resolve_list.end(), resolve_list);
        }
    }

    void tcp_client_impl::name_resolve_callback(void *args, int err, ub_result *result)
    {
        tcp_client_conn *conn = static_cast<tcp_client_conn*>(args);
        conn->resolve_succ = true;
        if (err || !result->havedata) {
            printf("[tcp_client_impl::name_resolve_callback] resolve error: %s\n",
                   ub_strerror(err));
            if (result)
                ub_resolve_free(result);
            delete conn;
            return;
        }

        conn->ip_addr = inet_ntoa(*(in_addr*)result->data[0]);
//        printf("The address of %s is %s\n", tc_conn->domain_name.c_str(), tc_conn->ip_addr.c_str());
        ub_resolve_free(result);
        conn->tcp_impl->m_resolve_ev->send_signal((const char*)&conn, sizeof(conn));
    }

    void tcp_client_impl::resolve_comp_callback(std::string& signal, void *args)
    {
        int64_t ptr = *(int64_t*)signal.data();
        tcp_client_conn *conn = reinterpret_cast<tcp_client_conn*>(ptr);
        tcp_client_impl *impl = static_cast<tcp_client_impl*>(args);
        conn->eth_impl = impl->m_eth_impl;

        conn->event = EPOLLOUT;
        conn->f = impl->tcp_client_callback;
        conn->args = conn;
        conn->conn_sock.create(PRT_TCP, USR_CLIENT, conn->ip_addr.c_str(), conn->port);

        if (conn->fd < impl->m_eth_impl->m_ev_array.size()) {
            eth_event *ev = impl->m_eth_impl->m_ev_array[conn->fd];
            if (ev) {
                conn->data_list = std::move(ev->data_list);
                for (auto&data : conn->data_list)
                    data->insert(data->find("Host:")+6, conn->domain_name);
                delete ev;
            }
        }
        impl->m_eth_impl->add_event(conn);
    }

    void tcp_client_impl::tcp_client_callback(eth_event *ev)
    {
        tcp_client_conn *tc_conn = dynamic_cast<tcp_client_conn*>(ev);
        if (EPOLLOUT == tc_conn->event) {
            tc_conn->event = EPOLLIN;
            std::list<std::string*> data_list = std::move(tc_conn->data_list);
            for (auto& data : data_list)
                tc_conn->eth_impl->async_write(tc_conn, data);

            if (EPOLLIN == tc_conn->event)
                tc_conn->eth_impl->handle_event(EPOLL_CTL_MOD, tc_conn);
            return;
        }

        int sts = 0;
        if (tc_conn->tcp_impl->m_expose) {       //判断当前使用的是否是基本的tcp_client
            std::string stream;     //读该tcp连接上的数据并执行回调
            sts = tc_conn->eth_impl->async_read(tc_conn->fd, stream);
            if (!stream.empty())
                tc_conn->tcp_impl->m_tcp_f(tc_conn->fd, stream, tc_conn->tcp_impl->m_tcp_args);
        } else {
            switch (tc_conn->tcp_impl->m_app_prt) {
                case PRT_HTTP: {		//当前使用的是http_client
                    auto http_conn = dynamic_cast<http_client_conn*>(tc_conn);
                    auto http_impl = dynamic_cast<http_client_impl*>(tc_conn->tcp_impl);
                    sts = http_impl->m_eth_impl->async_read(http_conn->fd, http_conn->stream_buffer);		//读http响应流
                    http_impl->check_http_stream(http_conn->fd, http_conn);		//解决粘包问题，在获取完整的http响应信息后执行回调
                    break;
                }
            }
        }

        if (sts <= 0) {     //读文件描述符检测到异常或发现对端已关闭连接
//            if (sts < 0)
//                printf("[tcp_client_impl::tcp_client_callback] 读文件描述符 %d 异常\n", tc_conn->fd);
//            else
//                printf("[tcp_client_impl::tcp_client_callback] 连接 %d 对端正常关闭\n", tc_conn->fd);
            tc_conn->eth_impl->remove_event(tc_conn);
        }
    }

    void tcp_client_impl::accept_request(std::string& signal, void *args)
    {
        epoll_thread_impl *impl = static_cast<epoll_thread_impl*>(args);
        write_sig *sig = (write_sig*)signal.data();
        if (sig->fd+1 > impl->m_ev_array.size())
            impl->m_ev_array.resize(sig->fd+1, 0);
        eth_event *ev = impl->m_ev_array[sig->fd];

        if (ev) {
            http_client_conn *conn = dynamic_cast<http_client_conn*>(ev);
            sig->data->insert(sig->data->find("Host:")+6, conn->domain_name);
            impl->async_write(ev, sig->data);
        } else {
            ev = new eth_event;
            ev->data_list.push_back(sig->data);
            impl->m_ev_array[sig->fd] = ev;
        }
    }

    tcp_server* epoll_thread::get_tcp_server(uint16_t port, std::function<void(int, const std::string&, uint16_t, std::string&, void*)> f,
                                             void *args /*= nullptr*/)
    {
        epoll_thread_impl *epoll_impl = static_cast<epoll_thread_impl*>(m_obj);
        if (!epoll_impl->m_tcp_server) {
            epoll_impl->m_tcp_server = new tcp_server(true);		//创建一个新的tcp_server
            tcp_server_impl *tcp_impl = static_cast<tcp_server_impl*>(epoll_impl->m_tcp_server->m_obj);
            tcp_impl->eth_impl = epoll_impl;
            tcp_impl->m_write_ev = get_event(tcp_impl->accept_write, epoll_impl);

            tcp_impl->m_tcp_f = std::move(f);		//保存回调函数及参数
            tcp_impl->m_tcp_args = args;
            tcp_impl->start_listen(port);			//开始监听
        }
        return epoll_impl->m_tcp_server;
    }

    void tcp_server_impl::start_listen(uint16_t port)
    {
        //创建tcp服务端的监听套接字，允许接收任意ip地址发送的服务请求，监听请求的端口为port
        fd = m_net_sock.create(PRT_TCP, USR_SERVER, nullptr, port);
        f = tcp_server_callback;
        args = this;

        eth_sig sig;
        sig.ev = this;
        sig.type = 0;
        sig.op = 1;     //加入epoll监听事件
        eth_impl->m_event->send_signal((const char*)&sig, sizeof(sig));
    }

    void tcp_server_impl::tcp_server_callback(eth_event *ev)
    {
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(struct sockaddr_in);
        tcp_server_impl *impl = dynamic_cast<tcp_server_impl*>(ev);
        while (true) {		//接受所有请求连接的tcp客户端
            int client_fd = accept(impl->fd, (struct sockaddr*)&addr, &addr_len);
            if (-1 == client_fd) {
                if (EAGAIN != errno)
                    perror("tcp_server_callback::accept");
                break;
            }

            setnonblocking(client_fd);			//将客户端连接文件描述符设为非阻塞并加入监听事件
            tcp_server_conn *conn = nullptr;
            if (impl->m_expose) {
                conn = new tcp_server_conn;
            } else {
                switch (impl->m_app_prt) {
                    case PRT_HTTP:		conn = new http_server_conn;    break;
                }
            }

            conn->fd = client_fd;
            conn->eth_impl = impl->eth_impl;
            conn->f = impl->read_tcp_stream;
            conn->args = conn;

            conn->ip_addr = inet_ntoa(addr.sin_addr);	//将地址转换为点分十进制格式的ip地址
            conn->port = ntohs(addr.sin_port);          //将端口由网络字节序转换为主机字节序
            conn->ts_impl = impl;
            impl->eth_impl->add_event(conn);        //将该连接加入监听事件
        }
    }

    void tcp_server_impl::read_tcp_stream(eth_event *ev)
    {
        tcp_server_conn *conn = dynamic_cast<tcp_server_conn*>(ev);
        int sts = 0;
        if (conn->ts_impl->m_expose) {		//判断当前使用的是否是基本的tcp_server
            std::string stream;
            sts = conn->eth_impl->async_read(conn->fd, stream);
            if (!stream.empty())		//若获取到数据流则执行回调函数
                conn->ts_impl->m_tcp_f(conn->fd, conn->ip_addr, conn->port,
                                       stream, conn->ts_impl->m_tcp_args);
        } else {
            switch (conn->ts_impl->m_app_prt) {
                case PRT_HTTP: {
                    auto http_conn = dynamic_cast<http_server_conn*>(conn);
                    auto http_impl = dynamic_cast<http_server_impl*>(conn->ts_impl);
                    sts = http_impl->eth_impl->async_read(http_conn->fd, http_conn->stream_buffer);		//读http请求流
                    http_impl->check_http_stream(http_conn->fd, http_conn);		//解决粘包问题，读取完整的http请求信息后执行回调
                    break;
                }
            }
        }

        if (sts <= 0) {		//读文件描述符检测到异常或发现对端已关闭连接
//            if (sts < 0)
//                printf("[tcp_server_impl::read_tcp_stream] 读文件描述符 %d 出现异常", ev->fd);
//            else
//                printf("[tcp_server_impl::read_tcp_stream] 连接 %d 对端正常关闭\n", ev->fd);
            conn->eth_impl->remove_event(conn);
        }
    }

    void tcp_server_impl::accept_write(std::string& signal, void *args)
    {
        epoll_thread_impl *eth_impl = static_cast<epoll_thread_impl*>(args);
        write_sig *sig = (write_sig*)signal.data();
        eth_event *ev = eth_impl->m_ev_array[sig->fd];
        if (!ev) {
            delete sig->data;
            return;
        }
        eth_impl->async_write(ev, sig->data);
    }

    http_client* epoll_thread::get_http_client(std::function<void(int, int, std::unordered_map<std::string, std::string>&, std::string&, void*)> f,
                                               void *args /*= nullptr*/)
    {
        epoll_thread_impl *epoll_impl = static_cast<epoll_thread_impl*>(m_obj);
        if (!epoll_impl->m_http_client) {
            epoll_impl->m_http_client = new http_client;		//创建一个新的http_client
            http_client_impl *http_impl = static_cast<http_client_impl*>(epoll_impl->m_http_client->m_obj);
//            http_impl->m_timer = get_timer(http_impl->name_resolve_process, http_impl);
//            http_impl->m_timer->start(10, 10);

            http_impl->m_resolve_ev = get_event(http_impl->resolve_comp_callback, http_impl);
            http_impl->m_write_ev = get_event(http_impl->accept_request, epoll_impl);

            http_impl->m_eth_impl = epoll_impl;
            http_impl->m_http_f = std::move(f);		//保存回调函数及参数
            http_impl->m_http_args = args;
        }
        return epoll_impl->m_http_client;
    }

    /**
     * 一个HTTP响应流例子如下所示：
     * HTTP/1.1 200 OK
     * Accept-Ranges: bytes
     * Connection: close
     * Pragma: no-cache
     * Content-Type: text/plain
     * Content-Length: 12("Hello World!"字节长度)
     *
     * Hello World!
     */
    void http_client_impl::check_http_stream(int fd, http_client_conn *conn)
    {
        while (true) {
            if (-1 == conn->m_content_len) {				//与之前的http响应流已完成分包处理，开始处理新的响应
                if (conn->stream_buffer.size() < 4)		//首先验证流的开始4个字节是否为签名"HTTP"
                    return;

                if (strncmp(conn->stream_buffer.c_str(), "HTTP", 4)) {		//签名出错，该流不是HTTP流
                    std::string::size_type sig_pos = conn->stream_buffer.find("HTTP");		//查找流中是否存在"HTTP"子串
                    if (std::string::npos == sig_pos) {
                        conn->stream_buffer.clear();		//不存在则清空所有数据流并返回
                        return;
                    }
                    conn->stream_buffer.erase(0, sig_pos);		//若存在则截断当前这段无效的数据流
                }

                //先判断是否已经获取到完整的响应流
                std::string::size_type delim_pos = conn->stream_buffer.find("\r\n\r\n");
                if (std::string::npos == delim_pos)		//还未完整的取到响应头
                    return;

                std::string headers = conn->stream_buffer.substr(0, delim_pos);
                conn->stream_buffer.erase(0, delim_pos+4);		//截断整个头部，包含4个字节的分隔符"\r\n\r\n"
                auto str_vec = split(headers, "\r\n");

                if (str_vec.size() <= 1)			//响应流中包含一个响应行以及至少一个头部字段
                    continue;

                bool header_err = false;
                std::unordered_map<std::string, std::string> header_kvs;
                for (size_t i = 0; i < str_vec.size(); ++i) {
                    if (0 == i) {		//首先解析响应行
                        //响应行包含空格分隔的三个字段，例如：HTTP/1.1(协议/版本) 200(状态码) OK(简单描述)
                        auto line_elem = split(str_vec[i], " ");
                        if (3 != line_elem.size()) {		//响应行出错，无效的响应流
                            header_err = true;
                            break;
                        }
                        conn->m_status = std::atoi(line_elem[1].c_str());		//记录中间的状态码
                    } else {
                        auto header = split(str_vec[i], ": ");		//头部字段键值对的分隔符为": "
                        if (header.size() >= 2)		//无效的头部字段直接丢弃，不符合格式的值发生截断
                            header_kvs[header[0]] = header[1];
                    }
                }
                if (header_err)
                    continue;

                if (header_kvs.end() == header_kvs.find("Content-Length"))
                    continue;		//响应头中不包含"Content-Length"字段，将此响应流视为无效数据流
                else
                    conn->m_content_len = std::atoi(header_kvs["Content-Length"].c_str());

                if (!conn->m_headers.empty())		//将之前请求响应过程中保留的头部键值对清除
                    conn->m_headers.clear();
                conn->m_headers = std::move(header_kvs);
            } else {
                //一个http响应流分多次发送，接收到上一个响应的数据流
            }

            if (conn->stream_buffer.size() < conn->m_content_len)		//响应体中不包含足够的数据，等待该连接上更多的数据到来
                return;

            //完成分包并取到完整的响应流，执行回调函数
            std::string body;
            if (conn->stream_buffer.size() == conn->m_content_len) {
                body = std::move(conn->stream_buffer);
            } else {		// >
                body = conn->stream_buffer.substr(0, conn->m_content_len);
                conn->stream_buffer.erase(0, conn->m_content_len);
            }
            m_http_f(fd, conn->m_status, conn->m_headers, body, m_http_args);
            conn->m_content_len = -1;
        }
    }

    http_server* epoll_thread::get_http_server(uint16_t port, std::function<void(int, const std::string&, const std::string&,
                                                                                 std::unordered_map<std::string, std::string>&, std::string*, void*)> f, void *args /*= nullptr*/)
    {
        epoll_thread_impl *epoll_impl = static_cast<epoll_thread_impl*>(m_obj);
        if (!epoll_impl->m_http_server) {
            epoll_impl->m_http_server = new http_server;			//创建一个新的http_server
            http_server_impl *http_impl = static_cast<http_server_impl*>(epoll_impl->m_http_server->m_obj);
            http_impl->eth_impl = epoll_impl;
            http_impl->m_write_ev = get_event(http_impl->accept_write, epoll_impl);

            http_impl->m_http_f = std::move(f);
            http_impl->m_http_args = args;
            http_impl->start_listen(port);		//开始监听
        }
        return epoll_impl->m_http_server;
    }

    /**
     * 一个HTTP请求流的例子如下所示：
     * POST /request HTTP/1.1
     * Accept-Ranges: bytes
     * Connection: close
     * Pragma: no-cache
     * Content-Type: text/plain
     * Content-Length: 12("Hello World!"字节长度)
     *
     * Hello World!
     */
    void http_server_impl::check_http_stream(int fd, http_server_conn *conn)
    {
        while (true) {
            if (-1 == conn->m_content_len) {			//与之前的http请求流已完成分包处理，开始处理新的请求
                std::string::size_type delim_pos = conn->stream_buffer.find("\r\n\r\n");
                if (std::string::npos == delim_pos)		//还未完整的取到请求头
                    return;

                //在完整的请求头中查找是否存在"HTTP"签名
                std::string::size_type sig_pos = conn->stream_buffer.find("HTTP");
                if (std::string::npos == sig_pos) {		//若不存在说明不是有效的头部，截断整个头部(包含分隔符"\r\n\r\n")
                    conn->stream_buffer.erase(0, delim_pos+4);
                    continue;
                }

                std::string headers = conn->stream_buffer.substr(0, delim_pos);
                conn->stream_buffer.erase(0, delim_pos+4);			//截断整个头部，包含分隔符
                auto str_vec = split(headers, "\r\n");
                if (str_vec.size() <= 1)		//请求流中包含一个请求行以及至少一个头部字段
                    continue;

                bool header_err = false;
                std::unordered_map<std::string, std::string> header_kvs;
                for (size_t i = 0; i < str_vec.size(); ++i) {
                    if (0 == i) {		//首先解析请求行
                        //请求行包含空格分隔的三个字段，例如：POST(请求方法) /request(url) HTTP/1.1(协议/版本)
                        auto line_elem = split(str_vec[i], " ");
                        if (3 != line_elem.size()) {		//请求行出错，无效的请求流
                            header_err = true;
                            break;
                        }

                        conn->m_method = line_elem[0];			//记录请求方法
                        conn->m_url = line_elem[1];					//记录请求的url(以"/"开始的部分)
                    } else {
                        auto header = split(str_vec[i], ": ");		//头部字段键值对的分隔符为": "
                        if (header.size() >= 2)		//无效的头部字段直接丢弃，不符合格式的值发生截断
                            header_kvs[header[0]] = header[1];
                    }
                }
                if (header_err)
                    continue;

                //有些请求流不存在请求体，此时头部中不包含"Content-Length"字段
                if (header_kvs.end() == header_kvs.find("Content-Length"))
                    conn->m_content_len = 0;
                else
                    conn->m_content_len = std::atoi(header_kvs["Content-Length"].c_str());

                if (!conn->m_headers.empty())		//将之前请求响应过程中保留的头部键值对清除
                    conn->m_headers.clear();
                conn->m_headers = std::move(header_kvs);
            } else {
                //一个http请求流分多次发送，接收到上一个请求的数据流
            }

            assert(conn->m_content_len >= 0);		//此时已正确获取到当前请求中的请求体信息
            if (0 == conn->m_content_len) {		//请求流中不包含请求体
                m_http_f(fd, conn->m_method, conn->m_url, conn->m_headers, nullptr, m_http_args);
            } else {
                if (conn->stream_buffer.size() < conn->m_content_len)
                    return;			//请求体中不包含足够的数据，等待该连接上更多的数据到来

                //完成分包并取到完整的请求流，执行回调函数
                std::string body;
                if (conn->stream_buffer.size() == conn->m_content_len) {
                    body = std::move(conn->stream_buffer);
                } else {		// >
                    body = conn->stream_buffer.substr(0, conn->m_content_len);
                    conn->stream_buffer.erase(0, conn->m_content_len);
                }
                m_http_f(fd, conn->m_method, conn->m_url, conn->m_headers, &body, m_http_args);
            }
            conn->m_content_len = -1;
        }
    }

    fs_monitor* epoll_thread::get_fs_monitor(std::function<void(const char*, uint32_t, void *args)> f,
                                             void *args /*= nullptr*/)
    {
        epoll_thread_impl *epoll_impl = static_cast<epoll_thread_impl*>(m_obj);
        if (!epoll_impl->m_fs_monitor) {
            epoll_impl->m_fs_monitor = new fs_monitor;
            fs_monitor_impl *monitor_impl = static_cast<fs_monitor_impl*>(epoll_impl->m_fs_monitor->m_obj);

            monitor_impl->eth_impl = epoll_impl;
            monitor_impl->m_monitor_f = std::move(f);
            monitor_impl->m_monitor_args = args;

            monitor_impl->fd = inotify_init1(IN_NONBLOCK);
            monitor_impl->f = monitor_impl->fs_monitory_callback;
            monitor_impl->args = monitor_impl;

            eth_sig sig;
            sig.ev = monitor_impl;
            sig.type = 0;
            sig.op = 1;
            epoll_impl->m_event->send_signal((const char*)&sig, sizeof(sig));
            monitor_impl->m_monitor_ev = get_event(monitor_impl->monitor_request, monitor_impl);
        }
        return epoll_impl->m_fs_monitor;
    }

    void fs_monitor_impl::fs_monitory_callback(eth_event *ev)
    {
        fs_monitor_impl *impl = dynamic_cast<fs_monitor_impl*>(ev);
        char buf[1024] = {0};
        int filter = 0, offset = 0;
        while (true) {
            int len = read(impl->fd, buf+offset, sizeof(buf)-offset);
            if (len <= 0) {
                if (EAGAIN != errno)
                    perror("fs_monitor::read");
                break;
            }

            len += offset;
            const char *ptr = buf;
            if (filter) {
                if (filter >= 1024) {
                    filter -= 1024;
                    offset = 0;
                    continue;
                } else {    // < 1024
                    ptr += filter;
                    filter = 0;
                }
            }

            while (true) {
                //incomplete inotify_event structure
                if (ptr+sizeof(inotify_event) > buf+sizeof(buf)) {      //read more
                    offset = buf+sizeof(buf)-ptr;
                    std::memmove(buf, ptr, offset);
                    break;
                }

                inotify_event *nfy_ev = (inotify_event*)ptr;
                //buffer can't hold the whole package
                if (nfy_ev->len+sizeof(inotify_event) > sizeof(buf)) {      //ignore events on the file
                    printf("[fs_monitory_callback] WARN: event package size greater than the buffer size!\n");
                    filter = ptr+sizeof(inotify_event)+nfy_ev->len-buf-sizeof(buf);
                    offset = 0;
                    break;
                }

                if (ptr+sizeof(inotify_event)+nfy_ev->len > buf+sizeof(buf)) {  //read more
                    offset = buf+sizeof(buf)-ptr;
                    std::memmove(buf, ptr, offset);
                    break;
                }

                if (impl->m_wd_mev.end() != impl->m_wd_mev.find(nfy_ev->wd)) {
                    auto& mev = impl->m_wd_mev[nfy_ev->wd];
                    std::string file_name = mev->path;
                    if (nfy_ev->len)
                        file_name += nfy_ev->name;

                    if ((nfy_ev->mask & IN_ISDIR) && mev->recur_flag) {      //directory
                        if ('/' != file_name.back())
                            file_name.push_back('/');

                        if ((nfy_ev->mask & IN_CREATE) &&
                            impl->m_path_mev.end() == impl->m_path_mev.find(file_name)) {
                            int watch_id = inotify_add_watch(impl->fd, file_name.c_str(), mev->mask);
                            if (__glibc_likely(-1 != watch_id))
                                impl->trigger_event(true, watch_id, file_name, mev->recur_flag, mev->mask);
                        } else if ((nfy_ev->mask & IN_DELETE) &&
                                   impl->m_path_mev.end() != impl->m_path_mev.find(file_name)) {
                            impl->trigger_event(false, impl->m_path_mev[file_name]->watch_id, file_name, 0, 0);
                        }
                    }
                    impl->m_monitor_f(file_name.c_str(), nfy_ev->mask, impl->m_monitor_args);
                }

                ptr += sizeof(inotify_event)+nfy_ev->len;
                if (ptr-buf == len)     //no more events to process
                    break;
            }
        }
    }

    void fs_monitor_impl::recursive_monitor(const std::string& root_dir, bool add, int mask)
    {
        DIR *dir = opendir(root_dir.c_str());
        if (__glibc_unlikely(!dir)) {
            perror("recursive_monitor::opendir failed");
            return;
        }

        struct stat st;
        struct dirent *ent = nullptr;
        while (ent = readdir(dir)) {
            if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
                continue;

            std::string path = root_dir+ent->d_name;
            stat(path.c_str(), &st);
            if (!S_ISDIR(st.st_mode))       //不是目录则不做处理
                continue;

            if ('/' != path.back())
                path.push_back('/');

            if (add) {
                if (m_path_mev.end() == m_path_mev.find(path)) {
                    int watch_id = inotify_add_watch(fd, path.c_str(), mask);
                    if (__glibc_unlikely(-1 == watch_id)) {
                        perror("recursive_monitor::inotify_add_watch");
                        continue;
                    }
                    trigger_event(true, watch_id, path, true, mask);
                }
            } else {    //remove
                if (m_path_mev.end() != m_path_mev.find(path))
                    trigger_event(false, m_path_mev[path]->watch_id, path, 0, 0);
            }
            recursive_monitor(path, add, mask);
        }
        closedir(dir);
    }

    void fs_monitor_impl::trigger_event(bool add, int watch_id, const std::string& path, int recur_flag, uint32_t mask)
    {
        if (add) {   //add watch
            std::shared_ptr<monitor_ev> mev = std::make_shared<monitor_ev>();
            mev->recur_flag = recur_flag;
            mev->watch_id = watch_id;
            mev->mask = mask;
            mev->path = path;

            m_path_mev[path] = mev;
            m_wd_mev[watch_id] = std::move(mev);
        } else {    //remove watch
            inotify_rm_watch(fd, watch_id);
            m_wd_mev.erase(watch_id);
            m_path_mev.erase(path);
        }
    }

    void fs_monitor_impl::monitor_request(std::string& signal, void *args)
    {
        fs_monitor_impl *impl = static_cast<fs_monitor_impl*>(args);
        int recur_flag = *(int*)(signal.data()+1);
        struct stat st;
        if ('1' == signal.front()) {    //add watch
            int mask = *(int*)(signal.data()+sizeof(int)+1);
            std::string path = signal.data()+sizeof(int)*2+1;
            stat(path.c_str(), &st);
            if (S_ISDIR(st.st_mode) && '/' != path.back())  //添加监控的是目录
                path.push_back('/');

            if (impl->m_path_mev.end() == impl->m_path_mev.find(path)) {
                int watch_id = inotify_add_watch(impl->fd, path.c_str(), mask);
                if (__glibc_unlikely(-1 == watch_id)) {
                    perror("fs_monitor::inotify_add_watch");
                    return;
                }

                impl->trigger_event(true, watch_id, path, recur_flag, mask);
                if (S_ISDIR(st.st_mode) && recur_flag)    //对子目录递归监控
                    impl->recursive_monitor(path, true, mask);
            }
        } else if ('0' == signal.front()) {     //remove watch
            std::string path = signal.data()+sizeof(int)+1;
            stat(path.c_str(), &st);
            if (S_ISDIR(st.st_mode) && '/' != path.back())  //移除监控的是目录
                path.push_back('/');

            if (impl->m_path_mev.end() != impl->m_path_mev.find(path)) {
                impl->trigger_event(false, impl->m_path_mev[path]->watch_id, path, 0, 0);
                if (S_ISDIR(st.st_mode) && recur_flag)      //对子目录递归移除
                    impl->recursive_monitor(path, false, -1);
            }
        }
    }
}
