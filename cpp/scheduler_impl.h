#pragma once

#include "stdafx.h"

namespace crx
{
    static const int EPOLL_SIZE = 256;

    static const int STACK_SIZE = 256*1024;     //栈大小设置为256K

    class scheduler_impl;
    class eth_event : public impl
    {
    public:
        eth_event() :fd(-1) {}
        virtual ~eth_event()
        {
            if (-1 != fd && STDIN_FILENO != fd)
                close(fd);
        }

        /*
         * @read_str: 将读到的数据追加在read_str尾部
         * @return param
         *      -> 1: 等待更多数据可读
         *      -> 0: 所读文件正常关闭
         *      -> -1: 出现异常，异常由errno描述
         */
        int async_read(std::string& read_str);

        int fd;      //与epoll事件关联的文件描述符
        std::function<void()> f;     //回调函数
        std::weak_ptr<scheduler_impl> sch_impl;
    };

    struct coroutine_impl : public coroutine
    {
        SUS_TYPE type;
        std::function<void(crx::scheduler *sch)> f;

        ucontext_t ctx;
        std::string stack;
        size_t size;

        coroutine_impl() : type(WAIT_EVENT), size(0)
        {
            co_id = 0;
            status = CO_UNKNOWN;
            is_share = false;
        }
    };

    enum
    {
        SIG_CTL = 0,
        TCP_CLI,
        TCP_SVR,
        HTTP_CLI,
        HTTP_SVR,
        SIMP_SVR,
        FS_MONI,
        EXT_DATA,
        IDX_MAX,
    };

    class scheduler_impl : public impl
    {
    public:
        scheduler_impl()
                :m_running_co(0)
                ,m_next_co(-1)
                ,m_epoll_fd(-1)
                ,m_log_idx(0)
                ,m_remote_log(false)
        {
            m_util_impls.resize(IDX_MAX);
        }

        virtual ~scheduler_impl() = default;

    public:
        size_t co_create(std::function<void(crx::scheduler *sch)>& f, scheduler *sch,
                         bool is_main_co, bool is_share = false, const char *comment = nullptr);

        static void coroutine_wrap(uint32_t low32, uint32_t hi32);

        void main_coroutine(scheduler *sch);

        void save_stack(std::shared_ptr<coroutine_impl>& co_impl, const char *top);

        void add_event(std::shared_ptr<eth_event> ev, uint32_t event = EPOLLIN);

        void remove_event(int fd);

        /*
         * 添加epoll事件，每个事件都采用edge-trigger的模式，只要可读/写，就一直进行读/写，直到不再有数据或者
         * 文件不再可读/出错，因此在此函数中会将文件设置为非阻塞状态，fd应当保证是一个有效的文件描述符
         */
        void handle_event(int op, int fd, uint32_t event);

        void periodic_trim_memory();

    public:
        std::string m_ini_file;
        int m_running_co, m_next_co;
        std::vector<std::shared_ptr<coroutine_impl>> m_cos;     //第一个协程为主协程，且在进程的生命周期内常驻内存
        std::vector<size_t> m_unused_cos;                       //those coroutines that unused

        bool m_go_done;
        int m_epoll_fd;		//epoll线程描述符及等待线程终止信号的事件描述符
        std::vector<std::shared_ptr<eth_event>> m_ev_array;

        uint32_t m_log_idx;
        bool m_remote_log;
        timer_wheel m_sec_wheel;
        std::vector<std::shared_ptr<impl>> m_util_impls;
    };
}