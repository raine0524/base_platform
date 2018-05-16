#include "stdafx.h"

namespace crx
{
    static const char* get_items(const char *p,int index)		//index从1开始计算
    {
        assert(p);
        if (index <= 1)
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
            m_proc_pid = std::string(buf);			//m_proc_pid指向该进程内存映像的根目录
        }

        int get_cpu_process_occupy()
        {
            FILE *fd = fopen (std::string(m_proc_pid+"stat").c_str(), "r");		//以读方式打开文件
            char line_buff[1024] = {0};
            fgets(line_buff, sizeof(line_buff), fd);

            process_cpu_occupy_t t;
            const char* q = get_items(line_buff, 14);		//取得从第14项开始的起始指针
            sscanf(q, "%u %u %u %u", &t.utime, &t.stime, &t.cutime, &t.cstime);	//格式化第14,15,16,17项

            fclose(fd);     //关闭文件fd
            return (t.utime + t.stime + t.cutime + t.cstime);		//计算当前进程占用cpu的总用时
        }

        static int get_cpu_total_occupy()
        {
            FILE *fd = fopen ("/proc/stat", "r");		//以读方式打开
            char buff[1024] = {0};
            fgets(buff, sizeof(buff), fd);

            char name[64] = {0};
            total_cpu_occupy_t t;
            //格式化第一行中的前5项
            sscanf (buff, "%s %u %u %u %u", name, &t.user, &t.nice,&t.system, &t.idle);

            fclose(fd);     //关闭文件fd
            return (t.user + t.nice + t.system + t.idle);			//计算cpu总的运行时长
        }

        pid_t m_pid;
        std::string m_proc_pid;
        std::unordered_map<std::string, int> m_proc_status;
        std::unordered_map<std::string, int> m_proc_meminfo;
    };

    statis::statis(pid_t pid)
    {
        m_impl = std::make_shared<statis_imp>(pid);
    }

    process_mem_occupy_t statis::get_process_mem_occupy()
    {
        auto impl = std::dynamic_pointer_cast<statis_imp>(m_impl);
        process_mem_occupy_t mem_meas;
        memset(&mem_meas, 0, sizeof(mem_meas));
        std::string mem_info = impl->m_proc_pid+"status";
        FILE *fd = fopen(mem_info.c_str(), "r");		//打开进程内存占用映像文件"/proc/{%pid%}/status"
        if (!fd) {
            printf("[statis::get_process_mem_occupy WARN] 打开文件 %s 失败，不再获取进程内存占用信息\n", mem_info.c_str());
            return mem_meas;
        }

        char key[128];
        int32_t value;
        char line_buff[256] = {0};		//读取行的缓冲区
        while (fgets(line_buff, sizeof(line_buff), fd)) {
            memset(key, 0, sizeof(key));
            sscanf(line_buff, "%s %d", key, &value);		//格式化每一行的键值对
            impl->m_proc_status[key] = value;
        }
        fclose(fd);

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
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_milliseconds));
        //度量间隔结束时点处的cpu总用时及进程的cpu占用情况
        int total_cpu_end = impl->get_cpu_total_occupy();
        int pro_cpu_end = impl->get_cpu_process_occupy();
        return 1.0f*(pro_cpu_end-pro_cpu_start)/(total_cpu_end-total_cpu_start);
    }

    sys_mem_dist_t statis::get_sys_mem_dist()
    {
        sys_mem_dist_t sys_mem;
        memset(&sys_mem, 0, sizeof(sys_mem));
        FILE *fd = fopen("/proc/meminfo", "r");
        if (!fd) {
            printf("[statis::get_sys_mem_dist WARN] 打开文件 /proc/meminfo 失败，不再获取系统内存信息\n");
            return sys_mem;
        }

        char key[128];
        int32_t value;
        char line_buff[256] = {0};		//获取行的缓冲区
        auto impl = std::dynamic_pointer_cast<statis_imp>(m_impl);
        while (fgets(line_buff, sizeof(line_buff), fd)) {
            memset(key, 0, sizeof(key));
            sscanf(line_buff, "%s %d", key, &value);		//格式化每一行的键值对
            impl->m_proc_meminfo[key] = value;
        }
        fclose(fd);

        sys_mem.mem_total = impl->m_proc_meminfo["MemTotal:"];
        return sys_mem;
    }

    int32_t statis::get_open_file_handle()
    {
        auto impl = std::dynamic_pointer_cast<statis_imp>(m_impl);
        DIR *dir = opendir(std::string(impl->m_proc_pid+"fd").c_str());		//打开/proc/{%pid/fd目录
        if (!dir) {
            perror("get_open_file_handle::opendir.\n");
            return 0;
        }

        int32_t total = 0;
        struct dirent *ptr;
        while ((ptr = readdir(dir))) {
            if (!strcmp(ptr->d_name, ".") || !strcmp(ptr->d_name, ".."))
                continue;

            total++;		//统计打开文件的总数
        }
        closedir(dir);
        return total-2;		//去掉标准输入和输出的统计
    }

    int32_t statis::get_disk_usage(const char *dir)
    {
        DIR *dp = opendir(dir);
        if (!dp) {
            perror("get_disk_usage::opendir");
            return 0;
        }

        int32_t total_bytes = 0;
        struct dirent *entry = nullptr;
        struct stat st;
        while ((entry = readdir(dp))) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
                continue;		//过滤本级和上级目录

            std::string file = std::string(dir)+"/"+entry->d_name;
            if (-1 == lstat(file.c_str(), &st)) {			//获取文件的属性
                perror("get_disk_usage::lstat");
                continue;
            }

            if (S_ISDIR(st.st_mode))		//若该文件是一个目录，则递归获取该目录中各个文件的大小
                total_bytes += get_disk_usage(file.c_str());
            else		//反之则直接计入磁盘的总占用量
                total_bytes += st.st_size;
        }
        closedir(dp);
        return total_bytes;
    }
}
