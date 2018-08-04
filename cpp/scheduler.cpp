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

            size_t i = 0;
            for (; i < cnt; ++i) {      //处理已触发的事件
                int fd = events[i].data.fd;
                if (fd < m_ev_array.size() && m_ev_array[fd])
                    m_ev_array[fd]->f();
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

    void scheduler_impl::add_event(std::shared_ptr<eth_event> ev, uint32_t event /*= EPOLLIN*/)
    {
        if (ev) {
            if (ev->fd >= m_ev_array.size())
                m_ev_array.resize((size_t)ev->fd+1);

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

    void scheduler_impl::periodic_trim_memory()
    {
        malloc_trim(0);
        m_sec_wheel.add_handler(5*1000, std::bind(&scheduler_impl::periodic_trim_memory, this));
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
}