import os
import logging
import logging.handlers


def log_config(prefix):
    logger = logging.getLogger(prefix)
    logger.setLevel(logging.INFO)
    logger.propagate = False

    # create log dir
    file_dir = os.path.abspath(os.path.dirname(__file__))
    log_path = os.path.abspath(os.path.join(file_dir, '..', 'logs'))
    if not os.path.exists(log_path):
        os.makedirs(log_path)

    # config logger with handler & formatter
    log_file = os.path.join(log_path, prefix+'.log')
    file_handler = logging.handlers.TimedRotatingFileHandler(log_file, when='midnight', interval=1, backupCount=30, delay=True)
    file_handler.setFormatter(logging.Formatter('%(asctime)s [%(levelname)s] %(message)s', '%Y/%m/%d %H:%M:%S'))
    logger.addHandler(file_handler)
