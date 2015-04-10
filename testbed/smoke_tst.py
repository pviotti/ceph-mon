#!/usr/bin/env python3
# Simple tool to perform smoke tests on
# the key-value store built inside Ceph Monitor.
# @author: Paolo
# @date: 9/4/2015

import random, socket, string, time

DEBUG = True 

servers = [ ("localhost", 5000), 
           ("localhost",5001), 
           ("localhost", 5002) ]

num_write = 50
key_len = 10
max_value = 10000
num_delete = 10

test_db = {}

def test():
    print("Ceph MON KVS smoke test -", num_write, "keys")
    tst_set()
    #time.sleep(5)
    tst_get()
    tst_list()
    tst_delete()
    tst_writeread()
    print("OK.")

def tst_set():
    for i in range(1, num_write+1):
        key = ''.join(random.choice(string.ascii_letters) for i in range(key_len))
        value = random.randint(0,max_value)
        test_db[key] = value
        cmd = "S " + key + " " + str(value)
        _print(cmd)
        ret = _send_recv(random.choice(servers), cmd)
        assert ret == b'A'
        _print("R: " + str(ret))

def tst_get():
    for key in test_db.keys():
        cmd = "G " + key
        _print(cmd)
        ret = _send_recv(random.choice(servers), cmd)
        _print("R: " + str(ret))
        assert int(ret) == test_db[key]

def tst_list():
    for server in servers:
        cmd = "L"
        ret = str(_send_recv(server, cmd))
        _print(ret)
        assert len(ret) > 0
        for key in test_db.keys():
            assert ret.find(key) != -1

def tst_delete():
    for i in range(1, num_delete+1):
        key = random.choice(list(test_db.keys()))
        cmd = "D " + key
        _print(cmd)
        ret = _send_recv(random.choice(servers), cmd)
        _print("R:" + str(ret))
        assert ret == b'A'

        #time.sleep(1)
        while True:
            cmd = "G " + key
            _print(cmd)
            ret = _send_recv(random.choice(servers), cmd)
            _print("R:" + str(ret))
            if ret == b'-1':
                break
            else:
                print("DELETE NOT YET APPLIED, LINEARIZABILITY FAULT !!!")

def tst_writeread():
    for i in range(1, num_write+1):
        key = ''.join(random.choice(string.ascii_letters) for i in range(key_len))
        value = random.randint(0,max_value)
        cmd = "S " + key + " " + str(value)
        _print(cmd)
        ret = _send_recv(random.choice(servers), cmd)
        assert ret == b'A'
        _print("R: " + str(ret))
        
        cmd = "G " + key
        _print(cmd)
        ret = _send_recv(random.choice(servers), cmd)
        _print("R: " + str(ret))
        assert int(ret) == value
        

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

