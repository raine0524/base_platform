#include "stdafx.h"

namespace crx
{
    const char *heart_beat = "just a heart beat";
    const char *service_quit_sig = "user_def_quit";

    const char *unknown_cmd = "未知命令或参数！「请输入help(h)显示帮助」\n";
    const char *crash_info = "后台服务已崩溃，请检查当前目录下的core文件查看崩溃时的调用堆栈\n";

    std::string crx_tmp_dir = ".crx_tmp/";		//kbase库的临时目录，用于存放一系列在服务运行过程中需要用到的临时文件
    std::string g_server_name;		//当前服务的名称

    console_impl::console_impl(console *c)
            :m_c(c)
            ,m_is_service(false)
            ,m_init(false)
            ,m_as_shell(false)
            ,m_pipe_conn(false)
            ,m_rd_fifo(-1)
            ,m_wr_fifo(-1)
            ,m_stdout_backup(-1)
    {
        m_cmds[false]["-start"] = {"在后台启动当前服务", start_daemon};
        m_cmds[false]["-stop"] = {"停止后台服务", stop_daemon};

        m_cmds[true]["h"] = {"显示帮助", print_help};
        m_cmds[true]["q"] = {"退出程序", quit_loop};
    }

    console_impl::~console_impl()
    {
        if (-1 != m_rd_fifo) {
            close(m_rd_fifo);
            m_rd_fifo = -1;
        }
        if (-1 != m_wr_fifo) {
            close(m_wr_fifo);
            m_wr_fifo = -1;
        }
        if (-1 != m_stdout_backup) {
            close(m_stdout_backup);
            m_stdout_backup = -1;
        }
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
        m_pipe_dir = crx_tmp_dir+"daemon_pipe/"+g_server_name;

        if (access(m_pipe_dir.c_str(), F_OK)) {
            mkdir_multi(m_pipe_dir.c_str());		//创建管道目录
        } else {
            depth_first_traverse_dir(m_pipe_dir.c_str(), [&](const std::string& file, void *arg) {
                int idx = file.front()-'0';
                m_pipe_name[idx] = m_pipe_dir+"/"+file;
            }, nullptr, false);
        }
    }

    //控制台预处理操作
    bool console_impl::preprocess(int argc, char *argv[])
    {
        signal(SIGPIPE, SIG_IGN);       //向已经断开连接的管道和套接字写数据时只返回错误,不主动退出进程
        auto sch_impl = (scheduler_impl*)m_c->m_obj;
        cons_pipe_name(argv[0]);

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
                std::vector<std::string> args = {"-start"};
                execute_cmd(args);
            }

            if (!strcmp("-stop", argv[argc-1])) {		//终止服务
                std::vector<std::string> args = {"-stop"};
                if (!access(m_pipe_name[0].c_str(), F_OK)) {		//管道文件存在
                    if (check_service_exist())			//进一步确认当前服务正在运行中，执行终止操作
                        execute_cmd(args);
                    else		//后台服务已崩溃，打印出错信息
                        printf("[%s] %s", g_server_name.c_str(), crash_info);
                } else {
                    printf("该服务 %s 还未启动，不再执行终止操作\n", g_server_name.c_str());
                }
                return true;		//通知后台进程退出之后也没必要继续运行
            }

            if (!strcmp("-restart", argv[argc-1])) {				//重启服务
                std::vector<std::string> args = {"-stop"};
                if (!access(m_pipe_name[0].c_str(), F_OK)) {		//管道文件存在，终止后台服务或打印崩溃信息
                    if (check_service_exist())
                        execute_cmd(args);
                    else
                        printf("[%s] %s", g_server_name.c_str(), crash_info);
                }
                args = {"-start"};
                execute_cmd(args);
            }
        }

        m_init = true;
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

    //执行命令，该命令分为带参运行形式以及运行时两种，由m_init变量指明当前执行的是哪种类型的命令
    bool console_impl::execute_cmd(std::vector<std::string>& args)
    {
        auto& cmds = m_cmds[m_init];
        if (cmds.end() == cmds.find(args[0]))
            return false;

        //构造命令对应的参数，这些参数不包含命令本身
        std::vector<std::string> fargs(args.begin()+1, args.end());
        cmds[args[0]].f(fargs, m_c);
        return true;
    }

    /**
     * 函数用于等待shell输入
     * @当前程序以普通进程运行时将调用该例程等待命令行输入
     * @当前程序以shell方式运行时将调用该例程将命令行输入参数传给后台运行的服务
     */
    void console_impl::listen_keyboard_input(int wr_fifo)
    {
        auto sch_impl = (scheduler_impl*)m_c->m_obj;
        std::cout<<g_server_name<<":\\>"<<std::endl;

        m_wr_fifo = wr_fifo;
        setnonblocking(STDIN_FILENO);       //将标准输入设为非阻塞

        auto eth_ev = new eth_event;
        eth_ev->fd = STDIN_FILENO;
        eth_ev->sch_impl = sch_impl;
        eth_ev->f = [&](scheduler *sch, eth_event *ev) {
            std::string input(256, 0);
            ssize_t ret = read(STDIN_FILENO, &input[0], input.size()-1);
            if (-1 == ret || 0 == ret)
                return;

            input.resize((size_t)ret);
            input.pop_back();		//读终端输入时去掉最后一个换行符'\n'
            if (input.empty()) {
                std::cout<<g_server_name<<":\\>"<<std::endl;
                return;
            }

            //命令行输入的一系列参数都是以空格分隔
            bool exit = ("q" == input) ? true : false;
            auto str_vec = crx::split(input, " ");
            if (!str_vec.empty()) {
                if (-1 == m_wr_fifo) {		//m_wr_fifo等于-1表示当前程序是以普通进程方式运行的
                    if (execute_cmd(str_vec)) {
                        if (!exit)
                            std::cout<<g_server_name<<":\\>"<<std::endl;
                    } else {        //若需要执行的命令并未通过add_cmd接口添加时，通知该命令未知
                        std::cout<<unknown_cmd<<std::endl;
                    }
                } else {		//若不等于-1则表示当前程序以shell方式运行
                    if ("h" == input || "q" == input)       //输入为"h"(帮助)或者"q"(退出)时直接执行相应命令
                        execute_cmd(str_vec);
                    else        //否则将命令行参数传给后台daemon进程
                        write(m_wr_fifo, input.data(), input.size());
                }
            }
        };
        eth_ev->args = eth_ev;
        sch_impl->add_event(eth_ev);
    }

    //当前程序以daemon进程在后台运行时，该进程通过管道接收命令行的输入参数
    void console_impl::listen_pipe_input()
    {
        //以非阻塞方式创建管道时，被动方首先打开非阻塞的读管道并在epoll上监听之
        //open函数带O_NONBLOCK标志调用时只表明不阻塞open函数，而并不表示对rd_fifo的读写同样也是非阻塞的
        m_rd_fifo = open(m_pipe_name[0].c_str(), O_RDONLY | O_NONBLOCK);
        crx::setnonblocking(m_rd_fifo);		//因此调用setnonblocking将该文件描述符设置为非阻塞的

        auto sch_impl = (scheduler_impl*)m_c->m_obj;
        eth_event *eth_ev = new eth_event;
        eth_ev->fd = m_rd_fifo;
        eth_ev->sch_impl = sch_impl;

        eth_ev->f = [&](scheduler *sch, eth_event *ev) {
            //当读管道可读时，说明有其他进程的读写管道已经创建，并且写管道已经与本进程的读管道完成连接，
            //此时在本进程中创建写管道并与其他进程的读管道完成连接
            if (!m_pipe_conn) {
                m_wr_fifo = open(m_pipe_name[1].c_str(), O_WRONLY);
                //将写管道重定向至标准输出文件描述符，shell进程通过管道接收后台进程的一系列运行时输出信息
                dup2(m_wr_fifo, STDOUT_FILENO);
                m_pipe_conn = true;
            }

            auto sch_impl = (scheduler_impl*)sch->m_obj;
            std::string cmd_buf;
            int sts = sch_impl->async_read(ev->fd, cmd_buf);
            if (sts <= 0) {			//读管道输入异常，将标准输出恢复成原先的值，并关闭已经打开的写管道
                dup2(m_stdout_backup, STDOUT_FILENO);
                close(m_wr_fifo);
                m_pipe_conn = false;
            } else {
                //用来建立连接的心跳包有可能会与控制台输入产生粘包问题，因此首先需要将心跳包部分截除
                if (!strncmp(cmd_buf.data(), heart_beat, strlen(heart_beat)))
                    cmd_buf = cmd_buf.substr(strlen(heart_beat));		//just used to establish connection...

                if (cmd_buf.empty())
                    return;

                std::vector<std::string> str_vec;
                if (!strncmp(cmd_buf.data(), service_quit_sig, strlen(service_quit_sig))) {
                    quit_loop(str_vec, m_c);		//收到停止daemon进程的命令，直接退出循环
                } else {
                    str_vec = crx::split(cmd_buf, " ");		//命令行参数以空格作为分隔符
                    if (!str_vec.empty() && !execute_cmd(str_vec))
                        std::cout<<unknown_cmd<<std::endl;
                }
            }
        };
        eth_ev->args = eth_ev;
        sch_impl->add_event(eth_ev);
    }

    /**
     * 该函数用于连接后台daemon进程
     * @stop_service为true时终止后台进程，false时仅仅作为shell与后台进程进行交互
     */
    void console_impl::connect_service(bool stop_service)
    {
        int wr_fifo = open(m_pipe_name[0].c_str(), O_WRONLY | O_NONBLOCK);
        int rd_fifo = open(m_pipe_name[1].c_str(), O_RDONLY | O_NONBLOCK);
        crx::setnonblocking(rd_fifo);
        write(wr_fifo, heart_beat, strlen(heart_beat));			//发送心跳包建立实际的连接过程

        bool excep = false;
        auto sch_impl = (scheduler_impl*)m_c->m_obj;

        eth_event *eth_ev = new eth_event;
        eth_ev->fd = rd_fifo;
        eth_ev->sch_impl = sch_impl;
        eth_ev->f = [&](scheduler *sch, eth_event *args) {
            std::string output;
            int sts = sch_impl->async_read(args->fd, output);
            if (!output.empty()) {
                printf("%s", output.c_str());		//打印后台daemon进程输出的运行时信息
                if (!stop_service && !excep)        //当前shell既不是用来停止后台服务，也未出现管道异常，则打印命令行提示符
                    std::cout<<g_server_name<<":\\>"<<std::endl;
            }

            if (sts <= 0) {		//对端关闭或异常
                excep = true;		//该变量指示出现异常
                sch_impl->remove_event(args);
                sch_impl->m_go_done = false;
            }
        };
        eth_ev->args = eth_ev;
        sch_impl->add_event(eth_ev);

        if (!stop_service)      //此时以shell方式运行当前进程，则等待控制台输入后执行相应操作
            listen_keyboard_input(wr_fifo);
        else        //若是停止后台daemon进程，则首先发送自定义的退出信号
            write(wr_fifo, service_quit_sig, strlen(service_quit_sig));
        m_c->co_yield(0);       //切换至主协程

        if (excep)
            printf("后台服务已关闭，退出当前shell！\n");

        close(wr_fifo);
        close(rd_fifo);
    }

    void console_impl::start_daemon(std::vector<std::string>& args, console *c)
    {
        auto sch_impl = (scheduler_impl*)c->m_obj;
        auto con_impl = (console_impl*)sch_impl->m_obj;
        con_impl->m_is_service = true;
        daemon(1, 0);		//创建守护进程，不切换进程当前工作目录

        std::string pid_str = std::to_string(getpid());
        con_impl->m_pipe_name[0] = con_impl->m_pipe_dir+"/0."+pid_str;
        con_impl->m_pipe_name[1] = con_impl->m_pipe_dir+"/1."+pid_str;
    }

    void console_impl::stop_daemon(std::vector<std::string>& args, console *c)
    {
        auto sch_impl = (scheduler_impl*)c->m_obj;
        auto con_impl = (console_impl*)sch_impl->m_obj;
        con_impl->connect_service(true);
    }

    void console_impl::quit_loop(std::vector<std::string>& args, console *c)
    {
        auto sch_impl = (scheduler_impl*)c->m_obj;
        auto con_impl = (console_impl*)sch_impl->m_obj;
        if (!con_impl->m_as_shell) {			//以shell方式运行时不需要执行init/destroy操作
            printf("[%s] 已发送控制台退出信号，正在执行退出操作...\n", g_server_name.c_str());
            fflush(stdout);
            c->destroy();
        }
        sch_impl->m_go_done = false;
    }

    //打印帮助信息
    void console_impl::print_help(std::vector<std::string>& args, console *c)
    {
        auto sch_impl = (scheduler_impl*)c->m_obj;
        auto con_impl = (console_impl*)sch_impl->m_obj;
        auto& cmds = con_impl->m_cmds[con_impl->m_init];
        std::cout<<std::right<<std::setw(5)<<"cmd";
        std::cout<<std::setw(10)<<' '<<std::setw(0)<<"comments"<<std::endl;
        for (auto& cmd : cmds) {
            std::string str = "  "+cmd.first;
            std::cout<<std::left<<std::setw(15)<<str<<cmd.second.comment<<std::endl;
        }
        std::cout<<std::endl;
    }

    console::console()
    {
        auto sch_impl = (scheduler_impl*)m_obj;
        sch_impl->m_obj = new console_impl(this);
    }

    console::~console()
    {
        auto sch_impl = (scheduler_impl*)m_obj;
        delete (console_impl*)sch_impl->m_obj;
    }

    //添加控制台命令
    void console::add_cmd(const char *cmd, std::function<void(std::vector<std::string>&, console*)> f,
                          const char *comment)
    {
        auto sch_impl = (scheduler_impl*)m_obj;
        auto con_impl = (console_impl*)sch_impl->m_obj;
        auto& cmd_map = con_impl->m_cmds[true];
        assert(cmd_map.end() == cmd_map.find(cmd));		//断言当前添加的命令在已添加的命令集中不重复存在
        cmd_map[cmd] = {comment, f};
    }

    int console::run(int argc, char *argv[])
    {
        auto sch_impl = (scheduler_impl*)m_obj;
        auto con_impl = (console_impl*)sch_impl->m_obj;

        //首先执行预处理操作，预处理主要和当前运行环境以及运行时所带参数有关
        if (con_impl->preprocess(argc, argv))
            return EXIT_SUCCESS;

        std::vector<std::string> str_vec;
        con_impl->print_help(str_vec, this);		//打印帮助信息

        if (!init(argc, argv))		//执行初始化，若初始化返回失败，直接退出当前进程
            return EXIT_FAILURE;

        if (con_impl->m_is_service) {			//若当前程序以后台daemon进程方式运行，则还需要创建读写两个fifo用于进程间通信
            mkfifo(con_impl->m_pipe_name[0].c_str(), 0777);
            mkfifo(con_impl->m_pipe_name[1].c_str(), 0777);
        }

        if (!con_impl->m_is_service) {			//前台运行时在控制台输入接收命令
            con_impl->listen_keyboard_input(-1);
        } else {			//后台运行时从管道文件中接收命令
            con_impl->m_stdout_backup = dup(STDOUT_FILENO);
            con_impl->listen_pipe_input();
        }

        sch_impl->m_sch->co_yield(0);       //切换至主协程

        if (con_impl->m_is_service) {			//退出时移除所创建的读写fifo
            remove(con_impl->m_pipe_name[0].c_str());
            remove(con_impl->m_pipe_name[1].c_str());
        }
        return EXIT_SUCCESS;
    }
}
