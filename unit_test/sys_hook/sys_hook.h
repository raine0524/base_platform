#pragma once

#include "stdafx.h"

class MockFileSystem : public testing::Test
{
public:
    std::string m_readlink_val;
    std::set<FILE*> m_open_files;
    int m_fgets_curr, m_fgets_num;

    int get_flag(int fd)
    {
        auto it = m_fd_flags.find(fd);
        if (m_fd_flags.end() == it)
            return -1;
        else
            return it->second;
    }

    int set_flag(int fd, int flag)
    {
        auto it = m_fd_flags.find(fd);
        if (m_fd_flags.end() == it)
            return -1;

        it->second = flag;
        return 0;
    }

protected:
    std::random_device m_rand;
    std::map<int, int> m_fd_flags;  //file desc/status flags
};

extern MockFileSystem *g_mock_fs;

class MockSystemTime : public testing::Test
{
public:
    timeval m_curr_time;

    void add_interval()
    {
        m_curr_time.tv_sec += m_interval.tv_sec;
        m_curr_time.tv_usec += m_interval.tv_usec;
    }

protected:
    std::random_device m_rand;
    timeval m_interval;
};

extern MockSystemTime *g_mock_st;