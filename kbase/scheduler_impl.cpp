#include "stdafx.h"

namespace crx
{
    scheduler::scheduler()
    {
        auto *impl = new scheduler_impl;
        impl->m_cos.reserve(32);        //预留32个协程
        m_obj = impl;
    }

    scheduler::~scheduler()
    {
        delete (scheduler_impl*)m_obj;
    }

    int scheduler::co_create(std::function<void(scheduler *sch, void *arg)> f, void *arg, bool is_share /*= false*/,
                             const char *comment /*= nullptr*/)
    {
        auto impl = (scheduler_impl*)m_obj;
        if (impl->m_cos.empty()) {      //还未创建主协程，此时没有任何协程
            std::function<void(scheduler *sch, void *arg)> main_co = impl->main_coroutine;
            impl->co_create(main_co, this, this, true, false, "main coroutine");
        }
        return impl->co_create(f, arg, this, false, is_share, comment);
    }

    int scheduler_impl::co_create(std::function<void(scheduler *sch, void *arg)>& f, void *arg, scheduler *sch,
                                  bool is_main_co, bool is_share /*= false*/, const char *comment /*= nullptr*/)
    {
        coroutine_impl *co_impl = nullptr;
        if (m_unused_list.empty()) {
            co_impl = new coroutine_impl;
            co_impl->co_id = (int)m_cos.size();
            m_cos.push_back(co_impl);
        } else {        //复用之前已创建的协程
            int co_id = m_unused_list.front();
            m_unused_list.pop_front();
            co_impl = m_cos[co_id];       //复用时其id不变
            if (co_impl->is_share != is_share)      //复用时堆栈的使用模式改变
                delete []co_impl->stack;
        }

        co_impl->status = CO_READY;
        co_impl->is_share = is_share;
        if (!is_share)      //不使用共享栈时将在协程创建的同时创建协程所用的栈
            co_impl->stack = new char[STACK_SIZE];

        if (comment) {
            bzero(co_impl->comment, sizeof(co_impl->comment));
            strcpy(co_impl->comment, comment);
        }
        co_impl->f = std::move(f);
        co_impl->arg = arg;

        bzero(&co_impl->ctx, sizeof(co_impl->ctx));
        if (!is_main_co) {     //主协程先于其他所有协程被创建
            auto main_co = m_cos[0];
            getcontext(&co_impl->ctx);
            if (is_share)
                co_impl->ctx.uc_stack.ss_sp = main_co->stack;
            else
                co_impl->ctx.uc_stack.ss_sp = co_impl->stack;
            co_impl->ctx.uc_stack.ss_size = STACK_SIZE;
            co_impl->ctx.uc_link = &main_co->ctx;

            auto ptr = (uint64_t)sch;
            makecontext(&co_impl->ctx, (void (*)())coroutine_wrap, 2, (uint32_t)ptr, (uint32_t)(ptr>>32));
        }
        return co_impl->co_id;
    }

    void scheduler_impl::main_coroutine(scheduler *sch, void *arg)
    {
        auto sch_impl = (scheduler_impl*)arg;
    }

    void scheduler_impl::coroutine_wrap(uint32_t low32, uint32_t hi32)
    {
        uintptr_t this_ptr = ((uintptr_t)hi32 << 32) | (uintptr_t)low32;
        auto sch = (scheduler*)this_ptr;
        auto sch_impl = (scheduler_impl*)sch->m_obj;
        coroutine_impl *co_impl = sch_impl->m_cos[sch_impl->m_running];
        co_impl->f(sch, co_impl->arg);

        //协程执行完成后退出，此时处于不可用状态，进入未使用队列等待复用
        sch_impl->m_running = 0;
        co_impl->status = CO_UNKNOWN;
        sch_impl->m_unused_list.push_back(co_impl->co_id);
    }

    void scheduler_impl::save_stack(coroutine_impl *co_impl, const char *top)
    {
        char stub = 0;
        assert(top-&stub <= STACK_SIZE);
        if (co_impl->capacity < top-&stub) {        //容量不足
            co_impl->capacity = top-&stub;
            delete []co_impl->stack;
            co_impl->stack = new char[co_impl->capacity];
        }
        co_impl->size = top-&stub;
        memcpy(co_impl->stack, &stub, co_impl->size);
    }

    void scheduler::co_yield(int co_id)
    {
        co_id = (-1 == co_id) ? 0 : co_id;
        auto sch_impl = (scheduler_impl*)m_obj;
        if (co_id >= sch_impl->m_cos.size() || sch_impl->m_running == co_id)        //co_id无效或对自身进行切换，直接返回
            return;

        auto curr_co = (-1 == sch_impl->m_running) ? nullptr : sch_impl->m_cos[sch_impl->m_running];
        auto yield_co = sch_impl->m_cos[co_id];
        if (!yield_co || CO_UNKNOWN == yield_co->status)      //指定协程无效或者状态指示不可用，同样不发生切换
            return;

        assert(CO_RUNNING != yield_co->status);
        sch_impl->m_running = co_id;
        if (curr_co) {
            auto main_co = sch_impl->m_cos[0];
            if (curr_co->is_share)      //当前协程使用的是共享栈模式，其使用的栈是主协程中申请的空间
                sch_impl->save_stack(curr_co, main_co->stack+STACK_SIZE);
            curr_co->status = CO_SUSPEND;

            //当前协程与待切换的协程都使用了共享栈
            if (curr_co->is_share && yield_co->is_share && CO_SUSPEND == yield_co->status) {
                sch_impl->m_next_co = co_id;
                swapcontext(&curr_co->ctx, &main_co->ctx);      //先切换回主协程
            } else {
                //待切换的协程使用的是共享栈模式并且当前处于挂起状态，恢复其栈空间至主协程的缓冲区中
                if (yield_co->is_share && CO_SUSPEND == yield_co->status)
                    memcpy(main_co->stack+STACK_SIZE-yield_co->size, yield_co->stack, yield_co->size);

                sch_impl->m_next_co = -1;
                yield_co->status = CO_RUNNING;
                swapcontext(&curr_co->ctx, &yield_co->ctx);
            }

            //此时位于主协程中，且主协程用于帮助从一个使用共享栈的协程切换到另一个使用共享栈的协程中
            while (-1 != sch_impl->m_next_co) {
                auto next_co = sch_impl->m_cos[sch_impl->m_next_co];
                sch_impl->m_next_co = -1;
                memcpy(main_co->stack+STACK_SIZE-next_co->size, next_co->stack, next_co->size);
                next_co->status = CO_RUNNING;
                swapcontext(&main_co->ctx, &next_co->ctx);
            }
        } else {        //当前执行流首次发生切换，此时从当前线程进入主执行流
            auto sch = (scheduler*)yield_co->arg;
            yield_co->status = CO_RUNNING;
            yield_co->f(sch, sch->m_obj);
        }
    }

    std::vector<coroutine*> scheduler::get_avail_cos()
    {
        auto sch_impl = (scheduler_impl*)m_obj;
        std::vector<coroutine*> cos;
        for (auto co_impl : sch_impl->m_cos)
            if (CO_UNKNOWN != co_impl->status)
                cos.push_back(co_impl);
        return cos;
    }
}