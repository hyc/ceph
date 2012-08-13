#ifndef CEPH_CLS_RGW_OPS_H
#define CEPH_CLS_RGW_OPS_H

#include <map>

#include "include/types.h"
#include "cls/rgw/cls_rgw_types.h"

struct rgw_cls_obj_prepare_op
{
  uint8_t op;
  string name;
  string tag;
  string locator;

  rgw_cls_obj_prepare_op() : op(0) {}

  void encode(bufferlist &bl) const {
    ENCODE_START(3, 3, bl);
    ::encode(op, bl);
    ::encode(name, bl);
    ::encode(tag, bl);
    ::encode(locator, bl);
    ENCODE_FINISH(bl);
  }
  void decode(bufferlist::iterator &bl) {
    DECODE_START_LEGACY_COMPAT_LEN(3, 3, 3, bl);
    ::decode(op, bl);
    ::decode(name, bl);
    ::decode(tag, bl);
    if (struct_v >= 2) {
      ::decode(locator, bl);
    }
    DECODE_FINISH(bl);
  }
  void dump(Formatter *f) const;
  static void generate_test_instances(list<rgw_cls_obj_prepare_op*>& o);
};
WRITE_CLASS_ENCODER(rgw_cls_obj_prepare_op)

struct rgw_cls_obj_complete_op
{
  uint8_t op;
  string name;
  string locator;
  uint64_t epoch;
  struct rgw_bucket_dir_entry_meta meta;
  string tag;

  void encode(bufferlist &bl) const {
    ENCODE_START(3, 3, bl);
    ::encode(op, bl);
    ::encode(name, bl);
    ::encode(epoch, bl);
    ::encode(meta, bl);
    ::encode(tag, bl);
    ::encode(locator, bl);
    ENCODE_FINISH(bl);
 }
  void decode(bufferlist::iterator &bl) {
    DECODE_START_LEGACY_COMPAT_LEN(3, 3, 3, bl);
    ::decode(op, bl);
    ::decode(name, bl);
    ::decode(epoch, bl);
    ::decode(meta, bl);
    ::decode(tag, bl);
    if (struct_v >= 2) {
      ::decode(locator, bl);
    }
    DECODE_FINISH(bl);
  }
  void dump(Formatter *f) const;
  static void generate_test_instances(list<rgw_cls_obj_complete_op*>& o);
};
WRITE_CLASS_ENCODER(rgw_cls_obj_complete_op)

struct rgw_cls_list_op
{
  string start_obj;
  uint32_t num_entries;
  string filter_prefix;

  rgw_cls_list_op() : num_entries(0) {}

  void encode(bufferlist &bl) const {
    ENCODE_START(3, 2, bl);
    ::encode(start_obj, bl);
    ::encode(num_entries, bl);
    ::encode(filter_prefix, bl);
    ENCODE_FINISH(bl);
  }
  void decode(bufferlist::iterator &bl) {
    DECODE_START_LEGACY_COMPAT_LEN(3, 2, 2, bl);
    ::decode(start_obj, bl);
    ::decode(num_entries, bl);
    if (struct_v >= 3)
      ::decode(filter_prefix, bl);
    DECODE_FINISH(bl);
  }
  void dump(Formatter *f) const;
  static void generate_test_instances(list<rgw_cls_list_op*>& o);
};
WRITE_CLASS_ENCODER(rgw_cls_list_op)

struct rgw_cls_list_ret
{
  rgw_bucket_dir dir;
  bool is_truncated;

  rgw_cls_list_ret() : is_truncated(false) {}

  void encode(bufferlist &bl) const {
    ENCODE_START(2, 2, bl);
    ::encode(dir, bl);
    ::encode(is_truncated, bl);
    ENCODE_FINISH(bl);
  }
  void decode(bufferlist::iterator &bl) {
    DECODE_START_LEGACY_COMPAT_LEN(2, 2, 2, bl);
    ::decode(dir, bl);
    ::decode(is_truncated, bl);
    DECODE_FINISH(bl);
  }
  void dump(Formatter *f) const;
  static void generate_test_instances(list<rgw_cls_list_ret*>& o);
};
WRITE_CLASS_ENCODER(rgw_cls_list_ret)

struct rgw_cls_usage_log_add_op {
  rgw_usage_log_info info;

  void encode(bufferlist& bl) const {
    ENCODE_START(1, 1, bl);
    ::encode(info, bl);
    ENCODE_FINISH(bl);
  }

  void decode(bufferlist::iterator& bl) {
    DECODE_START(1, bl);
    ::decode(info, bl);
    DECODE_FINISH(bl);
  }
};
WRITE_CLASS_ENCODER(rgw_cls_usage_log_add_op)

struct rgw_cls_usage_log_read_op {
  uint64_t start_epoch;
  uint64_t end_epoch;
  string owner;

  string iter;  // should be empty for the first call, non empty for subsequent calls
  uint32_t max_entries;

  void encode(bufferlist& bl) const {
    ENCODE_START(1, 1, bl);
    ::encode(start_epoch, bl);
    ::encode(end_epoch, bl);
    ::encode(owner, bl);
    ::encode(iter, bl);
    ::encode(max_entries, bl);
    ENCODE_FINISH(bl);
  }

  void decode(bufferlist::iterator& bl) {
    DECODE_START(1, bl);
    ::decode(start_epoch, bl);
    ::decode(end_epoch, bl);
    ::decode(owner, bl);
    ::decode(iter, bl);
    ::decode(max_entries, bl);
    DECODE_FINISH(bl);
  }
};
WRITE_CLASS_ENCODER(rgw_cls_usage_log_read_op)

struct rgw_cls_usage_log_read_ret {
  map<rgw_user_bucket, rgw_usage_log_entry> usage;
  bool truncated;
  string next_iter;

  void encode(bufferlist& bl) const {
    ENCODE_START(1, 1, bl);
    ::encode(usage, bl);
    ::encode(truncated, bl);
    ::encode(next_iter, bl);
    ENCODE_FINISH(bl);
  }

  void decode(bufferlist::iterator& bl) {
    DECODE_START(1, bl);
    ::decode(usage, bl);
    ::decode(truncated, bl);
    ::decode(next_iter, bl);
    DECODE_FINISH(bl);
  }
};
WRITE_CLASS_ENCODER(rgw_cls_usage_log_read_ret)

struct rgw_cls_usage_log_trim_op {
  uint64_t start_epoch;
  uint64_t end_epoch;
  string user;

  void encode(bufferlist& bl) const {
    ENCODE_START(1, 1, bl);
    ::encode(start_epoch, bl);
    ::encode(end_epoch, bl);
    ENCODE_FINISH(bl);
  }

  void decode(bufferlist::iterator& bl) {
    DECODE_START(1, bl);
    ::decode(start_epoch, bl);
    ::decode(end_epoch, bl);
    DECODE_FINISH(bl);
  }
};
WRITE_CLASS_ENCODER(rgw_cls_usage_log_trim_op)

struct cls_rgw_gc_obj_del_info
{
  string tag;
  cls_rgw_obj_chain chain;
  utime_t time;

  cls_rgw_gc_obj_del_info() {}


  void encode(bufferlist& bl) const {
    ENCODE_START(1, 1, bl);
    ::encode(tag, bl);
    ::encode(chain, bl);
    ::encode(time, bl);
    ENCODE_FINISH(bl);
  }

  void decode(bufferlist::iterator& bl) {
    DECODE_START(1, bl);
    ::decode(tag, bl);
    ::decode(chain, bl);
    ::decode(time, bl);
    DECODE_FINISH(bl);
  }
};
WRITE_CLASS_ENCODER(cls_rgw_gc_obj_del_info)

struct cls_rgw_gc_add_entry_op {
  cls_rgw_gc_op op;
  bufferlist entry;

  cls_rgw_gc_add_entry_op() {}

  void encode(bufferlist& bl) const {
    ENCODE_START(1, 1, bl);
    uint8_t o = (uint8_t)op;
    ::encode(o, bl);
    ::encode(entry, bl);
    ENCODE_FINISH(bl);
  }

  void decode(bufferlist::iterator& bl) {
    DECODE_START(1, bl);
    uint8_t o;
    ::decode(o, bl);
    op = (cls_rgw_gc_op)o;
    ::decode(entry, bl);
    DECODE_FINISH(bl);
  }
};
WRITE_CLASS_ENCODER(cls_rgw_gc_add_entry_op)

#endif
