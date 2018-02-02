#include "stdafx.h"

namespace crx
{
    static std::atomic<int> s_wrap_instances(0);
    const char *mat_module_name = "matplotlib.pyplot";
    const char *numpy_module_name = "numpy";
    const char *plot3d_module_name = "mpl_toolkits.mplot3d";

    py_env::py_env()
    {
        //函数库的初始化和销毁在环境中只需执行一次
        if (!s_wrap_instances) {
            //Initialize the Python interpreter.
            Py_Initialize();
            PyRun_SimpleString("import sys");
            PyRun_SimpleString("import warnings");

            //The followed statement append current directory into python's interpreter search path
            PyRun_SimpleString("sys.path.append('.')");
            PyRun_SimpleString("warnings.filterwarnings('ignore')");
        }
        s_wrap_instances++;
        m_obj = new py_env_impl;
    }

    py_env::~py_env()
    {
        py_env_impl *impl = static_cast<py_env_impl*>(m_obj);
        for (auto& obj : impl->m_objects)		//释放获取的一系列py_object对象
            delete obj;
        impl->m_objects.clear();

        for (auto& ret : impl->m_results_set)	//释放run_py_func函数的返回值
            Py_XDECREF(ret);
        impl->m_results_set.clear();

        //unload modules
        for (auto& pair : impl->m_persis_modules)
            Py_XDECREF(pair.second);
        impl->m_persis_modules.clear();
        delete impl;

        s_wrap_instances--;
        if (!s_wrap_instances)
            Py_Finalize();
    }

    void py_env::add_search_path(const char *path)
    {
        char py_sen[256] = {0};
        if (access(path, F_OK)) {
            printf("[py_env::add_search_path] 待添加的搜索路径 %s 不存在，添加失败\n", path);
            return;
        }
        sprintf(py_sen, "sys.path.append('%s')", path);
        PyRun_SimpleString(py_sen);
    }

    bool py_env::load_module(const char *module)
    {
        py_env_impl *impl = static_cast<py_env_impl*>(m_obj);
        if (impl->m_persis_modules.end() != impl->m_persis_modules.find(module)) {		//判断当前模块是否已在持久化模块集合中
            printf("[load_modules] 模块 %s 之前已被加载，不再加载！\n", module);
            return true;
        }

        PyObject *module_name = PyUnicode_DecodeFSDefault(module);		//获取模块名
        PyObject *py_module = PyImport_Import(module_name);		//导入指定模块
        bool import_succ = true;
        if (py_module) {
            impl->m_persis_modules[module] = py_module;		//加入持久化模块集合中
        } else {
            PyErr_Print();
            import_succ = false;
        }
        Py_XDECREF(module_name);
        return import_succ;
    }

    void py_env::unload_module(const char *module)
    {
        py_env_impl *impl = static_cast<py_env_impl*>(m_obj);
        if (impl->m_persis_modules.end() == impl->m_persis_modules.find(module)) {			//同上
            printf("[unload_modules] 模块 %s 之前还未加载，不再卸载！\n", module);
            return;
        }

        //从持久化模块集合中将其删除
        PyObject *py_module = impl->m_persis_modules[module];
        Py_XDECREF(py_module);
        impl->m_persis_modules.erase(module);
    }

    bool py_env::find_module(const char *module)
    {
        py_env_impl *impl = static_cast<py_env_impl*>(m_obj);		//查找指定模块是否在持久化模块集合中
        if (impl->m_persis_modules.end() != impl->m_persis_modules.find(module))
            return true;
        else
            return false;
    }

    PyObject* py_env::run_py_func(const char *module, const char *func, int argc, ...)
    {
        py_env_impl *impl = static_cast<py_env_impl*>(m_obj);
        if (impl->m_persis_modules.end() == impl->m_persis_modules.find(module) &&
            !load_module(module))
            return nullptr;
        PyObject *py_module = impl->m_persis_modules[module];		//获取相应的模块

        PyObject *py_func = PyObject_GetAttrString(py_module, func);		//从模块中获取指定对象
        if (!py_func || !PyCallable_Check(py_func)) {		//判断找到的对象是否为可调用的函数对象
            printf("[run_py_func] 模块 '%s' 中不存在函数名为 '%s'的对象，不再继续执行该函数！\n", module, func);
            return nullptr;
        }

        va_list vl;
        va_start(vl, argc);
        int tuple_cnt = 0;				//元组中元素的个数
        bool exist_dict = false;		//指明是否存在关键字参数
        std::unordered_map<std::string, int> arg_eqs;		//key是参数，value指明是否为关键字参数，-1表示非关键字参数，反之则为关键字参数
        for (int i = 0; i < argc; ++i) {
            std::string val = va_arg(vl, const char*);
            int pos = find_nth_pos(val, "=", 0);
            if (-1 == pos)		//find, this is not a keyword argument
                tuple_cnt++;
            else		//kw arg
                exist_dict = true;
            arg_eqs[val] = pos;
        }
        va_end(vl);

        PyObject *py_args = nullptr, *py_dict = nullptr;
        if (exist_dict) {		//将所有的关键字参数放在py_dict对象中，并且py_dict作为tuple中的一个元素存在
            tuple_cnt++;
            py_dict = PyDict_New();
        }
        py_args = PyTuple_New(tuple_cnt);

        tuple_cnt = 0;
        for (auto& pair : arg_eqs) {
            if (-1 == pair.second) {		//非关键字参数直接放在元组py_args中
                PyTuple_SetItem(py_args, tuple_cnt++, PyUnicode_FromString(pair.first.c_str()));
            } else {
                auto& val = pair.first;
                auto& pos = pair.second;		//关键字参数首先放在字典py_dict中
                PyDict_SetItemString(py_dict, val.substr(0, pos).c_str(),
                                     PyUnicode_FromString(val.substr(pos+1).c_str()));
            }
        }
        if (exist_dict) {		//随后再将字典py_dict存入元组py_args中
            PyTuple_SetItem(py_args, tuple_cnt, py_dict);
            Py_XDECREF(py_dict);
        }

        //调用函数对象py_func并将函数返回值存储在结果集中
        PyObject *ret_val = PyObject_CallObject(py_func, py_args);
        impl->m_results_set.insert(ret_val);
        Py_XDECREF(py_args);
        Py_XDECREF(py_func);
        return ret_val;
    }

    void py_env::release_retobj(PyObject *ret)
    {
        py_env_impl *impl = static_cast<py_env_impl*>(m_obj);
        if (impl->m_results_set.end() == impl->m_results_set.find(ret))
            return;

        //释放函数返回值并将其从结果集中移除
        Py_XDECREF(ret);
        impl->m_results_set.erase(ret);
    }

    py_plot* py_env::get_mat_plot()
    {
        //首先加载"matplotlib.pyplot"和"numpy"这两个模块
        bool matplot_b = load_module(mat_module_name);
        bool numpy_b = load_module(numpy_module_name);
        bool plot3d_b = load_module(plot3d_module_name);
        if (!matplot_b || !numpy_b || !plot3d_b)
            return nullptr;

        py_plot *plot = new py_plot;			//创建一个新的py_plot对象
        py_object_impl *obj_impl = static_cast<py_object_impl*>(plot->m_obj);
        obj_impl->env = this;

        py_env_impl *env_impl = static_cast<py_env_impl*>(m_obj);
        env_impl->m_objects.insert(plot);

        //从"matplotlib.pyplot"模块中获取绘图所需的一系列函数对象
        PyObject *mat_module = env_impl->m_persis_modules[mat_module_name];
        std::vector<std::string> func_vec = {"title", "text", "legend", "axis", "gca", "figure",
                                             "plot", "clf", "pause", "close", "savefig"};
        for (auto& func_name : func_vec) {
            PyObject *py_func = PyObject_GetAttrString(mat_module, func_name.c_str());
            if (!py_func || !PyCallable_Check(py_func)) {
                printf("[get_mat_plot] 模块 matplotlib.pyplot 中不存在 %s 对象，或该对象是不可调用的！\n", func_name.c_str());
                continue;
            }
            obj_impl->m_persis_funcs[func_name] = py_func;
        }
        return plot;
    }

    py_object::py_object()
    {
        m_obj = new py_object_impl;
    }

    py_object::~py_object()
    {
        py_object_impl *obj_impl = static_cast<py_object_impl*>(m_obj);
        for (auto& func : obj_impl->m_persis_funcs)			//释放已取得的函数对象
            Py_XDECREF(func.second);
        obj_impl->m_persis_funcs.clear();
        delete obj_impl;
    }

    void py_object::release()
    {
        py_object_impl *obj_impl = static_cast<py_object_impl*>(m_obj);
        py_env_impl *env_impl = static_cast<py_env_impl*>(obj_impl->env->m_obj);
        env_impl->m_objects.erase(this);			//销毁py_object对象并释放已申请的内存资源
        delete this;
    }

    py_plot::py_plot()
    {
        py_object_impl *obj_impl = static_cast<py_object_impl*>(m_obj);
        obj_impl->obj = new py_plot_impl(obj_impl);
    }

    py_plot::~py_plot()
    {
        py_object_impl *obj_impl = static_cast<py_object_impl*>(m_obj);
        delete (py_plot_impl*)obj_impl->obj;
    }

    void py_plot::set_title(const char *title, size_t font_size /*= 16*/)
    {
        static const std::string this_key = "title";
        py_object_impl *obj_impl = static_cast<py_object_impl*>(m_obj);

        PyObject *py_func = obj_impl->m_persis_funcs[this_key];		//获取"title"函数对象
        py_plot_impl *plot_impl = static_cast<py_plot_impl*>(obj_impl->obj);
        auto& title_malloc = plot_impl->m_func_objs[this_key];

        Py_XDECREF(title_malloc.py_args);
        title_malloc.py_args = PyTuple_New(1);
        //"title"函数指定标题的是一个普通参数
        PyTuple_SetItem(title_malloc.py_args, 0, PyUnicode_FromString(title));

        Py_XDECREF(title_malloc.py_kw);
        title_malloc.py_kw = PyDict_New();

        //用于指定标题字体大小的是一个关键字参数
        PyDict_SetItemString(title_malloc.py_kw, "fontsize", PyLong_FromLong(font_size));

        //保存"title"函数的返回值
        Py_XDECREF(title_malloc.ret_val);
        title_malloc.ret_val = PyObject_Call(py_func, title_malloc.py_args, title_malloc.py_kw);
    }

    void py_plot::set_text(double x_coor, double y_coor, const char *text, size_t font_size /*= 16*/)
    {
        static const std::string this_key = "text";
        py_object_impl *obj_impl = static_cast<py_object_impl*>(m_obj);

        PyObject *py_func = obj_impl->m_persis_funcs[this_key];		//获取"text"函数对象
        py_plot_impl *plot_impl = static_cast<py_plot_impl*>(obj_impl->obj);
        auto& text_malloc = plot_impl->m_func_objs[this_key];

        Py_XDECREF(text_malloc.py_args);
        text_malloc.py_args = PyTuple_New(3);

        //文本的坐标及文本的内容作为"text"函数的普通参数
        PyTuple_SetItem(text_malloc.py_args, 0, PyFloat_FromDouble(x_coor));
        PyTuple_SetItem(text_malloc.py_args, 1, PyFloat_FromDouble(y_coor));
        PyTuple_SetItem(text_malloc.py_args, 2, PyUnicode_FromString(text));

        Py_XDECREF(text_malloc.py_kw);
        text_malloc.py_kw = PyDict_New();

        //用于指定文本字体大小的是一个关键字参数
        PyDict_SetItemString(text_malloc.py_kw, "fontsize", PyLong_FromLong(font_size));

        //同上，保存"text"函数的返回值
        Py_XDECREF(text_malloc.ret_val);
        text_malloc.ret_val = PyObject_Call(py_func, text_malloc.py_args, text_malloc.py_kw);
    }

    void py_plot::set_legend(const std::vector<std::string>& legend_arr)
    {
        static const std::string this_key = "legend";
        py_object_impl *obj_impl = static_cast<py_object_impl*>(m_obj);

        PyObject *py_func = obj_impl->m_persis_funcs[this_key];		//获取"legend"函数对象
        py_plot_impl *plot_impl = static_cast<py_plot_impl*>(obj_impl->obj);
        auto& legend_malloc = plot_impl->m_func_objs[this_key];

        Py_XDECREF(legend_malloc.py_args);
        legend_malloc.py_args = PyTuple_New(1);

        Py_XDECREF(legend_malloc.py_lists[0]);
        legend_malloc.py_lists[0] = PyList_New(legend_arr.size());

        for (int i = 0; i < legend_arr.size(); ++i)     //将所有参数装入List列表对象中
            PyList_SetItem(legend_malloc.py_lists[0], i, PyUnicode_FromString(legend_arr[i].c_str()));
        //再将该List列表对象作为元组中的一个普通元素
        PyTuple_SetItem(legend_malloc.py_args, 0, legend_malloc.py_lists[0]);

        //同上，保存"legend"函数的返回值
        Py_XDECREF(legend_malloc.ret_val);
        legend_malloc.ret_val = PyObject_CallObject(py_func, legend_malloc.py_args);
    }

    void py_plot::set_xlim(const std::vector<double>& xlim_arr)
    {
        static const std::string this_key = "set_xlim";
        py_object_impl *obj_impl = static_cast<py_object_impl*>(m_obj);
        py_plot_impl *plot_impl = static_cast<py_plot_impl*>(obj_impl->obj);

        if (plot_impl->m_fig_gca.end() == plot_impl->m_fig_gca.find(plot_impl->m_curr_fig))
            return;

        auto& fig_gca = plot_impl->m_fig_gca[plot_impl->m_curr_fig];
        PyObject *py_func = fig_gca.axis_attrs[this_key];
        auto& xlim_malloc = plot_impl->m_func_objs[this_key];

        Py_XDECREF(xlim_malloc.py_args);
        xlim_malloc.py_args = PyTuple_New(1);

        Py_XDECREF(xlim_malloc.py_lists[0]);
        xlim_malloc.py_lists[0] = PyList_New(xlim_arr.size());

        for (int i = 0; i < xlim_arr.size(); ++i)       //将所有参数装入List列表对象中
            PyList_SetItem(xlim_malloc.py_lists[0], i, PyFloat_FromDouble(xlim_arr[i]));
        //再将该List列表对象作为元组中的一个普通元素
        PyTuple_SetItem(xlim_malloc.py_args, 0, xlim_malloc.py_lists[0]);

        //同上，保存"set_xlim"函数的返回值
        Py_XDECREF(xlim_malloc.ret_val);
        xlim_malloc.ret_val = PyObject_CallObject(py_func, xlim_malloc.py_args);
    }

    void py_plot::set_ylim(const std::vector<double>& ylim_arr)
    {
        static const std::string this_key = "set_ylim";
        py_object_impl *obj_impl = static_cast<py_object_impl*>(m_obj);
        py_plot_impl *plot_impl = static_cast<py_plot_impl*>(obj_impl->obj);

        if (plot_impl->m_fig_gca.end() == plot_impl->m_fig_gca.find(plot_impl->m_curr_fig))
            return;

        auto& fig_gca = plot_impl->m_fig_gca[plot_impl->m_curr_fig];
        PyObject *py_func = fig_gca.axis_attrs[this_key];
        auto& ylim_malloc = plot_impl->m_func_objs[this_key];

        Py_XDECREF(ylim_malloc.py_args);
        ylim_malloc.py_args = PyTuple_New(1);

        Py_XDECREF(ylim_malloc.py_lists[0]);
        ylim_malloc.py_lists[0] = PyList_New(ylim_arr.size());

        for (int i = 0; i < ylim_arr.size(); ++i)       //将所有参数装入List列表对象中
            PyList_SetItem(ylim_malloc.py_lists[0], i, PyFloat_FromDouble(ylim_arr[i]));
        //再将该List列表对象作为元组中的一个普通元素
        PyTuple_SetItem(ylim_malloc.py_args, 0, ylim_malloc.py_lists[0]);

        //同上，保存"set_ylim"函数的返回值
        Py_XDECREF(ylim_malloc.ret_val);
        ylim_malloc.ret_val = PyObject_CallObject(py_func, ylim_malloc.py_args);
    }

    void py_plot::set_xlabel(const char *label)
    {
        static const std::string this_key = "set_xlabel";
        py_object_impl *obj_impl = static_cast<py_object_impl*>(m_obj);
        py_plot_impl *plot_impl = static_cast<py_plot_impl*>(obj_impl->obj);
        plot_impl->set_label(this_key, label);
    }

    void py_plot::set_ylabel(const char *label)
    {
        static const std::string this_key = "set_ylabel";
        py_object_impl *obj_impl = static_cast<py_object_impl*>(m_obj);
        py_plot_impl *plot_impl = static_cast<py_plot_impl*>(obj_impl->obj);
        plot_impl->set_label(this_key, label);
    }

    void py_plot::set_zlabel(const char *label)
    {
        static const std::string this_key = "set_zlabel";
        py_object_impl *obj_impl = static_cast<py_object_impl*>(m_obj);
        py_plot_impl *plot_impl = static_cast<py_plot_impl*>(obj_impl->obj);
        plot_impl->set_label(this_key, label);
    }

    void py_plot_impl::set_label(const std::string& key, const char *label)
    {
        if (m_fig_gca.end() == m_fig_gca.find(m_curr_fig))
            return;

        auto& fig_gca = m_fig_gca[m_curr_fig];
        if (fig_gca.axis_attrs.end() == fig_gca.axis_attrs.find(key))
            return;

        PyObject *py_func = fig_gca.axis_attrs[key];
        auto& label_malloc = m_func_objs[key];

        Py_XDECREF(label_malloc.py_args);
        label_malloc.py_args = PyTuple_New(1);

        //"set_*label"函数指定*轴标签的是一个普通参数
        PyTuple_SetItem(label_malloc.py_args, 0, PyUnicode_FromString(label));

        //同上，保存"set_*label"函数的返回值
        Py_XDECREF(label_malloc.ret_val);
        label_malloc.ret_val = PyObject_CallObject(py_func, label_malloc.py_args);
    }

    void py_plot::set_axis(const std::vector<double>& axis_arr)
    {
        static const std::string this_key = "axis";
        py_object_impl *obj_impl = static_cast<py_object_impl*>(m_obj);

        PyObject *py_func = obj_impl->m_persis_funcs[this_key];		//获取"axis"函数对象
        py_plot_impl *plot_impl = static_cast<py_plot_impl*>(obj_impl->obj);
        auto& axis_malloc = plot_impl->m_func_objs[this_key];

        Py_XDECREF(axis_malloc.py_args);
        axis_malloc.py_args = PyTuple_New(1);

        Py_XDECREF(axis_malloc.py_lists[0]);
        axis_malloc.py_lists[0] = PyList_New(axis_arr.size());

        for (int i = 0; i < axis_arr.size(); ++i)       //将所有参数装入List列表对象中
            PyList_SetItem(axis_malloc.py_lists[0], i, PyFloat_FromDouble(axis_arr[i]));
        //再将该List列表对象作为元组中的一个普通元素
        PyTuple_SetItem(axis_malloc.py_args, 0, axis_malloc.py_lists[0]);

        //同上，保存"axis"函数的返回值
        Py_XDECREF(axis_malloc.ret_val);
        axis_malloc.ret_val = PyObject_CallObject(py_func, axis_malloc.py_args);
    }

    void py_plot::create_figure(int fig_num, DIM_TYPE type)
    {
        py_object_impl *obj_impl = static_cast<py_object_impl*>(m_obj);
        py_plot_impl *plot_impl = static_cast<py_plot_impl*>(obj_impl->obj);
        if (plot_impl->m_fig_gca.end() != plot_impl->m_fig_gca.find(fig_num))       //绘图窗口已存在
            return;

        plot_impl->call_figure(fig_num);
        plot_impl->m_fig_gca[fig_num].type = type;
        plot_impl->call_gca();
    }

    void py_plot::switch_figure(int which)
    {

        py_object_impl *obj_impl = static_cast<py_object_impl*>(m_obj);
        py_plot_impl *plot_impl = static_cast<py_plot_impl*>(obj_impl->obj);
        if (plot_impl->m_fig_gca.end() == plot_impl->m_fig_gca.find(which))     //绘图窗口不存在
            return;
        else
            plot_impl->call_figure(which);
    }

    void py_plot_impl::call_figure(int fig_num)
    {
        static const std::string this_key = "figure";
        PyObject *py_func = m_obj_impl->m_persis_funcs[this_key];     //获取"figure"函数对象
        auto& fig_malloc = m_func_objs[this_key];

        //args
        Py_XDECREF(fig_malloc.py_args);
        fig_malloc.py_args = PyTuple_New(1);

        //which作为figure函数的参数
        PyTuple_SetItem(fig_malloc.py_args, 0, PyLong_FromLong(fig_num));

        //同上，保存"figure"函数的返回值
        Py_XDECREF(fig_malloc.ret_val);
        fig_malloc.ret_val = PyObject_CallObject(py_func, fig_malloc.py_args);
        m_curr_fig = fig_num;
    }

    void py_plot_impl::call_gca()
    {
        auto& fig_gca = m_fig_gca[m_curr_fig];
        Py_XDECREF(fig_gca.gca_obj);
        for (auto& pair : fig_gca.axis_attrs)
            Py_XDECREF(pair.second);

        static const std::string this_key = "gca";
        PyObject *py_func = m_obj_impl->m_persis_funcs[this_key];   //获取"gca"函数对象

        //从"matplotlib.pyplot"模块中获取当前坐标实例
        if (DIM_2 == fig_gca.type) {
            fig_gca.gca_obj = PyObject_CallObject(py_func, nullptr);
        } else {
            auto& gca_malloc = m_func_objs[this_key];
            Py_XDECREF(gca_malloc.py_kw);
            gca_malloc.py_kw = PyDict_New();
            PyDict_SetItemString(gca_malloc.py_kw, "projection", PyUnicode_FromString("3d"));
            fig_gca.gca_obj = PyEval_CallObjectWithKeywords(py_func, nullptr, gca_malloc.py_kw);
        }

        std::vector<std::string> func_vec = {"set_xlim", "set_ylim", "set_xlabel", "set_ylabel"};
        if (DIM_3 == fig_gca.type)
            func_vec.push_back("set_zlabel");
        for (auto& func_name : func_vec) {
            PyObject *py_func = PyObject_GetAttrString(fig_gca.gca_obj, func_name.c_str());
            if (!py_func || !PyCallable_Check(py_func)) {
                printf("[call_gca] 获取对象 pyplot.gca 属性 %s 失败，或该对象不可调用！\n", func_name.c_str());
                continue;
            }
            fig_gca.axis_attrs[func_name] = py_func;
        }
    }

    void py_plot::plot(const std::vector<plot_function>& funcs, double linewidth /*= 1.0*/)
    {
        static const std::string this_key = "plot";
        py_object_impl *obj_impl = static_cast<py_object_impl*>(m_obj);
        py_plot_impl *plot_impl = static_cast<py_plot_impl*>(obj_impl->obj);
        if (plot_impl->m_fig_gca.end() == plot_impl->m_fig_gca.find(plot_impl->m_curr_fig))
            return;

        auto& fig_gca = plot_impl->m_fig_gca[plot_impl->m_curr_fig];
        PyObject *py_func = obj_impl->m_persis_funcs[this_key];		//获取"plot"函数对象
        auto& plot_malloc = plot_impl->m_func_objs[this_key];

        //args
        Py_XDECREF(plot_malloc.py_args);
        if (DIM_2 == fig_gca.type)      //2维
            plot_malloc.py_args = PyTuple_New(funcs.size()*3);
        else        //3维
            plot_malloc.py_args = PyTuple_New(funcs.size()*4);

        for (auto& list : plot_malloc.py_lists)
            Py_XDECREF(list);
        plot_malloc.py_lists.clear();
        if (DIM_2 == fig_gca.type)      //2维
            plot_malloc.py_lists.resize(funcs.size()*2);
        else        //3维
            plot_malloc.py_lists.resize(funcs.size()*3);

        for (int i = 0; i < funcs.size(); ++i) {
            const plot_function *pf = &funcs[i];
            if (DIM_2 == fig_gca.type) {        //2维
                plot_malloc.py_lists[i*2+0] = PyList_New(pf->point_vec.size());
                plot_malloc.py_lists[i*2+1] = PyList_New(pf->point_vec.size());
                //将二维函数对象中的点集按序分别装入两个不同的列表中，这两个列表分别对应函数对象的X及Y分量
                for (int j = 0; j < pf->point_vec.size(); ++j) {
                    PyList_SetItem(plot_malloc.py_lists[i*2+0], j, PyFloat_FromDouble(pf->point_vec[j].x));
                    PyList_SetItem(plot_malloc.py_lists[i*2+1], j, PyFloat_FromDouble(pf->point_vec[j].y));
                }
                //将这两个列表装入元组
                PyTuple_SetItem(plot_malloc.py_args, i*3+0, plot_malloc.py_lists[i*2+0]);
                PyTuple_SetItem(plot_malloc.py_args, i*3+1, plot_malloc.py_lists[i*2+1]);
            } else {        //3维
                plot_malloc.py_lists[i*3+0] = PyList_New(pf->point_vec.size());
                plot_malloc.py_lists[i*3+1] = PyList_New(pf->point_vec.size());
                plot_malloc.py_lists[i*3+2] = PyList_New(pf->point_vec.size());
                //讲三维函数对象中的点集按序分别装入三个不同的列表中，这三个列表分别对应函数对象的X/Y/Z分量
                for (int j = 0; j < pf->point_vec.size(); ++j) {
                    PyList_SetItem(plot_malloc.py_lists[i*3+0], j, PyFloat_FromDouble(pf->point_vec[j].x));
                    PyList_SetItem(plot_malloc.py_lists[i*3+1], j, PyFloat_FromDouble(pf->point_vec[j].y));
                    PyList_SetItem(plot_malloc.py_lists[i*3+2], j, PyFloat_FromDouble(pf->point_vec[j].z));
                }
                //将这三个列表装入元组
                PyTuple_SetItem(plot_malloc.py_args, i*4+0, plot_malloc.py_lists[i*3+0]);
                PyTuple_SetItem(plot_malloc.py_args, i*4+1, plot_malloc.py_lists[i*3+1]);
                PyTuple_SetItem(plot_malloc.py_args, i*4+2, plot_malloc.py_lists[i*3+2]);
            }

            //构造绘图函数的样式字符串
            std::string style = plot_impl->m_line_styles[pf->ls];
            if (MARKER_NONE != pf->pm)
                style.append(plot_impl->m_point_markers[pf->pm-1]);
            if (COL_DEFAULT != pf->col)
                style.append(plot_impl->m_colors[pf->col-1]);

            //将该样式字符串放在元组中的X、Y(Z)向量之后
            if (!style.empty()) {
                if (DIM_2 == fig_gca.type)
                    PyTuple_SetItem(plot_malloc.py_args, i*3+2, PyUnicode_FromString(style.c_str()));
                else
                    PyTuple_SetItem(plot_malloc.py_args, i*4+3, PyUnicode_FromString(style.c_str()));
            }
        }

        //kw
        Py_XDECREF(plot_malloc.py_kw);
        plot_malloc.py_kw = PyDict_New();

        //将函数对象的线的宽度设置为2
        PyDict_SetItemString(plot_malloc.py_kw, "linewidth", PyFloat_FromDouble(linewidth));

        Py_XDECREF(plot_malloc.ret_val);
        //同上，保存"plot"函数的返回值
        plot_malloc.ret_val = PyObject_Call(py_func, plot_malloc.py_args, plot_malloc.py_kw);
    }

    void py_plot::clf()
    {
        static const std::string this_key = "clf";
        py_object_impl *obj_impl = static_cast<py_object_impl*>(m_obj);
        py_plot_impl *plot_impl = static_cast<py_plot_impl*>(obj_impl->obj);
        if (plot_impl->m_fig_gca.end() == plot_impl->m_fig_gca.find(plot_impl->m_curr_fig))
            return;

        PyObject *py_func = obj_impl->m_persis_funcs[this_key];     //获取"clf"函数对象
        auto& clf_malloc = plot_impl->m_func_objs[this_key];

        Py_XDECREF(clf_malloc.ret_val);
        clf_malloc.ret_val = PyObject_CallObject(py_func, nullptr);
        plot_impl->call_gca();
    }

    void py_plot::pause(int millisec /*= 100*/)
    {
        static const std::string this_key = "pause";
        py_object_impl *obj_impl = static_cast<py_object_impl*>(m_obj);

        PyObject *py_func = obj_impl->m_persis_funcs[this_key];     //获取"pause"函数对象
        py_plot_impl *plot_impl = static_cast<py_plot_impl*>(obj_impl->obj);
        auto& pause_malloc = plot_impl->m_func_objs[this_key];

        Py_XDECREF(pause_malloc.py_args);
        pause_malloc.py_args = PyTuple_New(1);
        PyTuple_SetItem(pause_malloc.py_args, 0, PyFloat_FromDouble(millisec/1000.0));

        Py_XDECREF(pause_malloc.ret_val);
        pause_malloc.ret_val = PyObject_CallObject(py_func, pause_malloc.py_args);
    }

    void py_plot::close(int which)
    {
        py_object_impl *obj_impl = static_cast<py_object_impl*>(m_obj);
        py_plot_impl *plot_impl = static_cast<py_plot_impl*>(obj_impl->obj);

        static const std::string this_key = "close";
        auto& close_malloc = plot_impl->m_func_objs[this_key];
        PyObject *py_func = obj_impl->m_persis_funcs[this_key];     //获取"close"函数对象

        Py_XDECREF(close_malloc.py_args);
        close_malloc.py_args = PyTuple_New(1);
        if (-1 == which)
            PyTuple_SetItem(close_malloc.py_args, 0, PyUnicode_FromString("all"));
        else
            PyTuple_SetItem(close_malloc.py_args, 0, PyLong_FromLong(which));

        Py_XDECREF(close_malloc.ret_val);
        close_malloc.ret_val = PyObject_CallObject(py_func, close_malloc.py_args);

        if (-1 == which) {
            plot_impl->m_fig_gca.clear();
            plot_impl->m_curr_fig = -1;
        } else {
            auto it = plot_impl->m_fig_gca.find(which);
            if (plot_impl->m_fig_gca.end() != it)
                plot_impl->m_fig_gca.erase(it);
        }
    }

    void py_plot::save_figure(const char *png_prefix, int dpi /*= 75*/)
    {
        static const std::string this_key = "savefig";
        py_object_impl *obj_impl = static_cast<py_object_impl*>(m_obj);

        std::string str_prefix = png_prefix;
        size_t pos = str_prefix.rfind("/");
        if (std::string::npos != pos)		//判断是否存在路径分隔符，存在则首先构造多级目录
            mkdir_multi(str_prefix.substr(0, pos).c_str());

        PyObject *py_func = obj_impl->m_persis_funcs[this_key];		//获取"savefig"函数对象
        py_plot_impl *plot_impl = static_cast<py_plot_impl*>(obj_impl->obj);
        auto& savefig_malloc = plot_impl->m_func_objs[this_key];

        Py_XDECREF(savefig_malloc.py_args);
        savefig_malloc.py_args = PyTuple_New(1);

        std::string figure_name = str_prefix+".png";		//构造完整的图片名称(加上.png后缀)
        PyTuple_SetItem(savefig_malloc.py_args, 0, PyUnicode_FromString(figure_name.c_str()));

        Py_XDECREF(savefig_malloc.py_kw);
        savefig_malloc.py_kw = PyDict_New();
        /**
         * dpi表示单位显示面积上的像素点的个数，dpi值越大图像越清晰，同时图像所占用的存储容量也越大
         * 这是savefig函数的一个关键字参数
         */
        PyDict_SetItemString(savefig_malloc.py_kw, "dpi", PyLong_FromLong(dpi));

        //同上，保存"savefig"函数的返回值
        Py_XDECREF(savefig_malloc.ret_val);
        savefig_malloc.ret_val = PyObject_Call(py_func, savefig_malloc.py_args, savefig_malloc.py_kw);
    }
}
