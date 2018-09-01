#include "stdafx.h"

class FdFlagTest : public MockFileSystem
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

TEST_F(FdFlagTest, FdNonBlock)
{
    ASSERT_EQ(-1, crx::setnonblocking(-1));
    ASSERT_EQ(-1, crx::setnonblocking(0));
    ASSERT_TRUE(!crx::setnonblocking(1) && (0x12 | O_NONBLOCK) == m_fd_flags[1]);
    ASSERT_EQ(-1, crx::setnonblocking(2));
}

TEST_F(FdFlagTest, FdCloseOnExec)
{
    ASSERT_EQ(-1, crx::setcloseonexec(-1));
    ASSERT_EQ(-1, crx::setcloseonexec(0));
    ASSERT_TRUE(!crx::setcloseonexec(1) && (0x12 | FD_CLOEXEC) == m_fd_flags[1]);
    ASSERT_EQ(-1, crx::setcloseonexec(2));
}

int main(int argc, char *argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}