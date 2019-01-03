#pragma once

namespace crx
{
#pragma pack(1)
    /*
     * simp协议的头部，此处是对ctl_flag字段更详细的表述：
     *          -->第0位: 1-表示当前请求由库这一层处理  0-表示路由给上层应用
     *          -->第1位: 1-表示推送notify 0-非推送
     *          -->第2位: 当为非推送时这一位有效 1-request 0-response
     *          -->第3位: 1-表示registry发送的数据 0-其他服务发送
     *          -->第31位: 1-加密(暂不支持) 0-非加密
     *          其余字段暂时保留
     */
    struct simp_header
    {
        uint32_t magic_num;         //魔数，4个字节依次为0x5f3759df
        uint32_t length;            //body部分长度，整个一帧的大小为sizeof(simp_header)+length
        uint16_t type;              //表明数据的类型
        uint16_t cmd;               //若是请求类型的数据,指明哪一个请求
        uint32_t ctl_flag;          //控制字段
        unsigned char token[16];    //请求携带token，表明请求合法(token=md5(current_timestamp+name))

        simp_header()
        {
            bzero(this, sizeof(simp_header));
            magic_num = htonl(0x5f3759df);
        }
    };
#pragma pack()

    struct console_cmd
    {
        std::string cmd;        //命令
        std::function<void(std::vector<std::string>&)> f;     //回调函数
        std::string comment;    //注释
    };

    class console_impl : public impl
    {
    public:
        console_impl(console *c);
        virtual ~console_impl() {}

        void bind_core(int which);       //将线程绑定到指定的cpu核上

        bool preprocess(int argc, char *argv[]);

        void parse_config(const char *config);

        void execute_cmd(const std::string& cmd, std::vector<std::string>& args);

        void listen_keyboard_input();

        void quit_loop(std::vector<std::string>& args);

        void print_help(std::vector<std::string>& args);

    private:
        bool check_service_exist();

        void start_daemon();

        void connect_service();

        void stop_service(bool pout);

        void tcp_callback(bool client, int conn, const std::string& ip_addr, uint16_t port, char *data, size_t len);

    public:
        console *m_c;

        /*
         * @m_is_service：当前服务以daemon进程在后台运行时，该值为true，否则为false
         * @m_as_shell：若后台服务正在运行过程中再次启动该程序，则新的进程作为daemon进程的shell存在
         */
        bool m_is_service, m_as_shell, m_close_exp;
        std::vector<console_cmd> m_cmd_vec;

        int m_conn;     //无论何时都只能存在一个有效连接
        tcp_client m_client;
        tcp_server m_server;
        std::string m_simp_buf;
    };
}
