#pragma once
#include <mutex>
#include <thread>
#include "include/zlog/backend.h"

namespace zlog {

class LogImpl;

/*
 * TODO:
 *  - normalize terminology between here and server side,
 *  including protobuf.
 */

class Layout {
 public:
  Layout(uint32_t entry_size, uint32_t num_objects,
      uint32_t num_stripes) :
    entry_size_(entry_size),
    num_objects_(num_objects),
    num_stripes_(num_stripes),
    size_(num_objects_ * num_stripes)
  {}

  // number of entries
  uint64_t size() const {
    return size_;
  }

  uint64_t objectno(uint64_t position) const {
    const uint64_t stripe_num = position / num_objects_;
    const uint64_t stripepos = position % num_objects_;
    const uint64_t objectsetno = stripe_num / num_stripes_;
    const uint64_t objectno = objectsetno * num_objects_ + stripepos;
    return objectno;
  }

  uint32_t entry_size() const {
    return entry_size_;
  }

  uint32_t num_objects() const {
    return num_objects_;
  }

  uint32_t num_stripes() const {
    return num_stripes_;
  }

 private:
  const uint32_t entry_size_;
  const uint32_t num_objects_;
  const uint32_t num_stripes_;
  const uint64_t size_;
};

class ObjectSet {
 public:
  ObjectSet(Layout layout, uint32_t size, uint64_t minpos) :
    layout_(layout), size_(size),
    maxpos_(minpos + layout_.size() * size_ - 1)
  {}

  uint64_t maxpos() const {
    return maxpos_;
  }

  uint64_t objectno(uint64_t position) const {
    return layout_.objectno(position);
  }

  Layout layout() const {
    return layout_;
  }

 private:
  const Layout layout_;
  const uint32_t size_;
  const uint64_t maxpos_;
};

class Striper {
 public:
  Striper(LogImpl *log) : log_(log) {}

  void AddViews(const std::list<Backend::View>& views);
  void SetName(const std::string& log_name) {
    log_name_ = log_name;
  }
  int InitDataObject(uint64_t position, Backend *backend);

  int MapPosition(uint64_t position, std::string& oid, bool extend);

 private:
  struct Stripe {
    Backend::View view;
    uint64_t max_pos;
  };

  LogImpl *log_;

  std::string log_name_;

  uint64_t epoch_;
  std::map<uint64_t, ObjectSet> objsets_;

  std::map<uint64_t, ObjectSet>::iterator MapToObjectSet(uint64_t position);
  std::string ObjectName(const ObjectSet& os, uint64_t position);
  std::string ObjectName(uint64_t objectno);
};

}
