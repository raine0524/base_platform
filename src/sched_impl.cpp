#include "stdafx.h"

namespace crx
{
    scheduler::scheduler()
    {
        auto impl = std::make_shared<scheduler_impl>();
        impl->m_cos.reserve(64);        //预留64个协程
        impl->m_ev_array.reserve(128);  //预留128个epoll事件
        m_impl = std::move(impl);
    }

    scheduler::~scheduler()
    {
        auto impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        if (-1 != impl->m_epoll_fd)
            close(impl->m_epoll_fd);
    }

    size_t scheduler::co_create(std::function<void(scheduler *sch)> f, bool is_share /*= false*/, const char *comment /*= nullptr*/)
    {
        auto impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        return impl->co_create(f, this, false, is_share, comment);
    }

    bool scheduler::co_yield(size_t co_id, SUS_TYPE type /*= WAIT_EVENT*/)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        if (co_id >= sch_impl->m_cos.size())
            return false;

        if (sch_impl->m_running_co == co_id)        //co_id无效或对自身进行切换，直接返回
            return true;

        auto yield_co = sch_impl->m_cos[co_id];
        if (!yield_co || CO_UNKNOWN == yield_co->status || CO_RUNNING == yield_co->status)
            return false;               //指定协程无效或者状态指示不可用，同样不发生切换

        auto curr_co = sch_impl->m_cos[sch_impl->m_running_co];
        sch_impl->m_running_co = (int)co_id;

        auto main_co = sch_impl->m_cos[0];
        if (curr_co->is_share)      //当前协程使用的是共享栈模式，其使用的栈是主协程中申请的空间
            sch_impl->save_stack(curr_co, main_co->stack.data()+STACK_SIZE);

        curr_co->status = CO_SUSPEND;
        curr_co->type = type;

        //当前协程与待切换的协程都使用了共享栈
        if (curr_co->is_share && yield_co->is_share && CO_SUSPEND == yield_co->status) {
            sch_impl->m_next_co = (int)co_id;
            swapcontext(&curr_co->ctx, &main_co->ctx);      //先切换回主协程
        } else {
            //待切换的协程使用的是共享栈模式并且当前处于挂起状态，恢复其栈空间至主协程的缓冲区中
            if (yield_co->is_share && CO_SUSPEND == yield_co->status)
                memcpy(&main_co->stack[0]+STACK_SIZE-yield_co->size, yield_co->stack.data(), yield_co->size);

            sch_impl->m_next_co = -1;
            yield_co->status = CO_RUNNING;
            swapcontext(&curr_co->ctx, &yield_co->ctx);
        }

        //此时位于主协程中，且主协程用于帮助从一个使用共享栈的协程切换到另一个使用共享栈的协程中
        if (-1 != sch_impl->m_next_co) {
            auto next_co = sch_impl->m_cos[sch_impl->m_next_co];
            memcpy(&main_co->stack[0]+STACK_SIZE-next_co->size, next_co->stack.data(), next_co->size);
            next_co->status = CO_RUNNING;
            swapcontext(&main_co->ctx, &next_co->ctx);
        }
        return true;
    }

    std::vector<std::shared_ptr<coroutine>> scheduler::get_avail_cos()
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        std::vector<std::shared_ptr<coroutine>> cos;
        for (auto& co_impl : sch_impl->m_cos)
            if (CO_UNKNOWN != co_impl->status)
                cos.push_back(co_impl);
        return cos;
    }

    size_t scheduler_impl::co_create(std::function<void(scheduler *sch)>& f, scheduler *sch,
                                     bool is_main_co, bool is_share /*= false*/, const char *comment /*= nullptr*/)
    {
        std::shared_ptr<coroutine_impl> co_impl;
        if (m_unused_cos.empty()) {
            co_impl = std::make_shared<coroutine_impl>();
            co_impl->co_id = m_cos.size();
            m_cos.push_back(co_impl);
        } else {        //复用之前已创建的协程
            std::pop_heap(m_unused_cos.begin(), m_unused_cos.end(), std::greater<size_t>());
            size_t co_id = m_unused_cos.back();
            m_unused_cos.pop_back();

            co_impl = m_cos[co_id];                 //复用时其id不变
            if (co_impl->is_share != is_share)      //复用时堆栈的使用模式改变
                co_impl->stack.clear();
        }

        co_impl->status = is_main_co ? CO_RUNNING : CO_READY;
        co_impl->is_share = is_share;
        if (!is_share)      //不使用共享栈时将在协程创建的同时创建协程所用的栈
            co_impl->stack.resize(STACK_SIZE);

        if (comment) {
            bzero(co_impl->comment, sizeof(co_impl->comment));
            strcpy(co_impl->comment, comment);
        }
        co_impl->f = std::move(f);

        bzero(&co_impl->ctx, sizeof(co_impl->ctx));
        if (!is_main_co) {     //主协程先于其他所有协程被创建
            auto main_co = m_cos[0];
            getcontext(&co_impl->ctx);
            if (is_share)
                co_impl->ctx.uc_stack.ss_sp = &main_co->stack[0];
            else
                co_impl->ctx.uc_stack.ss_sp = &co_impl->stack[0];
            co_impl->ctx.uc_stack.ss_size = STACK_SIZE;
            co_impl->ctx.uc_link = &main_co->ctx;

            auto ptr = (uint64_t)sch;
            makecontext(&co_impl->ctx, (void (*)())coroutine_wrap, 2, (uint32_t)ptr, (uint32_t)(ptr>>32));
        }
        return co_impl->co_id;
    }

    void scheduler_impl::save_stack(std::shared_ptr<coroutine_impl>& co_impl, const char *top)
    {
        char stub = 0;
        assert(top-&stub <= STACK_SIZE);
        if (co_impl->stack.size() < top-&stub)      //容量不足
            co_impl->stack.resize(top-&stub);

        co_impl->size = top-&stub;
        memcpy(&co_impl->stack[0], &stub, co_impl->size);
    }

    void scheduler_impl::coroutine_wrap(uint32_t low32, uint32_t hi32)
    {
        uintptr_t this_ptr = ((uintptr_t)hi32 << 32) | (uintptr_t)low32;
        auto sch = (scheduler*)this_ptr;
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(sch->m_impl);
        auto co_impl = sch_impl->m_cos[sch_impl->m_running_co];
        co_impl->f(sch);

        //协程执行完成后退出，此时处于不可用状态，进入未使用队列等待复用
        sch_impl->m_running_co = 0;
        sch_impl->m_cos[0]->status = CO_RUNNING;
        co_impl->status = CO_UNKNOWN;
        sch_impl->m_unused_cos.push_back(co_impl->co_id);
        std::push_heap(sch_impl->m_unused_cos.begin(), sch_impl->m_unused_cos.end(), std::greater<size_t>());
    }

    void scheduler_impl::main_coroutine(scheduler *sch)
    {
        std::vector<epoll_event> events(EPOLL_SIZE);
        m_go_done = true;
        while (m_go_done) {
            int cnt = epoll_wait(m_epoll_fd, &events[0], EPOLL_SIZE, 10);		//时间片设置为10ms，与Linux内核使用的时间片相近

            if (-1 == cnt) {			//epoll_wait有可能因为中断操作而返回，然而此时并没有任何监听事件触发
                perror("main_coroutine::epoll_wait");
                continue;
            }

            if (cnt == EPOLL_SIZE)
                printf("[main_coroutine::epoll_wait WARN] 当前待处理的事件达到监听队列的上限 (%d)！\n", cnt);

            size_t i = 0;
            for (; i < cnt; ++i) {      //处理已触发的事件
                int fd = events[i].data.fd;
                auto ev = m_ev_array[fd];
                if (ev)
                    ev->f();
            }

            i = 1;
            while (i < m_cos.size()) {
                if (CO_SUSPEND == m_cos[i]->status && HAVE_REST == m_cos[i]->type)
                    sch->co_yield(i);
                ++i;
            }

            //当前未使用的协程数量过多(超过总量的3/4)，回收一部分资源(当前总量的1/2)
            size_t total_cos = m_cos.size();
            if (total_cos > 64 && m_unused_cos.size() >= 3*total_cos/4.0) {
                for (i = 0; i < total_cos/2; ++i) {
                    auto last_co = m_cos.back();
                    if (CO_UNKNOWN == last_co->status)      //从后往前释放资源，当最后一个协程无效时才释放该资源
                        m_cos.pop_back();
                    else
                        break;
                }

                //重新建立未使用co_id的最小堆
                m_unused_cos.clear();
                for (auto co : m_cos)
                    if (CO_UNKNOWN == co->status)
                        m_unused_cos.push_back(co->co_id);
                std::make_heap(m_unused_cos.begin(), m_unused_cos.end(), std::greater<size_t>());
            }
        }

        auto& impl = m_util_impls[SIMP_SVR];
        if (impl) {
            auto simp_impl = std::dynamic_pointer_cast<simpack_server_impl>(impl);
            simp_impl->stop();
        }
    }

    void scheduler_impl::handle_event(int op, int fd, uint32_t event)
    {
        struct epoll_event ev = {0};
        ev.events = event | EPOLLET;    //每个读/写事件都采用edge-trigger(边沿触发)的模式
        ev.data.fd = fd;

        if (-1 == epoll_ctl(m_epoll_fd, op, fd, &ev))
            perror("handle_event::epoll_ctl");
    }

    void scheduler_impl::flush_log_buffer()
    {
        for (size_t i = IDX_MAX; i < m_util_impls.size(); ++i) {
            auto impl = std::dynamic_pointer_cast<log_impl>(m_util_impls[i]);
            fflush(impl->m_fp);
        }

        //周期性的刷日志
        m_sec_wheel.add_handler(3*1000, std::bind(&scheduler_impl::flush_log_buffer, this));
    }

    void scheduler_impl::add_event(std::shared_ptr<eth_event> ev, uint32_t event /*= EPOLLIN*/)
    {
        if (ev) {
            if (ev->fd >= m_ev_array.size())
                m_ev_array.resize((size_t)ev->fd+1, nullptr);

            handle_event(EPOLL_CTL_ADD, ev->fd, event);
            m_ev_array[ev->fd] = std::move(ev);
        }
    }

    /*
     * 移除事件对象时，对内存资源的释放进行优化较为困难，本质上是因为事件指针可能指向多个不同的继承对象，在复用该内存区域时
     * 前后两个实际对象并不一致，此时进行针对性的优化意义不大，直接使用系统的一般化内存管理方案即可
     */
    void scheduler_impl::remove_event(int fd)
    {
        if (fd < 0 || fd >= m_ev_array.size())
            return;

        auto& ev = m_ev_array[fd];
        if (ev) {
            handle_event(EPOLL_CTL_DEL, fd, EPOLLIN);
            if (fd == m_ev_array.size()-1)
                m_ev_array.resize((size_t)fd);
            else
                m_ev_array[fd].reset();
        }
    }

    //异步读的一个原则是只要可读就一直读，直到errno被置为EAGAIN提示等待更多数据可读
    int eth_event::async_read(std::string& read_str)
    {
        size_t read_size = read_str.size();
        while (true) {
            read_str.resize(read_size+1024);
            ssize_t ret = read(fd, &read_str[read_size], 1024);
            read_size = read_str.size();
            if (-1 == ret) {
                read_str.resize(read_size-1024);        //本次循环未读到任何数据
                if (EAGAIN == errno) {		//等待缓冲区可读
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

    //[注：只有tcp套接字才需要异步写操作] 对端关闭或发生异常的情况下，待写数据仍然缓存，因为可能尝试重连
    int tcp_event::async_write(const char *data, size_t len)
    {
        if (!is_connect) {
            cache_data.push_back(std::string(data, len));
            return 1;
        }

        int sts = INT_MAX;      //正常写入且缓冲未满
        if (!cache_data.empty()) {      //先写所有的缓存数据
            for (auto it = cache_data.begin(); it != cache_data.end(); ) {
                ssize_t ret = write(fd, it->c_str(), it->size());
                if (-1 == ret) {
                    if (EAGAIN == errno) {              //等待缓冲区可写
                        sts = 1;
                        break;
                    } else if (EPIPE == errno) {        //对端关闭
                        sts = 0;
                    } else {        //发生异常
                        perror("tcp_event::async_write");
                        sts = -1;
                    }

                    if (data && len)
                        cache_data.push_back(std::string(data, len));
                    return sts;
                } else if (ret < it->size()) {      //写了一部分，等待缓冲区可写
                    if (ret > 0)
                        it->erase(0, (size_t)ret);
                    sts = 1;
                    break;
                } else {        //当前数据块全部写完
                    it = cache_data.erase(it);
                }
            }
        }

        if (data && len) {      //存在待写数据
            if (INT_MAX == sts) {       //正常写入且缓冲未满
                ssize_t ret = write(fd, data, len);
                if (-1 == ret) {
                    if (EAGAIN == errno) {      //等待缓冲区可写，此时缓存待写所有数据
                        sts = 1;
                    } else if (EPIPE == errno) {        //对端关闭
                        sts = 0;
                    } else {        //发生异常
                        perror("tcp_event::async_write");
                        sts = -1;
                    }

                    cache_data.push_back(std::string(data, len));
                    if (sts <= 0)
                        return sts;
                } else if (ret < len) {     //缓存未写入部分
                    cache_data.push_back(std::string(data+ret, len-ret));
                    sts = 1;
                } else {
                    //全部写完
                }
            } else {        //缓冲已满
                cache_data.push_back(std::string(data, len));
            }
        }

        if (EPOLLIN == event && 1 == sts) {     //当前监听的是可读事件且写缓冲已满，监听可写
            event = EPOLLOUT;
            sch_impl.lock()->handle_event(EPOLL_CTL_MOD, fd, EPOLLOUT);
        } else if (EPOLLOUT == event && INT_MAX == sts) {       //当前监听可写且已全部写完，监听可读
            event = EPOLLIN;
            sch_impl.lock()->handle_event(EPOLL_CTL_MOD, fd, EPOLLIN);
        }
        return sts;
    }

    int simpack_protocol(char *data, size_t len)
    {
        if (len < sizeof(simp_header))      //还未取到simp协议头
            return 0;

        uint32_t magic_num = ntohl(*(uint32_t *)data);
        if (0x5f3759df != magic_num) {
            size_t i = 1;
            for (; i < len; ++i) {
                magic_num = ntohl(*(uint32_t*)(data+i));
                if (0x5f3759df == magic_num)
                    break;
            }

            if (i < len)        //在后续流中找到该魔数，截断之前的无效流
                return -(int)i;
            else        //未找到，截断整个无效流
                return -(int)len;
        }

        auto header = (simp_header*)data;
        int ctx_len = ntohl(header->length)+sizeof(simp_header);

        if (len < ctx_len)
            return 0;
        else
            return ctx_len;
    }

    sigctl scheduler::get_sigctl()
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto& impl = sch_impl->m_util_impls[SIG_CTL];
        if (!impl) {
            auto sig_impl = std::make_shared<sigctl_impl>();
            impl = sig_impl;

            sig_impl->fd = signalfd(-1, &sig_impl->m_mask, SFD_NONBLOCK);
            sig_impl->f = std::bind(&sigctl_impl::sigctl_callback, sig_impl.get());
            sig_impl->sch_impl = sch_impl;
            sch_impl->add_event(sig_impl);
        }

        sigctl obj;
        obj.m_impl = impl;
        return obj;
    }

    void sigctl_impl::sigctl_callback()
    {
        int st_size = sizeof(m_fd_info);
        while (true) {
            int64_t ret = read(fd, &m_fd_info, (size_t)st_size);
            if (ret == st_size) {
                auto it = m_sig_cb.find(m_fd_info.ssi_signo);
                if (m_sig_cb.end() != it)
                    it->second(m_fd_info.ssi_ptr);
            } else {
                if (-1 == ret && EAGAIN != errno)
                    perror("sigctl_callback");
                else if (ret > 0)
                    fprintf(stderr, "invalid `signalfd_siginfo` size: %ld\n", ret);
                break;      //file closed & wait more data is normal
            }
        }
    }

    //添加/删除信号文件描述符相关的信号掩码值时，要使新的信号集合生效，必须要重新添加该epoll上监听的signalfd
    void sigctl_impl::handle_sig(int signo, bool add)
    {
        auto impl = sch_impl.lock();
        impl->handle_event(EPOLL_CTL_DEL, fd, EPOLLIN);
        if (add)
            sigaddset(&m_mask, signo);
        else
            sigdelset(&m_mask, signo);
        sigprocmask(SIG_SETMASK, &m_mask, nullptr);
        signalfd(fd, &m_mask, SFD_NONBLOCK);
        impl->handle_event(EPOLL_CTL_ADD, fd, EPOLLIN);
    }

    timer scheduler::get_timer(std::function<void()> f)
    {
        auto tmr_impl = std::make_shared<timer_impl>();
        tmr_impl->fd = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK);    //创建一个非阻塞的定时器资源
        if (__glibc_likely(-1 != tmr_impl->fd)) {
            auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
            tmr_impl->sch_impl = sch_impl;
            tmr_impl->f = std::bind(&timer_impl::timer_callback, tmr_impl.get());

            tmr_impl->m_f = std::move(f);
            sch_impl->add_event(tmr_impl);      //加入epoll监听事件
        } else {
            perror("scheduler::get_timer");
            tmr_impl.reset();
        }

        timer tmr;
        tmr.m_impl = tmr_impl;
        return tmr;
    }

    /*
     * 触发定时器回调时首先读文件描述符 fd，读操作将定时器状态切换为已读，若不执行读操作，
     * 则由于epoll采用edge-trigger边沿触发模式，定时器事件再次触发时将不再回调该函数
     */
    void timer_impl::timer_callback()
    {
        uint64_t cnt;
        read(fd, &cnt, sizeof(cnt));
        m_f();
    }

    timer_wheel scheduler::get_timer_wheel(uint64_t interval, size_t slot)
    {
        auto tw_impl = std::make_shared<timer_wheel_impl>();
        tw_impl->m_timer = get_timer(std::bind(&timer_wheel_impl::timer_wheel_callback, tw_impl.get()));
        if (tw_impl->m_timer.m_impl) {
            tw_impl->m_timer.start(interval, interval);
            tw_impl->m_slots.resize(slot);
        } else {
            printf("系统定时器获取失败\n");
            tw_impl.reset();
        }

        timer_wheel tw;
        tw.m_impl = tw_impl;
        return tw;
    }

    void timer_wheel_impl::timer_wheel_callback()
    {
        m_slot_idx = (m_slot_idx+1)%m_slots.size();
        for (auto& f : m_slots[m_slot_idx])
            f();
        m_slots[m_slot_idx].clear();
    }

    event scheduler::get_event(std::function<void(int)> f)
    {
        auto ev_impl = std::make_shared<event_impl>();
        ev_impl->fd = eventfd(0, EFD_NONBLOCK);			//创建一个非阻塞的事件资源
        if (__glibc_likely(-1 != ev_impl->fd)) {
            auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
            ev_impl->f = std::bind(&event_impl::event_callback, ev_impl.get());
            ev_impl->sch_impl = sch_impl;

            ev_impl->m_f = std::move(f);
            sch_impl->add_event(ev_impl);
        } else {
            perror("scheduler::get_event");
            ev_impl.reset();
        }

        event ev;
        ev.m_impl = ev_impl;
        return ev;
    }

    void event_impl::event_callback()
    {
        eventfd_t val;
        eventfd_read(fd, &val);       //读操作将事件重置

        for (auto signal : m_signals)
            m_f(signal);			//执行事件回调函数
    }

    udp_ins scheduler::get_udp_ins(bool is_server, uint16_t port,
                                   std::function<void(const std::string&, uint16_t, char*, size_t)> f)
    {
        auto ui_impl = std::make_shared<udp_ins_impl>();
        ui_impl->m_send_addr.sin_family = AF_INET;
        if (is_server)      //创建server端的udp套接字不需要指明ip地址，若port设置为0，则系统将随机绑定一个可用端口
            ui_impl->fd = ui_impl->m_net_sock.create(PRT_UDP, USR_SERVER, nullptr, port);
        else                //创建client端的udp套接字时不会使用ip地址和端口
            ui_impl->fd = ui_impl->m_net_sock.create(PRT_UDP, USR_CLIENT, "127.0.0.1", port);

        if (__glibc_likely(-1 != ui_impl->fd)) {
            auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
            ui_impl->f = std::bind(&udp_ins_impl::udp_ins_callback, ui_impl.get());
            ui_impl->sch_impl = sch_impl;

            ui_impl->m_f = std::move(f);
            sch_impl->add_event(ui_impl);
        } else {
            perror("scheduler::get_udp_ins");
            ui_impl.reset();
        }

        udp_ins ui;
        ui.m_impl = ui_impl;
        return ui;
    }

    void udp_ins_impl::udp_ins_callback()
    {
        bzero(&m_recv_addr, sizeof(m_recv_addr));
        m_recv_len = sizeof(m_recv_addr);
        while (true) {
            /*
             * 一个udp包的最大长度为65536个字节，因此在获取数据包的时候将应用层缓冲区大小设置为65536个字节，
             * 一次即可获取一个完整的udp包，同时使用udp传输时不需要考虑粘包的问题
             */
            ssize_t ret = recvfrom(fd, &m_recv_buffer[0], m_recv_buffer.size(), 0,
                    (struct sockaddr*)&m_recv_addr, &m_recv_len);

            if (-1 == ret) {
                if (EAGAIN != errno)
                    perror("udp_ins_callback::recvfrom");
                break;
            }

            m_recv_buffer[ret] = 0;		//字符串以0结尾
            std::string ip_addr = inet_ntoa(m_recv_addr.sin_addr);		//将地址转换为点分十进制格式的ip地址
            uint16_t port = ntohs(m_recv_addr.sin_port);

            //执行回调，将完整的udp数据包传给应用层
            m_f(ip_addr, port, &m_recv_buffer[0], (size_t)ret);
        }
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

    tcp_client scheduler::get_tcp_client(std::function<void(int, const std::string&, uint16_t, char*, size_t)> f)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto& impl = sch_impl->m_util_impls[TCP_CLI];
        if (!impl) {
            auto tcp_impl = std::make_shared<tcp_client_impl>();
            impl = tcp_impl;
            tcp_impl->m_util.m_sch = this;
            tcp_impl->m_util.m_f = std::move(f);			//记录回调函数及参数
        }

        tcp_client obj;
        obj.m_impl = impl;
        return obj;
    }

    void tcp_client_conn::tcp_client_callback()
    {
        int sts = 0;
        if (!is_connect) {
            sts = async_read(stream_buffer);        //首次回调时先通过读获取当前套接字的状态
            if (sts > 0) {      //连接成功
                tcp_impl->m_util.m_f(fd, ip_addr, conn_sock.m_port, nullptr, (size_t)last_conn);       //通知上层连接开启
                is_connect = true;
                sts = async_write(nullptr, 0);
            }
        } else {
            if (EPOLLIN == event) {
                sts = async_read(stream_buffer);        //读tcp响应流
                handle_stream(fd, this);
            } else {        //EPOLLOUT == event
                sts = async_write(nullptr, 0);
            }
        }

        if (sts <= 0) {     //读文件描述符检测到异常或发现对端已关闭连接
//            if (sts < 0)
//                printf("[tcp_client_conn::tcp_client_callback] 读文件描述符 %d 异常\n", fd);
//            else
//                printf("[tcp_client_conn::tcp_client_callback] 连接 %d 对端正常关闭\n", fd);

            if (-1 == retry || (retry > 0 && cnt < retry)) {        //若不断尝试重连或重连次数还未满足要求，则不释放资源
                if (is_connect) {       //已处于连接状态
                    tcp_client client;
                    client.m_impl = tcp_impl;
                    int conn = client.connect(ip_addr.c_str(), conn_sock.m_port, retry, timeout);       //创建一个新的tcp套接字
                    auto tcp_conn = std::dynamic_pointer_cast<tcp_client_conn>(sch_impl.lock()->m_ev_array[conn]);

                    //将当前缓存数据及扩展数据移入新的tcp_event上
                    tcp_conn->cache_data = std::move(cache_data);
                    tcp_conn->ext_data = std::move(ext_data);
                    tcp_conn->last_conn = fd;
                } else {
                    tcp_impl->m_util.m_timer_wheel.add_handler((uint64_t)timeout*1000,
                                                               std::bind(&tcp_client_conn::retry_connect, this));
                }
            }

            if (is_connect) {       //该套接字已经成功连接过对端，此时只能移除该套接字，无法复用
                tcp_impl->m_util.m_f(fd, ip_addr, conn_sock.m_port, nullptr, 0);       //通知上层连接关闭
                sch_impl.lock()->remove_event(fd);
            }
        }
    }

    void tcp_client_conn::retry_connect()
    {
        if (-1 == connect(conn_sock.m_sock_fd, (struct sockaddr*)&conn_sock.m_addr,
                sizeof(conn_sock.m_addr)) && EINPROGRESS != errno)
            perror("retry_connect");
        cnt = retry > 0 ? (cnt+1) : cnt;
        printf("尝试重连 [%d] ip=%s, port=%d, timeout=%d\n", fd, ip_addr.c_str(), conn_sock.m_port, timeout);
    }

    tcp_server scheduler::get_tcp_server(uint16_t port,
                                         std::function<void(int, const std::string&, uint16_t, char*, size_t)> f)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto& impl = sch_impl->m_util_impls[TCP_SVR];
        if (!impl) {
            auto tcp_impl = std::make_shared<tcp_server_impl>();
            impl = tcp_impl;

            tcp_impl->m_util.m_sch = this;
            tcp_impl->m_util.m_f = std::move(f);		//保存回调函数

            //创建tcp服务端的监听套接字，允许接收任意ip地址发送的服务请求，监听请求的端口为port
            tcp_impl->fd = tcp_impl->conn_sock.create(PRT_TCP, USR_SERVER, nullptr, port);
            tcp_impl->sch_impl = sch_impl;
            tcp_impl->f = std::bind(&tcp_server_impl::tcp_server_callback, tcp_impl.get());
            sch_impl->add_event(tcp_impl);
        }

        tcp_server obj;
        obj.m_impl = impl;
        return obj;
    }

    void tcp_server_impl::tcp_server_callback()
    {
        bzero(&m_accept_addr, sizeof(struct sockaddr_in));
        m_addr_len = sizeof(struct sockaddr_in);
        while (true) {		//接受所有请求连接的tcp客户端
            int client_fd = accept(fd, (struct sockaddr*)&m_accept_addr, &m_addr_len);
            if (-1 == client_fd) {
                if (EAGAIN != errno)
                    perror("tcp_server_callback::accept");
                break;
            }

            setnonblocking(client_fd);			//将客户端连接文件描述符设为非阻塞并加入监听事件
            std::shared_ptr<tcp_server_conn> conn;
            switch (m_util.m_app_prt) {
                case PRT_NONE:
                case PRT_SIMP:      conn = std::make_shared<tcp_server_conn>();                 break;
                case PRT_HTTP:		conn = std::make_shared<http_conn_t<tcp_server_conn>>();    break;
            }

            conn->fd = client_fd;
            conn->f = std::bind(&tcp_server_conn::read_tcp_stream, conn.get());
            conn->sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_util.m_sch->m_impl);

            conn->tcp_impl = this;
            conn->is_connect = true;
            conn->ip_addr = inet_ntoa(m_accept_addr.sin_addr);        //将地址转换为点分十进制格式的ip地址
            conn->conn_sock.m_port = ntohs(m_accept_addr.sin_port);   //将端口由网络字节序转换为主机字节序
            conn->conn_sock.m_sock_fd = client_fd;

            /*
             * 1分钟之后若信道上没有数据传输则发送tcp心跳包，总共发3次，每次间隔为5秒，因此若有套接字处于异常状态(TIME_WAIT/CLOSE_WAIT)
             * 则将在75秒之后关闭该套接字，以释放系统资源提供复用能力，但是tcp本身的keepalive仍然会存在问题，即当socket处于异常状态时
             * 应用层同样有数据需要重传时，tcp的keepalive将会无效
             * 具体参见链接描述: https://blog.csdn.net/ctthuangcheng/article/details/8596818
             */
            conn->conn_sock.set_keep_alive(1, 60, 5, 3);

            auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_util.m_sch->m_impl);
            sch_impl->add_event(conn);        //将该连接加入监听事件
        }
    }

    void tcp_server_conn::read_tcp_stream()
    {
        int sts = 0;
        if (EPOLLIN == event) {
            sts = async_read(stream_buffer);
            handle_stream(fd, this);
        } else {        //EPOLLOUT == event
            sts = async_write(nullptr, 0);
        }

        if (sts <= 0) {     //读文件描述符检测到异常或发现对端已关闭连接
//            if (sts < 0)
//                printf("[tcp_server_impl::read_tcp_stream] 读文件描述符 %d 出现异常", fd);
//            else
//                printf("[tcp_server_impl::read_tcp_stream] 连接 %d 对端正常关闭\n", fd);
            tcp_impl->m_util.m_f(fd, ip_addr, conn_sock.m_port, nullptr, 0);
            sch_impl.lock()->remove_event(fd);
        }
    }

    http_client scheduler::get_http_client(std::function<void(int, int, std::unordered_map<std::string, const char*>&, char*, size_t)> f)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto& impl = sch_impl->m_util_impls[HTTP_CLI];
        if (!impl) {
            auto http_impl = std::make_shared<http_impl_t<tcp_client_impl>>();
            impl = http_impl;

            http_impl->m_util.m_sch = this;
            http_impl->m_util.m_app_prt = PRT_HTTP;
            http_impl->m_util.m_protocol_hook = [this](int fd, char* data, size_t len) {
                auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
                auto conn = std::dynamic_pointer_cast<http_conn_t<tcp_client_conn>>(sch_impl->m_ev_array[fd]);
                return http_parser(true, conn, data, len);
            };
            http_impl->m_util.m_f = [this](int fd, const std::string& ip_addr, uint16_t port, char *data, size_t len) {
                auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
                auto http_impl = std::dynamic_pointer_cast<http_impl_t<tcp_client_impl>>(sch_impl->m_util_impls[HTTP_CLI]);
                tcp_callback_for_http<std::shared_ptr<http_impl_t<tcp_client_impl>>,
                        http_conn_t<tcp_client_conn>>(true, http_impl, fd, data, len);
            };
            http_impl->funcs.m_http_cli = std::move(f);		//保存回调函数

            auto ctl = get_sigctl();
            ctl.add_sig(SIGRTMIN+14, std::bind(&http_impl_t<tcp_client_impl>::name_resolve_callback, http_impl.get(), _1));
        }

        http_client obj;
        obj.m_impl = impl;
        return obj;
    }

    http_server scheduler::get_http_server(uint16_t port,
                                           std::function<void(int, const char*, const char*, std::unordered_map<std::string, const char*>&, char*, size_t)> f)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto& impl = sch_impl->m_util_impls[HTTP_SVR];
        if (!impl) {
            auto http_impl = std::make_shared<http_impl_t<tcp_server_impl>>();
            impl = http_impl;

            http_impl->m_util.m_app_prt = PRT_HTTP;
            http_impl->m_util.m_sch = this;
            http_impl->m_util.m_protocol_hook = [this](int fd, char* data, size_t len) {
                auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
                auto conn = std::dynamic_pointer_cast<http_conn_t<tcp_client_conn>>(sch_impl->m_ev_array[fd]);
                return http_parser(false, conn, data, len);
            };
            http_impl->m_util.m_f = [this](int fd, const std::string& ip_addr, uint16_t port, char *data, size_t len) {
                auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
                auto http_impl = std::dynamic_pointer_cast<http_impl_t<tcp_server_impl>>(sch_impl->m_util_impls[HTTP_SVR]);
                tcp_callback_for_http<std::shared_ptr<http_impl_t<tcp_server_impl>>,
                        http_conn_t<tcp_server_conn>>(true, http_impl, fd, data, len);
            };
            http_impl->funcs.m_http_svr = std::move(f);

            //创建tcp服务端的监听套接字，允许接收任意ip地址发送的服务请求，监听请求的端口为port
            http_impl->fd = http_impl->conn_sock.create(PRT_TCP, USR_SERVER, nullptr, port);
            http_impl->sch_impl = sch_impl;
            http_impl->f = std::bind(&http_impl_t<tcp_server_impl>::tcp_server_callback, http_impl.get());
            sch_impl->add_event(http_impl);
        }

        http_server obj;
        obj.m_impl = impl;
        return obj;
    }

    simpack_server
    scheduler::get_simpack_server(
            std::function<void(const crx::server_info&)> on_connect,
            std::function<void(const crx::server_info&)> on_disconnect,
            std::function<void(const crx::server_info&, const crx::server_cmd&, char*, size_t)> on_request,
            std::function<void(const crx::server_info&, const crx::server_cmd&, char*, size_t)> on_response,
            std::function<void(const crx::server_info&, const crx::server_cmd&, char*, size_t)> on_notify)
    {
        simpack_server obj;
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto& impl = sch_impl->m_util_impls[SIMP_SVR];
        if (!impl) {
            auto simp_impl = std::make_shared<simpack_server_impl>();
            obj.m_impl = impl = simp_impl;

            simp_impl->m_sch = this;
            simp_impl->m_on_connect = std::move(on_connect);
            simp_impl->m_on_disconnect = std::move(on_disconnect);
            simp_impl->m_on_request = std::move(on_request);
            simp_impl->m_on_response = std::move(on_response);
            simp_impl->m_on_notify = std::move(on_notify);

            if (sch_impl->m_ini_file.empty()) {
                printf("不存在配置文件\n");
                return obj;
            }

            ini ini_parser;
            ini_parser.load(sch_impl->m_ini_file.c_str());

            if (!ini_parser.has_section("registry")) {
                printf("配置文件 %s 没有 [registry] 节区\n", sch_impl->m_ini_file.c_str());
                return obj;
            }

            //使用simpack_server组件的应用必须配置registry节区以及ip,port,name这三个字段
            auto xutil = std::make_shared<simpack_xutil>();
            ini_parser.set_section("registry");
            xutil->info.ip = ini_parser.get_str("ip");
            xutil->info.port = (uint16_t)ini_parser.get_int("port");
            xutil->info.name = ini_parser.get_str("name");
            xutil->listen = ini_parser.get_int("listen");

            //创建tcp_client用于主动连接
            simp_impl->m_client = get_tcp_client(std::bind(&simpack_server_impl::simp_callback, simp_impl.get(), _1, _2, _3, _4, _5));
            auto cli_impl = std::dynamic_pointer_cast<tcp_client_impl>(simp_impl->m_client.m_impl);
            cli_impl->m_util.m_app_prt = PRT_SIMP;
            register_tcp_hook(true, [](int conn, char *data, size_t len) {
                return simpack_protocol(data, len); });
            sch_impl->m_util_impls[TCP_CLI].reset();

            //创建tcp_server用于被动连接
            simp_impl->m_server = get_tcp_server((uint16_t)xutil->listen,
                    std::bind(&simpack_server_impl::simp_callback, simp_impl.get(), _1, _2, _3, _4, _5));
            auto svr_impl = std::dynamic_pointer_cast<tcp_server_impl>(simp_impl->m_server.m_impl);
            svr_impl->m_util.m_app_prt = PRT_SIMP;
            register_tcp_hook(false, [](int conn, char *data, size_t len) {
                return simpack_protocol(data, len); });
            xutil->listen = simp_impl->m_server.get_port();
            sch_impl->m_util_impls[TCP_SVR].reset();

            //只有配置了名字才连接registry，否则作为单点应用
            if (!xutil->info.name.empty()) {
                //若连接失败则每隔三秒尝试重连一次
                int conn = simp_impl->m_client.connect(xutil->info.ip.c_str(), xutil->info.port, -1, 3);
                simp_impl->m_registry_conn = xutil->info.conn = conn;
                auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
                tcp_ev->ext_data = xutil;

                simp_impl->m_seria.insert("ip", xutil->info.ip.c_str(), xutil->info.ip.size());
                uint16_t net_port = htons((uint16_t)xutil->listen);
                simp_impl->m_seria.insert("port", (const char*)&net_port, sizeof(net_port));
                simp_impl->m_seria.insert("name", xutil->info.name.c_str(), xutil->info.name.size());
                auto ref = simp_impl->m_seria.get_string();

                //setup header
                auto header = (simp_header*)ref.data;
                header->length = htonl((uint32_t)(ref.len-sizeof(simp_header)));
                header->cmd = htons(CMD_REG_NAME);

                simp_impl->m_reg_str = std::string(ref.data, ref.len);
                simp_impl->m_client.send_data(conn, ref.data, ref.len);
                simp_impl->m_seria.reset();
            }
        }
        return obj;
    }

    void simpack_server_impl::stop()
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
        for (auto conn : m_ordinary_conn) {
            if (conn < sch_impl->m_ev_array.size() && sch_impl->m_ev_array[conn]) {
                auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
                auto xutil = std::dynamic_pointer_cast<simpack_xutil>(tcp_ev->ext_data);
                if ("__log_server__" != xutil->info.role)
                    m_on_disconnect(xutil->info);
                say_goodbye(xutil);
            }
        }

        if (-1 != m_registry_conn && m_registry_conn < sch_impl->m_ev_array.size() &&
            sch_impl->m_ev_array[m_registry_conn]) {
            auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[m_registry_conn]);
            auto xutil = std::dynamic_pointer_cast<simpack_xutil>(tcp_ev->ext_data);
            say_goodbye(xutil);
            m_registry_conn = -1;
        }
    }

    void simpack_server_impl::simp_callback(int conn, const std::string &ip, uint16_t port, char *data, size_t len)
    {
        if (!data) {            //连接开启/关闭回调
            if (len == m_registry_conn) {      //此时为重连
                printf("连接重新建立 %d->%d\n", m_registry_conn, conn);
                m_registry_conn = conn;
                auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
                auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
                auto xutil = std::dynamic_pointer_cast<simpack_xutil>(tcp_ev->ext_data);
                xutil->info.conn = conn;
                m_client.send_data(conn, m_reg_str.c_str(), m_reg_str.size());
            }
            return;
        }

        auto header = (simp_header*)data;
        auto header_len = sizeof(simp_header);
        if (len <= header_len)
            return;

        m_app_cmd.ses_id = ntohl(header->ses_id);
        m_app_cmd.req_id = ntohl(header->req_id);
        m_app_cmd.type = ntohs(header->type);
        m_app_cmd.cmd = ntohs(header->cmd);
        m_app_cmd.result = ntohs(header->result);

        uint32_t ctl_flag = ntohl(header->ctl_flag);
        bool is_registry = GET_BIT(ctl_flag, 3) != 0;
        if (GET_BIT(ctl_flag, 0)) {     //捕获控制请求
            capture_sharding(is_registry, conn, ip, port, data, len);
        } else {    //路由上层请求
            auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
            auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
            auto xutil = std::dynamic_pointer_cast<simpack_xutil>(tcp_ev->ext_data);
            if (memcmp(header->token, xutil->token, 16)) {
                printf("illegal request since token is mismatch\n");
                return;
            }

            if (GET_BIT(ctl_flag, 1)) {     //推送
                m_on_notify(xutil->info, m_app_cmd, data+header_len, len-header_len);
            } else {        //非推送
                if (GET_BIT(ctl_flag, 2))
                    m_on_request(xutil->info, m_app_cmd, data+header_len, len-header_len);
                else
                    m_on_response(xutil->info, m_app_cmd, data+header_len, len-header_len);
            }
        }
    }

    void simpack_server_impl::capture_sharding(bool registry, int conn, const std::string &ip, uint16_t port, char *data, size_t len)
    {
        auto header = (simp_header*)data;
        auto kvs = m_seria.dump(data+sizeof(simp_header), len-sizeof(simp_header));
        if (registry) {       //与registry建立的连接
            switch (m_app_cmd.cmd) {
                case CMD_REG_NAME:      handle_reg_name(conn, header->token, kvs);      break;
                case CMD_SVR_ONLINE:    handle_svr_online(header->token, kvs);          break;
                default:                printf("unknown cmd=%d\n", m_app_cmd.cmd);      break;
            }
        } else {        //其他连接
            //除了首个建立信道的命令之外，后续所有的请求/响应都需要验证token
            if (CMD_HELLO != m_app_cmd.cmd) {
                auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
                auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
                auto xutil = std::dynamic_pointer_cast<simpack_xutil>(tcp_ev->ext_data);
                if (memcmp(header->token, xutil->token, 16)) {
                    printf("illegal request since token is mismatch\n");
                    return;
                }
            }

            switch (m_app_cmd.cmd) {
                case CMD_HELLO: {
                    if (1 == m_app_cmd.type)
                        handle_hello_request(conn, ip, port, header->token, kvs);
                    else if (2 == m_app_cmd.type)
                        handle_hello_response(conn, m_app_cmd.result, header->token, kvs);
                    break;
                }
                case CMD_GOODBYE:       handle_goodbye(conn);                               break;
                default:                printf("unknown cmd=%d\n", m_app_cmd.cmd);          break;
            }
        }
    }

    void simpack_server_impl::handle_reg_name(int conn, unsigned char *token, std::unordered_map<std::string, mem_ref>& kvs)
    {
        if (m_app_cmd.result) {    //失败
            printf("pronounce failed: %s\n", kvs["error_info"].data);
            m_client.release(conn);
            return;
        }

        //成功
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
        auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[m_registry_conn]);
        auto xutil = std::dynamic_pointer_cast<simpack_xutil>(tcp_ev->ext_data);
        if (m_registry_conn != conn) {
            if (-1 != m_registry_conn) {
                printf("repeat connection with registry old=%d new=%d\n", m_registry_conn, conn);
                say_goodbye(xutil);
            }
            m_registry_conn = conn;
        }

        auto role_it = kvs.find("role");        //registry一定会返回当前节点的role
        std::string role(role_it->second.data, role_it->second.len);
        printf("pronounce succ: role=%s\n", role.c_str());
        xutil->info.role = std::move(role);

        auto clients_it = kvs.find("clients");
        if (kvs.end() != clients_it) {
            std::string clients(clients_it->second.data, clients_it->second.len);
            printf("pronounce succ: clients=%s\n", clients.c_str());
            auto client_ref = split(clients.c_str(), clients.size(), ",");
            for (auto& ref : client_ref)
                xutil->clients.insert(std::string(ref.data, ref.len));
        }
        memcpy(xutil->token, token, 16);    //保存该token用于与其他服务之间的通信

        //让registry通知所有连接该服务的主动方发起连接或是让本服务连接所有的被动方
        m_seria.insert("name", xutil->info.name.c_str(), xutil->info.name.size());
        auto ref = m_seria.get_string();
        bzero(&m_app_cmd, sizeof(m_app_cmd));
        m_app_cmd.cmd = CMD_SVR_ONLINE;
        send_package(3, m_registry_conn, m_app_cmd, true, xutil->token, ref.data, ref.len);
        m_seria.reset();
    }

    void simpack_server_impl::handle_svr_online(unsigned char *token, std::unordered_map<std::string, mem_ref>& kvs)
    {
        auto name_it = kvs.find("name");
        std::string svr_name(name_it->second.data, name_it->second.len);

        auto ip_it = kvs.find("ip");
        std::string ip(ip_it->second.data, ip_it->second.len);

        uint16_t port = ntohs(*(uint16_t*)kvs["port"].data);

        //握手
        int conn = m_client.connect(ip.c_str(), port);
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
        auto xutil = std::make_shared<simpack_xutil>();
        auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
        tcp_ev->ext_data = xutil;
        xutil->info.conn = conn;
        xutil->info.ip = std::move(ip);
        xutil->info.port = port;

        auto reg_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[m_registry_conn]);
        auto reg_xutil = std::dynamic_pointer_cast<simpack_xutil>(reg_ev->ext_data);
        m_seria.insert("name", reg_xutil->info.name.c_str(), reg_xutil->info.name.size());
        m_seria.insert("role", reg_xutil->info.role.c_str(), reg_xutil->info.role.size());

        auto ref = m_seria.get_string();
        bzero(&m_app_cmd, sizeof(m_app_cmd));
        m_app_cmd.cmd = CMD_HELLO;
        m_app_cmd.type = 1;
        send_package(1, conn, m_app_cmd, true, token, ref.data, ref.len);
        m_seria.reset();
    }

    void simpack_server_impl::handle_hello_request(int conn, const std::string &ip, uint16_t port, unsigned char *token,
                                                   std::unordered_map<std::string, mem_ref>& kvs)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
        auto reg_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[m_registry_conn]);
        auto reg_xutil = std::dynamic_pointer_cast<simpack_xutil>(reg_ev->ext_data);

        if (memcmp(token, reg_xutil->token, 16)) {
            printf("hello cmd with illegal token\n");
            return;
        }

        auto name_it = kvs.find("name");
        std::string cli_name(name_it->second.data, name_it->second.len);

        auto role_it = kvs.find("role");
        std::string cli_role(role_it->second.data, role_it->second.len);

        bool client_valid = reg_xutil->clients.end() != reg_xutil->clients.find(cli_name);
        printf("client %s[%s], conn=%d, ip=%s, port=%u say hello to me, valid=%d\n", cli_name.c_str(),
               cli_role.c_str(), conn, ip.c_str(), port, client_valid);

        unsigned char *new_token = nullptr;
        std::shared_ptr<simpack_xutil> ord_xutil;
        if (client_valid) {
            ord_xutil = std::make_shared<simpack_xutil>();
            auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
            tcp_ev->ext_data = ord_xutil;
            m_ordinary_conn.insert(conn);

            ord_xutil->info.conn = conn;
            ord_xutil->info.ip = std::move(ip);
            ord_xutil->info.port = port;
            ord_xutil->info.name = std::move(cli_name);
            ord_xutil->info.role = std::move(cli_role);

            datetime dt = get_datetime();
            std::string text = ord_xutil->info.name+"#"+reg_xutil->info.name+"-";
            text += std::to_string(dt.date)+std::to_string(dt.time);
            MD5((const unsigned char*)text.c_str(), text.size(), ord_xutil->token);
            new_token = ord_xutil->token;

            m_on_connect(ord_xutil->info);
            m_seria.insert("name", reg_xutil->info.name.c_str(), reg_xutil->info.name.size());
            m_seria.insert("role", reg_xutil->info.role.c_str(), reg_xutil->info.role.size());
        } else {
            std::string error_info = "unknown name ";
            error_info.append(cli_name).push_back(0);
            m_seria.insert("error_info", error_info.c_str(), error_info.size());
        }

        auto ref = m_seria.get_string();
        bzero(&m_app_cmd, sizeof(m_app_cmd));
        m_app_cmd.cmd = CMD_HELLO;
        m_app_cmd.type = 2;     //响应
        m_app_cmd.result = (uint16_t)(client_valid ? 0 : 1);
        send_package(2, conn, m_app_cmd, true, new_token, ref.data, ref.len);
        m_seria.reset();

        if (client_valid) {     //通知registry连接建立
            m_seria.insert("client", ord_xutil->info.name.c_str(), ord_xutil->info.name.size());
            m_seria.insert("server", reg_xutil->info.name.c_str(), reg_xutil->info.name.size());

            auto ref = m_seria.get_string();
            bzero(&m_app_cmd, sizeof(m_app_cmd));
            m_app_cmd.cmd = CMD_CONN_CON;
            send_package(1, reg_xutil->info.conn, m_app_cmd, true, reg_xutil->token, ref.data, ref.len);
            m_seria.reset();
        }
    }

    void simpack_server_impl::handle_hello_response(int conn, uint16_t result, unsigned char *token,
                                                    std::unordered_map<std::string, mem_ref>& kvs)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
        auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
        auto xutil = std::dynamic_pointer_cast<simpack_xutil>(tcp_ev->ext_data);
        if (result) {
            printf("say hello response error: %s\n", kvs["error_info"].data);
            auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
            sch_impl->remove_event(conn);
            return;
        }

        m_ordinary_conn.insert(conn);
        auto name_it = kvs.find("name");
        xutil->info.name = std::string(name_it->second.data, name_it->second.len);
        auto role_it = kvs.find("role");
        xutil->info.role = std::string(role_it->second.data, role_it->second.len);
        memcpy(xutil->token, token, 16);
        if ("__log_server__" == xutil->info.role) {
            auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
            m_log_conn = conn;
            for (auto& data : m_cache_logs) {
                auto header = (simp_header*)data.c_str();
                m_app_cmd.cmd = header->cmd;
                send_data(header->type, m_log_conn, m_app_cmd, data.c_str(), data.size());
            }
        } else {
            m_on_connect(xutil->info);
        }
    }

    void simpack_server_impl::say_goodbye(std::shared_ptr<simpack_xutil>& xutil)
    {
        //挥手
        m_seria.insert("name", xutil->info.name.c_str(), xutil->info.name.size());
        auto ref = m_seria.get_string();
        bzero(&m_app_cmd, sizeof(m_app_cmd));
        m_app_cmd.cmd = CMD_GOODBYE;
        send_package(3, xutil->info.conn, m_app_cmd, true, xutil->token, ref.data, ref.len);
        m_seria.reset();

        //通知服务下线之后断开对应连接
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
        sch_impl->remove_event(xutil->info.conn);
    }

    void simpack_server_impl::handle_goodbye(int conn)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
        auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
        auto xutil = std::dynamic_pointer_cast<simpack_xutil>(tcp_ev->ext_data);
        if ("__log_server__" != xutil->info.role)
            m_on_disconnect(xutil->info);
        sch_impl->remove_event(conn);
    }

    void simpack_server_impl::send_data(int type, int conn, const server_cmd& cmd, const char *data, size_t len)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
        if (0 < conn && conn < sch_impl->m_ev_array.size()) {
            auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
            auto xutil = std::dynamic_pointer_cast<simpack_xutil>(tcp_ev->ext_data);
            send_package(type, conn, cmd, false, xutil->token, data, len);
        }
    }

    void simpack_server_impl::send_package(int type, int conn, const server_cmd& cmd, bool lib_proc,
                                           unsigned char *token, const char *data, size_t len)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
        simp_header *header = nullptr;
        size_t total_len = len;
        if (len >= 4 && htonl(0x5f3759df) == *(uint32_t*)data) {    //上层使用的是基于simp协议的序列化组件seria
            header = (simp_header*)data;
            header->length = htonl((uint32_t)(len-sizeof(simp_header)));
        } else {
            m_simp_buf.resize(sizeof(simp_header));
            m_simp_buf.append(data, len);
            header = (simp_header*)&m_simp_buf[0];
            header->length = htonl((uint32_t)len);
            total_len = len+sizeof(simp_header);
        }

        header->ses_id = htonl(cmd.ses_id);
        header->req_id = htonl(cmd.req_id);
        header->type = htons(cmd.type);
        header->cmd = htons(cmd.cmd);
        header->result = htons(cmd.result);

        uint32_t ctl_flag = 0;
        if (lib_proc)
            SET_BIT(ctl_flag, 0);       //由库这一层处理

        if (1 == type) {            //request
            CLR_BIT(ctl_flag, 1);
            SET_BIT(ctl_flag, 2);
        } else if (2 == type) {     //response
            CLR_BIT(ctl_flag, 1);
            CLR_BIT(ctl_flag, 2);
        } else if (3 == type) {     //notify
            SET_BIT(ctl_flag, 1);
        } else {
            return;
        }
        header->ctl_flag = htonl(ctl_flag);

        if (token)      //带上token表示是合法的package
            memcpy(header->token, token, 16);

        auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
        tcp_ev->async_write((const char*)header, total_len);
    }

    log scheduler::get_log(const char *prefix, const char *root_dir /*= "log_files"*/, int max_size /*= 10*/)
    {
        crx::log ins;
        auto impl = std::make_shared<log_impl>();
        ins.m_impl = impl;

        impl->m_prefix = prefix;
        impl->m_root_dir = root_dir;
        impl->m_max_size = max_size*1024*1024;

        bool res = false;
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        if (sch_impl->m_remote_log)
            res = impl->get_remote_log(sch_impl);
        else
            res = impl->get_local_log(this, sch_impl);

        if (!res)
            ins.m_impl.reset();
        else
            sch_impl->m_util_impls.push_back(std::move(impl));
        return ins;
    }

    std::string log_impl::get_log_path(bool create)
    {
        m_now = get_datetime();
        m_last_day = m_now.t->tm_mday;
        m_last_hour = m_now.t->tm_hour;

        int ret = 0;
        std::string log_path(256, 0);
        auto pos = m_root_dir.find('@');        //a trick for local log & remote log
        if (std::string::npos != pos)
            ret = sprintf(&log_path[0], "%s/%d/%02d/%02d/%s/", m_root_dir.c_str(), m_now.t->tm_year+1900,
                          m_now.t->tm_mon+1, m_now.t->tm_mday, &m_root_dir[pos+1]);
        else
            ret = sprintf(&log_path[0], "%s/%d/%02d/%02d/", m_root_dir.c_str(), m_now.t->tm_year+1900,
                          m_now.t->tm_mon+1, m_now.t->tm_mday);
        log_path.resize((size_t)ret);
        if (create)
            mkdir_multi(log_path.c_str());
        return log_path;
    }

    void log_impl::create_log_file(const char *log_file)
    {
        if (m_fp)
            fclose(m_fp);
        m_fp = fopen(log_file, "a");
        if (!m_fp) {
            perror("get_local_log");
            return;
        }

        //使用自定义的日志缓冲，大小为64k
        setvbuf(m_fp, &m_log_buf[0], _IOFBF, m_log_buf.size());
        m_curr_size = (int)ftell(m_fp);
    }

    bool log_impl::get_local_log(scheduler *sch, std::shared_ptr<scheduler_impl>& sch_impl)
    {
        std::string log_path = get_log_path(true);
        size_t path_size = log_path.size();
        log_path.resize(256, 0);

        char file_wh[64] = {0};
        sprintf(file_wh, "%s_%02d", m_prefix.c_str(), m_now.t->tm_hour);
        depth_first_traverse_dir(log_path.c_str(), [&](const std::string& fname) {
            if (std::string::npos != fname.find(file_wh)) {
                auto pos = fname.rfind('[');
                int split_idx = atoi(&fname[pos+1]);
                if (split_idx > m_split_idx)
                    m_split_idx = split_idx;
            }
        }, false);
        sprintf(&log_path[0]+path_size, "%s-%d.log", file_wh, m_split_idx);
        create_log_file(log_path.c_str());
        if (!m_fp)
            return false;

        if (!sch_impl->m_sec_wheel.m_impl)       //创建一个秒盘
            sch_impl->m_sec_wheel = sch->get_timer_wheel(1000, 60);

        //每隔3秒刷一次缓冲
        sch_impl->m_sec_wheel.add_handler(3*1000, std::bind(&scheduler_impl::flush_log_buffer, sch_impl.get()));
        return true;
    }

    bool log_impl::get_remote_log(std::shared_ptr<scheduler_impl>& sch_impl)
    {
        auto& impl = sch_impl->m_util_impls[SIMP_SVR];
        if (!impl) {
            printf("远程日志基于 simpack_server 组件，请先实例化该对象\n");
            return false;
        }

        m_seria.insert("prefix", m_prefix.c_str(), m_prefix.size());
        m_seria.insert("root_dir", m_root_dir.c_str(), m_root_dir.size());
        m_log_idx = sch_impl->m_util_impls.size();
        uint32_t net_idx = htonl((uint32_t)m_log_idx);
        m_seria.insert("log_idx", (const char*)&net_idx, sizeof(net_idx));
        uint32_t net_size = htonl((uint32_t)m_max_size);
        m_seria.insert("max_size", (const char*)&net_size, sizeof(net_size));

        auto ref = m_seria.get_string();
        auto header = (simp_header*)ref.data;
        header->cmd = m_cmd.cmd = 1;
        header->type = m_cmd.type = 1;     //request

        auto simp_impl = std::dynamic_pointer_cast<simpack_server_impl>(impl);
        if (-1 != simp_impl->m_log_conn)
            simp_impl->send_data(1, simp_impl->m_log_conn, m_cmd, ref.data, ref.len);
        else
            simp_impl->m_cache_logs.push_back(std::string(ref.data, ref.len));
        m_seria.reset();
        m_simp_impl = simp_impl;
        return true;
    }

    void log::printf(const char *fmt, ...)
    {
        timeval tv = {0};
        gettimeofday(&tv, nullptr);

        //格式化
        auto impl = std::dynamic_pointer_cast<log_impl>(m_impl);
        if (tv.tv_sec != impl->m_last_sec) {
            impl->m_last_sec = tv.tv_sec;
            impl->m_now = get_datetime(&tv);
            sprintf(&impl->m_fmt_buf[0], "[%04d/%02d/%02d %02d:%02d:%02d.%06ld] ", impl->m_now.t->tm_year+1900, impl->m_now.t->tm_mon+1,
                    impl->m_now.t->tm_mday, impl->m_now.t->tm_hour, impl->m_now.t->tm_min, impl->m_now.t->tm_sec, tv.tv_usec);
        } else {
            sprintf(&impl->m_fmt_buf[21], "%06ld", tv.tv_usec);
            impl->m_fmt_buf[27] = ']';
        }

        va_list var1, var2;
        va_start(var1, fmt);
        va_copy(var2, var1);

        char *data = &impl->m_fmt_buf[0];
        size_t remain = impl->m_fmt_buf.size()-29;
        size_t ret = (size_t)vsnprintf(&impl->m_fmt_buf[29], remain, fmt, var1);
        if (ret > remain) {
            impl->m_fmt_tmp.resize(ret+30, 0);
            data = &impl->m_fmt_tmp[0];
            strncpy(&impl->m_fmt_tmp[0], impl->m_fmt_buf.c_str(), 29);
            ret = (size_t)vsnprintf(&impl->m_fmt_tmp[29], ret+1, fmt, var2);
        }

        va_end(var2);
        va_end(var1);

        ret += 29;
        if (impl->m_log_idx) {      //写远程日志
            uint32_t net_idx = htonl((uint32_t)impl->m_log_idx);
            impl->m_seria.insert("log_idx", (const char*)&net_idx, sizeof(net_idx));
            impl->m_seria.insert("data", data, ret);

            auto ref = impl->m_seria.get_string();
            auto header = (simp_header*)ref.data;
            header->cmd = impl->m_cmd.cmd = 1;
            header->type = impl->m_cmd.type = 3;    //notify

            if (-1 != impl->m_simp_impl->m_log_conn)
                impl->m_simp_impl->send_data(3, impl->m_simp_impl->m_log_conn, impl->m_cmd, ref.data, ref.len);
            else
                impl->m_simp_impl->m_cache_logs.push_back(std::string(ref.data, ref.len));
            impl->m_seria.reset();
        } else {        //本地
            impl->write_local_log(data, ret);
        }
    }

    void log_impl::rotate_log(bool create_dir)
    {
        std::string log_path = get_log_path(create_dir);
        size_t path_size = log_path.size();
        log_path.resize(256, 0);
        sprintf(&log_path[0]+path_size, "%s_%02ld-%d.log", m_prefix.c_str(), m_last_hour, m_split_idx);
        create_log_file(log_path.c_str());
    }

    void log_impl::write_local_log(const char *data, size_t len)
    {
        int this_hour = atoi(&data[12]);
        if (this_hour != m_last_hour) {     //时钟更新，创建新的日志文件
            m_last_hour = this_hour;
            m_split_idx = 0;

            bool create_dir = false;
            int this_day = atoi(&data[9]);
            if (this_day != m_last_day) {       //日期更新，创建新的目录
                m_last_day = this_day;
                create_dir = true;
            }
            rotate_log(create_dir);
        }

        if (m_fp) {
            fwrite(data, sizeof(char), len, m_fp);
            m_curr_size += len;
        }

        if (m_curr_size >= m_max_size) {    //当前日志文件已超过最大尺寸，创建新的日志文件
            m_split_idx++;
            rotate_log(false);
        }
    }

    fs_monitor scheduler::get_fs_monitor(std::function<void(const char*, uint32_t)> f)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto& impl = sch_impl->m_util_impls[FS_MONI];
        if (!impl) {
            auto moni_impl = std::make_shared<fs_monitor_impl>();
            impl = moni_impl;

            moni_impl->fd = inotify_init1(IN_NONBLOCK);
            moni_impl->sch_impl = sch_impl;
            moni_impl->f = std::bind(&fs_monitor_impl::fs_monitory_callback, moni_impl.get());

            moni_impl->m_monitor_f = std::move(f);
            sch_impl->add_event(moni_impl);
        }

        fs_monitor obj;
        obj.m_impl = impl;
        return obj;
    }

    void fs_monitor_impl::fs_monitory_callback()
    {
        char buf[1024] = {0};
        int filter = 0, offset = 0;
        while (true) {
            ssize_t len = read(fd, buf+offset, sizeof(buf)-offset);
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
                    offset = (int)(buf+sizeof(buf)-ptr);
                    std::memmove(buf, ptr, (size_t)offset);
                    break;
                }

                inotify_event *nfy_ev = (inotify_event*)ptr;
                //buffer can't hold the whole package
                if (nfy_ev->len+sizeof(inotify_event) > sizeof(buf)) {      //ignore events on the file
                    printf("[fs_monitory_callback] WARN: event package size greater than the buffer size!\n");
                    filter = (int)(ptr+sizeof(inotify_event)+nfy_ev->len-buf-sizeof(buf));
                    offset = 0;
                    break;
                }

                if (ptr+sizeof(inotify_event)+nfy_ev->len > buf+sizeof(buf)) {  //read more
                    offset = (int)(buf+sizeof(buf)-ptr);
                    std::memmove(buf, ptr, (size_t)offset);
                    break;
                }

                if (m_wd_mev.end() != m_wd_mev.find(nfy_ev->wd)) {
                    auto& mev = m_wd_mev[nfy_ev->wd];
                    std::string file_name = mev->path;
                    if (nfy_ev->len)
                        file_name += nfy_ev->name;

                    if ((nfy_ev->mask & IN_ISDIR) && mev->recur_flag) {      //directory
                        if ('/' != file_name.back())
                            file_name.push_back('/');

                        if ((nfy_ev->mask & IN_CREATE) &&
                            m_path_mev.end() == m_path_mev.find(file_name)) {
                            int watch_id = inotify_add_watch(fd, file_name.c_str(), mev->mask);
                            if (__glibc_likely(-1 != watch_id))
                                trigger_event(true, watch_id, file_name, mev->recur_flag, mev->mask);
                        } else if ((nfy_ev->mask & IN_DELETE) &&
                                   m_path_mev.end() != m_path_mev.find(file_name)) {
                            trigger_event(false, m_path_mev[file_name]->watch_id, file_name, 0, 0);
                        }
                    }
                    m_monitor_f(file_name.c_str(), nfy_ev->mask);
                }

                ptr += sizeof(inotify_event)+nfy_ev->len;
                if (ptr-buf == len)     //no more events to process
                    break;
            }
        }
    }

    void fs_monitor_impl::recursive_monitor(const std::string& root_dir, bool add, uint32_t mask)
    {
        depth_first_traverse_dir(root_dir.c_str(), [&](std::string& dir_name) {
            struct stat st = {0};
            stat(dir_name.c_str(), &st);
            if (!S_ISDIR(st.st_mode))       //仅对目录做处理
                return;

            if ('/' != dir_name.back())
                dir_name.push_back('/');

            if (add) {
                if (m_path_mev.end() == m_path_mev.find(dir_name)) {
                    int watch_id = inotify_add_watch(fd, dir_name.c_str(), mask);
                    if (__glibc_unlikely(-1 == watch_id)) {
                        perror("recursive_monitor::inotify_add_watch");
                        return;
                    }
                    trigger_event(true, watch_id, dir_name, true, mask);
                }
            } else {    //remove
                if (m_path_mev.end() != m_path_mev.find(dir_name))
                    trigger_event(false, m_path_mev[dir_name]->watch_id, dir_name, 0, 0);
            }
        }, true, false);
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