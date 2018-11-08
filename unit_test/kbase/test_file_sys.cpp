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
        int start = m_fgets_curr = m_rand()%1000;
        m_fgets_num = m_fgets_curr+m_rand()%10;
        auto result = crx::run_shell_cmd("test");

        std::string expect_val;
        for (; start < m_fgets_num-1; start++) {
            expect_val += "this is a line ";
            expect_val += std::to_string(start)+"\n";
        }
        if (start == m_fgets_num-1)
            expect_val += std::string(1023, 'a');
        ASSERT_TRUE(expect_val == result);
    }
}

TEST_F(FileSysTest, MkdirMulti)
{
    ASSERT_EQ(-1, crx::mkdir_multi(nullptr));
    for (int i = 0; i < 16; i++) {
        std::string path;
        int level_num = m_rand()%5+5;
        for (int j = 0; j < level_num; j++)
            path += std::string("/")+(char)(m_rand()%26+'a');

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

TEST_F(FileSysTest, GetLocalAddr) {
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

int main(int argc, char *argv[])
{
    std::string server_name = argv[0];
    crx::g_server_name = server_name.substr(server_name.rfind('/')+1);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}