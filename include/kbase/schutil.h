#pragma once

namespace crx
{
    class CRX_SHARE sigctl : public kobj
    {
    public:
        //增加监听信号及其回调函数，回调函数中的两个参数分别是：1-信号值 2-构造并发送信号时的回调参数
        void add_sig(int signo, std::function<void(int, uint64_t)> callback);

        //移除监听信号
        void remove_sig(int signo);
    };

    class CRX_SHARE timer : public kobj
    {
    public:
        /*
         * 启动定时器 (终止定时器只需要将timer类销毁即可)
         * @delay: 初始延迟
         * @interval: 间隔
         * 单位均为毫秒
         */
        void start(uint64_t delay, uint64_t interval);

        //重置定时器回到初始启动状态
        void reset();

        //分离定时器
        void detach();
    };

    class CRX_SHARE timer_wheel : public kobj
    {
    public:
        /*
         * 增加处理器，定时轮在延迟delay ms之后将执行相应的回调函数
         *      **若需要处理指定时间点触发的事件以及周期为天及以上的事件时可以考虑使用crontab
         *      **若需要处理精度更高的事件比如每隔10ms甚至每隔1ms触发的事件时，那么需要设计精度更高的专用定时器
         *
         * 在绝大多数场景下，timer_wheel已基本够用，但在负载较高的情况下定时轮可能不够灵敏，因此上层在使用时应限制
         * 添加处理器个数的上限，特别是尽量避免处理一个请求就添加一个对应的处理器，能复用尽量复用，对于某些相对耗时的
         * 重型请求尽量拆分，比如在一个协程中处理完一部分之后co_yield/co_sleep，等待上下文回切之后继续计算
         *
         * @delay 延迟时间，单位为毫秒
         * @f & arg 回调函数及回调参数
         * @return 若延迟时间 delay > 23:59:59x1000 ms，那么处理器添加失败
         */
        bool add_handler(size_t delay, std::function<void(int64_t)> f, int64_t arg = 0);
    };

    class CRX_SHARE event : public kobj
    {
    public:
        void send_signal(int64_t signal);       //发送事件信号

        void detach();      //分离事件
    };

    class CRX_SHARE udp_ins : public kobj
    {
    public:
        //获取使用的端口
        uint16_t get_port();

        //发送udp数据包(包的大小上限为65536个字节)
        void send_data(const char *ip_addr, uint16_t port, const char *data, size_t len);

        void detach();      //分离udp实例
    };
}
