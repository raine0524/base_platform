#include "stdafx.h"

namespace crx
{
    log *g_log = nullptr;

    scheduler::scheduler(bool remote_log /*= false*/)
    {
        auto *impl = new scheduler_impl(this);
        impl->m_cos.reserve(64);        //预留64个协程
        impl->m_ev_array.reserve(128);  //预留128个epoll事件
        impl->m_remote_log = remote_log;
        m_obj = impl;
    }

    scheduler::~scheduler()
    {
        auto impl = (scheduler_impl*)m_obj;
        for (auto co_impl : impl->m_cos)
            delete co_impl;

        for (auto log : impl->m_logs)
            delete log;

        if (impl->m_sigctl)
            delete impl->m_sigctl;

        if (impl->m_tcp_client) {
            delete (tcp_client_impl*)impl->m_tcp_client->m_obj;
            delete impl->m_tcp_client;
        }

        if (impl->m_tcp_server)
            delete impl->m_tcp_server;

        if (impl->m_http_client) {
            delete (http_client_impl*)impl->m_http_client->m_obj;
            delete impl->m_http_client;
        }

        if (impl->m_http_server)
            delete impl->m_http_server;

        if (impl->m_simp_server) {
            delete (simpack_server_impl*)impl->m_simp_server->m_obj;
            delete impl->m_simp_server;
        }

        if (impl->m_fs_monitor)
            delete impl->m_fs_monitor;

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

        auto yield_co = sch_impl->m_cos[co_id];
        if (!yield_co || CO_UNKNOWN == yield_co->status)      //指定协程无效或者状态指示不可用，同样不发生切换
            return false;

        assert(CO_RUNNING != yield_co->status);
        auto curr_co = sch_impl->m_cos[sch_impl->m_running_co];
        sch_impl->m_running_co = (int)co_id;

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
        if (m_unused_cos.empty()) {
            co_impl = new coroutine_impl;
            co_impl->co_id = m_cos.size();
            m_cos.push_back(co_impl);
        } else {        //复用之前已创建的协程
            std::pop_heap(m_unused_cos.begin(), m_unused_cos.end(), std::greater<size_t>());
            size_t co_id = m_unused_cos.back();
            m_unused_cos.pop_back();
            co_impl = m_cos[co_id];       //复用时其id不变
            if (co_impl->is_share != is_share) {        //复用时堆栈的使用模式改变
                delete []co_impl->stack;
                co_impl->stack = nullptr;
            }
        }

        co_impl->status = is_main_co ? CO_RUNNING : CO_READY;
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
        sch_impl->m_unused_cos.push_back(co_impl->co_id);
        std::push_heap(sch_impl->m_unused_cos.begin(), sch_impl->m_unused_cos.end(), std::greater<size_t>());
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
            for (; i < cnt; ++i) {      //处理已触发的事件
                int fd = events[i].data.fd;
                auto ev = m_ev_array[fd];
                if (ev)
                    ev->f(sch, ev->arg);
            }

            i = 1;
            while (i < m_cos.size()) {
                if (CO_SUSPEND == m_cos[i]->status && STS_HAVE_REST == m_cos[i]->sus_sts)
                    m_sch->co_yield(i);
                ++i;
            }

            //当前未使用的协程数量过多(超过总量的3/4)，回收一部分资源(当前总量的1/2)
            size_t total_cos = m_cos.size();
            if (total_cos > 64 && m_unused_cos.size() >= 3*total_cos/4.0) {
                for (i = 0; i < total_cos/2; ++i) {
                    auto last_co = m_cos.back();
                    if (CO_UNKNOWN == last_co->status) {        //从后往前释放资源，当最后一个协程无效时才释放该资源
                        delete last_co;
                        m_cos.pop_back();
                    } else {
                        break;
                    }
                }

                //重新建立未使用co_id的最小堆
                m_unused_cos.clear();
                for (auto co : m_cos)
                    if (CO_UNKNOWN == co->status)
                        m_unused_cos.push_back(co->co_id);
                std::make_heap(m_unused_cos.begin(), m_unused_cos.end(), std::greater<size_t>());
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

    /*
     * 移除事件对象时，对内存资源的释放进行优化较为困难，本质上是因为事件指针可能指向多个不同的继承对象，在复用该内存区域时
     * 前后两个实际对象并不一致，此时进行针对性的优化意义不大，直接使用系统的一般化内存管理方案即可
     */
    void scheduler_impl::remove_event(eth_event *ev)
    {
        if (ev) {
            handle_event(EPOLL_CTL_DEL, ev->fd, EPOLLIN);
            if (ev->fd == m_ev_array.size()-1)
                m_ev_array.resize((size_t)ev->fd);
            else
                m_ev_array[ev->fd] = nullptr;
            delete ev;
        }
    }

    //异步读的一个原则是只要可读就一直读，直到errno被置为EAGAIN提示等待更多数据可读
    int scheduler_impl::async_read(eth_event *ev, std::string& read_str)
    {
        size_t read_size = read_str.size();
        while (true) {
            read_str.resize(read_size+1024);
            ssize_t ret = read(ev->fd, &read_str[read_size], 1024);
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
        if (!ev->cache_data.empty()) {      //存在缓存数据，正在监听可写事件
            ev->cache_data.push_back(std::string(data, len));
            return;
        }

        ssize_t sts = write(ev->fd, data, len);
        if (sts == len)     //全部写完
            return;

        if (-1 == sts && EAGAIN != errno) {     //出现异常，不再监听关联事件
            perror("scheduler_impl::async_write");
            remove_event(ev);
            return;
        }

        if (-1 != sts) {
            data += sts;
            len -= sts;
        }

        ev->cache_data.push_back(std::string(data, len));
        ev->f_bk = std::move(ev->f);
        ev->f = switch_write;
        handle_event(EPOLL_CTL_MOD, ev->fd, EPOLLOUT);
    }

    void scheduler_impl::switch_write(scheduler *sch, eth_event *arg)
    {
        bool excep_occur = false;
        for (auto it = arg->cache_data.begin(); it != arg->cache_data.end(); ) {
            ssize_t sts = write(arg->fd, it->c_str(), it->size());
            if (-1 == sts) {
                if (EAGAIN != errno) {
                    perror("scheduler_impl::switch_write");
                    excep_occur = true;
                }
                break;
            } else if (sts < it->size()) {
                if (sts > 0)
                    it->erase(0, (size_t)sts);
                break;
            } else {
                it = arg->cache_data.erase(it);
            }
        }

        if (excep_occur) {      //发生异常，移除该文件描述符上的监听事件
            arg->cache_data.clear();
            arg->sch_impl->remove_event(arg);
        } else if (arg->cache_data.empty()) {   //切换监听读事件
            arg->f = std::move(arg->f_bk);
            arg->sch_impl->handle_event(EPOLL_CTL_MOD, arg->fd, EPOLLIN);
        }
    }

    log* scheduler::get_log(const char *prefix, const char *root_dir /*= "log_files"*/,
                            int max_size /*= 2*/, bool print_screen /*= true*/)
    {
        g_log = new log;
        auto impl = new log_impl;
        g_log->m_obj = impl;
        impl->m_prefix = prefix;
        impl->m_root_dir = root_dir;
        impl->m_max_size = max_size*1024*1024;
        impl->m_pscreen = print_screen;
        impl->m_now = get_datetime();

        char log_path[256] = {0}, file_wh[64] = {0};
        int ret = sprintf(log_path, "%s/%d/%02d/%02d/", root_dir, impl->m_now.t->tm_year+1900,
                          impl->m_now.t->tm_mon+1, impl->m_now.t->tm_mday);
        mkdir_multi(log_path);

        sprintf(file_wh, "%s_%02d", prefix, impl->m_now.t->tm_hour);
        depth_first_traverse_dir(log_path, [&](const std::string& fname, void *arg) {
            if (std::string::npos != fname.find(file_wh)) {
                auto pos = fname.rfind('[');
                int split_idx = atoi(&fname[pos+1]);
                if (split_idx > impl->m_split_idx)
                    impl->m_split_idx = split_idx;
            }
        }, nullptr, false);
        sprintf(log_path+ret, "%s[%02d].log", file_wh, impl->m_split_idx);

        if (-1 != impl->fd)
            close(impl->fd);
        if (access(log_path, F_OK))     //file not exist
            impl->fd = open(log_path, O_CREAT | O_WRONLY, 0644);
        else
            impl->fd = open(log_path, O_WRONLY);
        impl->m_curr_size = (int)lseek(impl->fd, 0, SEEK_END);

        auto sch_impl = (scheduler_impl*)m_obj;
        sch_impl->m_logs.push_back(g_log);
    }

    void log::printf(const char *fmt, ...)
    {
        timeval tv = {0};
        gettimeofday(&tv, nullptr);

        auto impl = (log_impl*)m_obj;
        if (tv.tv_sec != impl->m_last_sec) {
            impl->m_last_sec = tv.tv_sec;
            impl->m_now = get_datetime(&tv);
            sprintf(&impl->m_log_buffer[0], "[%04d/%02d/%02d %02d:%02d:%02d.%06ld] ", impl->m_now.t->tm_year+1900, impl->m_now.t->tm_mon+1,
                     impl->m_now.t->tm_mday, impl->m_now.t->tm_hour, impl->m_now.t->tm_min, impl->m_now.t->tm_sec, tv.tv_usec);
        } else {
            sprintf(&impl->m_log_buffer[21], "%06ld", tv.tv_usec);
            impl->m_log_buffer[27] = ']';
        }

        va_list var1, var2;
        va_start(var1, fmt);
        va_copy(var2, var1);

        char *data = &impl->m_log_buffer[0];
        size_t remain = impl->m_log_buffer.size()-29;
        size_t ret = (size_t)vsnprintf(&impl->m_log_buffer[29], remain, fmt, var1);
        if (ret > remain) {
            impl->m_temp.resize(ret+30, 0);
            data = &impl->m_temp[0];
            strncpy(&impl->m_temp[0], impl->m_log_buffer.c_str(), 29);
            ret = (size_t)vsnprintf(&impl->m_temp[29], ret+1, fmt, var2);
        }

        va_end(var2);
        va_end(var1);
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

    sigctl* scheduler::get_sigctl(std::function<void(int, uint64_t, void*)> f, void *arg /*= nullptr*/)
    {
        auto sch_impl = (scheduler_impl*)m_obj;
        if (!sch_impl->m_sigctl) {
            sch_impl->m_sigctl = new sigctl;        //创建一个新的sigctl
            sch_impl->m_sigctl->m_obj = new sigctl_impl;

            auto sig_impl = (sigctl_impl*)sch_impl->m_sigctl->m_obj;
            sig_impl->fd = signalfd(-1, &sig_impl->m_mask, SFD_NONBLOCK);
            sig_impl->f = sig_impl->sigctl_callback;
            sig_impl->arg = sig_impl;
            sig_impl->sch_impl = sch_impl;

            sig_impl->m_f = std::move(f);
            sig_impl->m_arg = arg;
            sch_impl->add_event(sig_impl);
        }
        return sch_impl->m_sigctl;
    }

    void sigctl_impl::sigctl_callback(scheduler *sch, eth_event *arg)
    {
        auto impl = dynamic_cast<sigctl_impl*>(arg);
        int st_size = sizeof(impl->m_fd_info);
        while (true) {
            int64_t ret = read(impl->fd, &impl->m_fd_info, (size_t)st_size);
            if (ret == st_size) {
                impl->m_f(impl->m_fd_info.ssi_signo, impl->m_fd_info.ssi_ptr, impl->m_arg);
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
    void sigctl_impl::handle_sigs(const std::vector<int>& sigset, bool add)
    {
        sch_impl->handle_event(EPOLL_CTL_DEL, fd, EPOLLIN);
        for (auto sig : sigset) {
            if (add)
                sigaddset(&m_mask, sig);
            else
                sigdelset(&m_mask, sig);
        }

        sigprocmask(SIG_SETMASK, &m_mask, nullptr);
        signalfd(fd, &m_mask, SFD_NONBLOCK);
        sch_impl->handle_event(EPOLL_CTL_ADD, fd, EPOLLIN);
    }

    timer* scheduler::get_timer(std::function<void(void*)> f, void *arg /*= nullptr*/)
    {
        auto tmr = new timer;
        tmr->m_obj = new timer_impl;
        auto tmr_impl = (timer_impl*)tmr->m_obj;
        tmr_impl->fd = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK);    //创建一个非阻塞的定时器资源
        if (__glibc_likely(-1 != tmr_impl->fd)) {
            auto sch_impl = (scheduler_impl*)m_obj;
            tmr_impl->sch_impl = sch_impl;
            tmr_impl->f = tmr_impl->timer_callback;
            tmr_impl->arg = tmr_impl;

            tmr_impl->m_f = std::move(f);
            tmr_impl->m_arg = arg;
            sch_impl->add_event(tmr_impl);      //加入epoll监听事件
        } else {
            perror("scheduler::get_timer");
            delete tmr_impl;
            delete tmr;
            tmr = nullptr;
        }
        return tmr;
    }

    /*
     * 触发定时器回调时首先读文件描述符 fd，读操作将定时器状态切换为已读，若不执行读操作，
     * 则由于epoll采用edge-trigger边沿触发模式，定时器事件再次触发时将不再回调该函数
     */
    void timer_impl::timer_callback(scheduler* sch, eth_event *arg)
    {
        auto impl = dynamic_cast<timer_impl*>(arg);
        uint64_t cnt;
        read(impl->fd, &cnt, sizeof(cnt));
        impl->m_f(impl->m_arg);
    }

    event* scheduler::get_event(std::function<void(int, void*)> f, void *arg /*= nullptr*/)
    {
        auto ev = new event;
        ev->m_obj = new event_impl;
        auto ev_impl = (event_impl*)ev->m_obj;
        ev_impl->fd = eventfd(0, EFD_NONBLOCK);			//创建一个非阻塞的事件资源
        if (__glibc_likely(-1 != ev_impl->fd)) {
            auto sch_impl = (scheduler_impl*)m_obj;
            ev_impl->sch_impl = sch_impl;
            ev_impl->f = ev_impl->event_callback;
            ev_impl->arg = ev_impl;

            ev_impl->m_f = std::move(f);
            ev_impl->m_arg = arg;
            sch_impl->add_event(ev_impl);
        } else {
            perror("scheduler::get_event");
            delete ev;
            delete ev_impl;
            ev = nullptr;
        }
        return ev;
    }

    void event_impl::event_callback(scheduler *sch, eth_event *arg)
    {
        auto impl = dynamic_cast<event_impl*>(arg);
        eventfd_t val;
        eventfd_read(impl->fd, &val);       //读操作将事件重置

        for (auto signal : impl->m_signals)
            impl->m_f(signal, impl->m_arg);			//执行事件回调函数
    }

    udp_ins* scheduler::get_udp_ins(bool is_server, uint16_t port,
                                    std::function<void(const std::string&, uint16_t, const char*, size_t, void*)> f,
                                    void *arg /*= nullptr*/)
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
            ui_impl->sch_impl = sch_impl;
            ui_impl->f = ui_impl->udp_ins_callback;
            ui_impl->arg = ui_impl;

            ui_impl->m_f = std::move(f);
            ui_impl->m_arg = arg;
            sch_impl->add_event(ui_impl);
        } else {
            perror("scheduler::get_udp_ins");
            delete ui;
            delete ui_impl;
            ui = nullptr;
        }
        return ui;
    }

    void udp_ins_impl::udp_ins_callback(scheduler *sch, eth_event *arg)
    {
        auto impl = dynamic_cast<udp_ins_impl*>(arg);
        bzero(&impl->m_recv_addr, sizeof(impl->m_recv_addr));
        impl->m_recv_len = sizeof(impl->m_recv_addr);

        while (true) {
            /*
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
            impl->m_f(ip_addr, port, impl->m_recv_buffer.data(), (size_t)ret, impl->m_arg);
        }
    }

    void scheduler::register_tcp_hook(bool client, std::function<int(int, char*, size_t, void*)> f,
                                      void *arg /*= nullptr*/)
    {
        auto sch_impl = (scheduler_impl*)m_obj;
        if (client) {
            auto tcp_impl = (tcp_client_impl*)sch_impl->m_tcp_client->m_obj;
            tcp_impl->m_protocol_hook = std::move(f);
            tcp_impl->m_protocol_arg = arg;
        } else {        //server
            auto tcp_impl = (tcp_server_impl*)sch_impl->m_tcp_server->m_obj;
            tcp_impl->m_protocol_hook = std::move(f);
            tcp_impl->m_protocol_arg = arg;
        }
    }

    tcp_client* scheduler::get_tcp_client(std::function<void(int, const std::string&, uint16_t, char*, size_t, void*)> f,
                                          void *arg /*= nullptr*/)
    {
        auto sch_impl = (scheduler_impl*)m_obj;
        if (!sch_impl->m_tcp_client) {
            sch_impl->m_tcp_client = new tcp_client;		//创建一个新的tcp_client
            sch_impl->m_tcp_client->m_obj = new tcp_client_impl;

            auto tcp_impl = (tcp_client_impl*)sch_impl->m_tcp_client->m_obj;
            tcp_impl->m_sch = this;
            tcp_impl->m_f = std::move(f);			//记录回调函数及参数
            tcp_impl->m_arg = arg;
        }
        return sch_impl->m_tcp_client;
    }

    void tcp_client_impl::tcp_client_callback(scheduler *sch, eth_event *ev)
    {
        auto sch_impl = (scheduler_impl*)sch->m_obj;
        auto tcp_conn = dynamic_cast<tcp_client_conn*>(ev);
        tcp_client_impl *tcp_impl = nullptr;
        switch (tcp_conn->app_prt) {
            case PRT_NONE: {
                tcp_impl = (tcp_client_impl*)sch_impl->m_tcp_client->m_obj;
                break;
            }
            case PRT_HTTP: {
                tcp_impl = (http_client_impl*)sch_impl->m_http_client->m_obj;
                break;
            }
            case PRT_SIMP: {
                auto simp_impl = (simpack_server_impl*)sch_impl->m_simp_server->m_obj;
                tcp_impl = (tcp_client_impl*)simp_impl->m_client->m_obj;
                break;
            }
        }

        if (!tcp_conn->is_connect) {
            tcp_conn->is_connect = true;
            tcp_conn->f_bk = std::move(tcp_conn->f);
            tcp_conn->sch_impl->switch_write(tcp_impl->m_sch, ev);
            return;
        }

        int sts = sch_impl->async_read(tcp_conn, tcp_conn->stream_buffer);        //读tcp响应流
        handle_stream(ev->fd, tcp_impl, tcp_conn);

        if (sts <= 0) {     //读文件描述符检测到异常或发现对端已关闭连接
//            if (sts < 0)
//                printf("[tcp_client_impl::tcp_client_callback] 读文件描述符 %d 异常\n", tc_conn->fd);
//            else
//                printf("[tcp_client_impl::tcp_client_callback] 连接 %d 对端正常关闭\n", tc_conn->fd);
            tcp_impl->m_f(tcp_conn->fd, tcp_conn->ip_addr, tcp_conn->port, nullptr, 0, tcp_impl->m_arg);
            sch_impl->remove_event(tcp_conn);
        }
    }

    tcp_server* scheduler::get_tcp_server(uint16_t port,
                                          std::function<void(int, const std::string&, uint16_t, char*, size_t, void*)> f,
                                          void *arg /*= nullptr*/)
    {
        auto sch_impl = (scheduler_impl*)m_obj;
        if (!sch_impl->m_tcp_server) {
            sch_impl->m_tcp_server = new tcp_server;
            sch_impl->m_tcp_server->m_obj = new tcp_server_impl;

            auto tcp_impl = (tcp_server_impl*)sch_impl->m_tcp_server->m_obj;
            tcp_impl->m_sch = this;
            tcp_impl->m_f = std::move(f);		//保存回调函数及参数
            tcp_impl->m_arg = arg;
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
        arg = this;
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
                case PRT_SIMP:      conn = new tcp_server_conn;     break;
            }

            conn->fd = client_fd;
            conn->f = impl->read_tcp_stream;
            conn->arg = conn;

            conn->app_prt = impl->m_app_prt;
            conn->ip_addr = inet_ntoa(impl->m_accept_addr.sin_addr);	//将地址转换为点分十进制格式的ip地址
            conn->port = ntohs(impl->m_accept_addr.sin_port);           //将端口由网络字节序转换为主机字节序
            auto sch_impl = (scheduler_impl*)sch->m_obj;
            sch_impl->add_event(conn);        //将该连接加入监听事件
        }
    }

    void tcp_server_impl::read_tcp_stream(scheduler *sch, eth_event *ev)
    {
        auto sch_impl = (scheduler_impl*)sch->m_obj;
        auto tcp_conn = dynamic_cast<tcp_server_conn*>(ev);
        tcp_server_impl *tcp_impl = nullptr;
        switch (tcp_conn->app_prt) {
            case PRT_NONE: {
                tcp_impl = (tcp_server_impl*)sch_impl->m_tcp_server->m_obj;
                break;
            }
            case PRT_HTTP: {
                tcp_impl = (http_server_impl*)sch_impl->m_http_server->m_obj;
                break;
            }
            case PRT_SIMP: {
                auto simp_impl = (simpack_server_impl*)sch_impl->m_simp_server->m_obj;
                tcp_impl = (tcp_server_impl*)simp_impl->m_server->m_obj;
                break;
            }
        }

        int sts = sch_impl->async_read(tcp_conn, tcp_conn->stream_buffer);
        handle_stream(ev->fd, tcp_impl, tcp_conn);

        if (sts <= 0) {		//读文件描述符检测到异常或发现对端已关闭连接
//            if (sts < 0)
//                printf("[tcp_server_impl::read_tcp_stream] 读文件描述符 %d 出现异常", ev->fd);
//            else
//                printf("[tcp_server_impl::read_tcp_stream] 连接 %d 对端正常关闭\n", ev->fd);
            tcp_impl->m_f(tcp_conn->fd, tcp_conn->ip_addr, tcp_conn->port, nullptr, 0, tcp_impl->m_arg);
            sch_impl->remove_event(tcp_conn);
        }
    }

    http_client* scheduler::get_http_client(std::function<void(int, int, std::unordered_map<std::string, const char*>&, const char*, size_t, void*)> f,
                                            void *arg /*= nullptr*/)
    {
        auto sch_impl = (scheduler_impl*)m_obj;
        if (!sch_impl->m_http_client) {
            sch_impl->m_http_client = new http_client;
            sch_impl->m_http_client->m_obj = new http_client_impl;

            auto http_impl = (http_client_impl*)sch_impl->m_http_client->m_obj;
            http_impl->m_sch = this;
            http_impl->m_app_prt = PRT_HTTP;
            http_impl->m_protocol_hook = http_impl->check_http_stream;
            http_impl->m_protocol_arg = http_impl;
            http_impl->m_f = http_impl->protocol_hook;
            http_impl->m_arg = http_impl;
            http_impl->m_http_f = std::move(f);		//保存回调函数及参数
            http_impl->m_http_arg = arg;

            sigctl *ctl = get_sigctl(http_impl->name_resolve_callback, http_impl);
            ctl->add_sigs({SIGRTMIN+14});
        }
        return sch_impl->m_http_client;
    }

    void http_client_impl::protocol_hook(int fd, const std::string& ip_addr, uint16_t port,
                                         char *data, size_t len, void *arg)
    {
        auto http_impl = (http_client_impl*)arg;
        auto sch_impl = (scheduler_impl*)http_impl->m_sch->m_obj;
        auto conn = dynamic_cast<http_client_conn*>(sch_impl->m_ev_array[fd]);

        const char *cb_data = nullptr;
        size_t cb_len = 0;
        if (len != 1 || '\n' != *data) {
            cb_data = data;
            cb_len = (size_t)conn->content_len;
        }

        http_impl->m_http_f(fd, conn->status, conn->headers, cb_data, cb_len,
                            http_impl->m_http_arg);

        if (sch_impl->m_ev_array[fd]) {
            conn->content_len = -1;
            conn->headers.clear();
        }
    }

    /*
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
        if (-1 == conn->content_len) {        //与之前的http响应流已完成分包处理，开始处理新的响应
            if (len < 4)        //首先验证流的开始4个字节是否为签名"HTTP"
                return 0;

            if (strncmp(data, "HTTP", 4)) {     //签名出错，该流不是HTTP流
                char *sig_pos = strstr(data, "HTTP");       //查找流中是否存在"HTTP"子串
                if (!sig_pos || sig_pos > data+len-4) {     //未找到子串或找到的子串已超出查找范围，无效的流
                    return -(int)len;
                } else {        //找到子串
                    return -(int)(sig_pos-data);    //先截断签名之前多余的数据
                }
            }

            //先判断是否已经获取到完整的响应头
            char *delim_pos = strstr(data, "\r\n\r\n");
            if (!delim_pos || delim_pos > data+len-4) {     //还未取到分隔符或分隔符已超出查找范围
                if (len > 8192)
                    return -8192;      //在HTTP签名和分隔符\r\n\r\n之间已有超过8k的数据，直接截断，保护缓冲
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
                    auto line_elem = split(line.data, line.len, " ");
                    if (line_elem.size() < 3) {		//响应行出错，无效的响应流
                        header_err = true;
                        break;
                    }
                    conn->status = std::atoi(line_elem[1].data);		//记录中间的状态码
                } else {
                    auto header = split(line.data, line.len, ": ");		//头部字段键值对的分隔符为": "
                    if (header.size() >= 2) {       //无效的头部字段直接丢弃，不符合格式的值发生截断
                        *(char*)(header[1].data+header[1].len) = 0;
                        conn->headers[std::string(header[0].data, header[0].len)] = header[1].data;
                    }
                }
            }

            auto it = conn->headers.find("Content-Length");
            if (header_err || conn->headers.end() == it) {    //头部有误或者未找到"Content-Length"字段，截断该头部
                conn->headers.clear();
                return -(int)(valid_header_len+4);
            }
            conn->content_len = std::stoi(conn->headers["Content-Length"]);
            assert(conn->content_len >= 0);

            //先截断http头部
            if (conn->content_len == 0) {
                conn->content_len = 1;
                return -(int)(valid_header_len+3);
            } else {
                return -(int)(valid_header_len+4);
            }
        }

        if (len < conn->content_len)      //响应体中不包含足够的数据，等待该连接上更多的数据到来
            return 0;
        else        //已取到响应体，通知上层执行m_f回调
            return conn->content_len;
    }

    http_server* scheduler::get_http_server(uint16_t port,
                                            std::function<void(int, const char*, const char*, std::unordered_map<std::string, const char*>&,
                                                               const char*, size_t, void*)> f,
                                            void *arg /*= nullptr*/)
    {
        auto sch_impl = (scheduler_impl*)m_obj;
        if (!sch_impl->m_http_server) {
            sch_impl->m_http_server = new http_server;
            sch_impl->m_http_server->m_obj = new http_server_impl;

            auto http_impl = (http_server_impl*)sch_impl->m_http_server->m_obj;
            http_impl->m_app_prt = PRT_HTTP;
            http_impl->m_sch = this;
            http_impl->m_protocol_hook = http_impl->check_http_stream;
            http_impl->m_protocol_arg = http_impl;
            http_impl->m_f = http_impl->protocol_hook;
            http_impl->m_arg = http_impl;
            http_impl->m_http_f = std::move(f);
            http_impl->m_http_arg = arg;
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
            cb_len = (size_t)conn->content_len;
        }

        http_impl->m_http_f(fd, conn->method, conn->url, conn->headers,
                            cb_data, cb_len, http_impl->m_http_arg);

        if (sch_impl->m_ev_array[fd]) {
            conn->content_len = -1;
            conn->headers.clear();
        }
    }

    /*
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
        if (-1 == conn->content_len) {        //与之前的http请求流已完成分包处理，开始处理新的请求
            char *delim_pos = strstr(data, "\r\n\r\n");
            if (!delim_pos || delim_pos > data+len-4) {     //还未取到分隔符或已超出查找范围
                if (len > 8192)
                    return -8192;      //在取到分隔符之前已有超过8k的数据，截断已保护缓冲
                else
                    return 0;           //等待接受更多的数据
            }

            char *sig_pos = strstr(data, "HTTP");       //在完整的请求头中查找是否存在"HTTP"签名
            if (!sig_pos || sig_pos > delim_pos)        //未取到签名或已超出查找范围，截断该请求头
                return -(int)(delim_pos-data+4);

            size_t valid_header_len = delim_pos-data;
            auto str_vec = split(data, valid_header_len, "\r\n");
            if (str_vec.size() <= 1)        //一次HTTP请求中包含一个请求行以及至少一个头部字段
                return -(int)(valid_header_len+4);

            bool header_err = false;
            for (size_t i = 0; i < str_vec.size(); ++i) {
                auto line = str_vec[i];
                if (0 == i) {		//首先解析请求行
                    //请求行包含空格分隔的三个字段，例如：POST(请求方法) /request(url) HTTP/1.1(协议/版本)
                    auto line_elem = split(line.data, line.len, " ");
                    if (line_elem.size() < 3) {		//请求行出错，无效的请求流
                        header_err = true;
                        break;
                    }

                    conn->method = line_elem[0].data;          //记录请求方法
                    *(char*)(line_elem[0].data+line_elem[0].len) = 0;
                    conn->url = line_elem[1].data;             //记录请求的url(以"/"开始的部分)
                    *(char*)(line_elem[1].data+line_elem[1].len) = 0;
                } else {
                    auto header = split(line.data, line.len, ": ");        //头部字段键值对的分隔符为": "
                    if (header.size() >= 2) {       //无效的头部字段直接丢弃，不符合格式的值发生截断
                        *(char*)(header[1].data+header[1].len) = 0;
                        conn->headers[std::string(header[0].data, header[0].len)] = header[1].data;
                    }
                }
            }

            if (header_err) {       //头部有误
                conn->headers.clear();
                return -(int)(valid_header_len+4);
            }

            int ret = 0;
            auto it = conn->headers.find("Content-Length");
            if (conn->headers.end() == it) {      //有些请求流不存在请求体，此时头部中不包含"Content-Length"字段
                //just a trick，返回0表示等待更多的数据，如果需要上层执行m_f回调必须返回大于0的值，因此在完成一次分包之后，
                //若没有请求体，可以将content_len设置为1，但只截断分隔符"\r\n\r\n"中的前三个字符，而将最后一个字符作为请求体
                conn->content_len = 1;
                ret = -(int)(valid_header_len+3);
            } else {
                conn->content_len = std::stoi(it->second);
                ret = -(int)(valid_header_len+4);
            }
            assert(conn->content_len > 0);    //此时已正确获取到当前请求中的请求体信息
            return ret;     //先截断http头部
        }

        if (len < conn->content_len)      //请求体中不包含足够的数据，等待该连接上更多的数据到来
            return 0;
        else        //已取到请求体，通知上层执行m_f回调
            return conn->content_len;
    }

    simpack_server* scheduler::get_simpack_server(void *arg)
    {
        auto sch_impl = (scheduler_impl*)m_obj;
        if (!sch_impl->m_simp_server) {
            sch_impl->m_simp_server = new simpack_server;
            sch_impl->m_simp_server->m_obj = new simpack_server_impl;

            auto simp_impl = (simpack_server_impl*)sch_impl->m_simp_server->m_obj;
            simp_impl->m_sch = this;
            simp_impl->m_arg = arg;
        }
        return sch_impl->m_simp_server;
    }

    void simpack_server_impl::simp_callback(int conn, const std::string &ip, uint16_t port, char *data, size_t len)
    {
        auto header_len = sizeof(simp_header);
        if (len <= header_len)
            return;

        auto header = (simp_header*)data;
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
            auto& wrapper = m_server_info[conn];
            if (memcmp(header->token, wrapper->token, 16)) {
                printf("illegal request since token is mismatch\n");
                return;
            }

            if (GET_BIT(ctl_flag, 1)) {     //推送
                if (m_on_notify)
                    m_on_notify(wrapper->info, m_app_cmd, data+header_len, len-header_len, m_arg);
            } else {        //非推送
                bool is_request = GET_BIT(ctl_flag, 2) != 0;
                if (is_request && m_on_request)            //客户端只接受响应
                    m_on_request(wrapper->info, m_app_cmd, data+header_len, len-header_len, m_arg);
                else if (!is_request && m_on_response)       //服务端只接受请求
                    m_on_response(wrapper->info, m_app_cmd, data+header_len, len-header_len, m_arg);
            }
        }
    }

    void simpack_server_impl::capture_sharding(bool registry, int conn, const std::string &ip, uint16_t port, char *data, size_t len)
    {
        auto header = (simp_header*)data;
        auto kvs = m_seria.dump(data+sizeof(simp_header), len-sizeof(simp_header));
        if (registry) {       //与registry建立的连接
            m_conf.info.conn = conn;
            switch (m_app_cmd.cmd) {
                case CMD_REG_NAME:      handle_reg_name(conn, header->token, kvs);      break;
                default:                printf("unknown cmd=%d\n", m_app_cmd.cmd);      break;
            }
        } else {        //其他连接
            if (conn >= m_server_info.size())
                m_server_info.resize((size_t)conn+1, nullptr);

            auto& wrapper = m_server_info[conn];
            if (!wrapper) {
                wrapper = std::make_shared<info_wrapper>();
                wrapper->info.conn = conn;
                wrapper->info.ip = std::move(ip);
                wrapper->info.port = port;
            }

            //除了首个建立信道的命令之外，后续所有的请求都需要验证token
            if (CMD_HELLO != m_app_cmd.cmd && memcmp(header->token, wrapper->token, 16)) {
                printf("illegal request since token is mismatch\n");
                return;
            }

            switch (m_app_cmd.cmd) {
                case CMD_GOODBYE:       handle_goodbye(conn, kvs);                      break;
                default:                printf("unknown cmd=%d\n", m_app_cmd.cmd);      break;
            }
        }
    }

    void simpack_server_impl::handle_reg_name(int conn, unsigned char *token, std::unordered_map<std::string, mem_ref>& kvs)
    {
        if (m_app_cmd.result) {    //失败
            auto info_it = kvs.find("error_info");
            if (kvs.end() != info_it) {
                std::string error_info(info_it->second.data, info_it->second.len);
                printf("pronounce failed: %s\n", error_info.c_str());
            }

            auto svr_impl = (tcp_server_impl*)m_server->m_obj;
            if (conn < svr_impl->sch_impl->m_ev_array.size())
                svr_impl->sch_impl->remove_event(svr_impl->sch_impl->m_ev_array[conn]);
            return;
        }

        //成功
        auto role_it = kvs.find("role");
        if (kvs.end() != role_it) {
            std::string role(role_it->second.data, role_it->second.len);
            printf("pronounce succ: role=%s\n", role.c_str());
            m_conf.info.role = std::move(role);
        }
        memcpy(m_conf.token, token, 16);       //保存该token用于与其他服务之间的通信
    }

    void simpack_server_impl::say_goodbye(bool registry, int conn)
    {
        auto& info = registry ? m_conf.info : m_server_info[conn]->info;
        auto token = registry ? &m_conf.token[0] : &m_server_info[conn]->token[0];
        m_seria.insert("name", info.name.c_str(), info.name.size());
        auto ref = m_seria.get_string();
        bzero(&m_app_cmd, sizeof(m_app_cmd));
        m_app_cmd.cmd = CMD_GOODBYE;
        send_package(3, conn, m_app_cmd, token, ref.data, ref.len);
        m_seria.reset();

        //通知服务下线之后断开对应连接
        auto sch_impl = (scheduler_impl*)m_sch->m_obj;
        if (conn < sch_impl->m_ev_array.size())
            sch_impl->remove_event(sch_impl->m_ev_array[conn]);
    }

    void simpack_server_impl::handle_goodbye(int conn, std::unordered_map<std::string, mem_ref>& kvs)
    {
        auto& wrapper = m_server_info[conn];
        if (m_on_disconnect)
            m_on_disconnect(wrapper->info, m_arg);

        if (conn == m_server_info.size()-1)
            m_server_info.resize((size_t)conn);
        else
            wrapper.reset();
    }

    void simpack_server_impl::send_package(int type, int conn, const server_cmd& cmd, unsigned char *token,
                                           const char *data, size_t len)
    {
        auto sch_impl = (scheduler_impl*)m_sch->m_obj;
        if (conn >= sch_impl->m_ev_array.size())
            return;

        auto ev = sch_impl->m_ev_array[conn];
        if (!ev)
            return;

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

        uint32_t ctl_flag = 0;
        CLR_BIT(ctl_flag, 0);       //由上层应用处理
        switch (type) {
            case 1: {       //request
                CLR_BIT(ctl_flag, 1);
                SET_BIT(ctl_flag, 2);
                break;
            }
            case 2: {       //response
                CLR_BIT(ctl_flag, 1);
                CLR_BIT(ctl_flag, 2);
                break;
            }
            case 3: {       //notify
                SET_BIT(ctl_flag, 1);
                break;
            }
            default: {
                return;
            }
        }
        header->ctl_flag = htonl(ctl_flag);

        memcpy(header->token, token, 16);      //带上token表示是合法的package
        sch_impl->async_write(ev, (const char*)header, total_len);
    }

    fs_monitor* scheduler::get_fs_monitor(std::function<void(const char*, uint32_t, void *arg)> f,
                                          void *arg /*= nullptr*/)
    {
        auto sch_impl = (scheduler_impl*)m_obj;
        if (!sch_impl->m_fs_monitor) {
            sch_impl->m_fs_monitor = new fs_monitor;
            sch_impl->m_fs_monitor->m_obj = new fs_monitor_impl;

            auto monitor_impl = (fs_monitor_impl*)sch_impl->m_fs_monitor->m_obj;
            monitor_impl->fd = inotify_init1(IN_NONBLOCK);
            monitor_impl->sch_impl = sch_impl;
            monitor_impl->f = monitor_impl->fs_monitory_callback;
            monitor_impl->arg = monitor_impl;

            monitor_impl->m_monitor_f = std::move(f);
            monitor_impl->m_monitor_arg = arg;
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
                    impl->m_monitor_f(file_name.c_str(), nfy_ev->mask, impl->m_monitor_arg);
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