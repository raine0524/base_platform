#include "stdafx.h"

class console_helper : public crx::console
{
public:
    bool init(int argc, char **argv) override { return true; }

    void destroy() override {}
};

class ConsoleTest : public MockFileSystem
{
public:
    void cmd_helper_adder(std::vector<std::string>& args);

    void SetUp() override
    {
        g_mock_fs = this;
        m_startup_cmd = "./ThisIsATest";
    }

    console_helper m_con_helper;
    int m_result;
    std::string m_startup_cmd;
};

TEST_F(ConsoleTest, TestDaemon)
{
    std::string daemon_arg = "-start";
    char *argv[2] = {&m_startup_cmd[0], &daemon_arg[0]};

    if (fork()) {       //parent process
        sleep(1);
        daemon_arg = "-stop";
        int status = m_con_helper.run(sizeof(argv)/sizeof(argv[0]), argv);
        ASSERT_EQ(status, EXIT_SUCCESS);
        ASSERT_STREQ(g_server_name.c_str(), "ThisIsATest");
    } else {        //child process
        int status = m_con_helper.run(sizeof(argv)/sizeof(argv[0]), argv);
        ASSERT_EQ(status, EXIT_SUCCESS);
    }
}

void ConsoleTest::cmd_helper_adder(std::vector<std::string>& args)
{
    auto sch_impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_con_helper.m_impl);
    ASSERT_EQ(args.size(), 2);
    int adder_a = std::stoi(args[0]), adder_b = std::stoi(args[1]);
    m_result = adder_a+adder_b;
    sch_impl->m_go_done = false;
}

TEST_F(ConsoleTest, TestRunCmd)
{
    g_mock_fs->m_hook_ewait = true;
    g_mock_fs->m_efd_cnt.emplace_back(std::make_pair(STDIN_FILENO, 1));
    auto sch_impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_con_helper.m_impl);
    auto con_impl = std::dynamic_pointer_cast<crx::console_impl>(sch_impl->m_util_impls[crx::EXT_DATA]);
    sch_impl->m_epoll_fd = epoll_create(crx::EPOLL_SIZE);
    con_impl->listen_keyboard_input();

    m_con_helper.add_cmd("test_adder", std::bind(&ConsoleTest::cmd_helper_adder, this, _1),
            "测试加法器 usage@test_adder a b");
    ASSERT_EQ(con_impl->m_cmd_vec.size(), 3);
    ASSERT_STREQ(con_impl->m_cmd_vec[2].cmd.c_str(), "test_adder");

    for (int i = 0; i < 8192; i++) {
        int adder_a = rand()%10000, adder_b = rand()%10000;
        int result = adder_a+adder_b;
        g_mock_fs->m_write_data = "test_adder "+std::to_string(adder_a)+" "+std::to_string(adder_b)+"\n";
        sch_impl->main_coroutine(&m_con_helper);
        ASSERT_EQ(result, m_result);
    }
    close(sch_impl->m_epoll_fd);
}

int main(int argc, char *argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
