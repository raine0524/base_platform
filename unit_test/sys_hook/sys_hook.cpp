#include "stdafx.h"

MockFileSystem *g_mock_fs = nullptr;
MockSystemTime *g_mock_st = nullptr;

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

    if (!strcmp("test", __command)) {
        auto fp = (FILE*)0x88;
        g_mock_fs->m_open_files.insert(fp);
        return fp;
    } else {
        typedef FILE* (*popen_pfn_t)(const char *__command, const char *__modes);
        static popen_pfn_t g_sys_popen = (popen_pfn_t)dlsym(RTLD_NEXT, "popen");
        return g_sys_popen(__command, __modes);
    }
}

char *fgets(char *__restrict __s, int __n, FILE *__restrict __stream)
{
    if ((void*)0x88 == __stream) {
        if (!__stream || g_mock_fs->m_fgets_curr == g_mock_fs->m_fgets_num)
            return nullptr;

        if (g_mock_fs->m_open_files.end() == g_mock_fs->m_open_files.find((FILE*)__stream))
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
    return -1;
}

int mkdir (const char *__path, __mode_t __mode) __THROW
{
    g_mock_fs->m_mkdir_num++;
    return 1;
}