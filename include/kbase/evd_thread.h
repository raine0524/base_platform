#pragma once

namespace crx
{
    /*
     * 将待处理的数据继承evd_thread_job类，并为这一类的数据设置一个类型，随后调用evd_thread_pool的job_dispatch接口，
     * 之后关注此种类型数据的evd_thread_processor将会收到这一数据，并在process_task中进行处理
     */
    class CRX_SHARE evd_job : public kobj
    {
    public:
        evd_job();
        evd_job(int type);

        void set_type(int type);		//设置任务类型，type取值>=0
        int get_type();					//获取任务类型

        void release();
    };

    class CRX_SHARE evd_proc : public kobj
    {
    public:
        evd_proc();

        void release();

        //获取当前处理器关注的类型个数
        size_t type_count();

        //针对每个任务类型执行相应的回调，@f: 回调函数, @args: 回调参数
        void for_each_type(std::function<void(int, void*)> f, void *args = nullptr);

    public:
        //可以注册多种类型
        void reg_type(int type);

        //处理任务，完成之后自动释放任务所占资源
        virtual void process_task(crx::evd_job *job) = 0;
    };

    class CRX_SHARE evd_pool : public kobj
    {
    public:
        evd_pool();
        virtual ~evd_pool();

        //启动事件驱动线程池，@cnt指明创建的线程个数
        void start(size_t cnt);

        //终止线程池
        void stop();

        //注册处理器
        void reg_proc(crx::evd_proc *proc);

        //注销处理器
        void unreg_proc(crx::evd_proc *proc);

        //所有处理指定类型job的processor将共用同一份job，因此processor在处理时应进行写时拷贝，以避免污染原始数据源
        void job_dispatch(crx::evd_job *job);
    };
}
