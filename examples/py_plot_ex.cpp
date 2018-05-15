#include "crx_pch.h"

class py_plot_test : public crx::console
{
public:
    virtual bool init(int argc, char *argv[]);
    virtual void destroy();

private:
    void plot_2d_func();

    void plot_3d_func();

    crx::plot_point lorenz(crx::plot_point& point, double s = 10, double r = 28, double b = 2.667);

private:
    crx::py_env m_py_wrapper;
    crx::py_plot *m_plot;
};

void py_plot_test::plot_2d_func()
{
    m_plot->create_figure(1, crx::DIM_2);
    m_plot->set_title("2-dimension function example");
    m_plot->set_text(2.2, 7.5, "$e^x$");
    m_plot->set_text(3.2, 7.5, "$2^x$");

    std::vector<double> axis_arr = {-4.0, 4.0, -0.5, 50.0};
    m_plot->set_axis(axis_arr);

//    std::vector<double> xlim_arr = {-4.0, 4.0}, ylim_arr = {-0.5, 50.0};
//    m_plot->set_xlim(xlim_arr);
//    m_plot->set_ylim(ylim_arr);

    crx::plot_point point;
    std::vector<crx::plot_function> funcs(2);
    crx::plot_function *pf1 = &funcs[0], *pf2 = &funcs[1];     //pf1: e^x, pf2: 2^x
    for (double x = -4; x <= 4; x += 8.0/50) {
        point.x = x;
        point.y = std::exp(x);
        pf1->point_vec.push_back(point);
        point.y = std::pow(2, x);
        pf2->point_vec.push_back(point);
    }
    pf1->ls = crx::STYLE_SOLID; pf2->ls = crx::STYLE_DASH_DOT;
    pf1->pm = crx::MARKER_CIRCLE; pf2->pm = crx::MARKER_TRIANGLE_UP;
    pf1->col = crx::COL_RED; pf2->col = crx::COL_BLUE;
    m_plot->plot(funcs, 2.0);

    //必须在plot函数之后调用set_legend，否则该调用无效，legend中的参数与plot中的函数逐个对应
    std::vector<std::string> legend_arr = {"$e^x$", "$2^x$"};
    m_plot->set_legend(legend_arr);
    m_plot->pause();
//    m_plot->save_figure("a/b/plot_2d_func");
}

crx::plot_point py_plot_test::lorenz(crx::plot_point& point, double s /*= 10*/, double r /*= 28*/, double b /*= 2.667*/)
{
    crx::plot_point axis_dot;
    axis_dot.x = s*(point.y-point.x);
    axis_dot.y = r*point.x-point.y-point.x*point.z;
    axis_dot.z = point.x*point.y-b*point.z;
    return axis_dot;
}

void py_plot_test::plot_3d_func()
{
    m_plot->create_figure(2, crx::DIM_3);
    m_plot->set_title("Lorenz Attractor");
    m_plot->set_xlabel("X Axis");
    m_plot->set_ylabel("Y Axis");
    m_plot->set_zlabel("Z Axis");

    std::vector<crx::plot_function> funcs(1);
    auto& pf = funcs[0];
    pf.point_vec.resize(10001);

    auto& point = pf.point_vec[0];
    point.x = 0.0; point.y = 1.0, point.z = 1.05;
    for (int i = 0; i < 10000; ++i) {
        auto& last_point = pf.point_vec[i], &curr_point = pf.point_vec[i+1];
        crx::plot_point axis_dot = lorenz(last_point);
        curr_point.x = last_point.x+axis_dot.x*0.01;
        curr_point.y = last_point.y+axis_dot.y*0.01;
        curr_point.z = last_point.z+axis_dot.z*0.01;
    }
    pf.ls = crx::STYLE_SOLID;
    pf.pm = crx::MARKER_NONE;
    pf.col = crx::COL_DEFAULT;
    m_plot->plot(funcs, 0.5);

    m_plot->pause();
//    m_plot->save_figure("a/b/plot_3d_func");
}

bool py_plot_test::init(int argc, char *argv[])
{
//	m_py_wrapper.run_py_func("pltPowerFuns", "MyPlotTest", 0);
	m_plot = m_py_wrapper.get_mat_plot();
	plot_2d_func();
	plot_3d_func();
	return true;
}

void py_plot_test::destroy()
{
	m_plot->release();
}

int main(int argc, char *argv[])
{
	py_plot_test plot;
	plot.run(argc, argv);
}
