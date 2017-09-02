#include "striper.h"
#include "include/zlog/backend.h"
#include <sstream>
#include <iostream>
#include "log_impl.h"

namespace zlog {

void Striper::AddViews(const std::list<Backend::View>& views)
{
  std::map<uint64_t, Backend::View> view_map;
  for (auto view : views) {
    auto ret = view_map.emplace(view.epoch, view);
    assert(ret.second);
  }

  // always initialize with epoch 0
  if (objsets_.empty()) {
    auto view = view_map.at(0);
    Layout layout(view.entry_size, view.stripe_width,
        view.entries_per_object);
    ObjectSet objset(layout, view.num_stripes, 0);
    objsets_.emplace(0, objset);
    epoch_ = 0;
  }

  // latest known object set
  auto latest_it = objsets_.end();
  latest_it--;

  // incorporate view state in epoch order
  uint64_t next_epoch = epoch_ + 1;
  for (auto it = view_map.find(next_epoch); it != view_map.end(); it++) {
    if (it->first != next_epoch)
      break;

    auto view = it->second;

    Layout layout(view.entry_size, view.stripe_width,
        view.entries_per_object);

    const uint64_t minpos = latest_it->second.maxpos() + 1;
    ObjectSet objset(layout, view.num_stripes, minpos);

    auto res = objsets_.emplace(minpos, objset);
    assert(res.second);
    latest_it = res.first;

    epoch_ = next_epoch;
    next_epoch++;
  }
}

std::map<uint64_t, ObjectSet>::iterator Striper::MapToObjectSet(uint64_t position)
{
  assert(!objsets_.empty());

  auto it = objsets_.upper_bound(position);
  assert(it != objsets_.begin());
  it--;

  if (position > it->second.maxpos())
    return objsets_.end();

  return it;
}

std::string Striper::ObjectName(uint64_t objectno)
{
  std::stringstream ss;
  ss << log_name_ << "." << objectno;
  return ss.str();
}

std::string Striper::ObjectName(const ObjectSet& os, uint64_t position)
{
  uint64_t objectno = os.objectno(position);
  return ObjectName(objectno);
}

int Striper::MapPosition(uint64_t position, std::string& oid, bool extend)
{
  auto it = MapToObjectSet(position);
  if (it == objsets_.end()) {
    if (!extend)
      return -EFAULT;

    int ret = log_->ExtendViews(position);
    if (ret)
      return ret;

    ret = log_->RefreshProjection();
    if (ret)
      return ret;

    it = MapToObjectSet(position);
    assert(it != objsets_.end());
  }

  oid = ObjectName(it->second, position);
  return 0;
}

int Striper::InitDataObject(uint64_t position, Backend *backend)
{
  auto it = MapToObjectSet(position);
  if (it == objsets_.end())
    return -EFAULT;

  uint64_t objectno = it->second.objectno(position);
  auto oid = ObjectName(objectno);
  auto layout = it->second.layout();

  return backend->InitDataObject(oid,
      layout.entry_size(),
      layout.num_objects(),
      layout.num_stripes(),
      objectno);
}

}
