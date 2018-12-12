#include "stdafx.h"

class FileSysTest : public MockFileSystem
{
protected:
    virtual void SetUp()
    {
        g_mock_fs = this;
        m_fd_flags[1] = 0x12;
    }

    virtual void TearDown()
    {
        m_fd_flags.clear();
    }
};

TEST_F(FileSysTest, FdNonBlock)
{
    ASSERT_EQ(-1, crx::setnonblocking(-1));
    ASSERT_EQ(-1, crx::setnonblocking(0));
    ASSERT_TRUE(!crx::setnonblocking(1) && (0x12 | O_NONBLOCK) == m_fd_flags[1]);
    ASSERT_EQ(-1, crx::setnonblocking(2));
}

TEST_F(FileSysTest, FdCloseOnExec)
{
    ASSERT_EQ(-1, crx::setcloseonexec(-1));
    ASSERT_EQ(-1, crx::setcloseonexec(0));
    ASSERT_TRUE(!crx::setcloseonexec(1) && (0x12 | FD_CLOEXEC) == m_fd_flags[1]);
    ASSERT_EQ(-1, crx::setcloseonexec(2));
}

TEST_F(FileSysTest, GetWorkSpace)
{
    ASSERT_STREQ("", crx::get_work_space().c_str());
    g_mock_fs->m_readlink_val = "this is a test";
    ASSERT_STRCASEEQ(g_mock_fs->m_readlink_val.c_str(), crx::get_work_space().c_str());

    //the length of workspace at most 255 bytes
    g_mock_fs->m_readlink_val = std::string(1024, 'a');
    ASSERT_TRUE(std::string(255, 'a') == crx::get_work_space());
    ASSERT_EQ(255, crx::get_work_space().size());
}

TEST_F(FileSysTest, RunShellCmd)
{
    ASSERT_STREQ("", crx::run_shell_cmd(nullptr).c_str());
    for (int i = 0; i < 16; i++) {
        int start = m_fgets_curr = g_rand() % 1000;
        m_fgets_num = m_fgets_curr + g_rand() % 10;
        auto result = crx::run_shell_cmd("__popen_test");

        std::string expect_val;
        for (; start < m_fgets_num - 1; start++) {
            expect_val += "this is a line ";
            expect_val += std::to_string(start) + "\n";
        }
        if (start == m_fgets_num - 1)
            expect_val += std::string(1023, 'a');
        ASSERT_TRUE(expect_val == result);
    }
}

TEST_F(FileSysTest, MkdirMulti)
{
    ASSERT_EQ(-1, crx::mkdir_multi(nullptr));
    for (int i = 0; i < 16; i++) {
        std::string path;
        int level_num = g_rand() % 5 + 5;
        for (int j = 0; j < level_num; j++)
            path += std::string("/") + (char) (g_rand() % 26 + 'a');

        g_mock_fs->m_mkdir_num = 0;
        ASSERT_EQ(0, crx::mkdir_multi(path.c_str()));
        ASSERT_EQ(level_num, g_mock_fs->m_mkdir_num);
    }
}

TEST_F(FileSysTest, CharsetConvert)
{
    ASSERT_STREQ("", crx::charset_convert("GB2312", "UTF-8", nullptr, 0).c_str());
    const char *englist_text = "hello world";
    ASSERT_STREQ(englist_text, crx::charset_convert("GB2312", "UTF-8", englist_text, strlen(englist_text)).c_str());
    const char *chinese_text = "这是一个测试";
    ASSERT_STREQ(chinese_text, crx::charset_convert("UTF-8", "UTF-8", chinese_text, strlen(chinese_text)).c_str());
    const char *japanese_text = "にっぽんご/にほんご";
    ASSERT_STREQ(japanese_text, crx::charset_convert("UTF-8", "UTF-8", japanese_text, strlen(japanese_text)).c_str());
}

TEST_F(FileSysTest, GetLocalAddr)
{
    ASSERT_STREQ("", crx::get_local_addr(crx::ADDR_IP, nullptr).c_str());
    ASSERT_STREQ("127.0.0.1", crx::get_local_addr(crx::ADDR_IP, "lo").c_str());

    char buffer[256] = {0};
    ifconf conf = {0};
    conf.ifc_ifcu.ifcu_buf = buffer;
    conf.ifc_len = sizeof(buffer);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ioctl(sock, SIOCGIFCONF, &conf);

    std::vector<std::string> card_names;
    ifreq *end = conf.ifc_ifcu.ifcu_req + conf.ifc_len / sizeof(ifreq);
    for (ifreq *start = conf.ifc_ifcu.ifcu_req; start != end; start++) {
        if (!strcmp("lo", start->ifr_ifrn.ifrn_name))
            card_names.push_back(start->ifr_ifrn.ifrn_name);
    }
    close(sock);

    for (auto &card : card_names) {
        auto ip_addr = crx::get_local_addr(crx::ADDR_IP, card.c_str());
        auto mac_addr = crx::get_local_addr(crx::ADDR_MAC, card.c_str());
        if (ip_addr.empty() || mac_addr.empty())
            continue;

        int fields[4] = {0};
        ASSERT_EQ(4, sscanf(ip_addr.c_str(), "%d.%d.%d.%d", &fields[0], &fields[1], &fields[2], &fields[3]));
        for (int i = 0; i < 4; i++)
            ASSERT_TRUE(0 <= fields[i] && fields[i] <= 255);
        char temp[32] = {0};
        sprintf(temp, "%d.%d.%d.%d", fields[0], fields[1], fields[2], fields[3]);
        ASSERT_STREQ(temp, ip_addr.c_str());
        ASSERT_TRUE(std::regex_match(mac_addr, std::regex("^([A-Fa-f0-9]{2}:){5}[A-Fa-f0-9]{2}$")));
    }
}

TEST_F(FileSysTest, GetFileSize)
{
    for (int i = 0; i < 16; i++) {
        int seed = g_rand() % 10000;
        if (seed % 2) {
            if (i % 2) {
                ASSERT_EQ(m_file_size, crx::get_file_size("__filesize_test"));
            } else {
                ASSERT_EQ(-1, crx::get_file_size("non_test"));
            }
        } else {
            ASSERT_EQ(-1, crx::get_file_size(nullptr));
        }
    }
}

TEST_F(FileSysTest, FindNthPos)
{
    ASSERT_EQ(-1, crx::find_nth_pos(std::string(), nullptr, 0));
    ASSERT_EQ(-1, crx::find_nth_pos("test", nullptr, 0));

    ASSERT_EQ(7, crx::find_nth_pos("hello world", "or", -1));
    ASSERT_EQ(-1, crx::find_nth_pos("hello world", "llow", -1));
    ASSERT_EQ(8, crx::find_nth_pos("this is a test", "a t", 0));
    ASSERT_EQ(3, crx::find_nth_pos("banana", "ana", 1));
    ASSERT_EQ(-1, crx::find_nth_pos("banana", "ana", 5));

    std::string src_str = "abababababababa";
    for (int i = 0; i < 5; i++) {
        int rand_idx = g_rand()%7;
        ASSERT_EQ(rand_idx*2, crx::find_nth_pos(src_str, "aba", rand_idx));
    }
}

TEST_F(FileSysTest, Trim)
{
    std::string trim_str = "   \t   hello world";
    ASSERT_STREQ("hello world", crx::trim(trim_str).c_str());
    trim_str = "hello world    \t    \t\t \t  ";
    ASSERT_STREQ("hello world", crx::trim(trim_str).c_str());
    trim_str = "  \t \t  \t  hello world  \t \t    \t   ";
    ASSERT_STREQ("hello world", crx::trim(trim_str).c_str());
}

TEST_F(FileSysTest, Split)
{
    auto result = crx::split(nullptr, 0, ".");
    ASSERT_TRUE(result.empty());
    result = crx::split("test", 4, nullptr);
    ASSERT_TRUE(result.empty());

    result = crx::split("test", 4, ".");
    ASSERT_EQ(1, result.size());
    ASSERT_STREQ("test", result.front().data);

    std::string src_str = "www.google.com";
    result = crx::split(src_str.c_str(), src_str.size(), ".");
    ASSERT_EQ(3, result.size());
    ASSERT_STREQ("www", std::string(result[0].data, result[0].len).c_str());
    ASSERT_STREQ("google", std::string(result[1].data, result[1].len).c_str());
    ASSERT_STREQ("com", result[2].data);

    src_str = "/home/daniel/workspace";
    result = crx::split(src_str.c_str(), src_str.size(), "/");
    ASSERT_EQ(3, result.size());
    ASSERT_STREQ("home", std::string(result[0].data, result[0].len).c_str());
    ASSERT_STREQ("daniel", std::string(result[1].data, result[1].len).c_str());
    ASSERT_STREQ("workspace", result[2].data);
}

TEST_F(FileSysTest, ConvertIPAddr)
{
    std::string ip_addr;
    const char *server_addr = "192.168.1.110";
    ASSERT_TRUE(crx::convert_ipaddr(server_addr, ip_addr));
    ASSERT_STREQ(server_addr, ip_addr.c_str());

    server_addr = "127.0.0.1";
    ASSERT_TRUE(crx::convert_ipaddr(server_addr, ip_addr));
    ASSERT_STREQ(server_addr, ip_addr.c_str());

    std::vector<std::string> addr_res;
    server_addr = "www.baidu.com";
    ASSERT_TRUE(crx::convert_ipaddr(server_addr, ip_addr));
    addr_res.push_back(std::move(ip_addr));

    server_addr = "www.bing.com";
    ASSERT_TRUE(crx::convert_ipaddr(server_addr, ip_addr));
    addr_res.push_back(std::move(ip_addr));

    for (auto& addr : addr_res) {
        int fields[4] = {0};
        ASSERT_EQ(4, sscanf(addr.c_str(), "%d.%d.%d.%d", &fields[0], &fields[1], &fields[2], &fields[3]));
        for (int i = 0; i < 4; i++)
            ASSERT_TRUE(0 <= fields[i] && fields[i] <= 255);

        char temp[32] = {0};
        sprintf(temp, "%d.%d.%d.%d", fields[0], fields[1], fields[2], fields[3]);
        ASSERT_STREQ(temp, addr.c_str());
    }
}

TEST_F(FileSysTest, DepthFirstTraverseDir)
{
    g_mock_fs->m_opendir_cnt = 0;
    g_mock_fs->m_readdir_cnt = 0;
    crx::depth_first_traverse_dir("__depth_test", [&](std::string& file) {
        ASSERT_STREQ(g_mock_fs->m_traverse_fname.c_str(), file.c_str());
    });
}

int main(int argc, char *argv[]) {
    std::string server_name = argv[0];
    g_server_name = server_name.substr(server_name.rfind('/') + 1);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
