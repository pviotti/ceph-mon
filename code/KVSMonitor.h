// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Key-value Store Monitor to do performance and correctness tests.
 *
 * Note: to avoid infamous error "undefined reference to 'vtable..."
 * remember to define all virtual methods that are not pure.
 * http://stackoverflow.com/questions/3065154/undefined-reference-to-vtable
 *
 */

#ifndef CEPH_KVSMONITOR_H
#define CEPH_KVSMONITOR_H

#include "include/types.h"
#include "msg/Messenger.h"
#include "PaxosService.h"
#include "Monitor.h"
#include "MonitorDBStore.h"
#include "../messages/MKVSOperation.h"

#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/shared_mutex.hpp>

#include <sstream>
#include <map>
#include <string>
#include <stdlib.h>

/*
 * number of threads involved in
 * *all* I/O operations of io_service
 * (also w/ clients sockets)
 */
#define KV_IOTHREADS_NUM 20
#define KV_SERVER_PORT 5000
#define KV_BUFMAXLEN 1024

using boost::asio::ip::tcp;

typedef boost::shared_mutex Lock;
typedef boost::unique_lock<Lock> WLock;
typedef boost::shared_lock<Lock> RLock;

class Database;
class KVSMonitor;
class ClientSession;

class Database {
public:
  Database() : epoch(0) { }

  void set(std::string key, int value);
  int get(std::string key);
  std::string list();
  void del(std::string key);

  kvs_op decode_and_apply_op(bufferlist::iterator &p);
  void encode_pending_op(bufferlist& blist);

  void incr_epoch();
  epoch_t get_epoch();

  kvs_op pending_op;
private:
  std::map<std::string, int> db;
  Lock db_lock;
  Lock epoch_lock;
  epoch_t epoch;
  utime_t last_changed;
};

class KVSMonitor: public PaxosService {
public:
  KVSMonitor(Monitor *mn, Paxos *p, const string& service_name) :
      PaxosService(mn, p, service_name), serving(false) { }

  void create_initial();
  void update_from_paxos(bool *need_bootstrap);
  void create_pending();
  void encode_pending(MonitorDBStore::TransactionRef t);
  virtual void encode_full(MonitorDBStore::TransactionRef t);
  void dump_info(Formatter *f);
  bool preprocess_query(PaxosServiceMessage *m);
  bool prepare_update(PaxosServiceMessage *m);
  bool should_propose(double& delay);

  void init();
  void start_server();
  void serve();
  void handle_client(ClientSession* new_connection,
      const boost::system::error_code& error);
  void on_shutdown();

  int local_db_get(std::string key);
  std::string local_db_list();

  std::map<std::string, boost::condition_variable*> cond_map;

private:
  bool serving;
  tcp::acceptor* acceptor_;
  boost::asio::io_service io_service;
  boost::thread_group tg;

  Database db;
};

class ClientSession {
public:
  ClientSession(boost::asio::io_service& io_service, KVSMonitor* kvs) :
      socket_(io_service), kvs(kvs) { }

  tcp::socket& socket();
  void start();

private:
  void handle_read(const boost::system::error_code& error,
      size_t bytes_transferred);

  tcp::socket socket_;
  char data_[KV_BUFMAXLEN];
  KVSMonitor* kvs;
  boost::mutex io_mutex;
};

#endif
