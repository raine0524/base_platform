import os
import logging
import logging.handlers
import struct

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

def dump_mem(mem):
    if isinstance(mem, str):
        xs = bytearray()
        for c in mem:       #每个unicode字符用两个字节编码
            two_bytes = ord(c).to_bytes(2, byteorder='little')
            for each in two_bytes:
                xs.append(each)
        print(" ".join("0x{:02x}".format(c) for c in xs))
    elif isinstance(mem, bytes):
        print(" ".join("0x{:02x}".format(c) for c in mem))
    elif isinstance(mem, int):
        int_bytes = mem.to_bytes(4, byteorder='little')
        print(" ".join("0x{:02x}".format(c) for c in int_bytes))
    elif isinstance(mem, float):
        float_bytes = bytearray(struct.pack("d", mem))
        print(" ".join("0x{:02x}".format(c) for c in float_bytes))
    else:
        raise TypeError('unsupport type %s' % type(mem))

if __name__ == '__main__':
    dump_mem('东西12')
    dump_mem('南北'.encode('utf-8'))
    dump_mem(255)
    dump_mem(63.59)
    dump_mem([1, 2, 3])
