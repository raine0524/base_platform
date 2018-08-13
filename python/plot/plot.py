import math
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
from multiprocessing import Process, Queue

import os
import sys
import time
import random
import logging

file_path = os.path.abspath(os.path.realpath(__file__))
py_dir = os.path.abspath(os.path.join(file_path, '..', '..'))
sys.path.append(py_dir)
from util import log_config

logger = logging.getLogger('plot')

class plot_mgr(Process):
    def __init__(self, queue):
        Process.__init__(self)
        self.queue = queue
        self.fig = None
        self.dim = 2
        self.point_num = 0

        self.title = None
        self.axis = dict()
        self.label = dict()
        self.text = list()
        self.legend = list()

        self.xs = self.ys = self.zs = None
        self.cmd_func = {1: self.create_figure, 2: self.clear_figure, 3: self.append_point}

        self.line_style = ['-', '--', '-.', ':']
        self.point_markers = ['.', ',', 'o', 'v', '^', '<', '>', 's', 'p', '*', '+', 'x', 'D']
        self.colors = ['b', 'g', 'r', 'c', 'm', 'y', 'k', 'w']
        self.style_array = list()

    def run(self):
        while True:
            item = self.queue.get()
            cmd = item[0]
            if 3 != cmd:
                logger.info('plot-%s subprocess get item: %s(%d)' % (self.name, item, self.queue.qsize()))

            if -1 != cmd:
                self.cmd_func[item[0]](item[1])
            else:
                break

    def _plot_figure(self, save):
        self.fig.clf()
        if 2 == self.dim:
            ax = self.fig.gca()
        else:
            ax = self.fig.gca(projection='3d')

        # set title
        if self.title:
            ax.set_title(self.title)

        # set axis
        if self.axis:
            if 'x' in self.axis:
                ax.set_xlim(self.axis['x'])
            if 'y' in self.axis:
                ax.set_ylim(self.axis['y'])
            if 'z' in self.axis:
                ax.set_zlim(self.axis['z'])

        # set label
        if 'x' in self.label:
            ax.set_xlabel(self.label['x'])
        if 'y' in self.label:
            ax.set_ylabel(self.label['y'])
        if 'z' in self.label:
            ax.set_zlabel(self.label['z'])

        # set text
        if self.text:
            for text in self.text:
                if 2 == self.dim:
                    ax.text(text[0], text[1], text[2])
                else:
                    ax.text(text[0], text[1], text[2], text[3])

        # plot
        for index in range(len(self.style_array)):
            if 2 == self.dim:
                ax.plot(self.xs[index], self.ys[index], self.style_array[index], linewidth=1.0)
            else:
                ax.plot(self.xs[index], self.ys[index], self.zs[index], self.style_array[index], linewidth=1.0)

        ax.legend(self.legend)
        plt.pause(0.1)

        if save:
            root_dir = os.path.abspath(os.path.join(file_path, '..', 'savedirs'))
            if not os.path.exists(root_dir):
                os.makedirs(root_dir)

            dir_split = os.path.split(save)
            save_path = os.path.abspath(os.path.join(root_dir, dir_split[0]))
            if not os.path.exists(save_path):
                os.makedirs(save_path)

            save_file = os.path.abspath(os.path.join(save_path, dir_split[1]))
            plt.savefig(save_file)

    def create_figure(self, req):
        try:
            if self.fig or 'dimension' not in req or 'funcs' not in req or 'point_num' not in req:
                logger.error('request need `dimension` and `funcs` and `point_num` field.')
                return

            self.fig = plt.figure()
            self.dim = req['dimension']
            self.point_num = req['point_num']

            if 'title' in req:
                self.title = req['title']

            """
            axis字段在json请求中格式如下(这个字段用于设置坐标轴):
            {"axis": {"x": [-10, 10], "y": [-20, 20], "z": [-30, 30]}}
            """
            if 'axis' in req:
                self.axis = req['axis']

            """
            label字段在json请求中的格式如下(这个字段用于设置坐标轴的标签)
            {"label": {"x": "x-example", "y": "y-example", "z": "z-example"}}
            """
            if 'label' in req:
                self.label = req['label']

            """
            text字段在json请求中格式如下(其中"zlim"字段在三维图形中才存在):
            {"text": [{"xlim": 1, "ylim": 2, "zlim": 3, "desc": "example"}, {}, ...]}
            """
            if 'text' in req:
                for sub_txt in req['text']:
                    if 2 == self.dim:
                        self.text.append([sub_txt['xlim'], sub_txt['ylim'], sub_txt['desc']])
                    else:
                        self.text.append([sub_txt['xlim'], sub_txt['ylim'], sub_txt['zlim'], sub_txt['desc']])

            """
            funcs字段在json请求中的格式如下(可以包含多个function,因此其值为一个数组)
            {"funcs": [{"init": [[-1.0, 1.0, 2.2], ...], "line": 0, "point": 0, "col": 0, "desc": "example函数"}, {}, ...]}
            其中init字段表示该函数的初始化点集,因此也是一个数组, -1.0,1.0,2.2分别表示x/y/z的值
            line-线的样式,取值范围为 [0, len(self.line_style))
            point-点的样式,取值范围为 [0, len(self.point_markers))
            col-绘制的颜色,取值范围为 [0, len(self.colors))
            """
            funcs = req['funcs']
            func_num = len(funcs)
            self.xs = [list() for _ in range(func_num)]
            self.ys = [list() for _ in range(func_num)]
            if 3 == self.dim:    #3维需要用到z轴
                self.zs = [list() for _ in range(func_num)]

            for index, func in enumerate(funcs):
                if 'init' in func:
                    for point in func['init']:
                        self.xs[index].append(point[0])
                        self.ys[index].append(point[1])
                        if 3 == self.dim:
                            self.zs[index].append(point[2])

                func_style = self.line_style[func['line']]
                func_style += self.point_markers[func['point']]
                func_style += self.colors[func['col']]
                self.style_array.append(func_style)
                self.legend.append(func['desc'])

            self._plot_figure(None)

        except Exception as e:
            logger.exception('catch exception of `create_figure`: %s' % str(e))

    def append_point(self, req):
        if 'funcs' not in req:
            return

        """
        funcs字段的请求格式如下:
        {"funcs": [[[1, 2, 3], [4, 5, 6]], [], []], "save": "a/b/test.png"}
        由于只是在指定函数中追加点集,因此不需要给出元素的下标,此外若只是追加某些函数的下标,在指定函数处依然需要一个空的列表
        """
        for index, func in enumerate(req['funcs']):
            # list中元素个数已满,去除队列头部的N个元素
            if self.point_num == len(self.xs[index]):
                append_num = len(func)
                for _ in range(append_num):
                    self.xs[f_index].pop(0)
                    self.ys[f_index].pop(0)
                    if 3 == self.dim:
                        self.zs[f_index].pop(0)

            for point in func:
                self.xs[index].append(point[0])
                self.ys[index].append(point[1])
                if 3 == self.dim:
                    self.zs[index].append(point[2])

        save = req['save'] if 'save' in req else None
        self._plot_figure(save)

    def clear_figure(self, req):
        if 0 == len(self.xs) or 0 == len(self.xs[0]):
            return

        func_num = len(self.xs)
        self.xs = [list() for _ in range(func_num)]
        self.ys = [list() for _ in range(func_num)]
        self.zs = [list() for _ in range(func_num)]
        self.fig.clear()
        plt.pause(0.5)


def lorenz(point3d, s=10, r=28, b=2.667):
    dot = [0 for _ in range(3)]
    dot[0] = s*(point3d[1]-point3d[0])
    dot[1] = r*point3d[0]-point3d[1]-point3d[0]*point3d[2]
    dot[2] = point3d[0]*point3d[1]-b*point3d[2]
    return dot


if __name__ == '__main__':
    log_config('plot')
    q = Queue()
    p = plot_mgr(q)
    p.start()

    if '2' == sys.argv[1]:
        cm = dict()
        cm['dimension'] = 2
        cm['point_num'] = 50
        cm['title'] = '2-dimension function example'
        cm['text'] = list()
        cm['text'].append({'xlim': 2.2, 'ylim': 7.5, 'desc': '$e^x$'})
        cm['text'].append({'xlim': 3.2, 'ylim': 7.5, 'desc': '$2^x$'})
        cm['axis'] = {'x': [-4.0, 4.0], 'y': [-0.5, 50.0]}
        cm['label'] = {'x': 'x-axis', 'y': 'y-axis'}
        cm['funcs'] = [{'init': [[0, -4.0, math.exp(-4.0)]], 'line': 0, 'point': 0, 'col': 0, 'desc': '$e^x$'},
                   {'init': [[0, -4.0, 2**-4.0]], 'line': 1, 'point': 1, 'col': 1, 'desc': '$2^x$'}]
        q.put([1, cm])

        dt = 8.0/50
        x = -4+dt
        inter_index = int(random.random()*49)
        print('clear figure when index = %d' % inter_index)
        save_index = int(random.random()*49)
        print('save figure when index = %d' % save_index)
        for i in range(49):
            am = dict()
            if inter_index == i:
                q.put([2, {}])
                continue

            am['funcs'] = [[[x, math.exp(x)]], [[x, 2**x]]]
            if i == save_index:
                am['save'] = 'a/b/test'

            q.put([3, am])
            time.sleep(0.5)
            x += dt
    elif '3' == sys.argv[1]:
        cm = dict()
        cm['dimension'] = 3
        cm['point_num'] = 10001
        cm['title'] = 'Lorenz Attractor'
        cm['axis'] = {'x': [-30.0, 30.0], 'y': [-30.0, 30.0], 'z': [0.0, 50.0]}
        cm['label'] = {'x': 'x-axis', 'y': 'y-axis', 'z': 'z-axis'}
        last_point = [0.0, 1.0, 1.05]
        cm['funcs'] = [{'init': [last_point], 'line': 0, 'point': 0, 'col': 0, 'desc': 'lorenz'}]
        q.put([1, cm])

        am = dict()
        curr_point = [0.0 for _ in range(3)]
        for i in range(10000):
            axis_dot = lorenz(last_point)
            curr_point[0] = last_point[0]+axis_dot[0]*0.01
            curr_point[1] = last_point[1]+axis_dot[1]*0.01
            curr_point[2] = last_point[2]+axis_dot[2]*0.01
            am['funcs'] = [[curr_point]]
            q.put([3, am])
            last_point = curr_point
            time.sleep(0.5)

    time.sleep(2)
    print('send exit signal, join subprocess')
    q.put([-1])
    p.join()
