#include "stdafx.h"

namespace crx
{
    static const char b64_table[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    static const char reverse_table[128] =
            {
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
            52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
            64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
            15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
            64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
            41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64
            };

    std::string base64_encode(const char *data, int len)
    {
        if (!data || len <= 0)
            return "";

        //带编码长度检查
        if (len > (std::numeric_limits<std::string::size_type>::max() / 4u) * 3u)
            return "";

        // Use = signs so the end is properly padded.
        std::string retval((size_t)(((len + 2) / 3) * 4), '=');
        size_t outpos = 0;
        int bits_collected = 0;
        unsigned int accumulator = 0;

        for (size_t i = 0; i < len; ++i) {
            accumulator = (accumulator << 8) | (data[i] & 0xffu);
            bits_collected += 8;
            while (bits_collected >= 6) {
                bits_collected -= 6;
                retval[outpos++] = b64_table[(accumulator >> bits_collected) & 0x3fu];
            }
        }

        if (bits_collected > 0) { // Any trailing bits that are missing.
            assert(bits_collected < 6);
            accumulator <<= 6 - bits_collected;
            retval[outpos++] = b64_table[accumulator & 0x3fu];
        }

        assert(outpos >= (retval.size() - 2) && outpos <= retval.size());
        return retval;
    }

    std::string base64_decode(const char *data, int len)
    {
        if (!data || len <= 0)
            return "";

        std::string retval;
        int bits_collected = 0;
        unsigned int accumulator = 0;

        for (size_t i = 0; i < len; ++i) {
            const int c = data[i];
            if (std::isspace(c) || c == '=')
                continue;		// Skip whitespace and padding. Be liberal in what you accept.

            if ((c > 127) || (c < 0) || (reverse_table[c] > 63))
                return "";

            accumulator = (accumulator << 6) | reverse_table[c];
            bits_collected += 6;
            if (bits_collected >= 8) {
                bits_collected -= 8;
                retval += (char)((accumulator >> bits_collected) & 0xffu);
            }
        }
        return retval;
    }

    int setnonblocking(int fd)
    {
        if (fd < 0) return -1;
        if (__glibc_unlikely(-1 == fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK))) {
            if (errno)
                log_error(g_lib_log, "setnonblocking %d failed: %s\n", fd, strerror(errno));
            return -1;
        }
        return 0;
    }

    int setcloseonexec(int fd)
    {
        if (fd < 0) return -1;
        if (__glibc_unlikely(-1 == fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC))) {
            if (errno)
                log_error(g_lib_log, "setcloseonexec %d failed: %s\n", fd, strerror(errno));
            return -1;
        }
        return 0;
    }

    int64_t measure_time(std::function<void()> f)
    {
        timeval start = {0}, end = {0};
        gettimeofday(&start, nullptr);
        f();
        gettimeofday(&end, nullptr);
        return ((int64_t)(end.tv_sec-start.tv_sec))*1000000+(int64_t)(end.tv_usec-start.tv_usec);
    }

    std::string get_work_space()
    {
        std::string path(256, 0);
        ssize_t ret = readlink("/proc/self/exe", &path[0], path.size()-1);		//读取链接指向的真实文件路径
        if (-1 == ret)
            path.clear();
        else
            path.resize((size_t)ret);
        return path;
    }

    std::string run_shell_cmd(const char *cmd_string)
    {
        FILE *pf = popen(cmd_string, "r");		//执行shell命令，命令的输出通过管道返回
        if (__glibc_unlikely(nullptr == pf)) {
            if (errno)
                log_error(g_lib_log, "popen failed: %s\n", strerror(errno));
            return "";
        }

        std::string result;
        char buffer[1024] = {0};
        while (fgets(buffer, 1024, pf))		//不停地获取shell命令的输出，命令执行完毕后统一返回所有输出
            result.append(buffer);
        pclose(pf);
        return result;
    }

    void dump_segment()
    {
        void *buffer[1024] = {0};
        int size = backtrace(buffer, 1024);		//获取函数调用栈中每个调用点的地址
        char **strings = backtrace_symbols(buffer, size);		//将地址转换为函数名及其在函数内部以十六进制表示的偏移量
        if (__glibc_unlikely(!strings)) {
            if (errno)
                log_error(g_lib_log, "backtrace_symbols failed: %s\n", strerror(errno));
            return;
        }

        //将原始的转换符号写入文件中，将其放在[ORIGIN]节区中
        char file_name[128] = {0}, cmd_string[256] = {0};
        sprintf(file_name, "dump_seg.%s.%d", g_server_name.c_str(), getpid());
        FILE *core_file = fopen(file_name, "a");
        fprintf(core_file, "[ORIGIN]\n");
        for (int i = 0; i < size; ++i)
            fprintf(core_file, "%s\n", strings[i]);
        fprintf(core_file, "\n[CONVERT]\n");

        //过滤掉开始的一条(调用dump_segment的记录)和最后两条记录
        for (int i = 1; i < size-2; ++i) {
            std::string sym_str = strings[i];
            size_t first = sym_str.find('[');
            size_t last = sym_str.rfind(']');
            std::string ins_addr(strings[i]+first+1, last-first-1);

            /*
             * judge whether shared library or not, example:
             * 			so_name				func_symbol				  offset			address
             * ../so/libkbase_d.so(_ZN3crx7console3runEiPPc+0xb9) [0x7fc1fdf920b3]
             */
            size_t left_bracket = sym_str.find('(');
            std::string elf_name(strings[i], left_bracket);		//获取当前指令所在映像对应的elf文件
            if (std::string::npos != elf_name.rfind(".so")) {		//so
                unsigned long so_ins_addr = std::stoul(ins_addr, nullptr, 0);
                size_t plus_symbol = sym_str.find('+');
                size_t right_bracket = sym_str.find(')');
                std::string ins_off_str(strings[i]+plus_symbol+1, right_bracket-plus_symbol-1);
                unsigned long ins_off = std::stoul(ins_off_str, nullptr, 0);
                unsigned long so_func_addr = so_ins_addr-ins_off;		//获取函数地址

                Dl_info so_info;
                if (!dladdr((void*)so_func_addr, &so_info))		//根据函数地址获取so文件的信息
                    continue;

                //计算当前指令在so文件中的相对偏移量
                unsigned long rela_off = so_ins_addr-(unsigned long)so_info.dli_fbase;
                //根据相对偏移计算当前指令对应的源文件及行号
                sprintf(cmd_string, "addr2line 0x%lx -e %s", rela_off, elf_name.c_str());
            } else {		//executable
                sprintf(cmd_string, "addr2line %s -e %s -f", ins_addr.c_str(), g_server_name.c_str());
            }

            std::string res = run_shell_cmd(cmd_string);
            if (res.empty())
                continue;

            auto str_vec = crx::split(res.data(), res.size(), "\n");
            if (str_vec.size() == 2) {		//将修饰后的函数名变换为源文件中对应的函数名
                auto symbol_mangle = std::string(str_vec[0].data, str_vec[0].len);
                sprintf(cmd_string, "c++filt %s", symbol_mangle.c_str());
                res = std::string(str_vec[1].data, str_vec[1].len) +" => "+run_shell_cmd(cmd_string);
            }
            fprintf(core_file, "%s", res.c_str());
        }
        fprintf(core_file, "%s", "\n");
        fclose(core_file);
        free(strings);
    }

    int mkdir_multi(const char *path, mode_t mode /*= 0755*/)
    {
        if (!path) return -1;
        std::string temp_path = path;
        if ('/' != temp_path.back())	//若原始路径不是以'/'结尾，则构造一个'/'结尾的目录路径
            temp_path.append("/");

        //根目录总是存在的
        const char *pch = ('/' == temp_path.front()) ? temp_path.c_str()+1 : temp_path.c_str();
        while (true) {
            pch = strstr(pch, "/");		//从上次查找处继续搜索下一个'/'
            if (!pch) break;

            //构造中间目录对应的目录名
            std::string middle_dir = temp_path.substr(0, pch-temp_path.c_str());
            if (access(middle_dir.c_str(), F_OK))		//创建由pch指向的中间目录
                mkdir(middle_dir.c_str(), mode);
            pch++;
        }
        return 0;
    }

    std::string charset_convert(const char *from_charset, const char *to_charset, const char *src_data, size_t src_len)
    {
        if (!from_charset || !to_charset || !src_data || !src_len)
            return "";

        /*
         * A chinese character in variable-length charset occupies 3 bytes at most,
         * so 5 times of source size reserved is enough
         */
        std::string res(src_len*5, 0);
        char *outbuf = &res[0];
        size_t outbytes = res.size();

        iconv_t cd = iconv_open(to_charset, from_charset);
        if (__glibc_unlikely(cd == (void*)-1))
            log_error(g_lib_log, "iconv_open failed: %s\n", strerror(errno));

        if (__glibc_unlikely(-1 == iconv(cd, (char**)&src_data, &src_len, &outbuf, &outbytes)))
            log_error(g_lib_log, "iconv failed: %s\n", strerror(errno));
        res.resize(res.size()-outbytes);
        iconv_close(cd);
        return res;
    }

    std::string get_local_addr(ADDR_TYPE type, const char *net_card /*= "eth0"*/)
    {
        std::string local_addr;
        if (!net_card)
            return local_addr;

        size_t cmd = 0;
        if (ADDR_MAC == type)
            cmd = SIOCGIFHWADDR;
        else if (ADDR_IP == type)
            cmd = SIOCGIFADDR;
        else
            return local_addr;

        int sock_fd = socket(AF_INET, SOCK_STREAM, 0);      //获取MAC或者IP地址都需要用到socket套接字
        if (__glibc_unlikely(-1 == sock_fd)) {
            log_error(g_lib_log, "socket create failed: %s\n", strerror(errno));
            return local_addr;
        }

        struct ifreq ifr;
        bzero(&ifr, sizeof(ifr));
        strcpy(ifr.ifr_name, net_card);

        try {
            if (__glibc_unlikely(-1 == ioctl(sock_fd, cmd, &ifr))) {      //根据不同的cmd获取不同的ifr结构体
                log_error(g_lib_log, "ioctl failed: %s\n", strerror(errno));
                throw -1;
            }

            char buffer[32] = {0};
            if (ADDR_MAC == type) {
                sprintf(buffer, "%02x:%02x:%02x:%02x:%02x:%02x",
                        (unsigned char)ifr.ifr_hwaddr.sa_data[0],
                        (unsigned char)ifr.ifr_hwaddr.sa_data[1],
                        (unsigned char)ifr.ifr_hwaddr.sa_data[2],
                        (unsigned char)ifr.ifr_hwaddr.sa_data[3],
                        (unsigned char)ifr.ifr_hwaddr.sa_data[4],
                        (unsigned char)ifr.ifr_hwaddr.sa_data[5]);
            } else {        // ADDR_IP == type
                struct sockaddr_in *addr = (struct sockaddr_in*)&ifr.ifr_addr;
                strcpy(buffer, inet_ntoa(addr->sin_addr));
            }
            local_addr = buffer;
        } catch (int exp) {}

        close(sock_fd);
        return local_addr;
    }

    datetime get_datetime(timeval *tv /*= nullptr*/)
    {
        timeval now;
        if (!tv) {
            gettimeofday(&now, nullptr);
            tv = &now;
        }

        datetime dt;
        char time_buffer[32] = {0};
        dt.t = localtime(&tv->tv_sec);		//获取当前时点并将其转化为tm结构体
        strftime(time_buffer, sizeof(time_buffer), "%Y%m%d", dt.t);
        dt.date = (uint32_t)atoi(time_buffer);
        strftime(time_buffer, sizeof(time_buffer), "%H%M%S", dt.t);
        dt.time = (uint32_t)atoi(time_buffer)*1000+(uint32_t)(tv->tv_usec/1000);		//时间精确到毫秒级
        dt.time_stamp = tv->tv_sec*1000+tv->tv_usec/1000;
        return dt;
    }

    int get_week_day(unsigned int date)
    {
        int year = date/10000, month = (date%10000)/100, day = (date%10000)%100;
        int c = year/100, y = year%100;
        if (1 == month || 2 == month) {
            c = (year-1)/100;
            y = (year-1)%100;
            month += 12;
        }

        int week_day = y+y/4+c/4-2*c+26*(month+1)/10+day-1;     //Zeller Formula
        week_day = week_day >= 0 ? (week_day%7) : (week_day%7+7);
        if (!week_day)
            week_day = 7;
        return week_day;
    }

    int get_Nth_day(unsigned int spec_day, int N)
    {
        tm spec_time;
        spec_time.tm_year = spec_day/10000-1900;
        spec_time.tm_mon = (spec_day%10000)/100-1;
        spec_time.tm_mday = spec_day%100;
        spec_time.tm_hour = 12;
        spec_time.tm_min = spec_time.tm_sec = 0;
        time_t spec_sec = mktime(&spec_time);
        spec_sec += N*24*3600;
        tm *p = localtime(&spec_sec);
        return (p->tm_year+1900)*10000+(p->tm_mon+1)*100+p->tm_mday;
    }

    int get_file_size(const char *file)
    {
        if (!file) return -1;
        if (__glibc_unlikely(access(file, F_OK))) {   //判断是否存在该文件
            return -1;
        }

        struct stat st = {0};
        if (__glibc_unlikely(-1 == stat(file, &st)))
            return -1;
        else
            return (int)st.st_size;
    }

    int find_nth_pos(const std::string& src, const char *pattern, int n)
    {
        if (src.empty() || !pattern)
            return -1;

        if (n < 0) {		//找src中的最后一个子串
            size_t pos = src.rfind(pattern);
            if (pos != std::string::npos)		//find
                return (int)pos;
            else
                return -1;
        }

        int cnt = -1;
        const char *pch = src.c_str()-1;
        while (true) {		//总是从当前字符的下一个字符开始查找模式串pattern
            pch = strstr(pch+1, pattern);
            if (!pch)		//not find
                break;
            else
                cnt++;			//记录在原始串中已经找到的模式串的个数 cnt+1

            if (cnt == n)
                return (int)(pch-src.c_str());
        }
        return -1;
    }

    std::string& trim(std::string& s)
    {
        if (!s.empty())
            s.erase(0, s.find_first_not_of(" \t"));   //清除字符串前面的空格及制表符
        if (!s.empty())
            s.erase(s.find_last_not_of(" \t")+1);     //清除字符串尾部的空格及制表符
        return s;
    }

    std::vector<mem_ref> split(const char *src, size_t len, const char *delim)
    {
        if (!src || !len || !delim)
            return std::vector<mem_ref>();

        size_t delim_len = strlen(delim);
        std::vector<mem_ref> tokens;
        const char *start = src, *end = src+len, *pos = nullptr;
        while (start < end) {
            pos = strstr(start, delim);
            if (!pos || pos > end-delim_len) {      //未找到子串或已超出查找范围
                tokens.push_back(mem_ref(start, end-start));
                break;
            }

            if (start != pos)
                tokens.push_back(mem_ref(start, pos-start));
            start = pos+delim_len;
        }
        return tokens;
    }

    bool convert_ipaddr(const char *server, std::string& ip_addr)
    {
        in_addr_t ret = inet_addr(server);      //判断服务器的地址是否为点分十进制的ip地址
        if (INADDR_NONE != ret) {               //已经是ip地址
            ip_addr = server;
            return true;
        }

        //是域名形式的主机地址
        struct hostent *hent = gethostbyname(server);		//根据域名获取主机相关信息
        if (!hent)
            return false;

        //只转换ipv4协议格式的ip地址
        if (AF_INET != hent->h_addrtype || !hent->h_addr_list[0]) {
            log_error(g_lib_log, "addr type %d != *AF_INET* or empty addr list\n", hent->h_addrtype);
            return false;
        }

        ip_addr.resize(32);     //取addr_list列表中的第一个地址转化为ip地址
        std::string oval = ip_addr;
        inet_ntop(hent->h_addrtype, hent->h_addr_list[0], &ip_addr[0], (socklen_t)ip_addr.size());
        if (ip_addr == oval)
            return false;
        else
            return true;
    }

    void depth_first_traverse_dir(const char *root_dir, std::function<void(std::string&)> f,
            bool with_path /*= true*/, bool filter_dir /*= true*/)
    {
        DIR *dir = opendir(root_dir);
        if (__glibc_unlikely(!dir)) {
            log_error(g_lib_log, "opendir failed: %s\n", strerror(errno));
            return;
        }

        struct stat st = {0};
        struct dirent *ent = nullptr;
        while ((ent = readdir(dir))) {
            if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
                continue;

            std::string file_name = root_dir;
            file_name = file_name+"/"+ent->d_name;
            if (__glibc_unlikely(-1 == stat(file_name.c_str(), &st))) {
                log_error(g_lib_log, "stat failed: %s\n", strerror(errno));
                return;
            }

            if (S_ISDIR(st.st_mode)) {  //directory
                depth_first_traverse_dir(file_name.c_str(), f, with_path);
                if (filter_dir)     //若选择过滤目录则直接进行下一轮循环
                    continue;
            }

            if (!with_path)
                file_name = ent->d_name;
            f(file_name);
        }
        closedir(dir);
    }
}
