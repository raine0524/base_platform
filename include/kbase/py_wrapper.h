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
        STYLE_SOLID,         //实线
        STYLE_DASHED,        //虚线
        STYLE_DASH_DOT,      //虚-点线
        STYLE_DOTTED,        //点线
    };

    enum POINT_MARKER       //点的记号
    {
        MARKER_NONE = 0,     //使用系统默认
        MARKER_POINT,           //实心点
        MARKER_PIXEL,           //像素
        MARKER_CIRCLE,          //空心点
        MARKER_TRIANGLE_DOWN,   //下/上/左/右三角
        MARKER_TRIANGLE_UP,
        MARKER_TRIANGLE_LEFT,
        MARKER_TRIANGLE_RIGHT,
        MARKER_SQUARE,          //正方形
        MARKER_PENTAGON,        //五角星
        MARKER_STAR,            //星形
        MARKER_PLUS,            //'+'
        MARKER_X,               //'x'
        MARKER_DIAMOND,         //菱形
    };

    enum COLOR      //点和线的颜色
    {
        COL_DEFAULT = 0,    //使用系统默认
        COL_BLUE,           //蓝
        COL_GREEN,          //绿
        COL_RED,            //红
        COL_CYAN,           //青
        COL_MAGENTA,        //紫
        COL_YELLOW,         //黄
        COL_BLACK,          //黑
        COL_WHITE,          //白
    };

    enum DIM_TYPE
    {
        DIM_2,      //2维
        DIM_3,      //3维
    };

    struct plot_point    //三维的点结构(绘制二维函数时不使用z字段)
    {
        double x;
        double y;
        double z;
        plot_point() : x(0), y(0), z(0) {}
    };

    struct plot_function     //函数绘图对象(支持三维)
    {
        std::deque<plot_point> point_vec;		//点集向量(支持三维)
        LINE_STYLE ls;				//线的类型
        POINT_MARKER pm;		//点的记号
        COLOR col;					//点和线的颜色

        plot_function()
                :ls(STYLE_SOLID)
                ,pm(MARKER_NONE)
                ,col(COL_DEFAULT) {}
    };

    class CRX_SHARE py_plot : public py_object
    {
    public:
        py_plot();
        virtual ~py_plot();

        void set_title(const char *title, size_t font_size = 16);		//plot标题

        void set_text(double x_coor, double y_coor, const char *text, size_t font_size = 16);		//plot文本

        void set_legend(const std::vector<std::string>& legend_arr);    //plot说明(必须在plot函数之后调用)

        void set_xlim(const std::vector<double>& xlim_arr);             //plot X坐标轴

        void set_ylim(const std::vector<double>& ylim_arr);             //plot Y坐标轴

        void set_xlabel(const char *label);     //设置x轴标签

        void set_ylabel(const char *label);     //设置y轴标签

        void set_zlabel(const char *label);     //设置z轴标签

        void set_axis(const std::vector<double>& axis_arr);             //plot坐标轴

        void create_figure(int fig_num, DIM_TYPE type);     //创建一个绘图窗口并切换至该窗口，若窗口已存在则不做任何操作

        void switch_figure(int which);                      //切换到另外一个绘图窗口，若不存在则不做任何操作

        void plot(const std::vector<plot_function>& funcs, double linewidth = 1.0);     //plot绘图(可变参数都是plot_function_2d*类型)

        void clf();                         //清理当前绘图

        void pause(int millisec = 100);     //更新当前绘制窗口(time为绘图所需时间,100ms已够,且不用绘制过于频繁)

        void close(int which);              //关闭指定的绘制窗口，若which为-1则关闭所有窗口

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
