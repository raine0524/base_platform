#pragma once

namespace crx
{
    enum CO_STATUS
    {
        CO_UNKNOWN = 0,
        CO_READY,
        CO_RUNNING,
        CO_SUSPEND,
    };

    struct coroutine
    {
        int co_id;          //协程id
        CO_STATUS status;   //当前运行状态
        bool is_share;      //是否使用共享栈
        char comment[64];   //简述
    };

    class CRX_SHARE scheduler : public kobj
    {
    public:
        scheduler();
        virtual ~scheduler();

        //创建一个协程, @share参数表示是否使用共享栈，创建成功将返回该协程的id，失败则返回-1
        int co_create(std::function<void(scheduler *sch, void *arg)> f, void *arg, bool is_share = false,
                      const char *comment = nullptr);

        //切换执行流，@co_id表示切换至哪个协程，若其为-1则代表切换回主协程
        void co_yield(int co_id);

        //获取当前调度器中所有可用的协程，可用指协程状态为CO_READY, CO_RUNNING, CO_SUSPEND之一
        std::vector<coroutine*> get_avail_cos();
    };
}