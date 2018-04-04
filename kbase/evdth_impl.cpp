#include "stdafx.h"

namespace crx
{
    evd_job::evd_job()
    {
        m_obj = new evd_job_impl(this);
    }

    evd_job::evd_job(int type)
    {
        evd_job_impl *impl = new evd_job_impl(this);
        impl->m_type = type;
        m_obj = impl;
    }

    void evd_job::set_type(int type)
    {
        auto impl = (evd_job_impl*)m_obj;
        impl->m_type = type;
    }

    int evd_job::get_type()
    {
        auto impl = (evd_job_impl*)m_obj;
        return impl->m_type;
    }

    void evd_job::release()
    {
        auto impl = (evd_job_impl*)m_obj;
        impl->reduce_ref();
    }

    evd_proc::evd_proc()
    {
        m_obj = new evd_proc_impl(this);
    }

    void evd_proc::release()
    {
        auto impl = (evd_proc_impl*)m_obj;
        impl->reduce_ref();
    }

    size_t evd_proc::type_count()
    {
        evd_proc_impl *impl = (evd_proc_impl*)m_obj;
        std::lock_guard<std::mutex> lck(impl->mtx);
        return impl->type_set.size();
    }

    void evd_proc::for_each_type(std::function<void(int, void*)> f,
                                 void *args /*= nullptr*/)
    {
        evd_proc_impl *impl = (evd_proc_impl*)m_obj;
        std::lock_guard<std::mutex> lck(impl->mtx);
        for (auto& type : impl->type_set)
            f(type, args);
    }

    void evd_proc::reg_type(int type)
    {
        evd_proc_impl *impl = (evd_proc_impl*)m_obj;
        std::lock_guard<std::mutex> lck(impl->mtx);
        impl->type_set.insert(type);
    }

    evd_pool::evd_pool()
    {
        m_obj = new evd_pool_impl;
    }

    evd_pool::~evd_pool()
    {
        delete (evd_pool_impl*)m_obj;
    }

    void evd_pool::start(size_t cnt)
    {
        auto impl = (evd_pool_impl*)m_obj;
        std::lock_guard<std::mutex> lck(impl->m_mtx);
        if (cnt == impl->m_lines.size())
            return;

        for (size_t i = 0; i < cnt; ++i) {		//启动cnt个生产线
            prod_line *line = new prod_line;
            line->th = std::thread(evd_pool_impl::thread_proc, line);
            impl->m_lines.push_back(line);
        }
    }

    void evd_pool::stop()
    {
        evd_pool_impl *impl = (evd_pool_impl*)m_obj;
        std::lock_guard<std::mutex> lck(impl->m_mtx);
        for (auto& line : impl->m_lines) {
            line->go_done = false;		//不再继续处理队列中的任务

            //传入订单类型为-1的任务，通知工作线程退出主循环
            pthread_mutex_lock(&line->mtx);
            line->task_list.emplace_front(new evd_job_impl(nullptr));
            pthread_mutex_unlock(&line->mtx);
            pthread_cond_signal(&line->cond);
            line->th.join();
            delete line;
        }
        impl->m_lines.clear();		//销毁所有的生产线
    }

    void evd_pool::reg_proc(evd_proc *proc)
    {
        int32_t product_idx = -1, min_strength = INT32_MAX;
        evd_pool_impl *pool_impl = (evd_pool_impl*)m_obj;
        std::lock_guard<std::mutex> lck(pool_impl->m_mtx);

        //寻找工作强度最小的生产线，将工人安排在该生产线上工作
        for (size_t i = 0; i < pool_impl->m_lines.size(); ++i) {
            auto& line = pool_impl->m_lines[i];
            if (line->work_strength < min_strength) {
                min_strength = line->work_strength;
                product_idx = i;
            }
        }

        //获取到指定生产线之后就可以安排生产了
        evd_proc_impl *proc_impl = (evd_proc_impl*)proc->m_obj;
        proc_impl->on_duty = true;
        proc_impl->product_idx = product_idx;
        proc->for_each_type([&](int type, void *args) {
            proc_impl->add_ref();
            pool_impl->m_arrange[type].insert(proc_impl);
        });

        //指定生产线的工作强度增加
        auto& line = pool_impl->m_lines[product_idx];
        line->work_strength += proc->type_count();
        printf("[evd_pool::reg_proc] 成功注册处理器，生产线流水号：%d，工作强度：%d\n",
               proc_impl->product_idx, line->work_strength);
    }

    void evd_pool::unreg_proc(evd_proc *proc)
    {
        evd_pool_impl *pool_impl = (evd_pool_impl*)m_obj;
        std::lock_guard<std::mutex> lck(pool_impl->m_mtx);

        evd_proc_impl *proc_impl = (evd_proc_impl*)proc->m_obj;
        proc_impl->on_duty = false;		//指示不再继续处理队列中可能存在的与该处理器相关的任务

        //移除与类型相关的处理器
        proc->for_each_type([&](int type, void *args) {
            proc_impl->reduce_ref();
            pool_impl->m_arrange[type].erase(proc_impl);
        });

        //减少指定生产线的工作强度
        auto& line = pool_impl->m_lines[proc_impl->product_idx];
        line->work_strength -= proc->type_count();
        printf("[evd_pool::unreg_proc] 已注销处理器，生产线流水号：%d，工作强度：%d\n",
               proc_impl->product_idx, line->work_strength);
    }

    void evd_pool::job_dispatch(evd_job *job)
    {
        evd_pool_impl *pool_impl = (evd_pool_impl*)m_obj;
        std::lock_guard<std::mutex> lck_outer(pool_impl->m_mtx);

        auto job_impl = (evd_job_impl*)job->m_obj;
        int type = job_impl->m_type;			//判断工作安排中是否需要处理此种类型的工作
        if (pool_impl->m_arrange.end() == pool_impl->m_arrange.find(type))
            return;

        /*
         * 在注册处理器时会为每个工人指定一个固定的生产线, 该生产线
         * 是在注册时点上工作强度最小的那条流水线
         */
        for (auto& proc_impl : pool_impl->m_arrange[type]) {
            int32_t index = proc_impl->product_idx;
            auto& line = pool_impl->m_lines[index];

            job_impl->add_ref();
            proc_impl->add_ref();

            //将任务放在指定生产线的任务队列中
            pthread_mutex_lock(&line->mtx);
            line->task_list.emplace_back(job_impl);
            line->task_list.back().proc = proc_impl;
            pthread_mutex_unlock(&line->mtx);
            pthread_cond_signal(&line->cond);
        }
    }

    void evd_pool_impl::thread_proc(prod_line *line)
    {
        if (!line)
            return;

        task_wrap wrap;
        while (line->go_done) {
            pthread_mutex_lock(&line->mtx);
            if (line->task_list.empty())		//当前没有要处理的订单，一直等待
                pthread_cond_wait(&line->cond, &line->mtx);
            wrap = line->task_list.front();		//先来先处理
            line->task_list.pop_front();
            pthread_mutex_unlock(&line->mtx);

            //收到生产线停工通知
            if (-1 == wrap.job->m_type) {
                wrap.reset();
                break;
            }

            if (wrap.proc->on_duty) {   //该变量指示是否处理队列中的任务
                evd_job *job = dynamic_cast<evd_job*>(wrap.job->m_kobj);
                evd_proc *proc = dynamic_cast<evd_proc*>(wrap.proc->m_kobj);
                proc->process_task(job);
            }
            wrap.reset();
        }
        for (auto& tw : line->task_list)
            tw.reset();
        line->task_list.clear();
    }
}
