#include "crx_pch.h"

class simple_simps : public crx::console
{
public:
    simple_simps() = default;
    virtual ~simple_simps() override = default;

public:
    bool init(int argc, char **argv) override;

    void destroy() override {}

    void test_remote_log(std::vector<std::string>& args);

private:
    void on_connect(const crx::server_info& info);

    void on_disconnect(const crx::server_info& info);

    void on_request(const crx::server_info& info, const crx::server_cmd& cmd, char *data, size_t len);

    void on_response(const crx::server_info& info, const crx::server_cmd& cmd, char *data, size_t len);

    void on_notify(const crx::server_info& info, const crx::server_cmd& cmd, char *data, size_t len);

private:
    crx::simpack_server m_server;
    crx::log m_log;
};