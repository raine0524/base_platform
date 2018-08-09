#include "stdafx.h"

namespace crx
{
    static const char* get_items(const char *p,int index)		//index从1开始计算
    {
        if (!p || index <= 1)
            return p;

        int count = 0;	//统计空格数
        for (unsigned int i = 0; i < strlen(p); i++) {
            if (' ' == *p) {		//第count个空格紧接的下一项为所要查找的第index项
                if (++count == index-1) {
                    p++;				//将p指向该项并返回
                    break;
                }
            }
            p++;
        }
        return p;
    }

    class statis_imp : public impl
    {
    public:
        statis_imp(pid_t pid)
                :m_pid(pid)
        {
            char buf[64] = {0};
            sprintf(buf, "/proc/%d/", pid);
            m_proc_pid = buf;       //m_proc_pid指向该进程内存映像的根目录
        }

        int get_cpu_process_occupy()
        {
            std::ifstream fin(m_proc_pid+"stat");
            if (!fin)
                return -1;

            std::string line;
            std::getline(fin, line);
            const char* q = get_items(line.data(), 14);		//取得从第14项开始的起始指针

            process_cpu_occupy_t t;
            sscanf(q, "%u %u %u %u", &t.utime, &t.stime, &t.cutime, &t.cstime);	//格式化第14,15,16,17项
            return (t.utime + t.stime + t.cutime + t.cstime);		//计算当前进程占用cpu的总用时
        }

        static int get_cpu_total_occupy()
        {
            std::ifstream fin("/proc/stat");
            if (!fin)
                return -1;

            std::string line;
            std::getline(fin, line);

            char name[64] = {0};
            total_cpu_occupy_t t;       //格式化第一行中的前5项
            sscanf (&line[0], "%s %u %u %u %u", name, &t.user, &t.nice,&t.system, &t.idle);
            return (t.user + t.nice + t.system + t.idle);			//计算cpu总的运行时长
        }

        pid_t m_pid;
        std::string m_proc_pid;
        std::map<std::string, int> m_proc_status;
        std::map<std::string, int> m_proc_meminfo;
    };

    statis::statis(pid_t pid)
    {
        m_impl = std::make_shared<statis_imp>(pid);
    }

    process_mem_occupy_t statis::get_process_mem_occupy()
    {
        auto impl = std::dynamic_pointer_cast<statis_imp>(m_impl);
        process_mem_occupy_t mem_meas;
        bzero(&mem_meas, sizeof(mem_meas));
        std::string mem_info = impl->m_proc_pid+"status";
        std::ifstream fin(mem_info);        //打开进程内存占用映像文件"/proc/{%pid%}/status"
        if (!fin)
            return mem_meas;

        char key[128];
        int value;
        std::string line;
        while (std::getline(fin, line)) {
            bzero(&key, sizeof(key));
            sscanf(&line[0], "%s %d", key, &value);		//格式化每一行的键值对
            impl->m_proc_status[key] = value;
        }

        //获取该结构体字段相关的键值对
        mem_meas.vm_peak = impl->m_proc_status["VmPeak:"];
        mem_meas.vm_size = impl->m_proc_status["VmSize:"];
        mem_meas.vm_hwm = impl->m_proc_status["VmHWM:"];
        mem_meas.vm_rss = impl->m_proc_status["VmRSS:"];
        return mem_meas;
    }

    float statis::get_process_cpu_occupy(int delay_milliseconds)
    {
        auto impl = std::dynamic_pointer_cast<statis_imp>(m_impl);
        //获取度量间隔起始时点处的cpu总用时及进程的cpu占用情况
        int total_cpu_start = impl->get_cpu_total_occupy();
        int pro_cpu_start = impl->get_cpu_process_occupy();

        //最好采用一个定时器周期性(比如5秒)的采集cpu的使用情况
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_milliseconds));

        //度量间隔结束时点处的cpu总用时及进程的cpu占用情况
        int total_cpu_end = impl->get_cpu_total_occupy();
        int pro_cpu_end = impl->get_cpu_process_occupy();
        return 1.0f*(pro_cpu_end-pro_cpu_start)/(total_cpu_end-total_cpu_start);
    }

    sys_mem_dist_t statis::get_sys_mem_dist()
    {
        sys_mem_dist_t sys_mem;
        bzero(&sys_mem, sizeof(sys_mem));
        std::ifstream fin("/proc/meminfo");
        if (!fin)
            return sys_mem;

        char key[128];
        int32_t value;
        std::string line;
        auto impl = std::dynamic_pointer_cast<statis_imp>(m_impl);
        while (std::getline(fin, line)) {
            bzero(key, sizeof(key));
            sscanf(&line[0], "%s %d", key, &value);		//格式化每一行的键值对
            impl->m_proc_meminfo[key] = value;
        }

        sys_mem.mem_total = impl->m_proc_meminfo["MemTotal:"];
        return sys_mem;
    }

    int statis::get_open_file_handle()
    {
        int total = 0;
        auto impl = std::dynamic_pointer_cast<statis_imp>(m_impl);
        std::string root_dir = impl->m_proc_pid+"fd";
        depth_first_traverse_dir(root_dir.c_str(), [&](std::string& file) {
            total++;
        });
        return total-3;     //去掉标准输入/输出/出错
    }

    int statis::get_disk_usage(const char *dir)
    {
        int total_bytes = 0;
        depth_first_traverse_dir(dir, [&](std::string& file) {      //目录已被过滤
            total_bytes += get_file_size(file.c_str());
        });
        return total_bytes;
    }
}
