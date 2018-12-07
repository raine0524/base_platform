#include "stdafx.h"

class SchedUtilTest : public testing::Test
{
public:
    void sig_test_helper(int signo, uint64_t sigval);

protected:
    void SetUp() override
    {
        auto impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
        impl->m_epoll_fd = epoll_create(crx::EPOLL_SIZE);
    }

    void TearDown() override
    {
        auto impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
        close(impl->m_epoll_fd);
    }

    crx::scheduler m_sch;
    std::map<int, int> m_sig_num;
};

void SchedUtilTest::sig_test_helper(int signo, uint64_t sigval)
{
    auto impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
    auto& sig_num = m_sig_num[signo];
    sig_num += sigval;
    impl->m_go_done = false;
}

TEST_F(SchedUtilTest, TestSigCtl)
{
    crx::sigctl sc = m_sch.get_sigctl();
    ASSERT_TRUE(sc.m_impl.get());

    for (int i = 0; i < 8; i++) {
        int signo = __SIGRTMIN+g_rand()%(_NSIG-__SIGRTMIN);     // 对实时信号进行测试
        sc.add_sig(signo, std::bind(&SchedUtilTest::sig_test_helper, this, _1, _2));
        m_sig_num[signo] = g_rand()%100;
    }

    auto impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
    for (auto& pair : m_sig_num) {
        int origin = pair.second;
        int send_cnt = g_rand()%5+5;

        for (int i = 0; i < send_cnt; i++) {
            sigval_t sv;
            sv.sival_int = g_rand()%100;
            origin += sv.sival_int;
            sigqueue(getpid(), pair.first, sv);
        }

        impl->main_coroutine(&m_sch);
        ASSERT_EQ(origin, pair.second);
    }
}

int main(int argc, char *argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
