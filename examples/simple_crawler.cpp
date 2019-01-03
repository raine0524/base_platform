#include "crx_pch.h"

class simple_crawler : public crx::console
{
public:
    virtual bool init(int argc, char *argv[]);

    virtual void destroy(){}

    void get_web_page(std::vector<std::string>& urls);

private:
    crx::http_client m_http_client;
    std::map<size_t, std::string> m_co_url;
    std::map<int, size_t> m_conn_co;
};

void simple_crawler::get_web_page(std::vector<std::string>& urls)
{
    for (auto& url : urls) {
        size_t co_id = co_create([this](crx::scheduler *sch, size_t co_id) {
            auto& co_url = m_co_url[co_id];
            int conn = m_http_client.connect(co_url.c_str(), 80);
            if (-1 == conn) {
                m_co_url.erase(co_id);
                return;
            }

            m_conn_co[conn] = co_id;
            m_http_client.GET(conn, "/", nullptr);
        }, true);

        pout("url: %s, co_id: %lu\n", url.c_str(), co_id);
        m_co_url[co_id] = std::move(url);
        co_yield(co_id);
    }
}

bool simple_crawler::init(int argc, char *argv[])
{
    m_http_client = get_http_client([this](int conn, int status,
            std::map<std::string, std::string>& header_kvs,
            const char* data, size_t len) {
        pout("\nresponse: %d %d\n\n", conn, status);
        if (data)
            pout("%s\n", data);
        m_http_client.release(conn);

        m_co_url.erase(m_conn_co[conn]);
        m_conn_co.erase(conn);
    });
    return true;
}

int main(int argc, char *argv[])
{
    simple_crawler crawler;
    crawler.add_cmd("get", [&](std::vector<std::string>& args) {
        crawler.get_web_page(args);
    }, "抓取指定网页");
    crawler.run(argc, argv);
}
