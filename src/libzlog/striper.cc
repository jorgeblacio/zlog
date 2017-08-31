#include "striper.h"
#include "include/zlog/backend.h"
#include <sstream>

void Striper::AddViews(const std::list<Backend::View>& views)
{
  std::map<uint64_t, Backend::View> ordered_views;
  for (auto v : views) {
    auto ret = ordered_views.emplace(v.epoch, v);
    assert(ret.second);
  }

  std::lock_guard<std::mutex> l(lock_);

  if (stripes_.empty()) {
    auto it = ordered_views.find(0);
    assert(it != ordered_views.end());
    Stripe s;
    s.view = it->second;
    s.max_pos = s.view.entries_per_object *
      s.view.stripe_width * s.view.num_stripes - 1;
    stripes_.emplace(0, s);
    epoch_ = 0;
    return;
  }
  // currently we only know how to do a single stripe :)
}

void Striper::MapPosition(uint64_t position, std::string& oid)
{
  assert(!stripes_.empty());
  auto it = stripes_.upper_bound(position);
  assert(it != stripes_.begin());
  it--;
  assert(position <= it->second.max_pos);
  auto view = it->second.view;

  // logical layout
  const uint64_t stripe_num = position / view.stripe_width;
  const uint64_t stripepos = position % view.stripe_width;
  const uint64_t objectsetno = stripe_num / view.entries_per_object;
  const uint64_t objectno = objectsetno * view.stripe_width + stripepos;

  std::stringstream ss;
  ss << log_name_ << "." << objectno;
  oid = ss.str();
}

int Striper::InitDataObject(uint64_t position, Backend *backend)
{
  assert(!stripes_.empty());
  auto it = stripes_.upper_bound(position);
  assert(it != stripes_.begin());
  it--;
  assert(position <= it->second.max_pos);
  auto view = it->second.view;

  // logical layout
  const uint64_t stripe_num = position / view.stripe_width;
  const uint64_t stripepos = position % view.stripe_width;
  const uint64_t objectsetno = stripe_num / view.entries_per_object;
  const uint64_t objectno = objectsetno * view.stripe_width + stripepos;

  std::stringstream ss;
  ss << log_name_ << "." << objectno;
  const std::string oid = ss.str();

  return backend->InitDataObject(oid, view.entry_size, view.stripe_width,
      view.entries_per_object, objectno);
}
