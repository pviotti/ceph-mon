// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab

#ifndef CEPH_MKVSOPERATION_H
#define CEPH_MKVSOPERATION_H

#include "messages/PaxosServiceMessage.h"

#include <sstream>
#include <string>

enum kvs_op_type {
  SET = 'S', GET = 'G', LIST = 'L', DEL = 'D'
};

struct kvs_op {
  kvs_op_type type;
  std::string key;
  int value;

  kvs_op() { }
  kvs_op(kvs_op_type t, std::string k, int v) :
      type(t), key(k), value(v) { }
  kvs_op(const kvs_op& op) :
      type(op.type), key(op.key), value(op.value) { }

  std::string to_string() const {
    std::ostringstream os;
    os << "kvs_op(" << (char) type << ", ";
    os << key << ", ";
    os << value << ")";
    return os.str();
  }
};

class MKVSOperation: public PaxosServiceMessage {
public:
  kvs_op op;

  MKVSOperation() :
      PaxosServiceMessage(MSG_KVS_OP, 0) { }
  MKVSOperation(kvs_op op) :
      PaxosServiceMessage(MSG_KVS_OP, 0), op(op) { }

private:
  ~MKVSOperation() { }

public:
  const char *get_type_name() const {
    return "kvs_op";
  }
  void print(ostream& o) const {
    o << op.to_string();
  }

  void encode_payload() {
    paxos_encode();
    ::encode_raw((char) op.type, payload);
    ::encode(op.key, payload);
    ::encode(op.value, payload);
  }
  void encode_payload(uint64_t features) {
    encode_payload();
  }
  void decode_payload() {
    bufferlist::iterator p = payload.begin();
    paxos_decode(p);
    char c_type;
    ::decode_raw(c_type, p);
    op.type = (kvs_op_type) c_type;
    ::decode(op.key, p);
    ::decode(op.value, p);
  }
};

#endif
