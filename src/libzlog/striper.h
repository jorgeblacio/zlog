#pragma once
#include <mutex>
#include "include/zlog/backend.h"

class Striper {
 public:
  void AddViews(const std::list<Backend::View>& views);
  int MapPosition(uint64_t position, std::string& oid);
  void SetName(const std::string& log_name) {
    log_name_ = log_name;
  }
  int InitDataObject(uint64_t position, Backend *backend);

 private:
  struct Stripe {
    Backend::View view;
    uint64_t max_pos;
  };

  std::mutex lock_;

  uint64_t epoch_;
  std::map<uint64_t, Stripe> stripes_;

  std::string log_name_;
};
