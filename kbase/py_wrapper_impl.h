#pragma once

namespace crx
{
    class py_object_impl
    {
    public:
        py_object_impl()
                :env(nullptr)
                ,obj(nullptr) {}

        py_env *env;		//python环境变量
        std::unordered_map<std::string, PyObject*> m_persis_funcs;		//持久化函数，key为函数名，value为函数对象
        void *obj;			//指向具体的python对象(扩展接口，目前只支持py_plot对象)
    };

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
    };

    class py_plot_impl
    {
    public:
        py_plot_impl(py_object_impl *obj_impl)
                :m_obj_impl(obj_impl)
                ,m_gca_obj(nullptr)
        {
            m_line_styles = {"-", "--", "-.", ":"};
            m_point_markers = {".", ",", "o", "v", "^", "<", ">", "s", "p", "*", "+", "x", "D"};
            m_colors = {"b", "g", "r", "c", "m", "y", "k", "w"};
        }
        virtual ~py_plot_impl()
        {
            for (auto& pair : m_persis_funcs)
                Py_XDECREF(pair.second);
            Py_XDECREF(m_gca_obj);

            for (auto& pair : m_func_objs) {
                Py_XDECREF(pair.second.py_args);
                for(auto& list : pair.second.py_lists)
                    Py_XDECREF(list);
                Py_XDECREF(pair.second.py_kw);
                Py_XDECREF(pair.second.ret_val);
            }
        }

        void get_gca();

        py_object_impl *m_obj_impl;
        PyObject *m_gca_obj;    //current axis instance
        std::unordered_map<std::string, PyObject*> m_persis_funcs;
        std::unordered_map<std::string, py_plot_malloc> m_func_objs;		//py_plot对象的一系列方法中需要用到的PyObject*对象，比如函数参数、返回值等

        std::vector<std::string> m_line_styles;			//对应于枚举类型`LINE_STYLE`，下同
        std::vector<std::string> m_point_markers;
        std::vector<std::string> m_colors;
    };

    class py_env_impl
    {
    public:
        py_env_impl() = default;
        virtual ~py_env_impl() = default;

        std::unordered_map<std::string, PyObject*> m_persis_modules;		//持久化模块，key为模块名，value为模块对象
        std::unordered_set<PyObject*> m_results_set;		//run_py_func函数调用存储的结果集
        std::unordered_set<py_object*> m_objects;			//从python环境中获取的具体的python对象集
    };
}
