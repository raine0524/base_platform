#include "stdafx.h"

namespace crx
{
    std::string g_server_name;		//当前服务的名称

    crx::log g_lib_log;

    console_impl::console_impl(console *c)
    :m_c(c)
    ,m_is_service(false)
    ,m_as_shell(false)
    ,m_close_exp(true)
    ,m_conn(-1)
    {
        simp_header stub_header;
        m_simp_buf = std::string((const char*)&stub_header, sizeof(simp_header));

        m_cmd_vec.push_back({"h", std::bind(&console_impl::print_help, this, _1), "显示帮助"});
        m_cmd_vec.push_back({"q", std::bind(&console_impl::quit_loop, this, _1), "退出程序"});
    }

    bool console_impl::check_service_exist()
    {
        net_socket sock(UNIX_DOMAIN);
        int ret = sock.create();
        close(sock.m_sock_fd);
        return ret < 0;
    }

    //该函数用于连接后台daemon进程
    void console_impl::connect_service()
    {
        m_client = m_c->get_tcp_client(std::bind(&console_impl::tcp_callback, this, true, _1, _2, _3, _4, _5));
        m_c->register_tcp_hook(true, [](int conn, char *data, size_t len) { return simpack_protocol(data, len); });
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_c->m_impl);
        sch_impl->m_util_impls[TCP_CLI].reset();

        auto cli_impl = std::dynamic_pointer_cast<tcp_client_impl>(m_client.m_impl);
        cli_impl->m_util.m_app_prt = PRT_SIMP;
        cli_impl->m_util.m_type = UNIX_DOMAIN;
        m_conn = m_client.connect("127.0.0.1", 0);
    }

    void console_impl::stop_service(bool pout)
    {
        m_close_exp = false;
        m_simp_buf.resize(sizeof(simp_header));
        ((simp_header*)&m_simp_buf[0])->length = htonl(UINT32_MAX);
        m_client.send_data(m_conn, m_simp_buf.data(), m_simp_buf.length());

        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_c->m_impl);
        sch_impl->main_coroutine(m_c);      //进入主协程
        m_client.release(m_conn);
        m_client.m_impl.reset();
        m_conn = -1;
        if (pout)
            printf("后台服务 %s 正常关闭，退出当前shell\n", g_server_name.c_str());
    }

    void console_impl::tcp_callback(bool client, int conn, const std::string& ip_addr, uint16_t port, char *data, size_t len)
    {
        if (client) {
            if (data)
                std::cout<<data+sizeof(simp_header)<<std::flush;

            if (!data && !len) {        //对端连接已关闭
                auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_c->m_impl);
                sch_impl->m_go_done = false;        //退出当前shell进程
                if (m_close_exp)
                    printf("\n后台服务 %s 异常关闭\n", g_server_name.c_str());
            }
            return;
        }

        //server
        if (!data && !len) {        //shell进程已关闭,释放此处连接资源
            m_server.release(conn);
            m_conn = -1;
            return;
        }

        std::vector<std::string> args;
        uint32_t length = ntohl(((simp_header*)data)->length);
        if (UINT32_MAX == length) {     //停止daemon进程
            quit_loop(args);
            return;
        }

        auto str_vec = split(data+sizeof(simp_header), length, " ");
        std::string cmd;
        for (size_t i = 0; i < str_vec.size(); ++i) {
            auto& arg = str_vec[i];
            if (0 == i)
                cmd = std::string(arg.data, arg.len);
            else
                args.emplace_back(arg.data, arg.len);
        }
        execute_cmd(cmd, args);
    }

    //控制台预处理操作
    bool console_impl::preprocess(int argc, char *argv[])
    {
        std::vector<std::string> stub_args;
        if (argc >= 2) {		//当前服务以带参模式运行，检测最后一个参数是否为{"-start", "-stop", "-restart"}之一
            bool exist = check_service_exist();
            if (!strcmp("-start", argv[argc-1])) {      //启动服务
                if (exist) {    //检测后台服务是否处于运行过程中
                    printf("当前服务 %s 正在后台运行中\n", g_server_name.c_str());
                    return true;
                }
                start_daemon();
            }

            if (!strcmp("-stop", argv[argc-1])) {       //终止服务
                if (exist) {            //确认当前服务正在运行中，执行终止操作
                    connect_service();
                    stop_service(true);
                } else {
                    printf("服务 %s 不在后台运行\n", g_server_name.c_str());
                }
                return true;		//通知后台进程退出之后也没必要继续运行
            }

            if (!strcmp("-restart", argv[argc-1])) {    //重启服务
                if (exist) {
                    connect_service();
                    stop_service(false);
                }
                start_daemon();
            }
        }

        //当前进程不是daemon进程并且后台服务存在，此时连接后台服务并以shell方式运行
        if (!m_is_service && check_service_exist()) {
            m_as_shell = true;
            connect_service();      //连接后台服务
            listen_keyboard_input();

            auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_c->m_impl);
            sch_impl->main_coroutine(m_c);
            return true;
        }
        return false;		//预处理失败表示在当前环境下还需要做进一步处理
    }

    void console_impl::execute_cmd(const std::string& cmd, std::vector<std::string>& args)
    {
        for (auto& con : m_cmd_vec)
            if (cmd == con.cmd)
                con.f(args);
    }

    /*
     * 函数用于等待shell输入
     * @当前程序以普通进程运行时将调用该例程等待命令行输入
     * @当前程序以shell方式运行时将调用该例程将命令行输入参数传给后台运行的服务
     */
    void console_impl::listen_keyboard_input()
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_c->m_impl);
        std::cout<<g_server_name<<":\\>"<<std::flush;

        auto ev = std::make_shared<eth_event>();
        ev->fd = STDIN_FILENO;
        setnonblocking(STDIN_FILENO);
        ev->sch_impl = sch_impl;
        ev->f = [this](uint32_t events) {
            std::string input;
            int ret = async_read(STDIN_FILENO, input);
            if (ret <= 0) {
                perror("read keyboard failed");
                return;
            }

            input.pop_back();   //读终端输入时去掉最后一个换行符'\n'
            trim(input);        //移除输入字符串前后的空格符
            if (input.empty()) {
                std::cout<<g_server_name<<":\\>"<<std::flush;
                return;
            }

            //命令行输入的一系列参数都是以空格分隔
            auto str_vec = split(input.data(), input.size(), " ");
            std::string cmd;
            std::vector<std::string> args;
            for (size_t i = 0; i < str_vec.size(); ++i) {
                auto& arg = str_vec[i];
                if (0 == i)
                    cmd = std::string(arg.data, arg.len);
                else
                    args.emplace_back(arg.data, arg.len);
            }

            bool find_cmd = false;
            for (auto& con : m_cmd_vec) {
                if (cmd == con.cmd) {
                    find_cmd = true;
                    break;
                }
            }
            if (!find_cmd) {        //若需要执行的命令并未通过add_cmd接口添加时，通知该命令未知
                std::cout<<"未知命令或参数！「请输入help(h)显示帮助」\n\n" <<g_server_name<<":\\>"<<std::flush;
                return;
            }

            if (m_as_shell) {       //当前程序以shell方式运行
                if ("h" == input || "q" == input) {     //输入为"h"(帮助)或者"q"(退出)时直接执行相应命令
                    execute_cmd(cmd, args);
                    if ("h" == input)
                        std::cout<<g_server_name<<":\\>"<<std::flush;
                } else {        //否则将命令行参数传给后台daemon进程
                    m_simp_buf.resize(sizeof(simp_header));
                    m_simp_buf.append(input);
                    ((simp_header*)&m_simp_buf[0])->length = htonl((uint32_t)input.length());
                    m_client.send_data(m_conn, m_simp_buf.data(), m_simp_buf.length());
                }
            } else {
                execute_cmd(cmd, args);
            }
        };
        sch_impl->add_event(ev);
    }

    void console_impl::start_daemon()
    {
        //创建一个unix域server
        m_server = m_c->get_tcp_server(-1, std::bind(&console_impl::tcp_callback, this, false, _1, _2, _3, _4, _5));
        auto svr_impl = std::dynamic_pointer_cast<tcp_server_impl>(m_server.m_impl);
        svr_impl->m_util.m_app_prt = PRT_SIMP;
        m_c->register_tcp_hook(false, [](int conn, char *data, size_t len) { return simpack_protocol(data, len); });
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_c->m_impl);
        sch_impl->m_util_impls[TCP_SVR].reset();

        m_is_service = true;
        daemon(1, 0);       //创建守护进程，不切换进程当前工作目录
    }

    void console_impl::quit_loop(std::vector<std::string>& args)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_c->m_impl);
        for (auto& co_impl : sch_impl->m_cos)
            co_impl->status = CO_UNKNOWN;

        if (!m_as_shell)        //以shell方式运行时不需要执行init/destroy操作
            m_c->destroy();
        sch_impl->m_go_done = false;
    }

    //打印帮助信息
    void console_impl::print_help(std::vector<std::string>& args)
    {
        std::cout<<std::right<<std::setw(5)<<"cmd";
        std::cout<<std::setw(10)<<' '<<std::setw(0)<<"comments\n";
        for (auto& cmd : m_cmd_vec) {
            std::string str = "  "+cmd.cmd;
            std::cout<<std::left<<std::setw(15)<<str<<cmd.comment<<'\n';
        }
        std::cout<<std::endl;
    }

    console::console()
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        sch_impl->m_util_impls[EXT_DATA] = std::make_shared<console_impl>(this);
    }

    //添加控制台命令
    void console::add_cmd(const char *cmd, std::function<void(std::vector<std::string>&)> f,
                          const char *comment)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto con_impl = std::dynamic_pointer_cast<console_impl>(sch_impl->m_util_impls[EXT_DATA]);
        for (auto& con : con_impl->m_cmd_vec)
            assert(cmd != con.cmd);
        con_impl->m_cmd_vec.push_back({cmd, f, comment});
    }

    void console::pout(const char *fmt, ...)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto con_impl = std::dynamic_pointer_cast<console_impl>(sch_impl->m_util_impls[EXT_DATA]);
        if (!con_impl->m_is_service || con_impl->m_conn < 0) {
            va_list val;
            va_start(val, fmt);
            vprintf(fmt, val);
            va_end(val);
            return;
        }

        va_list vl1, vl2;
        va_start(vl1, fmt);
        va_copy(vl2, vl1);
        int ret = vsnprintf(nullptr, 0, fmt, vl1);
        std::string buf(ret+1, 0);
        vsnprintf(&buf[0], buf.size(), fmt, vl2);
        va_end(vl1);
        va_end(vl2);

        con_impl->m_simp_buf.resize(sizeof(simp_header));
        con_impl->m_simp_buf.append(buf);
        ((simp_header*)&con_impl->m_simp_buf[0])->length = htonl((uint32_t)ret);
        con_impl->m_server.send_data(con_impl->m_conn, con_impl->m_simp_buf.data(), con_impl->m_simp_buf.size());
    }

    void console_impl::bind_core(int which)
    {
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(which, &mask);
        if (syscall(__NR_gettid) == getpid()) {     //main thread
            if (__glibc_unlikely(sched_setaffinity(0, sizeof(mask), &mask) < 0))
                log_error(g_lib_log, "bind core failed: %s\n", strerror(errno));
        } else {
            if (__glibc_unlikely(pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask) < 0))
                log_error(g_lib_log, "bind core failed: %s\n", strerror(errno));
        }
    }

    int console::run(int argc, char *argv[], const char *conf /*= "ini/server.ini"*/, int bind_flag /*= -1*/)
    {
        //get server name
        g_server_name = argv[0];
        g_server_name = g_server_name.substr(g_server_name.rfind("/")+1);

        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto con_impl = std::dynamic_pointer_cast<console_impl>(sch_impl->m_util_impls[EXT_DATA]);

        sch_impl->m_epoll_fd = epoll_create(EPOLL_SIZE);
        if (__glibc_unlikely(-1 == sch_impl->m_epoll_fd)) {
            printf("epoll_create failed: %s\n", strerror(errno));
            return EXIT_FAILURE;
        }

        std::function<void(scheduler *sch, size_t co_id)> stub;
        sch_impl->co_create(stub, this, true, false, "main coroutine");        //创建主协程

        /*
         * 首先执行预处理操作，预处理主要和当前运行环境以及运行时所带参数有关，在预处理操作中可能只是简单的停止后台服务，或连接后台服务
         * 执行一些命令，此时只需要有一个主协程，并进入主协程进行简单的读写操作即可，在这种使用场景下当前进程仅仅只是真实服务的一个shell，
         * 不需要使用日志或者创建其他运行时需要用到的资源
         */
        if (con_impl->preprocess(argc, argv))
            return EXIT_SUCCESS;

        if (!access(conf, F_OK)) {      //解析日志配置
            sch_impl->m_ini_file = conf;
            ini ini_parser;
            ini_parser.load(conf);
            if (ini_parser.has_section("log")) {
                ini_parser.set_section("log");
                sch_impl->m_remote_log = ini_parser.get_int("remote") != 0;
            }
            g_lib_log = get_log("kbase");
        }

        //处理绑核操作
        int cpu_num = get_nprocs();
        if (-1 != bind_flag) {
            if (INT_MAX == bind_flag)
                con_impl->bind_core(con_impl->m_random()%cpu_num);
            else if (0 <= bind_flag && bind_flag < cpu_num)     //bind_flag的取值范围为0~cpu_num-1
                con_impl->bind_core(bind_flag);
        }

        signal(SIGPIPE, SIG_IGN);       //向已经断开连接的管道和套接字写数据时只返回错误,不主动退出进程
        if (!sch_impl->m_sec_wheel.m_impl)       //创建一个定时轮
            sch_impl->m_sec_wheel = get_timer_wheel();
        sch_impl->m_sec_wheel.add_handler(5*1000, std::bind(&scheduler_impl::periodic_trim_memory, sch_impl.get()));

        //打印帮助信息
        std::vector<std::string> str_vec;
        con_impl->print_help(str_vec);

        if (!init(argc, argv))		//执行初始化，若初始化返回失败，直接退出当前进程
            return EXIT_FAILURE;

        if (!con_impl->m_is_service)        //前台运行时在控制台输入接收命令
            con_impl->listen_keyboard_input();

        sch_impl->main_coroutine(this);     //进入主协程
        return EXIT_SUCCESS;
    }
}
