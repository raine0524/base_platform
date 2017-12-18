#include "crx_pch.h"

class py_plot_test : public crx::console
{
public:
	virtual bool init(int argc, char *argv[]);
	virtual void destroy() {}

private:
	crx::py_env m_py_wrapper;
};

bool py_plot_test::init(int argc, char *argv[])
{
	//m_py_wrapper.run_py_func("pltPowerFuns", "MyPlotTest", 0);

	crx::py_plot *plot = m_py_wrapper.get_mat_plot();
	plot->set_title("A simple example");
	plot->set_text(2.2, 7.5, "$e^x$");
	plot->set_text(3.2, 7.5, "$2^x$");
	plot->set_axis(4, -4.0, 4.0, -0.5, 50.0);
	//plot->set_xlim(2, -4.0, 4.0);
	//plot->set_ylim(2, -0.5, 50.0);

	crx::plot_point_2d point;
	crx::plot_function_2d pf1, pf2;		//pf1: e^x, pf2: 2^x
	for (double x = -4; x <= 4; x += 8.0/50) {
		point.x = x;
		point.y = std::exp(x);
		pf1.point_vec.push_back(point);
		point.y = std::pow(2, x);
		pf2.point_vec.push_back(point);
	}
	pf1.ls = crx::SOLID; pf2.ls = crx::DASH_DOT;
	pf1.pm = crx::CIRCLE; pf2.pm = crx::TRIANGLE_UP;
	pf1.col = crx::RED; pf2.col = crx::BLUE;
	plot->plot(2, &pf1, &pf2);
	//必须在plot函数之后调用set_legend，否则该调用无效，legend中的参数与plot中的函数逐个对应
	plot->set_legend(2, "$e^x$", "$2^x$");
	plot->pause();

	//plot->save_figure("a/b/test");
	plot->release();
	return true;
}

int main(int argc, char *argv[])
{
	py_plot_test plot;
	plot.run(argc, argv);
}
