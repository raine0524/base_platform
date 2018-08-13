#include "crx_pch.h"

class http_plot : public crx::console
{
public:
    bool init(int argc, char **argv) override
    {
        m_ext_headers = {{"Figure-Name", "test-1"}};
        m_client = get_http_client([](int conn, int sts, std::map<std::string,
                const char*>& headers, char *data, size_t len) {
            printf("[%d] status: %d ==> %s", conn, sts, data);
        });
        return true;
    }

    void destroy() override {}

    void plot_2d_func();

    void plot_3d_func();

private:
    std::vector<double> lorenz(std::vector<double>& point, double s = 10, double r = 28, double b = 2.667);

private:
    std::map<std::string, std::string> m_ext_headers;
    crx::http_client m_client;
};

void http_plot::plot_2d_func()
{
    size_t co_id = co_create([this](scheduler *sch, size_t co_id) {
        rapidjson::Document doc;
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

        doc.SetObject();
        rapidjson::Document::AllocatorType& alloc = doc.GetAllocator();
        doc.AddMember("dimension", 2, alloc);       //plot 2d figure
        doc.AddMember("point_num", 50, alloc);      //point number
        doc.AddMember("title", rapidjson::Value().SetString("2-dimension function example"), alloc);

        rapidjson::Value plot_text(rapidjson::kArrayType);
        rapidjson::Value text_obj(rapidjson::kObjectType);
        text_obj.AddMember("xlim", 2.2, alloc);
        text_obj.AddMember("ylim", 7.5, alloc);
        text_obj.AddMember("desc", rapidjson::Value().SetString("$e^x$"), alloc);
        plot_text.PushBack(text_obj, alloc);

        text_obj.SetObject();
        text_obj.AddMember("xlim", 3.2, alloc);
        text_obj.AddMember("ylim", 7.5, alloc);
        text_obj.AddMember("desc", rapidjson::Value().SetString("$2^x$"), alloc);
        plot_text.PushBack(text_obj, alloc);
        doc.AddMember("text", plot_text, alloc);

        rapidjson::Value plot_axis(rapidjson::kObjectType);
        rapidjson::Value axis_one(rapidjson::kArrayType);
        axis_one.PushBack(-4.0, alloc);
        axis_one.PushBack(4.0, alloc);
        plot_axis.AddMember("x", axis_one, alloc);      // x: [-4.0, 4.0]
        axis_one.SetArray();
        axis_one.PushBack(-0.5, alloc);
        axis_one.PushBack(50.0, alloc);
        plot_axis.AddMember("y", axis_one, alloc);      // y: [-0.5, 50.0]
        doc.AddMember("axis", plot_axis, alloc);

        rapidjson::Value plot_label(rapidjson::kObjectType);
        plot_label.AddMember("x", rapidjson::Value().SetString("x-axis"), alloc);
        plot_label.AddMember("y", rapidjson::Value().SetString("y-axis"), alloc);
        doc.AddMember("label", plot_label, alloc);

        rapidjson::Value plot_funcs(rapidjson::kArrayType);
        rapidjson::Value plot_func(rapidjson::kObjectType);
        plot_func.AddMember("init", rapidjson::Value(rapidjson::kArrayType), alloc);
        rapidjson::Value init_point(rapidjson::kArrayType);
        init_point.PushBack(0, alloc);
        init_point.PushBack(-4.0, alloc);
        init_point.PushBack(std::exp(-4.0), alloc);
        plot_func["init"].PushBack(init_point, alloc);
        plot_func.AddMember("line", 0, alloc);
        plot_func.AddMember("point", 0, alloc);
        plot_func.AddMember("col", 0, alloc);
        plot_func.AddMember("desc", rapidjson::Value().SetString("$e^x$"), alloc);
        plot_funcs.PushBack(plot_func, alloc);

        plot_func.SetObject();
        plot_func.AddMember("init", rapidjson::Value(rapidjson::kArrayType), alloc);
        init_point.SetArray();
        init_point.PushBack(0, alloc);
        init_point.PushBack(-4.0, alloc);
        init_point.PushBack(std::pow(2, -4.0), alloc);
        plot_func["init"].PushBack(init_point, alloc);
        plot_func.AddMember("line", 1, alloc);
        plot_func.AddMember("point", 1, alloc);
        plot_func.AddMember("col", 1, alloc);
        plot_func.AddMember("desc", rapidjson::Value().SetString("$2^x$"), alloc);
        plot_funcs.PushBack(plot_func, alloc);
        doc.AddMember("funcs", plot_funcs, alloc);

        doc.Accept(writer);
        int conn = m_client.connect("127.0.0.1", 19915);
        m_client.POST(conn, "/plot/create", &m_ext_headers, buffer.GetString(), buffer.GetLength());
        co_sleep(1);

        double dt = 8.0/50;
        for (double x = -4+dt; x <= 4; x += dt) {
            doc.RemoveAllMembers();
            doc.AddMember("funcs", rapidjson::Value(rapidjson::kArrayType), alloc);
            doc["funcs"].PushBack(rapidjson::Value(rapidjson::kArrayType), alloc);
            rapidjson::Value point(rapidjson::kArrayType);
            point.PushBack(x, alloc);
            point.PushBack(std::exp(x), alloc);
            doc["funcs"][0].PushBack(point, alloc);

            doc["funcs"].PushBack(rapidjson::Value(rapidjson::kArrayType), alloc);
            point.SetArray();
            point.PushBack(x, alloc);
            point.PushBack(std::pow(2, x), alloc);
            doc["funcs"][1].PushBack(point, alloc);

            buffer.Clear();
            writer.Reset(buffer);
            doc.Accept(writer);

            m_client.POST(conn, "/plot/append", &m_ext_headers, buffer.GetString(), buffer.GetLength());
            co_sleep(1);
        }

        co_sleep(2);
        m_client.GET(conn, "/plot/destroy", &m_ext_headers);
        m_client.release(conn);
    });
    co_yield(co_id);
}

std::vector<double> http_plot::lorenz(std::vector<double> &point, double s /*= 10*/, double r /*= 28*/, double b /*= 2.667*/)
{
    std::vector<double> axis_dot(3, 0.0);
    axis_dot[0] = s*(point[1]-point[0]);
    axis_dot[1] = r*point[0]-point[1]-point[0]*point[2];
    axis_dot[2] = point[0]*point[1]-b*point[2];
    return axis_dot;
}

void http_plot::plot_3d_func()
{
    size_t co_id = co_create([this](scheduler *sch, size_t co_id) {
        rapidjson::Document doc;
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

        doc.SetObject();
        rapidjson::Document::AllocatorType& alloc = doc.GetAllocator();
        doc.AddMember("dimension", 3, alloc);       //plot 3d figure
        doc.AddMember("point_num", 10001, alloc);
        doc.AddMember("title", rapidjson::Value().SetString("Lorenz Attractor"), alloc);

        rapidjson::Value plot_axis(rapidjson::kObjectType);
        rapidjson::Value axis_one(rapidjson::kArrayType);
        axis_one.PushBack(-30.0, alloc);
        axis_one.PushBack(30.0, alloc);
        plot_axis.AddMember("x", axis_one, alloc);
        axis_one.SetArray();
        axis_one.PushBack(-30.0, alloc);
        axis_one.PushBack(30.0, alloc);
        plot_axis.AddMember("y", axis_one, alloc);
        axis_one.SetArray();
        axis_one.PushBack(0.0, alloc);
        axis_one.PushBack(50.0, alloc);
        plot_axis.AddMember("z", axis_one, alloc);
        doc.AddMember("axis", plot_axis, alloc);

        rapidjson::Value plot_label(rapidjson::kObjectType);
        plot_label.AddMember("x", rapidjson::Value().SetString("x-axis"), alloc);
        plot_label.AddMember("y", rapidjson::Value().SetString("y-axis"), alloc);
        plot_label.AddMember("z", rapidjson::Value().SetString("z-axis"), alloc);
        doc.AddMember("label", plot_label, alloc);

        rapidjson::Value plot_funcs(rapidjson::kArrayType);
        plot_funcs.PushBack(rapidjson::Value(rapidjson::kObjectType), alloc);
        auto& lorenz_plot = plot_funcs[0];
        lorenz_plot.AddMember("init", rapidjson::Value(rapidjson::kArrayType), alloc);
        rapidjson::Value init_point(rapidjson::kArrayType);
        init_point.PushBack(0.0, alloc);
        init_point.PushBack(1.0, alloc);
        init_point.PushBack(1.05, alloc);
        lorenz_plot["init"].PushBack(init_point, alloc);
        lorenz_plot.AddMember("line", 0, alloc);
        lorenz_plot.AddMember("point", 0, alloc);
        lorenz_plot.AddMember("col", 0, alloc);
        lorenz_plot.AddMember("desc", "lorenz", alloc);
        doc.AddMember("funcs", plot_funcs, alloc);

        doc.Accept(writer);
        int conn = m_client.connect("127.0.0.1", 19915);
        m_client.POST(conn, "/plot/create", &m_ext_headers, buffer.GetString(), buffer.GetLength());
        co_sleep(1);

        std::vector<double> last_point, curr_point(3, 0.0);
        last_point = {0.0, 1.0, 1.05};
        for (int i = 0; i < 10000; ++i) {
            auto axis_dot = lorenz(last_point);
            curr_point[0] = last_point[0]+axis_dot[0]*0.01;
            curr_point[1] = last_point[1]+axis_dot[1]*0.01;
            curr_point[2] = last_point[2]+axis_dot[2]*0.01;

            doc.RemoveAllMembers();
            doc.AddMember("funcs", rapidjson::Value(rapidjson::kArrayType), alloc);
            doc["funcs"].PushBack(rapidjson::Value(rapidjson::kArrayType), alloc);
            rapidjson::Value point(rapidjson::kArrayType);
            point.PushBack(curr_point[0], alloc);
            point.PushBack(curr_point[1], alloc);
            point.PushBack(curr_point[2], alloc);
            doc["funcs"][0].PushBack(point, alloc);

            buffer.Clear();
            writer.Reset(buffer);
            doc.Accept(writer);

            m_client.POST(conn, "/plot/append", &m_ext_headers, buffer.GetString(), buffer.GetLength());
            last_point = curr_point;
            co_sleep(1);
        }

        co_sleep(2);
        m_client.GET(conn, "/plot/destroy", &m_ext_headers);
        m_client.release(conn);
    });
    co_yield(co_id);
}

int main(int argc, char *argv[])
{
    http_plot plot;
    plot.add_cmd("2dp", [&](std::vector<std::string>& args) {
        plot.plot_2d_func();
    }, "动态绘制一个2维的图形");
    plot.add_cmd("3dp", [&](std::vector<std::string>& args) {
        plot.plot_3d_func();
    }, "动态绘制一个3维的图形");
    return plot.run(argc, argv);
}