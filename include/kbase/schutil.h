#pragma once

#include "crx_pch.h"

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
         * 增加定时轮处理器，定时轮将根据延迟以及一个tick的间隔时间选择适当的槽，将回调函数放在这个槽内，等到
         * 指针指向这个槽之后，执行相应的回调函数
         *
         * @delay 延迟时间，单位为毫秒
         * @callback 回调函数
         * @return 若延迟时间>interval*slot，那么处理器添加失败
         */
        bool add_handler(uint64_t delay, std::function<void()> callback);
    };

    class CRX_SHARE event : public kobj
    {
    public:
        void send_signal(int signal);       //发送事件信号

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
