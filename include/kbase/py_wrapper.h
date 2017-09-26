#pragma once

namespace crx
{
    class CRX_SHARE py_object : public kobj
    {
    public:
        py_object();
        virtual ~py_object();

        virtual void release();
    };

    enum LINE_STYLE     //线的类型
    {
        SOLID = 0,			//实线
        DASHED,				//虚线
        DASH_DOT,			//虚-点线
        DOTTED,				//点线
    };

    enum POINT_MARKER       //点的记号
    {
        POINT = 0,					//实心点
        PIXEL,							//像素
        CIRCLE,							//空心点
        TRIANGLE_DOWN,		//下/上/左/右三角
        TRIANGLE_UP,
        TRIANGLE_LEFT,
        TRIANGLE_RIGHT,
        SQUARE,						//正方形
        PENTAGON,					//五角星
        STAR,							//星形
        PLUS,							//'+'
        X,									//'x'
        DIAMOND,					//菱形
    };

    enum COLOR      //点和线的颜色
    {
        BLUE = 0,			//蓝
        GREEN,				//绿
        RED,					//红
        CYAN,				//青
        MAGENTA,		//紫
        YELLOW,			//黄
        BLACK,				//黑
        WHITE,				//白
    };

    struct plot_point_2d    //二维的点结构
    {
        double x;
        double y;

        plot_point_2d()
                :x(0)
                ,y(0) {}
    };

    struct plot_function_2d     //二维函数绘图对象
    {
        std::deque<plot_point_2d> point_vec;		//二维点集向量
        LINE_STYLE ls;				//线的类型
        POINT_MARKER pm;		//点的记号
        COLOR col;					//点和线的颜色

        plot_function_2d()
                :ls(SOLID)
                ,pm(POINT)
                ,col(BLUE) {}
    };

    class CRX_SHARE py_plot : public py_object
    {
    public:
        py_plot();
        virtual ~py_plot();

        void set_title(const char *title, size_t font_size = 16);		//plot标题

        void set_text(double x_coor, double y_coor, const char *text, size_t font_size = 16);		//plot文本

        void set_legend(int argc, ...);		//plot说明(可变参数都是const char*类型，必须在plot函数之后调用)

        void set_xlim(int argc, ...);       //plot X坐标轴(可变参数都是double类型)

        void set_ylim(int argc, ...);       //plot Y坐标轴(可变参数都是double类型)

        void set_axis(int argc, ...);       //plot坐标轴(可变参数都是double类型，例如: 必须传入3.0而不是传入3)

        void plot(int argc, ...);           //plot绘图(可变参数都是plot_function_2d*类型)

        void clf();                         //清理当前绘图

        void pause(int millisec = 100);     //更新当前绘制窗口(time为绘图所需时间,100ms已够,且不用绘制过于频繁)

        void close();                       //关闭当前绘制窗口

        void save_figure(const char *png_prefix, int dpi = 75);		//plot保存图片(图片名称将自动加上.png后缀)，支持多级目录保存方式
    };

    class CRX_SHARE py_env : public kobj
    {
    public:
        py_env();
        virtual ~py_env();

        //将path加入python解释器的搜索路径
        void add_search_path(const char *path);

        //加载指定模块'module'
        bool load_module(const char *module);

        //卸载指定模块'module'
        void unload_module(const char *module);

        //查找指定模块'module'
        bool find_module(const char *module);

        /**
         * 运行python模块中的函数(支持关键字参数，例如"block=False"形式的参数)：
         * @module：模块名
         * @func：函数名
         * @argc：参数个数
         * @可变参数都是const char*类型！
         * NOTE：传入python函数的参数都是str类型，所以对于指定的脚本要手动包一层都是str参数的函数
         */
        PyObject* run_py_func(const char *module, const char *func, int argc, ...);

        //释放由run_py_func返回的PyObject对象
        void release_retobj(PyObject *ret);

        //获取py_plot绘图对象(使用完毕之后调用release手动释放资源)
        py_plot* get_mat_plot();
    };
}
