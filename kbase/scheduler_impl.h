#pragma once

namespace crx
{
    static const int STACK_SIZE = 1*1024*1024*1024;     //栈大小设置为256K

    struct coroutine_impl : public coroutine
    {
        std::function<void(crx::scheduler *sch, void *arg)> f;
        void *arg;

        ucontext_t ctx;
        char *stack;
        size_t capacity, size;

        coroutine_impl()
                :arg(nullptr)
                ,stack(nullptr)
                ,capacity(0)
                ,size(0)
        {
            co_id = -1;
            status = CO_UNKNOWN;
            is_share = false;
        }

        virtual ~coroutine_impl()
        {
            if (stack) {
                delete []stack;
                stack = nullptr;
            }
        }
    };

    class scheduler_impl
    {
    public:
        scheduler_impl() : m_running(-1), m_next_co(-1) {}
        virtual ~scheduler_impl() {}

        int co_create(std::function<void(crx::scheduler *sch, void *arg)>& f, void *arg, scheduler *sch,
                      bool is_main_co, bool is_share = false, const char *comment = nullptr);

        static void main_coroutine(scheduler *sch, void *arg);

        void save_stack(coroutine_impl *co, const char *top);

    private:
        static void coroutine_wrap(uint32_t low32, uint32_t hi32);

    public:
        int m_running, m_next_co;
        std::vector<coroutine_impl*> m_cos;     //第一个协程为主协程，且在进程的生命周期内常驻内存
        std::list<int> m_unused_list;        //those coroutines that unused
    };
}