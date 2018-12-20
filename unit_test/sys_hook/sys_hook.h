#pragma once

class MockFileSystem : public testing::Test
{
public:
    std::string m_readlink_val;
    std::set<FILE*> m_open_files;
    int m_fgets_curr, m_fgets_num, m_mkdir_num;

    std::map<DIR*, dirent> m_dir_ent;
    int m_opendir_cnt, m_readdir_cnt;
    std::string m_traverse_fname;

    bool m_hook_ewait;
    std::vector<std::pair<int, int>> m_efd_cnt;
    std::string m_write_data;

    MockFileSystem() : m_hook_ewait(false) {}

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

    int get_file_size()
    {
        m_file_size = rand()%10000;
        return m_file_size;
    }

protected:
    std::map<int, int> m_fd_flags;  //file desc/status flags
    int m_file_size;
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
    timeval m_interval;
};

extern MockSystemTime *g_mock_st;
