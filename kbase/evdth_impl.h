#pragma once

namespace crx
{
    class control_block
    {
    public:
        control_block(kobj *obj)
                :m_use_count(1)
                ,m_kobj(obj) {}

        virtual ~control_block() {};

        void add_ref()
        {
            m_use_count++;
        }

        void reduce_ref()
        {
            if (--m_use_count)
                return;

            if (m_kobj)
                delete m_kobj;
            delete this;
        }

        std::atomic<int> m_use_count;
        kobj *m_kobj;
    };

    class evd_job_impl : public control_block
    {
    public:
        evd_job_impl(evd_job *job)
                :control_block(job)
                ,m_type(-1) {}
        int m_type;
    };

    class evd_proc_impl : public control_block
    {
    public:
        evd_proc_impl(evd_proc *proc)
                :control_block(proc)
                ,on_duty(true)
                ,product_idx(-1) {}

        bool on_duty;				//指示是否执行队列中的任务
        int product_idx;		//将工人安排在第product_index条生产线上

        std::mutex mtx;
        std::unordered_set<int> type_set;
    };

    struct task_wrap    //将待处理的数据与处理器绑在一起放在指定的流水线上
    {
        evd_job_impl *job;      //数据
        evd_proc_impl *proc;    //处理器

        task_wrap()
                :job(nullptr)
                ,proc(nullptr) {}

        task_wrap(evd_job_impl *j)
                :job(j)
                ,proc(nullptr) {}

        void reset()
        {
            if (job) {
                job->reduce_ref();
                job = nullptr;
            }
            if (proc) {
                proc->reduce_ref();
                proc = nullptr;
            }
        }
    };

    struct prod_line
    {
        prod_line()
                :go_done(true)
                ,work_strength(0)
        {
            pthread_mutex_init(&mtx, nullptr);
            pthread_cond_init(&cond, nullptr);
        }

        virtual ~prod_line()
        {
            pthread_cond_destroy(&cond);
            pthread_mutex_destroy(&mtx);
        }

        std::thread th;
        pthread_mutex_t mtx;
        pthread_cond_t cond;

        bool go_done;
        int32_t work_strength;		//工作强度，用于生产线的负载均衡
        std::deque<task_wrap> task_list;
    };

    class evd_pool_impl
    {
    public:
        static void thread_proc(prod_line *line);

        std::mutex m_mtx;
        std::unordered_map<int, std::unordered_set<evd_proc_impl*>> m_arrange;	//job type->processors
        std::vector<prod_line*> m_lines;		//生产线
    };
}
