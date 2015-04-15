#!/usr/bin/env python3
# Simple tool to perform concurrency tests on
# the key-value store built inside Ceph Monitor.
# @author: Paolo
# @date: 15/4/2015

import random, socket, string, time
import concurrent.futures

DEBUG = True 

servers = [ ("localhost", 5000), 
           ("localhost",5001), 
           ("localhost", 5002) ]

num_write = 500
key_len = 10
max_value = 10000

test_db = {}

def test():
    print("Ceph MON KVS concurrency test")
    tst_conc()
    tst_get()
    print("OK.")

def tst_conc():
    e = concurrent.futures.ThreadPoolExecutor(num_write)
    for i in e.map(_issue_set, range(1, num_write+1)):
        pass
    e.shutdown(wait=True)

def tst_get():
    for key in test_db.keys():
        cmd = "G " + key
        _print(cmd)
        ret = _send_recv(random.choice(servers), cmd)
        _print("R: " + str(ret))
        assert int(ret) == test_db[key]

def _issue_set(x):
    key = ''.join(random.choice(string.ascii_letters) for i in range(key_len))
    value = random.randint(0,max_value)
    test_db[key] = value
    cmd = "S " + key + " " + str(value)
    _print(cmd)
    ret = _send_recv(random.choice(servers), cmd)
    #assert ret == b'A'
    _print("R: " + str(ret))

def _send_recv(host_port, cmd):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(host_port)
    s.send(cmd.encode())
    return s.recv(10000)

def _print(str):
    if DEBUG:
        print(str)


if __name__=="__main__":
    test()

