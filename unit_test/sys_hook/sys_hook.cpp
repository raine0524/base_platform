#include "stdafx.h"

MockFileSystem *g_mock_fs = nullptr;
MockSystemTime *g_mock_st = nullptr;

std::random_device g_rand;

int fcntl(int __fd, int __cmd, ...)
{
    if (F_GETFD == __cmd || F_GETFL == __cmd) {
        return g_mock_fs->get_flag(__fd);
    } else if (F_SETFD == __cmd || F_SETFL == __cmd) {
        va_list val;
        va_start(val, __cmd);
        int flag = va_arg(val, int);
        va_end(val);
        return g_mock_fs->set_flag(__fd, flag);
    }
    return 0;
}

int gettimeofday(struct timeval *__tv, __timezone_ptr_t __tz) __THROW
{
    if (g_mock_st) {
        *__tv = g_mock_st->m_curr_time;
        g_mock_st->add_interval();
    } else {
        typedef int (*gettime_pfn_t)(struct timeval *__tv, __timezone_ptr_t __tz);
        static gettime_pfn_t g_sys_gettime = (gettime_pfn_t)dlsym(RTLD_NEXT, "gettimeofday");
        g_sys_gettime(__tv, __tz);
    }
    return 0;
}

ssize_t readlink(const char *__restrict __path, char *__restrict __buf, size_t __len) __THROW
{
    if (g_mock_fs->m_readlink_val.empty())
        return -1;

    ssize_t str_size = g_mock_fs->m_readlink_val.size();
    strncpy(__buf, g_mock_fs->m_readlink_val.c_str(), __len);
    return str_size > __len ? __len : str_size;
}

FILE *popen(const char *__command, const char *__modes)
{
    if (!__command || !__modes)
        return nullptr;

    if (!strcmp("__popen_test", __command)) {
        auto fp = (FILE*)0x88;
        g_mock_fs->m_open_files.insert(fp);
        return fp;
    } else {
        typedef FILE* (*popen_pfn_t)(const char *__command, const char *__modes);
        static popen_pfn_t g_sys_popen = (popen_pfn_t)dlsym(RTLD_NEXT, "popen");
        return g_sys_popen(__command, __modes);
    }
}

char *fgets(char *__restrict __s, int __n, FILE *__stream)
{
    if ((void*)0x88 == __stream) {
        if (!__stream || g_mock_fs->m_fgets_curr == g_mock_fs->m_fgets_num)
            return nullptr;

        if (g_mock_fs->m_open_files.end() == g_mock_fs->m_open_files.find(__stream))
            return nullptr;

        if (g_mock_fs->m_fgets_curr <= g_mock_fs->m_fgets_num-2) {
            snprintf(__s, (size_t)__n, "this is a line %d\n", g_mock_fs->m_fgets_curr);
        } else if (g_mock_fs->m_fgets_curr == g_mock_fs->m_fgets_num-1) {
            for (int i = 0; i < __n-1; i++)
                __s[i] = 'a';
            __s[__n-1] = 0;
        }

        g_mock_fs->m_fgets_curr++;
        return __s;
    } else {
        typedef char* (*fgets_pfn_t)(char *__restrict __s, int __n, FILE *__restrict __stream);
        static fgets_pfn_t g_sys_fgets = (fgets_pfn_t)dlsym(RTLD_NEXT, "fgets");
        return g_sys_fgets(__s, __n, __stream);
    }
}

int pclose(FILE *__stream)
{
    if ((void*)0x88 == __stream) {
        g_mock_fs->m_open_files.erase(__stream);
        return 0;
    } else {
        typedef int (*pclose_pfn_t)(FILE *__stream);
        static pclose_pfn_t g_sys_pclose = (pclose_pfn_t)dlsym(RTLD_NEXT, "pclose");
        return g_sys_pclose(__stream);
    }
}

int access (const char *__name, int __type) __THROW
{
    if (strstr(__name, "test")) {
        return 0;
    } else {
        return -1;
    }
}

// hook stat
int __xstat (int vers, const char *name, struct stat *buf) __THROW
{
    if (!strncmp(name, "__filesize_test", 15)) {
        buf->st_size = g_mock_fs->get_file_size();
        return 0;
    } else if (strstr(name, "__readdir_test")) {
        if (g_rand()%10 >= 4) {
            buf->st_mode = 33204;       // file
            g_mock_fs->m_traverse_fname = name;
        } else {
            buf->st_mode = 16893;       // dir
        }
    } else {
        typedef int (*xstat_pfn_t)(int vers, const char *name, struct stat *buf);
        static xstat_pfn_t g_sys_xstat = (xstat_pfn_t)dlsym(RTLD_NEXT, "__xstat");
        return g_sys_xstat(vers, name, buf);
    }
}

int mkdir (const char *__path, __mode_t __mode) __THROW
{
    g_mock_fs->m_mkdir_num++;
    return 1;
}

DIR *opendir (const char *__name)
{
    if (!strncmp(__name, "__depth_test", 4)) {
        g_mock_fs->m_opendir_cnt++;
        if (g_mock_fs->m_opendir_cnt >= 10) {
            return nullptr;
        } else {
            DIR *ret = reinterpret_cast<DIR*>(g_mock_fs->m_opendir_cnt);
            g_mock_fs->m_dir_ent[ret];
            return ret;
        }
    } else {
        typedef DIR* (*opendir_pfn_t) (const char *__name);
        static opendir_pfn_t g_sys_opendir = (opendir_pfn_t)dlsym(RTLD_NEXT, "opendir");
        return g_sys_opendir(__name);
    }
}

struct dirent *readdir (DIR *__dirp)
{
    auto it = g_mock_fs->m_dir_ent.find(__dirp);
    if (g_mock_fs->m_dir_ent.end() != it) {
        if (g_rand()%10 <= 2)
            return nullptr;

        dirent *ent = &it->second;
        std::string file_name = "test"+std::to_string(g_mock_fs->m_readdir_cnt++)+"__readdir_test";
        strcpy(ent->d_name, file_name.c_str());
        return ent;
    } else {
        typedef dirent* (*readdir_pfn_t) (DIR *__dirp);
        static readdir_pfn_t g_sys_readdir = (readdir_pfn_t)dlsym(RTLD_NEXT, "readdir");
        return g_sys_readdir(__dirp);
    }
}

int closedir (DIR *__dirp)
{
    auto it = g_mock_fs->m_dir_ent.find(__dirp);
    if (g_mock_fs->m_dir_ent.end() != it) {
        g_mock_fs->m_dir_ent.erase(__dirp);
    } else {
        typedef int (*closedir_pfn_t) (DIR *__dirp);
        static closedir_pfn_t g_sys_closedir = (closedir_pfn_t)dlsym(RTLD_NEXT, "closedir");
        return g_sys_closedir(__dirp);
    }
    return 0;
}

int epoll_wait (int __epfd, struct epoll_event *__events, int __maxevents, int __timeout)
{
    if (g_mock_fs->m_hook_ewait) {
        int ev_cnt = 0;
        for (auto& pair : g_mock_fs->m_efd_cnt) {
            for (int i = 0; i < pair.second; i++) {
                assert(ev_cnt < __maxevents);
                __events[ev_cnt].events = EPOLLIN;
                __events[ev_cnt].data.fd = pair.first;
                ev_cnt++;
            }
        }
        return ev_cnt;
    }

    typedef int (*ewait_pfn_t) (int, struct epoll_event*, int, int);
    static ewait_pfn_t g_sys_ewait = (ewait_pfn_t)dlsym(RTLD_NEXT, "epoll_wait");
    return g_sys_ewait(__epfd, __events, __maxevents, __timeout);
}

ssize_t read (int __fd, void *__buf, size_t __nbytes)
{
    if (g_mock_fs->m_hook_ewait) {
        if (!g_mock_fs->m_write_data.empty()) {
            memcpy(__buf, g_mock_fs->m_write_data.c_str(), g_mock_fs->m_write_data.size());
            return g_mock_fs->m_write_data.size();
        } else {
            return 1;
        }
    }

    typedef ssize_t (*read_pfn_t) (int, void*, size_t);
    static read_pfn_t g_sys_read = (read_pfn_t)dlsym(RTLD_NEXT, "read");
    return g_sys_read(__fd, __buf, __nbytes);
}
