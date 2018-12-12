from flask import Flask, request, make_response, jsonify
from werkzeug.serving import WSGIRequestHandler

from signal import signal, SIGCHLD, SIG_IGN
from multiprocessing import Queue
from plot import plot_mgr, log_config

app = Flask(__name__)

ploters = dict()


@app.route('/plot/create', methods=['POST'])
def plot_create():
    fig_name = request.headers.get('Figure-Name')
    if fig_name in ploters:
        return make_response(jsonify({'status': -1, 'info': 'repeat create figure'}), 400)

    queue = Queue()
    proc = plot_mgr(queue)
    proc.start()
    ploters[fig_name] = dict()
    ploters[fig_name]['process'] = proc
    ploters[fig_name]['queue'] = queue
    queue.put([1, request.json])
    return make_response(jsonify({'status': 0}), 200)


@app.route('/plot/destroy', methods=['GET'])
def plot_destroy():
    fig_name = request.headers.get('Figure-Name')
    if fig_name not in ploters:
        return make_response(jsonify({'status': 1, 'info': 'destroy not exist figure'}), 200)
    else:
        ploters[fig_name]['queue'].put([-1])
        ploters.pop(fig_name)
        return make_response(jsonify({'status': 0}), 200)


@app.route('/plot/clear')
def plot_clear():
    fig_name = request.headers.get('Figure-Name')
    if fig_name not in ploters:
        return make_response(jsonify({'status': 1, 'info': 'clear not exist figure'}), 200)
    else:
        ploters[fig_name]['queue'].put([2, {}])
        return make_response(jsonify({'status': 0}), 200)


@app.route('/plot/append', methods=['POST'])
def plot_append():
    fig_name = request.headers.get('Figure-Name')
    if fig_name not in ploters:
        return make_response(jsonify({'status': 1, 'info': 'append not exist figure'}), 200)
    else:
        ploters[fig_name]['queue'].put([3, request.json])
        return make_response(jsonify({'status': 0}), 200)


if __name__ == '__main__':
    log_config('plot')
    WSGIRequestHandler.protocol_version = "HTTP/1.1"
    signal(SIGCHLD, SIG_IGN)
    app.run(port=19915)
