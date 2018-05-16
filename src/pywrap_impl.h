#pragma once

namespace crx
{
    struct py_plot_malloc
    {
        PyObject *py_args;		//*arg(可变参数)
        std::vector<PyObject*> py_lists;
        PyObject *py_kw;			//**kw(关键字参数)
        PyObject *ret_val;			//返回值

        py_plot_malloc()
                :py_args(nullptr)
                ,py_lists(16, nullptr)
                ,py_kw(nullptr)
                ,ret_val(nullptr) {}

        virtual ~py_plot_malloc()
        {
            Py_XDECREF(py_args);
            for(auto elem : py_lists)
                Py_XDECREF(elem);
            Py_XDECREF(py_kw);
            Py_XDECREF(ret_val);
        }
    };

    struct py_plot_gca
    {
        DIM_TYPE type;
        PyObject *gca_obj;      //current axis instance
        std::unordered_map<std::string, PyObject*> axis_attrs;

        py_plot_gca() : gca_obj(nullptr) {}
        virtual ~py_plot_gca()
        {
            for (auto& pair : axis_attrs)
                Py_XDECREF(pair.second);
            Py_XDECREF(gca_obj);
        }
    };

    class py_plot_impl : public impl
    {
    public:
        py_plot_impl() : m_curr_fig(-1)
        {
            m_line_styles = {"-", "--", "-.", ":"};
            m_point_markers = {".", ",", "o", "v", "^", "<", ">", "s", "p", "*", "+", "x", "D"};
            m_colors = {"b", "g", "r", "c", "m", "y", "k", "w"};
        }

        virtual ~py_plot_impl()
        {
            for (auto& func : m_persis_funcs)			//释放已取得的函数对象
                Py_XDECREF(func.second);
        }

        void call_figure(int fig_num);

        void call_gca();

        void set_label(const std::string& key, const char *label);

        std::unordered_map<std::string, PyObject*> m_persis_funcs;      //持久化函数，key为函数名，value为函数对象
        std::unordered_map<std::string, py_plot_malloc> m_func_objs;    //py_plot对象的一系列方法中需要用到的PyObject*对象，比如函数参数、返回值等

        int m_curr_fig;
        std::unordered_map<int, py_plot_gca> m_fig_gca;

        std::vector<std::string> m_line_styles;			//对应于枚举类型`LINE_STYLE`，下同
        std::vector<std::string> m_point_markers;
        std::vector<std::string> m_colors;
    };

    class py_env_impl : public impl
    {
    public:
        std::unordered_map<std::string, PyObject*> m_persis_modules;    //持久化模块，key为模块名，value为模块对象
        std::unordered_set<PyObject*> m_results_set;                    //run_py_func函数调用存储的结果集
    };
}
