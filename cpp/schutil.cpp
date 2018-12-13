#include "stdafx.h"

namespace crx
{
    sigctl scheduler::get_sigctl()
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto& impl = sch_impl->m_util_impls[SIG_CTL];
        if (!impl) {
            auto sig_impl = std::make_shared<sigctl_impl>();
            sig_impl->fd = signalfd(-1, &sig_impl->m_mask, SFD_NONBLOCK);
            if (__glibc_likely(sig_impl->fd > 0)) {
                sig_impl->f = std::bind(&sigctl_impl::sigctl_callback, sig_impl.get(), _1);
                sig_impl->sch_impl = sch_impl;
                sch_impl->add_event(sig_impl);
                impl = sig_impl;
            } else {
                g_lib_log.printf(LVL_ERROR, "signalfd failed: %s\n", strerror(errno));
            }
        }

        sigctl obj;
        obj.m_impl = impl;
        return obj;
    }

    void sigctl_impl::sigctl_callback(uint32_t events)
    {
        int st_size = sizeof(m_fd_info);
        while (true) {
            int64_t ret = read(fd, &m_fd_info, (size_t)st_size);
            if (__glibc_likely(ret == st_size)) {
                auto it = m_sig_cb.find(m_fd_info.ssi_signo);
                if (m_sig_cb.end() != it)
                    it->second(m_fd_info.ssi_signo, m_fd_info.ssi_ptr);
            } else {
                if (-1 == ret && EAGAIN != errno)
                    g_lib_log.printf(LVL_ERROR, "read failed: %s\n", strerror(errno));
                else if (ret > 0)
                    g_lib_log.printf(LVL_ERROR, "invalid size: %ld\n", ret);
                break;      //file closed & wait more data is normal
            }
        }
    }

    void sigctl::add_sig(int signo, std::function<void(int, uint64_t)> callback)
    {
        if (!m_impl) return;
        auto impl = std::dynamic_pointer_cast<sigctl_impl>(m_impl);
        if (impl->m_sig_cb.end() == impl->m_sig_cb.find(signo)) {
            impl->handle_sig(signo, true);
            impl->m_sig_cb[signo] = std::move(callback);
        }
    }

    void sigctl::remove_sig(int signo)
    {
        if (!m_impl) return;
        auto impl = std::dynamic_pointer_cast<sigctl_impl>(m_impl);
        if (impl->m_sig_cb.end() != impl->m_sig_cb.find(signo)) {
            impl->handle_sig(signo, false);
            impl->m_sig_cb.erase(signo);
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

        if (__glibc_unlikely(-1 == sigprocmask(SIG_SETMASK, &m_mask, nullptr)))
            g_lib_log.printf(LVL_ERROR, "sigprocmask failed: %s\n", strerror(errno));

        if (__glibc_unlikely(-1 == signalfd(fd, &m_mask, SFD_NONBLOCK)))
            g_lib_log.printf(LVL_ERROR, "signalfd failed: %s\n", strerror(errno));

        impl->handle_event(EPOLL_CTL_ADD, fd, EPOLLIN);
    }

    timer scheduler::get_timer(std::function<void(int64_t)> f, int64_t cb_arg /*= 0*/)
    {
        auto tmr_impl = std::make_shared<timer_impl>();
        tmr_impl->fd = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK);    //创建一个非阻塞的定时器资源
        if (__glibc_likely(-1 != tmr_impl->fd)) {
            auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
            tmr_impl->sch_impl = sch_impl;
            tmr_impl->f = std::bind(&timer_impl::timer_callback, tmr_impl.get(), _1);
            tmr_impl->m_f = std::move(f);
            tmr_impl->m_arg = cb_arg;
            sch_impl->add_event(tmr_impl);      //加入epoll监听事件
        } else {
            g_lib_log.printf(LVL_ERROR, "timerfd_create failed: %s\n", strerror(errno));
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
    void timer_impl::timer_callback(uint32_t events)
    {
        uint64_t cnt;
        read(fd, &cnt, sizeof(cnt));
        m_f(m_arg);
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

        struct itimerspec time_setting = {0};
        time_setting.it_value.tv_sec = now.tv_sec+delay_nanos/nano_per_sec+add;     //设置初始延迟
        time_setting.it_value.tv_nsec = tv_nsec;
        time_setting.it_interval.tv_sec = interval_nanos/nano_per_sec;              //设置时间间隔
        time_setting.it_interval.tv_nsec = interval_nanos%nano_per_sec;
        if (__glibc_unlikely(-1 == timerfd_settime(impl->fd, TFD_TIMER_ABSTIME, &time_setting, nullptr)))
            g_lib_log.printf(LVL_ERROR, "timerfd_settime failed: %s\n", strerror(errno));
    }

    void timer::detach()
    {
        if (!m_impl) return;
        auto tmr_impl = std::dynamic_pointer_cast<timer_impl>(m_impl);
        if (!tmr_impl->sch_impl.expired()) {
            auto sch_impl = tmr_impl->sch_impl.lock();
            sch_impl->remove_event(tmr_impl->fd);       //移除该定时器相关的监听事件
        }
    }

    timer_wheel scheduler::get_timer_wheel()
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto& impl = sch_impl->m_util_impls[TMR_WHL];
        if (!impl) {
            auto tw_impl = std::make_shared<timer_wheel_impl>();
            tw_impl->m_timer_vec.resize(4);
            for (int i = 0; i < tw_impl->m_timer_vec.size(); i++) {
                auto& slot = tw_impl->m_timer_vec[i];
                slot.slot_idx = 0;
                switch (i) {
                    case 0:     //时钟盘
                        slot.tick = 60*60*1000;
                        slot.elems.resize(24);
                        break;
                    case 1:     //分钟盘
                        slot.tick = 60*1000;
                        slot.elems.resize(60);
                        break;
                    case 2:     //秒盘
                        slot.tick = 1000;
                        slot.elems.resize(60);
                        break;
                    case 3:     //微秒盘, 100ms一跳
                        slot.tick = 100;
                        slot.elems.resize(10);
                        break;
                    default:
                        break;
                }

                slot.tmr = get_timer(std::bind(&timer_wheel_impl::timer_wheel_callback, tw_impl.get(), _1), i);
                slot.tmr.start(slot.tick, slot.tick);
            }
            impl = tw_impl;
        }

        timer_wheel obj;
        obj.m_impl = impl;
        return obj;
    }

    void timer_wheel_impl::timer_wheel_callback(int64_t arg)
    {
        auto& slot = m_timer_vec[arg];
        slot.slot_idx = (slot.slot_idx+1)%slot.elems.size();
        for (auto& elem : slot.elems[slot.slot_idx]) {
            if (elem.remain <= 0) {
                elem.f(elem.arg);
                continue;
            }

            for (int i = (int)arg+1; i < m_timer_vec.size(); i++) {
                auto& this_slot = m_timer_vec[i];
                if (elem.remain < this_slot.tick)
                    continue;

                int quotient = (int)(elem.remain*1.0/this_slot.tick);
                int new_idx = (int)((this_slot.slot_idx+quotient)%this_slot.elems.size());
                this_slot.elems[new_idx].emplace_back();

                auto& new_elem = this_slot.elems[new_idx].back();
                new_elem.remain = elem.remain-this_slot.tick*quotient;
                new_elem.f = std::move(elem.f);
                new_elem.arg = elem.arg;
                break;
            }
        }
        slot.elems[slot.slot_idx].clear();
    }

    bool timer_wheel::add_handler(size_t delay, std::function<void(int64_t)> f, int64_t arg /*= 0*/)
    {
        if (!m_impl || delay >= 24*60*60*1000-1000)
            return false;

        delay = (delay/100+1)*100;        //首先将延迟时间正则化
        auto tw_impl = std::dynamic_pointer_cast<timer_wheel_impl>(m_impl);
        for (int i = 0; i < tw_impl->m_timer_vec.size(); i++) {
            auto& this_slot = tw_impl->m_timer_vec[i];
            if (delay < this_slot.tick)
                continue;

            int quotient = (int)(delay/this_slot.tick);
            int new_idx = (int)((this_slot.slot_idx+quotient)%this_slot.elems.size());
            this_slot.elems[new_idx].emplace_back();

            auto& new_elem = this_slot.elems[new_idx].back();
            new_elem.f = std::move(f);
            new_elem.arg = arg;
            new_elem.remain = delay-this_slot.tick*quotient;
            for (int j = i+1; j < tw_impl->m_timer_vec.size(); j++) {
                auto& nslot = tw_impl->m_timer_vec[j];
                new_elem.remain += (nslot.elems.size() - nslot.slot_idx - 1) * nslot.tick;
            }
            break;
        }
        return true;
    }

    event scheduler::get_event(std::function<void(int64_t)> f)
    {
        auto ev_impl = std::make_shared<event_impl>();
        ev_impl->fd = eventfd(0, EFD_NONBLOCK);			//创建一个非阻塞的事件资源
        if (__glibc_likely(-1 != ev_impl->fd)) {
            auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
            ev_impl->f = std::bind(&event_impl::event_callback, ev_impl.get(), _1);
            ev_impl->sch_impl = sch_impl;
            ev_impl->m_f = std::move(f);
            sch_impl->add_event(ev_impl);
        } else {
            g_lib_log.printf(LVL_ERROR, "eventfd failed: %s\n", strerror(errno));
            ev_impl.reset();
        }

        event ev;
        ev.m_impl = ev_impl;
        return ev;
    }

    void event_impl::event_callback(uint32_t events)
    {
        eventfd_t val;
        eventfd_read(fd, &val);       //读操作将事件重置
        for (auto signal : m_signals)
            m_f(signal);			//执行事件回调函数
        m_signals.clear();
    }

    void event::send_signal(int64_t signal)
    {
        if (!m_impl) return;
        auto impl = std::dynamic_pointer_cast<event_impl>(m_impl);
        impl->m_signals.push_back(signal);      //将信号加入事件相关的信号集
        eventfd_write(impl->fd, 1);             //设置事件
    }

    void event::detach()
    {
        if (!m_impl) return;
        auto ev_impl = std::dynamic_pointer_cast<event_impl>(m_impl);
        if (!ev_impl->sch_impl.expired()) {
            auto sch_impl = ev_impl->sch_impl.lock();
            sch_impl->remove_event(ev_impl->fd);    //移除该定时器相关的监听事件
        }
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
            ui_impl->f = std::bind(&udp_ins_impl::udp_ins_callback, ui_impl.get(), _1);
            ui_impl->sch_impl = sch_impl;
            ui_impl->m_f = std::move(f);
            sch_impl->add_event(ui_impl);
        } else {
            g_lib_log.printf(LVL_ERROR, "get_udp_ins failed of create socket(%d)\n", ui_impl->fd);
            ui_impl.reset();
        }

        udp_ins ui;
        ui.m_impl = ui_impl;
        return ui;
    }

    void udp_ins_impl::udp_ins_callback(uint32_t events)
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
                    g_lib_log.printf(LVL_ERROR, "recvfrom failed: %s\n", strerror(errno));
                break;
            }

            m_recv_buffer[ret] = 0;		//字符串以0结尾
            std::string ip_addr = inet_ntoa(m_recv_addr.sin_addr);		//将地址转换为点分十进制格式的ip地址
            uint16_t port = ntohs(m_recv_addr.sin_port);

            //执行回调，将完整的udp数据包传给应用层
            m_f(ip_addr, port, &m_recv_buffer[0], (size_t)ret);
        }
    }

    uint16_t udp_ins::get_port()
    {
        if (!m_impl) return 0;
        auto impl = std::dynamic_pointer_cast<udp_ins_impl>(m_impl);
        return impl->m_net_sock.m_port;
    }

    void udp_ins::send_data(const char *ip_addr, uint16_t port, const char *data, size_t len)
    {
        if (!m_impl) return;
        auto impl = std::dynamic_pointer_cast<udp_ins_impl>(m_impl);
        impl->m_send_addr.sin_addr.s_addr = inet_addr(ip_addr);
        impl->m_send_addr.sin_port = htons(port);
        if (__glibc_unlikely(-1 == sendto(impl->fd, data, len, 0,
                (struct sockaddr*)&impl->m_send_addr, sizeof(struct sockaddr))))
            g_lib_log.printf(LVL_ERROR, "sendto failed: %s\n", strerror(errno));
    }

    void udp_ins::detach()
    {
        if (!m_impl) return;
        auto ui_impl = std::dynamic_pointer_cast<udp_ins_impl>(m_impl);
        if (!ui_impl->sch_impl.expired()) {
            auto sch_impl = ui_impl->sch_impl.lock();
            sch_impl->remove_event(ui_impl->fd);        //移除该定时器相关的监听事件
        }
    }
}
