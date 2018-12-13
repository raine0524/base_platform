#pragma once

namespace crx
{
    class sigctl_impl : public eth_event
    {
    public:
        sigctl_impl()
        {
            sigemptyset(&m_mask);
            bzero(&m_fd_info, sizeof(m_fd_info));
        }

        void sigctl_callback(uint32_t events);

        void handle_sig(int signo, bool add);

        sigset_t m_mask;
        signalfd_siginfo m_fd_info;
        std::map<int, std::function<void(int, uint64_t)>> m_sig_cb;
    };

    class timer_impl : public eth_event
    {
    public:
        timer_impl() : m_delay(0), m_interval(0) {}

        void timer_callback(uint32_t events);

        uint64_t m_delay, m_interval;        //分别对应于首次触发时的延迟时间以及周期性触发时的间隔时间
        std::function<void(int64_t)> m_f;
        int64_t m_arg;
    };

    struct wheel_elem
    {
        int64_t remain;     // unit: ms
        std::function<void(int64_t)> f;
        int64_t arg;
    };

    struct wheel_slot
    {
        timer tmr;
        size_t slot_idx, tick;
        std::vector<std::vector<wheel_elem>> elems;
    };

    class timer_wheel_impl : public impl
    {
    public:
        virtual ~timer_wheel_impl()
        {
            for (auto& slot : m_timer_vec)
                slot.tmr.detach();
            m_timer_vec.clear();
        }

        void timer_wheel_callback(int64_t arg);

        std::vector<wheel_slot> m_timer_vec;     // 0-hour timer, 1-minute timer, 2-second timer, 3-mills timer
    };

    class event_impl : public eth_event
    {
    public:
        void event_callback(uint32_t events);

        std::list<int> m_signals;		//同一个事件可以由多个信号源发送多个不同的信号
        std::function<void(int)> m_f;
    };

    class udp_ins_impl : public eth_event
    {
    public:
        udp_ins_impl() : m_net_sock(NORM_TRANS), m_recv_buffer(65536, 0)
        {
            bzero(&m_send_addr, sizeof(m_send_addr));
        }

        void udp_ins_callback(uint32_t events);

        struct sockaddr_in m_send_addr, m_recv_addr;
        socklen_t m_recv_len;

        net_socket m_net_sock;			//udp套接字
        std::string m_recv_buffer;		//接收缓冲区
        std::function<void(const std::string&, uint16_t, char*, size_t)> m_f;       //收到数据时触发的回调函数
    };
}
