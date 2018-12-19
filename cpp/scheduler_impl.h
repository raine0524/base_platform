#pragma once

namespace crx
{
    static const int EPOLL_SIZE = 512;

    static const int STACK_SIZE = 256*1024;     //栈大小设置为256K

    /*
     * @read_str: 将读到的数据追加在read_str尾部
     * @return param
     *      -> 1: 等待更多数据可读
     *      -> 0: 所读文件正常关闭
     *      -> -1: 出现异常，异常由errno描述
     */
    int async_read(int fd, std::string& read_str);

    class scheduler_impl;
    class eth_event : public impl
    {
    public:
        eth_event() : fd(-1) {}
        virtual ~eth_event()
        {
            if (-1 != fd && STDIN_FILENO != fd)
                close(fd);
        }

        int fd;      //与epoll事件关联的文件描述符
        std::function<void(uint32_t events)> f;     //回调函数
        std::weak_ptr<scheduler_impl> sch_impl;
    };

    struct coroutine_impl : public coroutine
    {
        SUS_TYPE type;
        std::function<void(crx::scheduler *sch, size_t co_id)> f;

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

    class logger_impl : public impl
    {
    public:
        logger_impl()
        :m_sch_impl(nullptr)
        ,m_last_sec(-1)
        ,m_last_date(-1)
        ,m_fmt_buf(1024, 0)
        ,m_log_buf(65536, 0)
        ,m_fp(nullptr) {}

        void init_logger();

        void rotate_log();

        void flush_log_buffer();

        std::string m_prefix, m_log_file;
        scheduler_impl *m_sch_impl;
        datetime m_now;

        int64_t m_last_sec, m_last_date;
        std::string m_fmt_buf, m_fmt_tmp, m_log_buf;
        FILE *m_fp;
    };

    enum
    {
        SIG_CTL = 0,
        TMR_WHL,
        TCP_CLI,
        TCP_SVR,
        HTTP_CLI,
        HTTP_SVR,
        WS_CLI,
        WS_SVR,
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
        ,m_log_lvl(LVL_DEBUG)
        ,m_log_root("logs")
        ,m_back_cnt(100)
        {
            m_util_impls.resize(IDX_MAX);
        }

        size_t co_create(std::function<void(crx::scheduler *sch, size_t co_id)>& f, scheduler *sch,
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

        std::shared_ptr<logger_impl> get_logger(const char *prefix);

    public:
        int m_running_co, m_next_co;
        std::vector<std::shared_ptr<coroutine_impl>> m_cos;     //第一个协程为主协程，且在进程的生命周期内常驻内存
        std::vector<size_t> m_unused_cos;                       //those coroutines that unused

        bool m_go_done;
        int m_epoll_fd;		//epoll线程描述符及等待线程终止信号的事件描述符
        std::vector<std::shared_ptr<eth_event>> m_ev_array;

        LOG_LEVEL m_log_lvl;
        std::string m_log_root;
        int m_back_cnt;

        timer_wheel m_wheel;
        std::vector<std::shared_ptr<impl>> m_util_impls;
    };
}
