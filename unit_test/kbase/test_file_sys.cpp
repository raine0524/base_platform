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

    crx::dump_segment();
}

int main(int argc, char *argv[])
{
    std::string server_name = argv[0];
    crx::g_server_name = server_name.substr(server_name.rfind('/')+1);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}