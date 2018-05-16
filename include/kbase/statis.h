#pragma once

namespace crx
{
    struct process_cpu_occupy_t
    {
        int utime;		//该任务在用户态运行的时间，单位为jiffies
        int stime;		//该任务在核心态运行的时间，单位为jiffies
        int cutime;	//所有已死线程在用户态运行的时间，单位为jiffies
        int cstime;		//所有已死在核心态运行的时间，单位为jiffies
    };

    struct total_cpu_occupy_t
    {
        int user;		//从系统启动开始累计到当前时刻，处于用户态的运行时间，不包含nice值为负的进程。
        int nice;		//从系统启动开始累计到当前时刻，nice值为负的进程所占用的CPU时间
        int system;	//从系统启动开始累计到当前时刻，处于核心态的运行时间
        int idle;			//从系统启动开始累计到当前时刻，除IO等待时间以外的其它等待时间
    };

    struct process_mem_occupy_t
    {
        int vm_peak;		//进程所占虚拟内存的峰值
        int vm_size;		//进程所占用的虚拟内存的值
        int vm_hwm;		//进程所占物理内存的峰值
        int vm_rss;		//进程所占用的实际物理内存
    };

    struct sys_mem_dist_t
    {
        int mem_total;		//系统的内存总量
    };

    class CRX_SHARE statis : public kobj
    {
    public:
        statis(pid_t pid);

        virtual ~statis() {}

        //获取进程的内存占用情况(包括虚拟内存和实际的物理内存)
        process_mem_occupy_t get_process_mem_occupy();

        //获取进程的CPU占用情况(delay_milliseconds表示度量间隔)
        float get_process_cpu_occupy(int delay_milliseconds);

        //获取整个系统的内存分布信息
        sys_mem_dist_t get_sys_mem_dist();

        //获取打开的文件句柄的个数(排除系统的标准输入和标准输出)
        int32_t get_open_file_handle();

        //获取指定目录所占用的磁盘大小
        static int32_t get_disk_usage(const char *dir);
    };
}
