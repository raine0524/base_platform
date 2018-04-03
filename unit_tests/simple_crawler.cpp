#include "crx_pch.h"

class py_plot_test : public crx::console
{
public:
    virtual bool init(int argc, char *argv[]);

    virtual void destroy(){}

    void get_web_page(std::vector<std::string>& urls);

private:
    crx::http_client *m_http_client;
};

void py_plot_test::get_web_page(std::vector<std::string>& urls)
{
    for (auto& url : urls) {
        size_t co_id = co_create([&](crx::scheduler *sch, void *arg) {
            int conn = m_http_client->connect(url.c_str(), 80);
            m_http_client->GET(conn, "/", nullptr);
        }, nullptr, true);
        co_yield(co_id);
    }
}

bool py_plot_test::init(int argc, char *argv[])
{
    m_http_client = get_http_client([&](int conn, int status, std::unordered_map<std::string, std::string>& header_kvs,
                                        const char* data, size_t len, void *args) {
        printf("get data: %s\n", data);
    });
    return true;
}

int main(int argc, char *argv[])
{
    py_plot_test plot;
    plot.add_cmd("get", [&](std::vector<std::string>& args, crx::console *c) {
        plot.get_web_page(args);
    }, "抓取指定网页");
    plot.run(argc, argv);
}
