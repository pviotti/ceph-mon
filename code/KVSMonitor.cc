// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab

#include "KVSMonitor.h"
#include "include/assert.h"

using boost::asio::ip::tcp;

#define dout_subsys ceph_subsys_mon
#undef dout_prefix
#define dout_prefix _prefix(_dout, mon)
static ostream& _prefix(std::ostream *_dout, Monitor *mon) {
  return *_dout << "mon." << mon->name << "@" << mon->rank << "("
      << mon->get_state_name() << ")";
}

void KVSMonitor::init() {
  dout(1) << "KVS init" << dendl;

  if (serving)
    return; // needed?

  // Write "kvslast_committed" and "kvsfirst_committed" on the DB
  // otherwise it keeps proposing the first commit.
  MonitorDBStore::TransactionRef t(new MonitorDBStore::Transaction);
  t->put(get_service_name(), "last_committed", 1);
  t->put(get_service_name(), "first_committed", 1);
  mon->store->apply_transaction(t);

  db.incr_epoch();
}

void KVSMonitor::start_server() {
  if (serving)
    return;

  int myport = KV_SERVER_PORT + mon->rank;
  dout(1) << "KVS start server on port " << myport << dendl;
  acceptor_ = new tcp::acceptor(io_service, tcp::endpoint(tcp::v4(), myport));
  acceptor_->set_option(tcp::acceptor::reuse_address(true));
  serving = true;

  serve();

  for (int i = 0; i < KV_IOTHREADS_NUM; ++i)
    tg.create_thread(boost::bind(&boost::asio::io_service::run, &io_service));
}

void KVSMonitor::serve() {
  dout(1) << "KVS serve" << dendl;

  ClientSession* new_session = new ClientSession(acceptor_->get_io_service(),
      this);

  acceptor_->async_accept(new_session->socket(),
      boost::bind(&KVSMonitor::handle_client, this, new_session,
          boost::asio::placeholders::error));
}

void KVSMonitor::handle_client(ClientSession* new_session,
    const boost::system::error_code& error) {
  dout(1) << "KVS handle client" << dendl;

  if (!error)
    boost::thread workerThread(&ClientSession::start, new_session);
  else
    delete new_session;

  if (serving)
    serve();
}

void KVSMonitor::on_shutdown() {
  try {
    dout(1) << "KVS shutdown server" << dendl;
    serving = false;
    io_service.stop();
    tg.join_all();
    delete acceptor_;
  } catch (std::exception& e) {
    dout(1) << "Exception in stop_server: " << e.what() << dendl;
  }
}

// -------------------------------------------------------------
// PaxosService inherited functions

void KVSMonitor::dump_info(Formatter *f) {
  dout(1) << "KVS dump info" << dendl;
}

void KVSMonitor::create_initial() {
  dout(1) << "KVS create initial" << dendl;
}

/**
 * @invariant This function is only called on a Leader.
 * @remarks This created state is then modified by incoming messages.
 * @remarks Called at startup and after every Paxos ratification round.
 */
void KVSMonitor::create_pending() {
  dout(1) << "KVS create pending" << dendl;
}

bool KVSMonitor::preprocess_query(PaxosServiceMessage *m) {
  dout(1) << "KVS preprocess query" << dendl;
  return false;
}

/**
 * Set the pending state.
 * @invariant Only called on a Leader.
 * @param m An update message
 */
bool KVSMonitor::prepare_update(PaxosServiceMessage *m) {
  MKVSOperation* kvo = (MKVSOperation*) m;
  // pending_op thread safety is guaranteed by
  // the lock invoked in ClientSession::handle_read
  if (!db.pending_op.is_nop()){
    dout(1) << "MULTIPLE ONGOING PROPOSALS: " << db.pending_op.to_string() <<
        "; trying to propose: " << kvo->op.to_string() << dendl;
    assert(0 == "MULTIPLE ONGOING PROPOSALS.");
  }
  db.pending_op = kvs_op(kvo->op);
  dout(1) << "KVS prepare update: " << db.pending_op.to_string() << dendl;
  return true;
}

/**
 * @invariant Only called on a Leader.
 */
bool KVSMonitor::should_propose(double& delay) {
  dout(1) << "KVS should propose" << dendl;
  delay = 0.0;
  return true;
}

/**
 * @invariant Only called on a Leader.
 */
void KVSMonitor::encode_full(MonitorDBStore::TransactionRef t) {
  dout(1) << "KVS encode full" << dendl;
}

/**
 * 1. Encodes the transaction to a bufferlist
 * 2. Writes the bufferlist on the durable DB
 * @invariant Only called on a Leader.
 * @param t The transaction to be encoded.
 */
void KVSMonitor::encode_pending(MonitorDBStore::TransactionRef t) {
  dout(1) << "KVS encode pending" << dendl;
  bufferlist bl;
  db.encode_pending_op(bl);

  epoch_t epoch = db.get_epoch();
  put_version(t, epoch + 1, bl);
  put_last_committed(t, epoch + 1);
}

void KVSMonitor::update_from_paxos(bool *need_bootstrap) {

  db.pending_op = kvs_op();
  version_t version = get_last_committed();
  epoch_t epoch = db.get_epoch();
  dout(1) << "KVS update from paxos, last committed: " << version
            << ", db.epoch: " << epoch << dendl;
  if (version <= epoch) {
    dout(1) << "Paxos last version < KVS epoch: not applying the update." << dendl;
    return;
  } else if (version != epoch + 1)
    dout(1) << "Paxos last version != KVS epoch +1: GAP WARNING" << dendl;      // XXX

  bufferlist command_bl;
  int ret = get_version(version, command_bl);
  assert(ret == 0);
  assert(command_bl.length());

  bufferlist::iterator p = command_bl.begin();
  kvs_op op = db.decode_and_apply_op(p);
  db.incr_epoch();

  std::string op_str = op.to_string();
  if (cond_map.find(op_str) != cond_map.end())
    cond_map[op_str]->notify_one();
}


// -------------------------------------------------------------
// DB-related functions

#define SLEEP_LIN_READ_MS 50

int KVSMonitor::local_db_get(std::string key) {

  version_t version = get_last_committed();
  epoch_t epoch = db.get_epoch();
  dout(1) << "KVS local_db_get, last committed: " << version
              << ", db.epoch: " << epoch << dendl;

  // freshness of read: check versions and Paxos lease validity
  if (is_readable(version) && (version >= epoch)){
    int ret = db.get(key);
    dout(1) << "KVS local_db_get readable, returning READ: " << ret << dendl;
    return ret;
  } else {
    dout(1) << "KVS local_db_get NOT readable: recursive GET call for key " << key << dendl;
    // XXX ugly hack: should implement wait and signal or callbacks somehow...
    boost::this_thread::sleep(boost::posix_time::milliseconds(SLEEP_LIN_READ_MS));
    return local_db_get(key);
  }
}

std::string KVSMonitor::local_db_list() {

  version_t version = get_last_committed();
  epoch_t epoch = db.get_epoch();
  dout(1) << "KVS local_db_list, last committed: " << version
              << ", db.epoch: " << epoch << dendl;

  // freshness of read: check versions and Paxos lease validity
  if (is_readable(version) && (version >= epoch)){
    dout(1) << "KVS local_db_list readable, returning" << dendl;
    return db.list();
  } else {
    dout(1) << "KVS local_db_list NOT readable: recursive LIST call" << dendl;
    // XXX ugly hack: should implement wait and signal or callbacks somehow...
    boost::this_thread::sleep(boost::posix_time::milliseconds(SLEEP_LIN_READ_MS));
    return local_db_list();
  }
}

/***************************************************************
 *                          Database                           *
 ***************************************************************/

#undef dout_prefix
#define dout_prefix _prefix_client(_dout)
static ostream& _prefix_client(std::ostream *_dout) {
  return *_dout << "database ";
}

void Database::set(std::string key, int value) {
  WLock w_lock(db_lock);
  db[key] = value;
}
int Database::get(std::string key) {
  RLock r_lock(db_lock);
  if (db.find(key) != db.end())
    return db[key];
  else
    return -1;
}
std::string Database::list() {
  RLock r_lock(db_lock);
  std::ostringstream os;
  for (std::map<string, int>::iterator iter = db.begin(); iter != db.end();
      ++iter)
    os << iter->first << ": " << iter->second << "; ";
  return os.str();
}
void Database::del(std::string key) {
  WLock w_lock(db_lock);
  db.erase(key);
}

kvs_op Database::decode_and_apply_op(bufferlist::iterator &p) {

  kvs_op op;
  DECODE_START(1, p);
  char c_type;
  ::decode_raw(c_type, p);
  op.type = (kvs_op_type) c_type;
  ::decode(op.key, p);
  ::decode(op.value, p);
  DECODE_FINISH(p);

  dout(1) << "KVS update from paxos, decode_and_apply_op: "
      << op.to_string() << dendl;

  switch (op.type) {
  case SET:
    set(op.key, op.value);
    break;
  case DEL:
    del(op.key);
    break;
  default:	// GET and LIST are served locally
    assert(0 == "Unsupported operation.\
		GET and LIST operation should be served locally.");
    break;
  }

  return op;
}

void Database::encode_pending_op(bufferlist& blist) {
  ENCODE_START(1, 1, blist);
  ::encode_raw((char) pending_op.type, blist);
  ::encode(pending_op.key, blist);
  ::encode(pending_op.value, blist);
  ENCODE_FINISH(blist);
}

void Database::incr_epoch() {
  WLock w_lock(epoch_lock);
  epoch++;
  last_changed = ceph_clock_now(g_ceph_context);
}

epoch_t Database::get_epoch() {
  RLock r_lock(epoch_lock);
  return epoch;
}

/***************************************************************
 *                     Client Session                          *
 ***************************************************************/

tcp::socket& ClientSession::socket() {
  return socket_;
}

void ClientSession::start() {
  socket_.async_read_some(boost::asio::buffer(data_, KV_BUFMAXLEN),
      boost::bind(&ClientSession::handle_read, this,
          boost::asio::placeholders::error,
          boost::asio::placeholders::bytes_transferred));
}

void ClientSession::handle_read(const boost::system::error_code& error,
    size_t bytes_transferred) {
  if (!error) {
    std::istringstream request(std::string(data_, bytes_transferred));
    boost::asio::streambuf resp;
    std::ostream os(&resp);

    char optype_char = 0;
    std::string key;
    int value;
    if (request >> optype_char)
      switch (optype_char) {
      case SET: {
        if (request >> key >> value) {
          kvs_op op(SET, key, value);
          MKVSOperation* mop = new MKVSOperation(op);
          mop->set_connection(kvs->mon->con_self);              // to handle forwarding to leader
          mop->set_src(entity_name_t::MON(kvs->mon->rank));

          // This lock on the leader also guarantees that
          // only one single proposal may be ongoing at any time.
          kvs->mon->lock.Lock();				// because of SafeTimer
          kvs->dispatch(mop);
          kvs->mon->lock.Unlock();

          // Map of condition variables may not be properly thread safe
          // but it's unlikely that this will change anything anyway
          // and making it thread safe would be an overkill.
          boost::unique_lock < boost::mutex > c_lock(io_mutex);
          std::string op_str = op.to_string();
          kvs->cond_map[op_str] = new boost::condition_variable();
          kvs->cond_map[op_str]->wait(c_lock);
          kvs->cond_map.erase(op_str);

          os << "A";
        } else
          os << "N";
        break;
      }
      case DEL:
        if (request >> key) {
          kvs_op op(DEL, key, -1);
          MKVSOperation* mop = new MKVSOperation(op);
          mop->set_connection(kvs->mon->con_self);              // to handle forwarding to leader
          mop->set_src(entity_name_t::MON(kvs->mon->rank));

          kvs->mon->lock.Lock();				// because of SafeTimer
          kvs->dispatch(mop);
          kvs->mon->lock.Unlock();

          boost::unique_lock < boost::mutex > c_lock(io_mutex);
          std::string op_str = op.to_string();
          kvs->cond_map[op_str] = new boost::condition_variable();
          kvs->cond_map[op_str]->wait(c_lock);
          kvs->cond_map.erase(op_str);

          os << "A";
        } else
          os << "N";
        break;
      case GET:
        if (request >> key)
          os << kvs->local_db_get(key);
        else
          os << "N";
        break;
      case LIST:
        os << kvs->local_db_list();
        break;
      default:
        os << "N";
      }

    boost::asio::write(socket_, resp);
    boost::system::error_code ignored_ec;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
    socket_.close();
  } else
    delete this;
}
