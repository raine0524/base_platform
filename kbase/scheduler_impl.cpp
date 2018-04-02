#include "stdafx.h"
#include "scheduler_impl.h"

namespace crx
{
    scheduler::scheduler()
    {
        auto *impl = new scheduler_impl(this);
        impl->m_cos.reserve(64);        //预留64个协程
        impl->m_ev_array.reserve(128);  //预留128个epoll事件
        m_obj = impl;
    }

    scheduler::~scheduler()
    {
        auto impl = (scheduler_impl*)m_obj;
        for (auto co_impl : impl->m_cos)
            delete co_impl;
        impl->m_cos.clear();

        if (impl->m_http_client) {
            delete (http_client_impl*)impl->m_http_client->m_obj;
            delete impl->m_http_client;
        }
        if (impl->m_http_server) {
            delete (http_server_impl*)impl->m_http_server->m_obj;
            delete impl->m_http_server;
        }
        if (impl->m_tcp_client) {
            delete (tcp_client_impl*)impl->m_tcp_client->m_obj;
            delete impl->m_tcp_client;
        }
        if (impl->m_tcp_server) {
            delete (tcp_server_impl*)impl->m_tcp_server->m_obj;
            delete impl->m_tcp_server;
        }
        if (impl->m_fs_monitor) {
            delete (fs_monitor_impl*)impl->m_fs_monitor->m_obj;
            delete impl->m_fs_monitor;
        }
        if (-1 != impl->m_epoll_fd) {
            close(impl->m_epoll_fd);
            impl->m_epoll_fd = -1;
        }
        delete impl;
    }

    size_t scheduler::co_create(std::function<void(scheduler *sch, void *arg)> f, void *arg,
                                bool is_share /*= false*/, const char *comment /*= nullptr*/)
    {
        auto impl = (scheduler_impl*)m_obj;
        return impl->co_create(f, arg, this, false, is_share, comment);
    }

    bool scheduler::co_yield(size_t co_id, SUS_STATUS sts /*= STS_WAIT*/)
    {
        auto sch_impl = (scheduler_impl*)m_obj;
        assert(co_id < sch_impl->m_cos.size());
        if (sch_impl->m_running_co == co_id)        //co_id无效或对自身进行切换，直接返回
            return true;

        auto curr_co = (-1 == sch_impl->m_running_co) ? nullptr : sch_impl->m_cos[sch_impl->m_running_co];
        auto yield_co = sch_impl->m_cos[co_id];
        if (!yield_co || CO_UNKNOWN == yield_co->status)      //指定协程无效或者状态指示不可用，同样不发生切换
            return false;

        assert(CO_RUNNING != yield_co->status);
        sch_impl->m_running_co = (int)co_id;
        if (curr_co) {
            auto main_co = sch_impl->m_cos[0];
            if (curr_co->is_share)      //当前协程使用的是共享栈模式，其使用的栈是主协程中申请的空间
                sch_impl->save_stack(curr_co, main_co->stack+STACK_SIZE);

            curr_co->status = CO_SUSPEND;
            curr_co->sus_sts = sts;

            //当前协程与待切换的协程都使用了共享栈
            if (curr_co->is_share && yield_co->is_share && CO_SUSPEND == yield_co->status) {
                sch_impl->m_next_co = (int)co_id;
                swapcontext(&curr_co->ctx, &main_co->ctx);      //先切换回主协程
            } else {
                //待切换的协程使用的是共享栈模式并且当前处于挂起状态，恢复其栈空间至主协程的缓冲区中
                if (yield_co->is_share && CO_SUSPEND == yield_co->status)
                    memcpy(main_co->stack+STACK_SIZE-yield_co->size, yield_co->stack, yield_co->size);

                sch_impl->m_next_co = -1;
                yield_co->status = CO_RUNNING;
                swapcontext(&curr_co->ctx, &yield_co->ctx);
            }

            //此时位于主协程中，且主协程用于帮助从一个使用共享栈的协程切换到另一个使用共享栈的协程中
            while (-1 != sch_impl->m_next_co) {
                auto next_co = sch_impl->m_cos[sch_impl->m_next_co];
                sch_impl->m_next_co = -1;
                memcpy(main_co->stack+STACK_SIZE-next_co->size, next_co->stack, next_co->size);
                next_co->status = CO_RUNNING;
                swapcontext(&main_co->ctx, &next_co->ctx);
            }
        } else {        //当前执行流首次发生切换，此时从当前线程进入主执行流
            yield_co->status = CO_RUNNING;
            sch_impl->main_coroutine(this);
        }
        return true;
    }

    std::vector<coroutine*> scheduler::get_avail_cos()
    {
        auto sch_impl = (scheduler_impl*)m_obj;
        std::vector<coroutine*> cos;
        for (auto co_impl : sch_impl->m_cos)
            if (CO_UNKNOWN != co_impl->status)
                cos.push_back(co_impl);
        return cos;
    }

    size_t scheduler_impl::co_create(std::function<void(scheduler *sch, void *arg)>& f, void *arg, scheduler *sch,
                                     bool is_main_co, bool is_share /*= false*/, const char *comment /*= nullptr*/)
    {
        coroutine_impl *co_impl = nullptr;
        if (m_unused_list.empty()) {
            co_impl = new coroutine_impl;
            co_impl->co_id = m_cos.size();
            m_cos.push_back(co_impl);
        } else {        //复用之前已创建的协程
            size_t co_id = m_unused_list.front();
            m_unused_list.pop_front();
            co_impl = m_cos[co_id];       //复用时其id不变
            if (co_impl->is_share != is_share) {        //复用时堆栈的使用模式改变
                delete []co_impl->stack;
                co_impl->stack = nullptr;
            }
        }

        co_impl->status = CO_READY;
        co_impl->is_share = is_share;
        if (!is_share)      //不使用共享栈时将在协程创建的同时创建协程所用的栈
            co_impl->stack = new char[STACK_SIZE];

        if (comment) {
            bzero(co_impl->comment, sizeof(co_impl->comment));
            strcpy(co_impl->comment, comment);
        }
        co_impl->f = std::move(f);
        co_impl->arg = arg;

        bzero(&co_impl->ctx, sizeof(co_impl->ctx));
        if (!is_main_co) {     //主协程先于其他所有协程被创建
            auto main_co = m_cos[0];
            getcontext(&co_impl->ctx);
            if (is_share)
                co_impl->ctx.uc_stack.ss_sp = main_co->stack;
            else
                co_impl->ctx.uc_stack.ss_sp = co_impl->stack;
            co_impl->ctx.uc_stack.ss_size = STACK_SIZE;
            co_impl->ctx.uc_link = &main_co->ctx;

            auto ptr = (uint64_t)sch;
            makecontext(&co_impl->ctx, (void (*)())coroutine_wrap, 2, (uint32_t)ptr, (uint32_t)(ptr>>32));
        }
        return co_impl->co_id;
    }

    void scheduler_impl::save_stack(coroutine_impl *co_impl, const char *top)
    {
        char stub = 0;
        assert(top-&stub <= STACK_SIZE);
        if (co_impl->capacity < top-&stub) {        //容量不足
            co_impl->capacity = top-&stub;
            delete []co_impl->stack;
            co_impl->stack = new char[co_impl->capacity];
        }
        co_impl->size = top-&stub;
        memcpy(co_impl->stack, &stub, co_impl->size);
    }

    void scheduler_impl::coroutine_wrap(uint32_t low32, uint32_t hi32)
    {
        uintptr_t this_ptr = ((uintptr_t)hi32 << 32) | (uintptr_t)low32;
        auto sch = (scheduler*)this_ptr;
        auto sch_impl = (scheduler_impl*)sch->m_obj;
        coroutine_impl *co_impl = sch_impl->m_cos[sch_impl->m_running_co];
        co_impl->f(sch, co_impl->arg);

        //协程执行完成后退出，此时处于不可用状态，进入未使用队列等待复用
        sch_impl->m_running_co = 0;
        sch_impl->m_cos[0]->status = CO_RUNNING;
        co_impl->status = CO_UNKNOWN;
        sch_impl->m_unused_list.push_back(co_impl->co_id);
    }

    void scheduler_impl::main_coroutine(scheduler *sch)
    {
        auto events = new epoll_event[EPOLL_SIZE];
        m_go_done = true;
        while (m_go_done) {
            int cnt = epoll_wait(m_epoll_fd, events, EPOLL_SIZE, 10);		//时间片设置为10ms，与Linux内核使用的时间片相近

            if (-1 == cnt) {			//epoll_wait有可能因为中断操作而返回，然而此时并没有任何监听事件触发
                perror("main_coroutine::epoll_wait");
                continue;
            }

            if (cnt == EPOLL_SIZE)
                printf("[main_coroutine::epoll_wait WARN] 当前待处理的事件达到监听队列的上限 (%d)！\n", cnt);

            size_t i = 0;
            while (i < cnt) {       //处理已触发的事件
                int fd = events[i].data.fd;
                auto ev = m_ev_array[fd];
                if (ev) {
                    /*
                     * 为避免不必要的协程切换操作，库中通过get_xxx方式获取的实例均把co_id设置为0(即主协程的co_id)，除非在一次
                     * 函数调用链中涉及到I/O操作，否则均不会调用co_create函数
                     */
                    if (!ev->co_id)
                        ev->f(sch, ev->args);
                    else if (CO_UNKNOWN != m_cos[ev->co_id]->status)
                        m_sch->co_yield(ev->co_id);
                }
                ++i;
            }

            i = 1;
            while (i < m_cos.size()) {
                if (CO_SUSPEND == m_cos[i]->status && STS_HAVE_REST == m_cos[i]->sus_sts)
                    m_sch->co_yield(i);
                ++i;
            }

            //当前未使用的协程数量过多，回收一部分资源
            size_t total_cos = m_cos.size();
            if (total_cos > 64 && m_unused_list.size() > (size_t)(total_cos/2.0)) {
                auto last_co = m_cos.back();
                if (CO_UNKNOWN == last_co->status) {        //从后往前释放资源，当最后一个协程无效时才释放该资源
                    delete last_co;
                    m_cos.pop_back();
                }
            }
        }

        for (auto ev : m_ev_array)
            remove_event(ev);
        delete []events;
    }

    void scheduler_impl::handle_event(int op, int fd, uint32_t event)
    {
        struct epoll_event ev = {0};
        ev.events = event | EPOLLET;    //每个读/写事件都采用edge-trigger(边沿触发)的模式
        ev.data.fd = fd;

        if (-1 == epoll_ctl(m_epoll_fd, op, fd, &ev))
            perror("handle_event::epoll_ctl");
    }

    void scheduler_impl::add_event(eth_event *ev, uint32_t event /*= EPOLLIN*/)
    {
        if (ev) {
            if (ev->fd >= m_ev_array.size())
                m_ev_array.resize((size_t)ev->fd+1, nullptr);
            m_ev_array[ev->fd] = ev;
            handle_event(EPOLL_CTL_ADD, ev->fd, event);
        }
    }

    void scheduler_impl::remove_event(eth_event *ev)
    {
        if (ev) {
            handle_event(EPOLL_CTL_DEL, ev->fd, EPOLLIN);
            m_ev_array[ev->fd] = nullptr;
            delete ev;
        }
    }

    //异步读的一个原则是只要可读就一直读，直到errno被置为EAGAIN提示等待更多数据可读
    int scheduler_impl::async_read(int fd, std::string& read_str)
    {
        size_t read_size = read_str.size();
        while (true) {
            read_str.resize(read_size+1024);
            ssize_t ret = read(fd, &read_str[read_size], 1024);
            read_size = read_str.size();
            if (-1 == ret) {
                read_str.resize(read_size-1024);        //本次循环未读到任何数据
                if (EAGAIN == errno) {		//等待更多数据可读
                    return 1;
                } else {		//异常状态
                    perror("scheduler::async_read");
                    return -1;
                }
            }

            if (0 == ret) {     //已读到文件末尾，若为套接字文件，表明对端已关闭
                read_str.resize(read_size-1024);
                return 0;
            }

            if (ret < 1024) {
                read_str.resize(read_size+ret-1024);        //没有更多的数据可读
                return 1;
            }
        }
    }

    void scheduler_impl::async_write(eth_event *ev, const char *data, size_t len)
    {
        size_t write_len = 0;
        while (true) {
            ssize_t sts = write(ev->fd, data+write_len, len-write_len);
            if (sts > 0)
                write_len += sts;

            if (write_len == len)     //数据已全部写完
                return;

            if (-1 == sts && EAGAIN != errno) {      //写数据出现异常，不再监听该文件描述符上的事件
                perror("scheduler_impl::async_write");
                remove_event(ev);
                return;
            }

            //仍有部分数据未写完，在该文件描述符上监听可写事件，在可写时将剩余数据写入
            handle_event(EPOLL_CTL_MOD, ev->fd, EPOLLOUT);
            m_sch->co_yield(0);     //切回主协程处理其他事件
        }

        //所有数据写完之后继续在该文件描述符上监听可读事件
        handle_event(EPOLL_CTL_MOD, ev->fd, EPOLLIN);
    }

    sigctl* scheduler::get_sigctl(std::function<void(int, void*)> f, void *args /*= nullptr*/)
    {
        auto sch_impl = (scheduler_impl*)m_obj;
        if (!sch_impl->m_sigctl) {

        }
        return sch_impl->m_sigctl;
    }

    timer* scheduler::get_timer(std::function<void(void*)> f, void *args /*= nullptr*/)
    {
        auto tmr = new timer;
        tmr->m_obj = new timer_impl;
        auto tmr_impl = (timer_impl*)tmr->m_obj;
        tmr_impl->fd = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK);    //创建一个非阻塞的定时器资源
        if (__glibc_likely(-1 != tmr_impl->fd)) {
            auto sch_impl = (scheduler_impl*)m_obj;
            tmr_impl->co_id = 0;
            tmr_impl->sch_impl = sch_impl;
            tmr_impl->f = tmr_impl->timer_callback;
            tmr_impl->args = tmr_impl;

            tmr_impl->m_f = std::move(f);
            tmr_impl->m_args = args;
            sch_impl->add_event(tmr_impl);      //加入epoll监听事件
        } else {
            perror("scheduler::get_timer");
            delete tmr_impl;
            delete tmr;
            tmr = nullptr;
        }
        return tmr;
    }

    /**
     * 触发定时器回调时首先读文件描述符 fd，读操作将定时器状态切换为已读，若不执行读操作，
     * 则由于epoll采用edge-trigger边沿触发模式，定时器事件再次触发时将不再回调该函数
     */
    void timer_impl::timer_callback(scheduler* sch, eth_event *args)
    {
        auto impl = dynamic_cast<timer_impl*>(args);
        uint64_t cnt;
        read(impl->fd, &cnt, sizeof(cnt));
        impl->m_f(impl->m_args);
    }

    event* scheduler::get_event(std::function<void(int, void*)> f, void *args /*= nullptr*/)
    {
        auto ev = new event;
        ev->m_obj = new event_impl;
        auto ev_impl = (event_impl*)ev->m_obj;
        ev_impl->fd = eventfd(0, EFD_NONBLOCK);			//创建一个非阻塞的事件资源
        if (__glibc_likely(-1 != ev_impl->fd)) {
            auto sch_impl = (scheduler_impl*)m_obj;
            ev_impl->co_id = 0;
            ev_impl->sch_impl = sch_impl;
            ev_impl->f = ev_impl->event_callback;
            ev_impl->args = ev_impl;

            ev_impl->m_f = std::move(f);
            ev_impl->m_args = args;
            sch_impl->add_event(ev_impl);
        } else {
            perror("scheduler::get_event");
            delete ev;
            delete ev_impl;
            ev = nullptr;
        }
        return ev;
    }

    void event_impl::event_callback(scheduler *sch, eth_event *args)
    {
        auto impl = dynamic_cast<event_impl*>(args);
        eventfd_t val;
        eventfd_read(impl->fd, &val);       //读操作将事件重置

        for (auto signal : impl->m_signals)
            impl->m_f(signal, impl->m_args);			//执行事件回调函数
    }

    udp_ins* scheduler::get_udp_ins(bool is_server, uint16_t port,
                                    std::function<void(const std::string&, uint16_t, const char*, size_t, void*)> f,
                                    void *args /*= nullptr*/)
    {
        auto ui = new udp_ins;
        ui->m_obj = new udp_ins_impl;

        auto ui_impl = (udp_ins_impl*)ui->m_obj;
        memset(&ui_impl->m_send_addr, 0, sizeof(ui_impl->m_send_addr));
        ui_impl->m_send_addr.sin_family = AF_INET;
        if (is_server)		//创建server端的udp套接字不需要指明ip地址，若port设置为0，则系统将随机绑定一个可用端口
            ui_impl->fd = ui_impl->m_net_sock.create(PRT_UDP, USR_SERVER, nullptr, port);
        else		//创建client端的udp套接字时不会使用ip地址和端口
            ui_impl->fd = ui_impl->m_net_sock.create(PRT_UDP, USR_CLIENT, "127.0.0.1", port);

        if (__glibc_likely(-1 != ui_impl->fd)) {
            auto sch_impl = (scheduler_impl*)m_obj;
            ui_impl->co_id = 0;
            ui_impl->sch_impl = sch_impl;
            ui_impl->f = ui_impl->udp_ins_callback;
            ui_impl->args = ui_impl;

            ui_impl->m_f = std::move(f);
            ui_impl->m_args = args;
            sch_impl->add_event(ui_impl);
        } else {
            perror("scheduler::get_udp_ins");
            delete ui;
            delete ui_impl;
            ui = nullptr;
        }
        return ui;
    }

    void udp_ins_impl::udp_ins_callback(scheduler *sch, eth_event *args)
    {
        auto impl = dynamic_cast<udp_ins_impl*>(args);
        bzero(&impl->m_recv_addr, sizeof(impl->m_recv_addr));
        impl->m_recv_len = sizeof(impl->m_recv_addr);

        while (true) {
            /**
             * 一个udp包的最大长度为65536个字节，因此在获取数据包的时候将应用层缓冲区大小设置为65536个字节，
             * 一次即可获取一个完整的udp包，同时使用udp传输时不需要考虑粘包的问题
             */
            ssize_t ret = recvfrom(impl->fd, &impl->m_recv_buffer[0], impl->m_recv_buffer.size(),
                                   0, (struct sockaddr*)&impl->m_recv_addr, &impl->m_recv_len);

            if (-1 == ret) {
                if (EAGAIN != errno)
                    perror("udp_ins_callback::recvfrom");
                break;
            }

            impl->m_recv_buffer[ret] = 0;		//字符串以0结尾
            std::string ip_addr = inet_ntoa(impl->m_recv_addr.sin_addr);		//将地址转换为点分十进制格式的ip地址
            uint16_t port = ntohs(impl->m_recv_addr.sin_port);

            //执行回调，将完整的udp数据包传给应用层
            impl->m_f(ip_addr, port, impl->m_recv_buffer.data(), (size_t)ret, impl->m_args);
        }
    }

    void scheduler::register_tcp_hook(bool client, std::function<int(int, char*, size_t, void*)> f,
                                      void *args /*= nullptr*/)
    {
        auto sch_impl = (scheduler_impl*)m_obj;
        if (client) {
            auto tcp_impl = (tcp_client_impl*)sch_impl->m_tcp_client->m_obj;
            tcp_impl->m_protocol_hook = std::move(f);
            tcp_impl->m_protocol_args = args;
        } else {        //server
            auto tcp_impl = (tcp_server_impl*)sch_impl->m_tcp_server->m_obj;
            tcp_impl->m_protocol_hook = std::move(f);
            tcp_impl->m_protocol_args = args;
        }
    }

    tcp_client* scheduler::get_tcp_client(std::function<void(int, const std::string&, uint16_t, char*, size_t, void*)> f,
                                          void *args /*= nullptr*/)
    {
        auto sch_impl = (scheduler_impl*)m_obj;
        if (!sch_impl->m_tcp_client) {
            sch_impl->m_tcp_client = new tcp_client;		//创建一个新的tcp_client
            sch_impl->m_tcp_client->m_obj = new tcp_client_impl;

            auto tcp_impl = (tcp_client_impl*)sch_impl->m_tcp_client->m_obj;
            tcp_impl->m_sch = this;
            tcp_impl->m_tcp_f = std::move(f);			//记录回调函数及参数
            tcp_impl->m_tcp_args = args;
        }
        return sch_impl->m_tcp_client;
    }

    void tcp_client_impl::tcp_client_callback(scheduler *sch, eth_event *ev)
    {
        auto sch_impl = (scheduler_impl*)sch->m_obj;
        auto tcp_impl = (tcp_client_impl*)sch_impl->m_tcp_client->m_obj;
        auto tcp_conn = dynamic_cast<tcp_client_conn*>(ev);

        int sts = sch_impl->async_read(tcp_conn->fd, tcp_conn->stream_buffer);        //读tcp响应流
        handle_tcp_stream(ev->fd, tcp_impl, tcp_conn);

        if (sts <= 0) {     //读文件描述符检测到异常或发现对端已关闭连接
//            if (sts < 0)
//                printf("[tcp_client_impl::tcp_client_callback] 读文件描述符 %d 异常\n", tc_conn->fd);
//            else
//                printf("[tcp_client_impl::tcp_client_callback] 连接 %d 对端正常关闭\n", tc_conn->fd);
            sch_impl->remove_event(tcp_conn);
        }
    }

    tcp_server* scheduler::get_tcp_server(uint16_t port,
                                          std::function<void(int, const std::string&, uint16_t, char*, size_t, void*)> f,
                                          void *args /*= nullptr*/)
    {
        auto sch_impl = (scheduler_impl*)m_obj;
        if (!sch_impl->m_tcp_server) {
            sch_impl->m_tcp_server = new tcp_server;
            sch_impl->m_tcp_server->m_obj = new tcp_server_impl;

            auto tcp_impl = (tcp_server_impl*)sch_impl->m_tcp_server->m_obj;
            tcp_impl->co_id = 0;
            tcp_impl->m_tcp_f = std::move(f);		//保存回调函数及参数
            tcp_impl->m_tcp_args = args;
            tcp_impl->start_listen(sch_impl, port);			//开始监听
        }
        return sch_impl->m_tcp_server;
    }

    void tcp_server_impl::start_listen(scheduler_impl *impl, uint16_t port)
    {
        //创建tcp服务端的监听套接字，允许接收任意ip地址发送的服务请求，监听请求的端口为port
        fd = m_net_sock.create(PRT_TCP, USR_SERVER, nullptr, port);
        sch_impl = impl;
        f = tcp_server_callback;
        args = this;
        impl->add_event(this);      //加入epoll监听事件
    }

    void tcp_server_impl::tcp_server_callback(scheduler *sch, eth_event *ev)
    {
        tcp_server_impl *impl = dynamic_cast<tcp_server_impl*>(ev);
        bzero(&impl->m_accept_addr, sizeof(struct sockaddr_in));
        impl->m_addr_len = sizeof(struct sockaddr_in);
        while (true) {		//接受所有请求连接的tcp客户端
            int client_fd = accept(impl->fd, (struct sockaddr*)&impl->m_accept_addr, &impl->m_addr_len);
            if (-1 == client_fd) {
                if (EAGAIN != errno)
                    perror("tcp_server_callback::accept");
                break;
            }

            setnonblocking(client_fd);			//将客户端连接文件描述符设为非阻塞并加入监听事件
            tcp_server_conn *conn = nullptr;
            switch (impl->m_app_prt) {
                case PRT_NONE:      conn = new tcp_server_conn;     break;
                case PRT_HTTP:		conn = new http_server_conn;    break;
            }

            conn->fd = client_fd;
            conn->f = impl->read_tcp_stream;
            conn->args = conn;

            conn->ip_addr = inet_ntoa(impl->m_accept_addr.sin_addr);	//将地址转换为点分十进制格式的ip地址
            conn->port = ntohs(impl->m_accept_addr.sin_port);           //将端口由网络字节序转换为主机字节序
            auto sch_impl = (scheduler_impl*)sch->m_obj;
            sch_impl->add_event(conn);        //将该连接加入监听事件
        }
    }

    void tcp_server_impl::read_tcp_stream(scheduler *sch, eth_event *ev)
    {
        auto sch_impl = (scheduler_impl*)sch->m_obj;
        auto tcp_impl = (tcp_server_impl*)sch_impl->m_tcp_server->m_obj;
        auto tcp_conn = dynamic_cast<tcp_server_conn*>(ev);

        int sts = sch_impl->async_read(tcp_conn->fd, tcp_conn->stream_buffer);
        handle_tcp_stream(ev->fd, tcp_impl, tcp_conn);

        if (sts <= 0) {		//读文件描述符检测到异常或发现对端已关闭连接
//            if (sts < 0)
//                printf("[tcp_server_impl::read_tcp_stream] 读文件描述符 %d 出现异常", ev->fd);
//            else
//                printf("[tcp_server_impl::read_tcp_stream] 连接 %d 对端正常关闭\n", ev->fd);
            sch_impl->remove_event(tcp_conn);
        }
    }

    http_client* scheduler::get_http_client(std::function<void(int, int, std::unordered_map<std::string, std::string>&, const char*, size_t, void*)> f,
                                            void *args /*= nullptr*/)
    {
        auto sch_impl = (scheduler_impl*)m_obj;
        if (!sch_impl->m_http_client) {
            sch_impl->m_http_client = new http_client;
            sch_impl->m_http_client->m_obj = new http_client_impl;

            auto http_impl = (http_client_impl*)sch_impl->m_http_client->m_obj;
            http_impl->m_sch = this;
            http_impl->m_app_prt = PRT_HTTP;
            http_impl->m_protocol_hook = http_impl->check_http_stream;
            http_impl->m_protocol_args = http_impl;
            http_impl->m_tcp_f = http_impl->protocol_hook;
            http_impl->m_tcp_args = http_impl;
            http_impl->m_http_f = std::move(f);		//保存回调函数及参数
            http_impl->m_http_args = args;
        }
        return sch_impl->m_http_client;
    }

    void http_client_impl::protocol_hook(int fd, const std::string& ip_addr, uint16_t port,
                                         char *data, size_t len, void *arg)
    {
        auto http_impl = (http_client_impl*)arg;
        auto sch_impl = (scheduler_impl*)http_impl->m_sch->m_obj;
        auto conn = dynamic_cast<http_client_conn*>(sch_impl->m_ev_array[fd]);
        http_impl->m_http_f(fd, conn->m_status, conn->m_headers, data, (size_t)conn->m_content_len,
                            http_impl->m_http_args);
        conn->m_content_len = -1;
        conn->m_headers.clear();
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
    int http_client_impl::check_http_stream(int fd, char* data, size_t len, void* arg)
    {
        auto http_impl = (http_client_impl*)arg;
        auto sch_impl = (scheduler_impl*)http_impl->m_sch->m_obj;
        if (fd >= sch_impl->m_ev_array.size() || !sch_impl->m_ev_array[fd])
            return -(int)len;       //检查是否越界以及连接是否有效

        auto conn = dynamic_cast<http_client_conn*>(sch_impl->m_ev_array[fd]);
        if (-1 == conn->m_content_len) {        //与之前的http响应流已完成分包处理，开始处理新的响应
            if (len < 4)        //首先验证流的开始4个字节是否为签名"HTTP"
                return 0;

            if (strncmp(data, "HTTP", 4)) {     //签名出错，该流不是HTTP流
                char *sig_pos = strstr(data, "HTTP");       //查找流中是否存在"HTTP"子串
                if (!sig_pos || sig_pos > data+len-4) {         //未找到子串或找到的子串已超出查找范围
                    if (len > 65536)
                        return -65536;      //若缓存的数据已超过64k，则截断前64k个字节，保障缓冲区不会一直被写入无效数据
                    else
                        return 0;           //缓存数据还不够多，当前状态不够明朗，等待更多数据到来
                } else {        //找到的子串
                    return -(int)(sig_pos-data);    //先截断签名之前多余的数据
                }
            }

            //先判断是否已经获取到完整的响应流
            char *delim_pos = strstr(data, "\r\n\r\n");
            if (!delim_pos || delim_pos > data+len-4) {     //还未取到分隔符或分隔符已超出查找范围
                if (len > 65536)
                    return -65536;      //在HTTP签名和分隔符\r\n\r\n之间已有超过64k的数据，直接截断，保护缓冲
                else
                    return 0;           //等待更多数据到来
            }

            size_t valid_header_len = delim_pos-data;
            auto str_vec = split(data, valid_header_len, "\r\n");
            if (str_vec.size() <= 1)        //HTTP流的头部中包含一个响应行以及至少一个头部字段，头部无效
                return -(int)(valid_header_len+4);

            bool header_err = false;
            for (size_t i = 0; i < str_vec.size(); ++i) {
                auto& line = str_vec[i];
                if (0 == i) {		//首先解析响应行
                    //响应行包含空格分隔的三个字段，例如：HTTP/1.1(协议/版本) 200(状态码) OK(简单描述)
                    auto line_elem = split(line.data(), line.size(), " ");
                    if (3 != line_elem.size()) {		//响应行出错，无效的响应流
                        header_err = true;
                        break;
                    }
                    conn->m_status = std::stoi(line_elem[1]);		//记录中间的状态码
                } else {
                    auto header = split(line.data(), line.size(), ": ");		//头部字段键值对的分隔符为": "
                    if (header.size() >= 2)		//无效的头部字段直接丢弃，不符合格式的值发生截断
                        conn->m_headers[header[0]] = header[1];
                }
            }

            auto it = conn->m_headers.find("Content-Length");
            if (header_err || conn->m_headers.end() == it) {    //头部有误或者未找到"Content-Length"字段，截断该头部
                conn->m_headers.clear();
                return -(int)(valid_header_len+4);
            }
            conn->m_content_len = std::stoi(conn->m_headers["Content-Length"]);
            assert(conn->m_content_len > 0);
            return -(int)(valid_header_len+4);      //先截断http头部
        }

        if (len < conn->m_content_len)      //响应体中不包含足够的数据，等待该连接上更多的数据到来
            return 0;
        else        //已取到响应体，通知上层执行m_tcp_f回调
            return conn->m_content_len;
    }

    http_server* scheduler::get_http_server(uint16_t port,
                                            std::function<void(int, const std::string&, const std::string&, std::unordered_map<std::string, std::string>&,
                                                               const char*, size_t, void*)> f,
                                            void *args /*= nullptr*/)
    {
        auto sch_impl = (scheduler_impl*)m_obj;
        if (!sch_impl->m_http_server) {
            sch_impl->m_http_server = new http_server;
            sch_impl->m_http_server->m_obj = new http_server_impl;

            auto http_impl = (http_server_impl*)sch_impl->m_http_server->m_obj;
            http_impl->co_id = 0;
            http_impl->m_app_prt = PRT_HTTP;
            http_impl->m_protocol_hook = http_impl->check_http_stream;
            http_impl->m_protocol_args = http_impl;
            http_impl->m_tcp_f = http_impl->protocol_hook;
            http_impl->m_tcp_args = http_impl;
            http_impl->m_http_f = std::move(f);
            http_impl->m_http_args = args;
            http_impl->start_listen(sch_impl, port);		//开始监听
        }
        return sch_impl->m_http_server;
    }

    void http_server_impl::protocol_hook(int fd, const std::string& ip_addr, uint16_t port,
                                         char *data, size_t len, void *arg)
    {
        auto http_impl = (http_server_impl*)arg;
        auto sch_impl = http_impl->sch_impl;
        auto conn = dynamic_cast<http_server_conn*>(sch_impl->m_ev_array[fd]);

        const char *cb_data = nullptr;
        size_t cb_len = 0;
        if (len != 1 || '\n' != *data) {     //请求流中包含请求体
            cb_data = data;
            cb_len = (size_t)conn->m_content_len;
        }

        http_impl->m_http_f(fd, conn->m_method, conn->m_url, conn->m_headers,
                            cb_data, cb_len, http_impl->m_http_args);
        conn->m_content_len = -1;
        conn->m_headers.clear();
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
    int http_server_impl::check_http_stream(int fd, char* data, size_t len, void* arg)
    {
        auto http_impl = (http_server_impl*)arg;
        auto sch_impl = http_impl->sch_impl;
        if (fd >= sch_impl->m_ev_array.size() || !sch_impl->m_ev_array[fd])
            return -(int)len;       //检查是否越界以及连接是否有效

        auto conn = dynamic_cast<http_server_conn*>(sch_impl->m_ev_array[fd]);
        if (-1 == conn->m_content_len) {        //与之前的http请求流已完成分包处理，开始处理新的请求
            char *delim_pos = strstr(data, "\r\n\r\n");
            if (!delim_pos || delim_pos > data+len-4) {     //还未取到分隔符或已超出查找范围
                if (len > 65536)
                    return -65536;      //在取到分隔符之前已有超过64k的数据，截断已保护缓冲
                else
                    return 0;           //等待接受更多的数据
            }

            char *sig_pos = strstr(data, "HTTP");     //在完整的请求头中查找是否存在"HTTP"签名
            if (!sig_pos || sig_pos > data+len-4) {         //未取到签名或已超出查找范围
                if (len > 65536)
                    return -65536;      //在取到该签名之前已有超过64k的数据，截断
                else
                    return 0;
            }

            size_t valid_header_len = delim_pos-data;
            auto str_vec = split(data, valid_header_len, "\r\n");
            if (str_vec.size() <= 1)        //一次HTTP请求中包含一个请求行以及至少一个头部字段
                return -(int)(valid_header_len+4);

            bool header_err = false;
            for (size_t i = 0; i < str_vec.size(); ++i) {
                auto line = str_vec[i];
                if (0 == i) {		//首先解析请求行
                    //请求行包含空格分隔的三个字段，例如：POST(请求方法) /request(url) HTTP/1.1(协议/版本)
                    auto line_elem = split(line.data(), line.size(), " ");
                    if (3 != line_elem.size()) {		//请求行出错，无效的请求流
                        header_err = true;
                        break;
                    }

                    conn->m_method = line_elem[0];          //记录请求方法
                    conn->m_url = line_elem[1];             //记录请求的url(以"/"开始的部分)
                } else {
                    auto header = split(line.data(), line.size(), ": ");        //头部字段键值对的分隔符为": "
                    if (header.size() >= 2)     //无效的头部字段直接丢弃，不符合格式的值发生截断
                        conn->m_headers[header[0]] = header[1];
                }
            }

            if (header_err) {       //头部有误
                conn->m_headers.clear();
                return -(int)(valid_header_len+4);
            }

            int ret = 0;
            auto it = conn->m_headers.find("Content-Length");
            if (conn->m_headers.end() == it) {      //有些请求流不存在请求体，此时头部中不包含"Content-Length"字段
                //just a trick，返回0表示等待更多的数据，如果需要上层执行m_tcp_f回调必须返回大于0的值，因此在完成一次分包之后，
                //若没有请求体，可以将content_len设置为1，但只截断分隔符"\r\n\r\n"中的前三个字符，而将最后一个字符作为请求体
                conn->m_content_len = 1;
                ret = -(int)(valid_header_len+3);
            } else {
                conn->m_content_len = std::stoi(it->second);
                ret = -(int)(valid_header_len+4);
            }
            assert(conn->m_content_len > 0);    //此时已正确获取到当前请求中的请求体信息
            return ret;     //先截断http头部
        }

        if (len < conn->m_content_len)      //请求体中不包含足够的数据，等待该连接上更多的数据到来
            return 0;
        else        //已取到请求体，通知上层执行m_tcp_f回调
            return conn->m_content_len;
    }

    fs_monitor* scheduler::get_fs_monitor(std::function<void(const char*, uint32_t, void *args)> f,
                                          void *args /*= nullptr*/)
    {
        auto sch_impl = (scheduler_impl*)m_obj;
        if (!sch_impl->m_fs_monitor) {
            sch_impl->m_fs_monitor = new fs_monitor;
            sch_impl->m_fs_monitor->m_obj = new fs_monitor_impl;

            auto monitor_impl = (fs_monitor_impl*)sch_impl->m_fs_monitor->m_obj;
            monitor_impl->co_id = 0;
            monitor_impl->fd = inotify_init1(IN_NONBLOCK);
            monitor_impl->sch_impl = sch_impl;
            monitor_impl->f = monitor_impl->fs_monitory_callback;
            monitor_impl->args = monitor_impl;

            monitor_impl->m_monitor_f = std::move(f);
            monitor_impl->m_monitor_args = args;
            sch_impl->add_event(monitor_impl);
        }
        return sch_impl->m_fs_monitor;
    }

    void fs_monitor_impl::fs_monitory_callback(scheduler *sch, eth_event *ev)
    {
        fs_monitor_impl *impl = dynamic_cast<fs_monitor_impl*>(ev);
        char buf[1024] = {0};
        int filter = 0, offset = 0;
        while (true) {
            ssize_t len = read(impl->fd, buf+offset, sizeof(buf)-offset);
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

    void fs_monitor_impl::recursive_monitor(const std::string& root_dir, bool add, uint32_t mask)
    {
        DIR *dir = opendir(root_dir.c_str());
        if (__glibc_unlikely(!dir)) {
            perror("recursive_monitor::opendir failed");
            return;
        }

        struct stat st;
        struct dirent *ent = nullptr;
        while ((ent = readdir(dir))) {
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

    void fs_monitor_impl::trigger_event(bool add, int watch_id, const std::string& path, bool recur_flag, uint32_t mask)
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
}