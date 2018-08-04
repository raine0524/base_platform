#include "stdafx.h"

namespace crx
{
    const char *heart_beat = "just a heart beat";
    const char *service_quit_sig = "user_def_quit";

    int g_wr_fifo = -1;

    const char *unknown_cmd = "未知命令或参数！「请输入help(h)显示帮助」\n\n";
    const char *crash_info = "后台服务已崩溃，请检查当前目录下的core文件查看崩溃时的调用堆栈\n";

    std::string crx_tmp_dir = ".crx_tmp/";		//kbase库的临时目录，用于存放一系列在服务运行过程中需要用到的临时文件
    std::string g_server_name;		//当前服务的名称

    console_impl::console_impl(console *c) : m_c(c), m_is_service(false), m_as_shell(false)
    {
        m_cmd_idx["h"] = m_cmd_vec.size();
        m_cmd_vec.push_back({"h", std::bind(&console_impl::print_help, this, _1), "显示帮助"});
        m_cmd_idx["q"] = m_cmd_vec.size();
        m_cmd_vec.push_back({"q", std::bind(&console_impl::quit_loop, this, _1), "退出程序"});
    }

    //检查当前服务是否存在，若/proc/{%pid%}文件不存在，则说明后台进程已崩溃
    bool console_impl::check_service_exist()
    {
        size_t pos = m_pipe_name[0].rfind(".");
        std::string proc_dir = "/proc/"+m_pipe_name[0].substr(pos+1);
        if (access(proc_dir.c_str(), F_OK)) {		//服务不存在
            remove(m_pipe_name[0].c_str());
            remove(m_pipe_name[1].c_str());
            return false;
        } else {
            return true;
        }
    }

    //设置当前服务的名称，并构造服务所需的管道文件
    void console_impl::cons_pipe_name(const char *argv_0)
    {
        g_server_name = argv_0;
        g_server_name = g_server_name.substr(g_server_name.rfind("/")+1);
        openlog(g_server_name.c_str(), LOG_PID | LOG_CONS, LOG_USER);
        m_pipe_dir = crx_tmp_dir+"daemon_pipe/"+g_server_name;

        if (access(m_pipe_dir.c_str(), F_OK)) {
            mkdir_multi(m_pipe_dir.c_str());		//创建管道目录
        } else {
            depth_first_traverse_dir(m_pipe_dir.c_str(), [&](std::string& file) {
                int idx = file.front()-'0';
                m_pipe_name[idx] = m_pipe_dir+"/"+file;
            }, false);
        }
    }

    //控制台预处理操作
    bool console_impl::preprocess(int argc, char *argv[])
    {
        signal(SIGPIPE, SIG_IGN);       //向已经断开连接的管道和套接字写数据时只返回错误,不主动退出进程
        cons_pipe_name(argv[0]);

        std::vector<std::string> stub_args;
        if (argc >= 2) {		//当前服务以带参模式运行，检测最后一个参数是否为{"-start", "-stop", "-restart"}之一
            if (!strcmp("-start", argv[argc-1])) {		//启动服务
                if (!access(m_pipe_name[0].c_str(), F_OK)) {		//管道文件存在
                    if (check_service_exist()) {		//进一步检测后台服务是否仍在运行过程中
                        printf("当前服务 %s 正在后台运行中\n", g_server_name.c_str());
                        return true;
                    } else {
                        printf("[%s] %s", g_server_name.c_str(), crash_info);
                    }
                }
                start_daemon();
            }

            if (!strcmp("-stop", argv[argc-1])) {		//终止服务
                if (!access(m_pipe_name[0].c_str(), F_OK)) {		//管道文件存在
                    if (check_service_exist())			//进一步确认当前服务正在运行中，执行终止操作
                        connect_service(true);
                    else		//后台服务已崩溃，打印出错信息
                        printf("[%s] %s", g_server_name.c_str(), crash_info);
                } else {
                    printf("该服务 %s 还未启动，不再执行终止操作\n", g_server_name.c_str());
                }
                return true;		//通知后台进程退出之后也没必要继续运行
            }

            if (!strcmp("-restart", argv[argc-1])) {				//重启服务
                if (!access(m_pipe_name[0].c_str(), F_OK)) {		//管道文件存在，终止后台服务或打印崩溃信息
                    if (check_service_exist())
                        connect_service(true);
                    else
                        printf("[%s] %s", g_server_name.c_str(), crash_info);
                }
                start_daemon();
            }
        }

        //当前进程不是daemon进程并且管道存在，此时通过管道连接后台服务并以shell方式运行
        if (!m_is_service && !access(m_pipe_name[0].c_str(), F_OK)) {
            if (check_service_exist()) {		//后台服务正在运行中
                m_as_shell = true;
                connect_service(false);		//连接后台服务
                return true;
            }
            printf("[%s] %s", g_server_name.c_str(), crash_info);		//后台服务已崩溃
        }
        return false;		//预处理失败表示在当前环境下还需要做进一步处理
    }

    bool console_impl::execute_cmd(const std::string& cmd, std::vector<std::string>& args)
    {
        auto idx_it = m_cmd_idx.find(cmd);
        if (m_cmd_idx.end() != idx_it) {
            m_cmd_vec[idx_it->second].f(args);
            return true;
        }
        return false;
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
            std::string input(256, 0);
            ssize_t ret = read(STDIN_FILENO, &input[0], input.size()-1);
            if (-1 == ret || 0 == ret) {
                syslog(LOG_ERR, "read keyboard failed: %s\n", strerror(errno));
                return;
            }

            input.resize((size_t)ret);
            input.pop_back();   //读终端输入时去掉最后一个换行符'\n'
            trim(input);        //移除输入字符串前后的空格符
            if (input.empty()) {
                std::cout<<g_server_name<<":\\>"<<std::flush;
                return;
            }

            //命令行输入的一系列参数都是以空格分隔
            auto str_vec = crx::split(input.data(), input.size(), " ");
            std::string cmd;
            std::vector<std::string> args;
            for (size_t i = 0; i < str_vec.size(); ++i) {
                auto& arg = str_vec[i];
                if (0 == i)
                    cmd = std::string(arg.data, arg.len);
                else
                    args.push_back(std::string(arg.data, arg.len));
            }

            if (!str_vec.empty()) {
                if (-1 == g_wr_fifo) {		//m_wr_fifo等于-1表示当前程序是以普通进程方式运行的
                    if (execute_cmd(cmd, args)) {
                        if ("q" != input)
                            std::cout<<g_server_name<<":\\>"<<std::flush;
                    } else {        //若需要执行的命令并未通过add_cmd接口添加时，通知该命令未知
                        std::cout<<unknown_cmd<<g_server_name<<":\\>"<<std::flush;
                    }
                } else {		//若不等于-1则表示当前程序以shell方式运行
                    if ("h" == input || "q" == input) {     //输入为"h"(帮助)或者"q"(退出)时直接执行相应命令
                        execute_cmd(cmd, args);
                        if ("h" == input)
                            std::cout<<g_server_name<<":\\>"<<std::flush;
                    } else {        //否则将命令行参数传给后台daemon进程
                        write(g_wr_fifo, input.data(), input.size());
                    }
                }
            }
        };
        sch_impl->add_event(ev);
    }

    //当前程序以daemon进程在后台运行时，该进程通过管道接收命令行的输入参数
    void console_impl::listen_pipe_input()
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_c->m_impl);
        m_console_ev = std::make_shared<eth_event>();

        //open函数带O_NONBLOCK标志调用时只表明不阻塞open函数，而并不表示对rd_fifo的读写同样也是非阻塞的
        m_console_ev->fd = open(m_pipe_name[0].c_str(), O_RDONLY | O_NONBLOCK);
        crx::setnonblocking(m_console_ev->fd);

        m_console_ev->sch_impl = sch_impl;
        m_console_ev->f = [this](uint32_t events) {
            //当读管道可读时，说明有其他进程的读写管道已经创建，并且写管道已经与本进程的读管道完成连接，
            //此时在本进程中打开写管道并与其他进程的读管道完成连接
            if (-1 == g_wr_fifo)
                g_wr_fifo = open(m_pipe_name[1].c_str(), O_WRONLY | O_NONBLOCK);

            std::string cmd_buf;
            int sts = m_console_ev->async_read(cmd_buf);
            if (sts <= 0) {			//读管道输入异常，将标准输出恢复成原先的值，并关闭已经打开的写管道
                close(g_wr_fifo);
                g_wr_fifo = -1;
            } else {
                //用来建立连接的心跳包有可能会与控制台输入产生粘包问题，因此首先需要将心跳包部分截除
                if (!strncmp(cmd_buf.data(), heart_beat, strlen(heart_beat)))
                    cmd_buf = cmd_buf.substr(strlen(heart_beat));		//just used to establish connection...

                trim(cmd_buf);
                if (cmd_buf.empty())
                    return;

                std::vector<std::string> args;
                if (!strncmp(cmd_buf.data(), service_quit_sig, strlen(service_quit_sig))) {
                    quit_loop(args);		//收到停止daemon进程的命令，直接退出循环
                } else {
                    auto str_vec = crx::split(cmd_buf.data(), cmd_buf.size(), " ");		//命令行参数以空格作为分隔符
                    std::string cmd;
                    for (size_t i = 0; i < str_vec.size(); ++i) {
                        auto& arg = str_vec[i];
                        if (0 == i)
                            cmd = std::string(arg.data, arg.len);
                        else
                            args.push_back(std::string(arg.data, arg.len));
                    }

                    if (!str_vec.empty() && !execute_cmd(cmd, args))
                        std::cout<<unknown_cmd<<std::flush;
                }
            }
        };
        sch_impl->add_event(m_console_ev);
    }

    /*
     * 该函数用于连接后台daemon进程
     * @stop_service为true时终止后台进程，false时仅仅作为shell与后台进程进行交互
     */
    void console_impl::connect_service(bool stop_service)
    {
        int rd_fifo = open(m_pipe_name[1].c_str(), O_RDONLY | O_NONBLOCK);
        crx::setnonblocking(rd_fifo);

        g_wr_fifo = open(m_pipe_name[0].c_str(), O_WRONLY | O_NONBLOCK);
        crx::setnonblocking(g_wr_fifo);

        if (!stop_service)
            write(g_wr_fifo, heart_beat, strlen(heart_beat));			//发送心跳包建立实际的连接过程

        bool excep = false;
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_c->m_impl);

        auto eth_ev = std::make_shared<eth_event>();
        eth_ev->fd = rd_fifo;
        eth_ev->sch_impl = sch_impl;
        eth_ev->f = [&](uint32_t events) {
            std::string output;
            int sts = eth_ev->async_read(output);
            if (!output.empty()) {
                std::cout<<output;      //打印后台daemon进程输出的运行时信息
                if (!stop_service && !excep)        //当前shell既不是用来停止后台服务，也未出现管道异常，则打印命令行提示符
                    std::cout<<g_server_name<<":\\>"<<std::flush;
            }

            if (sts <= 0) {		//对端关闭或异常
                excep = true;		//该变量指示出现异常
                sch_impl->m_go_done = false;
                sch_impl->remove_event(eth_ev->fd);
            }
        };
        sch_impl->add_event(eth_ev);

        if (!stop_service)      //此时以shell方式运行当前进程，则等待控制台输入后执行相应操作
            listen_keyboard_input();
        else        //若是停止后台daemon进程，则首先发送自定义的退出信号
            write(g_wr_fifo, service_quit_sig, strlen(service_quit_sig));

        sch_impl->main_coroutine(m_c);      //进入主协程
        close(g_wr_fifo);
        close(rd_fifo);

        if (stop_service) {     //移除daemon进程创建的fifo
            remove(m_pipe_name[0].c_str());
            remove(m_pipe_name[1].c_str());
            printf("后台服务已关闭，退出当前shell！\n");
        }
    }

    void console_impl::start_daemon()
    {
        m_is_service = true;
        daemon(1, 0);		//创建守护进程，不切换进程当前工作目录

        std::string pid_str = std::to_string(getpid());
        m_pipe_name[0] = m_pipe_dir+"/0."+pid_str;
        m_pipe_name[1] = m_pipe_dir+"/1."+pid_str;
    }

    void console_impl::quit_loop(std::vector<std::string>& args)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_c->m_impl);
        for (auto co_impl : sch_impl->m_cos)
            co_impl->status = CO_UNKNOWN;

        if (!m_as_shell) {			//以shell方式运行时不需要执行init/destroy操作
            printf("[%s] 已发送控制台退出信号，正在执行退出操作...\n", g_server_name.c_str());
            fflush(stdout);
            m_c->destroy();
        }
        sch_impl->m_go_done = false;
    }

    //打印帮助信息
    void console_impl::print_help(std::vector<std::string>& args)
    {
        std::cout<<std::right<<std::setw(5)<<"cmd";
        std::cout<<std::setw(10)<<' '<<std::setw(0)<<"comments"<<std::endl;
        for (auto& cmd : m_cmd_vec) {
            std::string str = "  "+cmd.cmd;
            std::cout<<std::left<<std::setw(15)<<str<<cmd.comment<<std::endl;
        }
        std::cout<<std::endl;
    }

    console::console()
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        sch_impl->m_util_impls[EXT_DATA] = std::make_shared<console_impl>(this);
    }

    console::~console()
    {
        closelog();
    }

    //添加控制台命令
    void console::add_cmd(const char *cmd, std::function<void(std::vector<std::string>&)> f,
                          const char *comment)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto con_impl = std::dynamic_pointer_cast<console_impl>(sch_impl->m_util_impls[EXT_DATA]);
        assert(con_impl->m_cmd_idx.end() == con_impl->m_cmd_idx.find(cmd));		//断言当前添加的命令在已添加的命令集中不重复存在
        con_impl->m_cmd_idx[cmd] = con_impl->m_cmd_vec.size();
        con_impl->m_cmd_vec.push_back({cmd, f, comment});
    }

    void console_impl::bind_core(int which)
    {
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(which, &mask);
        if (syscall(__NR_gettid) == getpid()) {     //main thread
            if (sched_setaffinity(0, sizeof(mask), &mask) < 0)
                syslog(LOG_ERR, "bind core failed: %s\n", strerror(errno));
        } else {
            if (pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask) < 0)
                syslog(LOG_ERR, "bind core failed: %s\n", strerror(errno));
        }
    }

    int console::run(int argc, char *argv[], const char *conf /*= "ini/server.ini"*/, int bind_flag /*= -1*/)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto con_impl = std::dynamic_pointer_cast<console_impl>(sch_impl->m_util_impls[EXT_DATA]);

        sch_impl->m_epoll_fd = epoll_create(EPOLL_SIZE);
        if (__glibc_unlikely(-1 == sch_impl->m_epoll_fd)) {
            syslog(LOG_ERR, "epoll_create failed: %s\n", strerror(errno));
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

        //解析日志配置
        if (!access(conf, F_OK)) {
            sch_impl->m_ini_file = conf;
            ini ini_parser;
            ini_parser.load(conf);
            if (ini_parser.has_section("log")) {
                ini_parser.set_section("log");
                sch_impl->m_remote_log = ini_parser.get_int("remote") != 0;
            }
        }

        if (!sch_impl->m_sec_wheel.m_impl)       //创建一个秒盘
            sch_impl->m_sec_wheel = get_timer_wheel(1000, 60);
        sch_impl->m_sec_wheel.add_handler(5*1000, std::bind(&scheduler_impl::periodic_trim_memory, sch_impl.get()));

        //处理绑核操作
        int cpu_num = get_nprocs();
        if (-1 != bind_flag) {
            if (INT_MAX == bind_flag)
                con_impl->bind_core(con_impl->m_random()%cpu_num);
            else if (0 <= bind_flag && bind_flag < cpu_num)     //bind_flag的取值范围为0~cpu_num-1
                con_impl->bind_core(bind_flag);
        }

        //打印帮助信息
        std::vector<std::string> str_vec;
        con_impl->print_help(str_vec);

        if (!init(argc, argv))		//执行初始化，若初始化返回失败，直接退出当前进程
            return EXIT_FAILURE;

        if (con_impl->m_is_service) {			//若当前程序以后台daemon进程方式运行，则还需要创建读写两个fifo用于进程间通信
            mkfifo(con_impl->m_pipe_name[0].c_str(), 0777);
            mkfifo(con_impl->m_pipe_name[1].c_str(), 0777);
        }

        if (!con_impl->m_is_service)        //前台运行时在控制台输入接收命令
            con_impl->listen_keyboard_input();
        else        //后台运行时从管道文件中接收命令
            con_impl->listen_pipe_input();

        sch_impl->main_coroutine(this);     //进入主协程
        return EXIT_SUCCESS;
    }
}