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
        if (!data || len <= 0)		//接口安全性检查
            return "";

        //带编码长度检查
        if (len > (std::numeric_limits<std::string::size_type>::max() / 4u) * 3u)
            return "";

        // Use = signs so the end is properly padded.
        std::string retval((((len + 2) / 3) * 4), '=');
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
        if (!data || len <= 0)		//接口安全性检查
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

    bool setnonblocking(int fd)
    {
        if (fd < 0)		//接口安全性检查
            return false;

        if (-1 == fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK)) {
            perror("crx::setnonblocking failed");
            return false;
        }
        return true;
    }

    bool setcloseonexec(int fd)
    {
        if (fd < 0)		//接口安全性检查
            return false;

        if (-1 == fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC)) {
            perror("crx::setcloseonexec failed");
            return false;
        }
        return true;
    }

    bool symlink_exist(const char *file)
    {
        if (!file)		//接口参数安全性检查
            return false;

        struct stat buf;
        return (0 == lstat(file, &buf));
    }

    void dump_segment()
    {
        void *buffer[1024] = {0};
        size_t size = backtrace(buffer, 1024);		//获取函数调用栈中每个调用点的地址
        char **strings = backtrace_symbols(buffer, size);		//将地址转换为函数名及其在函数内部以十六进制表示的偏移量
        if (!strings) {
            perror("backtrace_symbols failed");
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
            int first = sym_str.find('[');
            int last = sym_str.rfind(']');
            std::string ins_addr(strings[i]+first+1, last-first-1);

            /**
             * judge whether shared library or not, example:
             * 			so_name				func_symbol				  offset			address
             * ../so/libkbase_d.so(_ZN3crx7console3runEiPPc+0xb9) [0x7fc1fdf920b3]
             */
            int left_bracket = sym_str.find('(');
            std::string elf_name(strings[i], left_bracket);		//获取当前指令所在映像对应的elf文件
            if (std::string::npos != elf_name.rfind(".so")) {		//so
                unsigned long so_ins_addr = std::stoul(ins_addr, nullptr, 0);
                int plus_symbol = sym_str.find('+');
                int right_bracket = sym_str.find(')');
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

            auto str_vec = crx::split(res, "\n");
            if (str_vec.size() == 2) {		//将修饰后的函数名变换为源文件中对应的函数名
                sprintf(cmd_string, "c++filt %s", str_vec[0].c_str());
                res = str_vec[1]+" => "+run_shell_cmd(cmd_string);
            }
            fprintf(core_file, "%s", res.c_str());
        }
        fprintf(core_file, "%s", "\n\n");
        fclose(core_file);
        free(strings);
    }

    void mkdir_multi(const char *path, mode_t mode /*= 0755*/)
    {
        if (!path)		//接口参数安全性检查
            return;

        std::string temp_path = path;
        if ('/' != temp_path.back())	//若原始路径不是以'/'结尾，则构造一个'/'结尾的目录路径
            temp_path.append("/");

        const char *pch = temp_path.c_str();
        while (true) {
            pch = strstr(pch, "/");		//从上次查找处继续搜索下一个'/'
            if (!pch)
                break;

            //构造中间目录对应的目录名
            std::string middle_dir = temp_path.substr(0, pch-temp_path.c_str());
            if (access(middle_dir.c_str(), F_OK))		//创建由pch指向的中间目录
                mkdir(middle_dir.c_str(), mode);
            pch++;
        }
    }

    std::string charset_convert(const char *from_charset, const char *to_charset, const char *src_data, int src_len)
    {
        if (!src_data || src_len <= 0)		//接口安全性检查
            return "";

        /**
         * A chinese character in variable-length charset occupies 3 bytes at most,
         * so 5 times of source size reserved is enough
         */
        std::string res(src_len*5, 0);
        char *outbuf = &res[0];
        size_t inbytes = src_len, outbytes = res.size();
        iconv_t cd = nullptr;
        if ((cd = iconv_open(to_charset, from_charset)) == (void*)-1)
            perror("charset_convert::iconv_open");
        if (-1 == iconv(cd, (char**)&src_data, &inbytes, &outbuf, &outbytes))
            perror("charset_convert::iconv");
        res.resize(strlen(res.c_str()));
        iconv_close(cd);
        return res;
    }

    std::string get_local_addr(ADDR_TYPE type, const char *net_card /*= "eth0"*/)
    {
        if (!net_card)		//接口安全性检查
            return "";

        char buffer[32] = {0};
        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        strcpy(ifr.ifr_name, net_card);
        int sock_fd = ::socket(AF_INET, SOCK_STREAM, 0);		//获取MAC或者IP地址都需要用到socket套接字
        if (-1 == sock_fd) {
            perror("get_local_addr::socket");
            return "";
        }
        int cmd = -1;
        switch (type) {
            case ADDR_MAC: cmd = SIOCGIFHWADDR; break;
            case ADDR_IP: cmd = SIOCGIFADDR; break;
            default: goto exit_flag;
        }

        if (-1 == ioctl(sock_fd, cmd, &ifr)) {		//根据不同的cmd获取不同的ifr结构体
            perror("get_local_addr::ioctl");
            goto exit_flag;
        }

        switch (type) {
            case ADDR_MAC:
                sprintf(buffer, "%02x:%02x:%02x:%02x:%02x:%02x",
                        (unsigned char)ifr.ifr_hwaddr.sa_data[0],
                        (unsigned char)ifr.ifr_hwaddr.sa_data[1],
                        (unsigned char)ifr.ifr_hwaddr.sa_data[2],
                        (unsigned char)ifr.ifr_hwaddr.sa_data[3],
                        (unsigned char)ifr.ifr_hwaddr.sa_data[4],
                        (unsigned char)ifr.ifr_hwaddr.sa_data[5]);
                break;
            case ADDR_IP:
                struct sockaddr_in *addr = (struct sockaddr_in*)&ifr.ifr_addr;
                strcpy(buffer, inet_ntoa(addr->sin_addr));
                break;
        }

        exit_flag:
        close(sock_fd);
        return buffer;
    }

    std::string get_current_working_path()
    {
        char temp[32] = {0};
        sprintf(temp, "/proc/%d/exe", getpid());		//"/proc/{%pid%}/exe"是一个指向可执行文件的软连接
        std::string path(256, 0);
        readlink(temp, &path[0], path.size());		//读取链接指向的真实文件路径
        return path;
    }

    datetime get_current_datetime()
    {
        datetime dt;
        char time_buffer[64] = {0};

        timeval tv;
        gettimeofday(&tv, nullptr);
        tm *timeinfo = localtime(&tv.tv_sec);		//获取当前时点并将其转化为tm结构体
        strftime(time_buffer, sizeof(time_buffer), "%Y%m%d", timeinfo);
        dt.date = atoi(time_buffer);
        strftime(time_buffer, sizeof(time_buffer), "%H%M%S", timeinfo);
        dt.time = atoi(time_buffer)*1000+tv.tv_usec/1000;		//时间精确到毫秒级
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

    unsigned int get_Nth_day(unsigned int spec_day, int N)
    {
        tm spec_time;
        spec_time.tm_year = spec_day/10000;
        spec_time.tm_mon = (spec_day%10000)/100;
        spec_time.tm_mday = spec_day%100;
        spec_time.tm_hour = 12;
        spec_time.tm_min = spec_time.tm_sec = 0;
        time_t spec_sec = mktime(&spec_time);
        spec_sec += N*24*3600;
        tm *p = localtime(&spec_sec);
        return p->tm_year*10000+p->tm_mon*100+p->tm_mday;
    }

    int get_file_size(const char *file)
    {
        if (!file)			//接口安全性检查
            return -1;

        if (-1 == access(file, 0)) {		//判断是否存在该文件
            return -1;
        } else {		//文件存在返回文件大小
            struct stat st;
            if (-1 == stat(file, &st))
                return -1;
            else
                return st.st_size;
        }
    }

    int find_nth_pos(const std::string& src, const char *pattern, int n)
    {
        if (src.empty() || !pattern)		//接口安全性检查
            return -1;

        if (n < 0) {		//找src中的最后一个子串
            size_t pos = src.rfind(pattern);
            if (pos != std::string::npos)		//find
                return pos;
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
                return pch-src.c_str();
        }
        return -1;
    }

    std::string& trim(std::string& s)
    {
        if (s.empty())		//接口安全性检查
            return s;

        s.erase(0, s.find_first_not_of(" "));		//清除字符串前面的空格及制表符
        s.erase(s.find_last_not_of(" ")+1);		//清除字符串尾部的空格及制表符
        return s;
    }

    std::vector<std::string> split(const std::string& s, const char *delimiters)
    {
        std::vector<std::string> tokens;
        std::size_t start = 0, end = 0;
        while (std::string::npos != (end = s.find(delimiters, start))) {		//在原始串s中查找从start开始的分隔符delimiters
            std::string temp = s.substr(start, end - start);		//若找到则截取start与end之间的子串
            if (temp != "") tokens.push_back(temp);			//判断是否为空，非空则加入结果集
            start = end + strlen(delimiters);
        }
        std::string temp = s.substr(start);
        if (temp != "") tokens.push_back(temp);
        return tokens;
    }

    bool convert_ipaddr(const char *server, std::string& ip_addr)
    {
        in_addr_t ret = inet_addr(server);		//判断服务器的地址是否为点分十进制的ip地址
        if (INADDR_NONE != ret) {		//已经是ip地址
            ip_addr = server;
            return true;
        }

        //是域名形式的主机地址
        struct hostent *hent = gethostbyname(server);		//根据域名获取主机相关信息
        if (!hent)
            return false;

        //当前使用的协议为ipv4
        assert(hent->h_addrtype == AF_INET);
        ip_addr.resize(32);
        if (!hent->h_addr_list[0])
            return false;

        //取addr_list列表中的第一个地址转化为ip地址
        inet_ntop(hent->h_addrtype, hent->h_addr_list[0], &ip_addr[0], ip_addr.size()-1);
        return true;
    }

    void depth_first_traverse_dir(const char *root_dir, std::function<void(const std::string&, void*)>f,
                                  void *args, bool with_path /*= true*/)
    {
        DIR *dir = opendir(root_dir);
        if (!dir) {
            perror("depth_first_traverse_dir::opendir failed");
            return;
        }

        struct stat st;
        struct dirent *ent = nullptr;
        while (ent = readdir(dir)) {
            if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
                continue;

            std::string file_name = root_dir;
            file_name = file_name+"/"+ent->d_name;
            if (-1 == stat(file_name.c_str(), &st)){
                perror("depth_first_traverse_dir::stat failed");
                return;
            }

            if (S_ISDIR(st.st_mode)) {		//directory
                depth_first_traverse_dir(file_name.c_str(), f, args, with_path);
            } else {
                if (!with_path)
                    file_name = ent->d_name;
                f(file_name, args);
            }
        }
        closedir(dir);
    }

    std::string run_shell_cmd(const char *cmd_string)
    {
        FILE *pf = popen(cmd_string, "r");		//执行shell命令，命令的输出通过管道返回
        if (!pf) {
            perror("run_shell_cmd::popen failed");
            return "";
        }

        char buffer[1024] = {0};
        std::string result;
        while (fgets(buffer, 1024, pf))		//不停地获取shell命令的输出，命令执行完毕后统一返回所有输出
            result.append(buffer);
        pclose(pf);
        return result;
    }
}
