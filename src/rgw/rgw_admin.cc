#include <errno.h>

#include <iostream>
#include <sstream>
#include <string>

using namespace std;

#include "common/config.h"
#include "common/ceph_argparse.h"
#include "common/Formatter.h"
#include "global/global_init.h"
#include "common/errno.h"
#include "include/utime.h"

#include "common/armor.h"
#include "rgw_user.h"
#include "rgw_rados.h"
#include "rgw_acl.h"
#include "rgw_acl_s3.h"
#include "rgw_log.h"
#include "rgw_formats.h"
#include "auth/Crypto.h"

#define dout_subsys ceph_subsys_rgw

#define SECRET_KEY_LEN 40
#define PUBLIC_ID_LEN 20

void _usage() 
{
  cerr << "usage: radosgw-admin <cmd> [options...]" << std::endl;
  cerr << "commands:\n";
  cerr << "  user create                create a new user\n" ;
  cerr << "  user modify                modify user\n";
  cerr << "  user info                  get user info\n";
  cerr << "  user rm                    remove user\n";
  cerr << "  user suspend               suspend a user\n";
  cerr << "  user enable                reenable user after suspension\n";
  cerr << "  subuser create             create a new subuser\n" ;
  cerr << "  subuser modify             modify subuser\n";
  cerr << "  subuser rm                 remove subuser\n";
  cerr << "  key create                 create access key\n";
  cerr << "  key rm                     remove access key\n";
  cerr << "  bucket list                list buckets\n";
  cerr << "  bucket link                link bucket to specified user\n";
  cerr << "  bucket unlink              unlink bucket from specified user\n";
  cerr << "  bucket stats               returns bucket statistics\n";
  cerr << "  bucket info                show bucket information\n";
  cerr << "  bucket rm                  remove bucket\n";
  cerr << "  object rm                  remove object\n";
  cerr << "  pool add                   add an existing pool for data placement\n";
  cerr << "  pool rm                    remove an existing pool from data placement set\n";
  cerr << "  pools list                 list placement active set\n";
  cerr << "  policy                     read bucket/object policy\n";
  cerr << "  log list                   list log objects\n";
  cerr << "  log show                   dump a log from specific object or (bucket + date\n";
  cerr << "                             + bucket-id)\n";
  cerr << "  log rm                     remove log object\n";
  cerr << "  usage show                 show usage (by user, date range)\n";
  cerr << "  usage trim                 trim usage (by user, date range)\n";
  cerr << "  temp remove                remove temporary objects that were created up to\n";
  cerr << "                             specified date (and optional time)\n";
  cerr << "  gc list                    dump expired garbage collection objects\n";
  cerr << "  gc process                 manually process garbage\n";
  cerr << "options:\n";
  cerr << "   --uid=<id>                user id\n";
  cerr << "   --auth-uid=<auid>         librados uid\n";
  cerr << "   --subuser=<name>          subuser name\n";
  cerr << "   --access-key=<key>        S3 access key\n";
  cerr << "   --email=<email>\n";
  cerr << "   --secret=<key>            specify secret key\n";
  cerr << "   --gen-access-key          generate random access key (for S3)\n";
  cerr << "   --gen-secret              generate random secret key\n";
  cerr << "   --key-type=<type>         key type, options are: swift, s3\n";
  cerr << "   --access=<access>         Set access permissions for sub-user, should be one\n";
  cerr << "                             of read, write, readwrite, full\n";
  cerr << "   --display-name=<name>\n";
  cerr << "   --bucket=<bucket>\n";
  cerr << "   --pool=<pool>\n";
  cerr << "   --object=<object>\n";
  cerr << "   --date=<date>\n";
  cerr << "   --start-date=<date>\n";
  cerr << "   --end-date=<date>\n";
  cerr << "   --bucket-id=<bucket-id>\n";
  cerr << "   --format=<format>         specify output format for certain operations: xml,\n";
  cerr << "                             json\n";
  cerr << "   --purge-data              when specified, user removal will also purge all the\n";
  cerr << "                             user data\n";
  cerr << "   --purge-keys              when specified, subuser removal will also purge all the\n";
  cerr << "                             subuser keys\n";
  cerr << "   --purge-objects           remove a bucket's objects before deleting it\n";
  cerr << "                             (NOTE: required to delete a non-empty bucket)\n";
  cerr << "   --show-log-entries=<flag> enable/disable dump of log entries on log show\n";
  cerr << "   --show-log-sum=<flag>     enable/disable dump of log summation on log show\n";
  cerr << "   --skip-zero-entries       log show only dumps entries that don't have zero value\n";
  cerr << "                             in one of the numeric field\n";
  cerr << "   --yes-i-really-mean-it    required for certain operations\n";
  cerr << "\n";
  cerr << "<date> := \"YYYY-MM-DD[ hh:mm:ss]\"\n";
  cerr << "\n";
  generic_client_usage();
}

int usage()
{
  _usage();
  return 1;
}

void usage_exit()
{
  _usage();
  exit(1);
}

enum {
  OPT_NO_CMD = 0,
  OPT_USER_CREATE,
  OPT_USER_INFO,
  OPT_USER_MODIFY,
  OPT_USER_RM,
  OPT_USER_SUSPEND,
  OPT_USER_ENABLE,
  OPT_SUBUSER_CREATE,
  OPT_SUBUSER_MODIFY,
  OPT_SUBUSER_RM,
  OPT_KEY_CREATE,
  OPT_KEY_RM,
  OPT_BUCKETS_LIST,
  OPT_BUCKET_LINK,
  OPT_BUCKET_UNLINK,
  OPT_BUCKET_STATS,
  OPT_BUCKET_RM,
  OPT_POLICY,
  OPT_POOL_ADD,
  OPT_POOL_RM,
  OPT_POOLS_LIST,
  OPT_LOG_LIST,
  OPT_LOG_SHOW,
  OPT_LOG_RM,
  OPT_USAGE_SHOW,
  OPT_USAGE_TRIM,
  OPT_TEMP_REMOVE,
  OPT_OBJECT_RM,
  OPT_GC_LIST,
  OPT_GC_PROCESS,
};

static uint32_t str_to_perm(const char *str)
{
  if (strcasecmp(str, "read") == 0)
    return RGW_PERM_READ;
  else if (strcasecmp(str, "write") == 0)
    return RGW_PERM_WRITE;
  else if (strcasecmp(str, "readwrite") == 0)
    return RGW_PERM_READ | RGW_PERM_WRITE;
  else if (strcasecmp(str, "full") == 0)
    return RGW_PERM_FULL_CONTROL;

  usage_exit();
  return 0; // unreachable
}

struct rgw_flags_desc {
  uint32_t mask;
  const char *str;
};

static struct rgw_flags_desc rgw_perms[] = {
 { RGW_PERM_FULL_CONTROL, "full-control" },
 { RGW_PERM_READ | RGW_PERM_WRITE, "read-write" },
 { RGW_PERM_READ, "read" },
 { RGW_PERM_WRITE, "write" },
 { RGW_PERM_READ_ACP, "read-acp" },
 { RGW_PERM_WRITE_ACP, "read-acp" },
 { 0, NULL }
};

static void perm_to_str(uint32_t mask, char *buf, int len)
{
  const char *sep = "";
  int pos = 0;
  if (!mask) {
    snprintf(buf, len, "<none>");
    return;
  }
  while (mask) {
    uint32_t orig_mask = mask;
    for (int i = 0; rgw_perms[i].mask; i++) {
      struct rgw_flags_desc *desc = &rgw_perms[i];
      if ((mask & desc->mask) == desc->mask) {
        pos += snprintf(buf + pos, len - pos, "%s%s", sep, desc->str);
        if (pos == len)
          return;
        sep = ", ";
        mask &= ~desc->mask;
        if (!mask)
          return;
      }
    }
    if (mask == orig_mask) // no change
      break;
  }
}

static int get_cmd(const char *cmd, const char *prev_cmd, bool *need_more)
{
  *need_more = false;
  if (strcmp(cmd, "user") == 0 ||
      strcmp(cmd, "subuser") == 0 ||
      strcmp(cmd, "key") == 0 ||
      strcmp(cmd, "buckets") == 0 ||
      strcmp(cmd, "bucket") == 0 ||
      strcmp(cmd, "pool") == 0 ||
      strcmp(cmd, "pools") == 0 ||
      strcmp(cmd, "log") == 0 ||
      strcmp(cmd, "usage") == 0 ||
      strcmp(cmd, "object") == 0 ||
      strcmp(cmd, "temp") == 0 ||
      strcmp(cmd, "gc") == 0) {
    *need_more = true;
    return 0;
  }

  if (strcmp(cmd, "policy") == 0)
    return OPT_POLICY;

  if (!prev_cmd)
    return -EINVAL;

  if (strcmp(prev_cmd, "user") == 0) {
    if (strcmp(cmd, "create") == 0)
      return OPT_USER_CREATE;
    if (strcmp(cmd, "info") == 0)
      return OPT_USER_INFO;
    if (strcmp(cmd, "modify") == 0)
      return OPT_USER_MODIFY;
    if (strcmp(cmd, "rm") == 0)
      return OPT_USER_RM;
    if (strcmp(cmd, "suspend") == 0)
      return OPT_USER_SUSPEND;
    if (strcmp(cmd, "enable") == 0)
      return OPT_USER_ENABLE;
  } else if (strcmp(prev_cmd, "subuser") == 0) {
    if (strcmp(cmd, "create") == 0)
      return OPT_SUBUSER_CREATE;
    if (strcmp(cmd, "modify") == 0)
      return OPT_SUBUSER_MODIFY;
    if (strcmp(cmd, "rm") == 0)
      return OPT_SUBUSER_RM;
  } else if (strcmp(prev_cmd, "key") == 0) {
    if (strcmp(cmd, "create") == 0)
      return OPT_KEY_CREATE;
    if (strcmp(cmd, "rm") == 0)
      return OPT_KEY_RM;
  } else if (strcmp(prev_cmd, "buckets") == 0) {
    if (strcmp(cmd, "list") == 0)
      return OPT_BUCKETS_LIST;
  } else if (strcmp(prev_cmd, "bucket") == 0) {
    if (strcmp(cmd, "list") == 0)
      return OPT_BUCKETS_LIST;
    if (strcmp(cmd, "link") == 0)
      return OPT_BUCKET_LINK;
    if (strcmp(cmd, "unlink") == 0)
      return OPT_BUCKET_UNLINK;
    if (strcmp(cmd, "stats") == 0)
      return OPT_BUCKET_STATS;
    if (strcmp(cmd, "rm") == 0)
      return OPT_BUCKET_RM;
  } else if (strcmp(prev_cmd, "log") == 0) {
    if (strcmp(cmd, "list") == 0)
      return OPT_LOG_LIST;
    if (strcmp(cmd, "show") == 0)
      return OPT_LOG_SHOW;
    if (strcmp(cmd, "rm") == 0)
      return OPT_LOG_RM;
  } else if (strcmp(prev_cmd, "usage") == 0) {
    if (strcmp(cmd, "show") == 0)
      return OPT_USAGE_SHOW;
    if (strcmp(cmd, "trim") == 0)
      return OPT_USAGE_TRIM;
  } else if (strcmp(prev_cmd, "temp") == 0) {
    if (strcmp(cmd, "remove") == 0)
      return OPT_TEMP_REMOVE;
  } else if (strcmp(prev_cmd, "pool") == 0) {
    if (strcmp(cmd, "add") == 0)
      return OPT_POOL_ADD;
    if (strcmp(cmd, "rm") == 0)
      return OPT_POOL_RM;
  } else if (strcmp(prev_cmd, "pools") == 0) {
    if (strcmp(cmd, "list") == 0)
      return OPT_POOLS_LIST;
  } else if (strcmp(prev_cmd, "object") == 0) {
    if (strcmp(cmd, "rm") == 0)
      return OPT_OBJECT_RM;
  } else if (strcmp(prev_cmd, "gc") == 0) {
    if (strcmp(cmd, "list") == 0)
      return OPT_GC_LIST;
    if (strcmp(cmd, "process") == 0)
      return OPT_GC_PROCESS;
  }

  return -EINVAL;
}

string escape_str(string& src, char c)
{
  int pos = 0;
  string s = src;
  string dest;

  do {
    int new_pos = src.find(c, pos);
    if (new_pos >= 0) {
      dest += src.substr(pos, new_pos - pos);
      dest += "\\";
      dest += c;
    } else {
      dest += src.substr(pos);
      return dest;
    }
    pos = new_pos + 1;
  } while (pos < (int)src.size());

  return dest;
}

static void show_user_info(RGWUserInfo& info, Formatter *formatter)
{
  map<string, RGWAccessKey>::iterator kiter;
  map<string, RGWSubUser>::iterator uiter;


  formatter->open_object_section("user_info");

  formatter->dump_string("user_id", info.user_id);
  formatter->dump_int("rados_uid", info.auid);
  formatter->dump_string("display_name", info.display_name);
  formatter->dump_string("email", info.user_email);
  formatter->dump_int("suspended", (int)info.suspended);
  formatter->dump_int("max_buckets", (int)info.max_buckets);

  // subusers
  formatter->open_array_section("subusers");
  for (uiter = info.subusers.begin(); uiter != info.subusers.end(); ++uiter) {
    RGWSubUser& u = uiter->second;
    formatter->open_object_section("user");
    formatter->dump_format("id", "%s:%s", info.user_id.c_str(), u.name.c_str());
    char buf[256];
    perm_to_str(u.perm_mask, buf, sizeof(buf));
    formatter->dump_string("permissions", buf);
    formatter->close_section();
    formatter->flush(cout);
  }
  formatter->close_section();

  // keys
  formatter->open_array_section("keys");
  for (kiter = info.access_keys.begin(); kiter != info.access_keys.end(); ++kiter) {
    RGWAccessKey& k = kiter->second;
    const char *sep = (k.subuser.empty() ? "" : ":");
    const char *subuser = (k.subuser.empty() ? "" : k.subuser.c_str());
    formatter->open_object_section("key");
    formatter->dump_format("user", "%s%s%s", info.user_id.c_str(), sep, subuser);
    formatter->dump_string("access_key", k.id);
    formatter->dump_string("secret_key", k.key);
    formatter->close_section();
  }
  formatter->close_section();

  formatter->open_array_section("swift_keys");
  for (kiter = info.swift_keys.begin(); kiter != info.swift_keys.end(); ++kiter) {
    RGWAccessKey& k = kiter->second;
    const char *sep = (k.subuser.empty() ? "" : ":");
    const char *subuser = (k.subuser.empty() ? "" : k.subuser.c_str());
    formatter->open_object_section("key");
    formatter->dump_format("user", "%s%s%s", info.user_id.c_str(), sep, subuser);
    formatter->dump_string("secret_key", k.key);
    formatter->close_section();
  }
  formatter->close_section();

  formatter->close_section();
  formatter->flush(cout);
  cout << std::endl;
}

static int create_bucket(string bucket_str, string& user_id, string& display_name, uint64_t auid)
{
  RGWAccessControlPolicy policy, old_policy;
  map<string, bufferlist> attrs;
  bufferlist aclbl;
  string no_oid;
  rgw_obj obj;
  RGWBucketInfo bucket_info;

  int ret;

  // defaule policy (private)
  policy.create_default(user_id, display_name);
  policy.encode(aclbl);

  ret = rgwstore->get_bucket_info(NULL, bucket_str, bucket_info);
  if (ret < 0)
    return ret;

  rgw_bucket& bucket = bucket_info.bucket;

  ret = rgwstore->create_bucket(user_id, bucket, attrs, false, auid);
  if (ret && ret != -EEXIST)   
    goto done;

  obj.init(bucket, no_oid);

  ret = rgwstore->set_attr(NULL, obj, RGW_ATTR_ACL, aclbl);
  if (ret < 0) {
    cerr << "couldn't set acl on bucket" << std::endl;
  }

  ret = rgw_add_bucket(user_id, bucket);

  dout(20) << "ret=" << ret << dendl;

  if (ret == -EEXIST)
    ret = 0;
done:
  return ret;
}

static void remove_old_indexes(RGWUserInfo& old_info, RGWUserInfo new_info)
{
  int ret;
  bool success = true;

  if (!old_info.user_id.empty() && old_info.user_id.compare(new_info.user_id) != 0) {
    ret = rgw_remove_uid_index(old_info.user_id);
    if (ret < 0 && ret != -ENOENT) {
      cerr << "ERROR: could not remove index for uid " << old_info.user_id << " return code: " << ret << std::endl;
      success = false;
    }
  }

  if (!old_info.user_email.empty() &&
      old_info.user_email.compare(new_info.user_email) != 0) {
    ret = rgw_remove_email_index(old_info.user_email);
    if (ret < 0 && ret != -ENOENT) {
      cerr << "ERROR: could not remove index for email " << old_info.user_email << " return code: " << ret << std::endl;
      success = false;
    }
  }

  map<string, RGWAccessKey>::iterator old_iter;
  for (old_iter = old_info.swift_keys.begin(); old_iter != old_info.swift_keys.end(); ++old_iter) {
    RGWAccessKey& swift_key = old_iter->second;
    map<string, RGWAccessKey>::iterator new_iter = new_info.swift_keys.find(swift_key.id);
    if (new_iter == new_info.swift_keys.end()) {
      ret = rgw_remove_swift_name_index(swift_key.id);
      if (ret < 0 && ret != -ENOENT) {
        cerr << "ERROR: could not remove index for swift_name " << swift_key.id << " return code: " << ret << std::endl;
        success = false;
      }
    }
  }

  /* we're not removing access keys here.. keys are removed explicitly using the key rm command and removing the old key
     index is handled there */

  if (!success)
    cerr << "ERROR: this should be fixed manually!" << std::endl;
}

static bool char_is_unreserved_url(char c)
{
  if (isalnum(c))
    return true;

  switch (c) {
  case '-':
  case '.':
  case '_':
  case '~':
    return true;
  default:
    return false;
  }
}

static bool validate_access_key(string& key)
{
  const char *p = key.c_str();
  while (*p) {
    if (!char_is_unreserved_url(*p))
      return false;
    p++;
  }
  return true;
}

int bucket_stats(rgw_bucket& bucket, Formatter *formatter)
{
  RGWBucketInfo bucket_info;
  int r = rgwstore->get_bucket_info(NULL, bucket.name, bucket_info);
  if (r < 0)
    return r;

  map<RGWObjCategory, RGWBucketStats> stats;
  int ret = rgwstore->get_bucket_stats(bucket, stats);
  if (ret < 0) {
    cerr << "error getting bucket stats ret=" << ret << std::endl;
    return ret;
  }
  map<RGWObjCategory, RGWBucketStats>::iterator iter;
  formatter->open_object_section("stats");
  formatter->dump_string("bucket", bucket.name);
  formatter->dump_string("pool", bucket.pool);
  
  formatter->dump_string("id", bucket.bucket_id);
  formatter->dump_string("marker", bucket.marker);
  formatter->dump_string("owner", bucket_info.owner);
  formatter->open_object_section("usage");
  for (iter = stats.begin(); iter != stats.end(); ++iter) {
    RGWBucketStats& s = iter->second;
    const char *cat_name = rgw_obj_category_name(iter->first);
    formatter->open_object_section(cat_name);
    formatter->dump_int("size_kb", s.num_kb);
    formatter->dump_int("size_kb_actual", s.num_kb_rounded);
    formatter->dump_int("num_objects", s.num_objects);
    formatter->close_section();
    formatter->flush(cout);
  }
  formatter->close_section();
  formatter->close_section();
  return 0;
}

enum ObjectKeyType {
  KEY_TYPE_SWIFT,
  KEY_TYPE_S3,
};

static void parse_date(string& date, uint64_t *epoch, string *out_date = NULL, string *out_time = NULL)
{
  struct tm tm;

  memset(&tm, 0, sizeof(tm));

  const char *p = strptime(date.c_str(), "%Y-%m-%d", &tm);
  if (p) {
    if (*p == ' ') {
      p++;
      strptime(p, " %H:%M:%S", &tm);
    }
  } else {
    return;
  }
  time_t t = timegm(&tm);
  if (epoch)
    *epoch = (uint64_t)t;

  if (out_date) {
    char buf[32];
    strftime(buf, sizeof(buf), "%F", &tm);
    *out_date = buf;
  }
  if (out_time) {
    char buf[32];
    strftime(buf, sizeof(buf), "%T", &tm);
    *out_time = buf;
  }
}

static int remove_object(rgw_bucket& bucket, std::string& object)
{
  int ret = -EINVAL;
  RGWRadosCtx *rctx = new RGWRadosCtx();
  rgw_obj obj(bucket,object);

  ret = rgwstore->delete_obj(rctx, obj);

  return ret;
}

static int remove_bucket(rgw_bucket& bucket, bool delete_children)
{
  int ret;
  map<RGWObjCategory, RGWBucketStats> stats;
  std::vector<RGWObjEnt> objs;
  std::string prefix, delim, marker, ns;
  map<string, bool> common_prefixes;
  rgw_obj obj;
  RGWBucketInfo info;
  bufferlist bl;

  static rgw_bucket pi_buckets_rados = RGW_ROOT_BUCKET;
  ret = rgwstore->get_bucket_stats(bucket, stats);
  if (ret < 0)
    return ret;

  obj.bucket = bucket;
  int max = 1000;

  ret = rgw_get_obj(NULL, pi_buckets_rados, bucket.name, bl, NULL);

  bufferlist::iterator iter = bl.begin();
  try {
    ::decode(info, iter);
  } catch (buffer::error& err) {
    cerr << "ERROR: could not decode buffer info, caught buffer::error" << std::endl;
    return -EIO;
  }

  if (delete_children) {
    ret = rgwstore->list_objects(bucket, max, prefix, delim, marker, objs, common_prefixes,
                                 false, ns, (bool *)false, NULL);
    if (ret < 0)
      return ret;

    while (objs.size() > 0) {
      std::vector<RGWObjEnt>::iterator it = objs.begin();
      for (it = objs.begin(); it != objs.end(); it++) {
        ret = remove_object(bucket, (*it).name);
        if (ret < 0)
          return ret;
      }
      objs.clear();

      ret = rgwstore->list_objects(bucket, max, prefix, delim, marker, objs, common_prefixes,
                                   false, ns, (bool *)false, NULL);
      if (ret < 0)
        return ret;
    }
  }

  ret = rgwstore->delete_bucket(bucket);
  if (ret < 0) {
    cerr << "ERROR: could not remove bucket " << bucket.name << std::endl;

    return ret;
  }

  ret = rgw_remove_user_bucket_info(info.owner, bucket);
  if (ret < 0) {
    cerr << "ERROR: unable to remove user bucket information" << std::endl;
  }

  return ret;
}

int main(int argc, char **argv) 
{
  vector<const char*> args;
  argv_to_vec(argc, (const char **)argv, args);
  env_to_vec(args);

  global_init(NULL, args, CEPH_ENTITY_TYPE_CLIENT, CODE_ENVIRONMENT_UTILITY, 0);
  common_init_finish(g_ceph_context);

  std::string user_id, access_key, secret_key, user_email, display_name;
  std::string bucket_name, pool_name, object;
  std::string date, subuser, access, format;
  std::string start_date, end_date;
  std::string key_type_str;
  ObjectKeyType key_type = KEY_TYPE_S3;
  rgw_bucket bucket;
  uint32_t perm_mask = 0;
  bool specified_perm_mask = false;
  uint64_t auid = -1;
  RGWUserInfo info;
  RGWRados *store;
  int opt_cmd = OPT_NO_CMD;
  bool need_more;
  int gen_secret = false;
  int gen_key = false;
  bool implicit_gen_secret = true;
  bool implicit_gen_key = true;
  char secret_key_buf[SECRET_KEY_LEN + 1];
  char public_id_buf[PUBLIC_ID_LEN + 1];
  bool user_modify_op;
  string bucket_id;
  Formatter *formatter = NULL;
  int purge_data = false;
  RGWBucketInfo bucket_info;
  int pretty_format = false;
  int show_log_entries = true;
  int show_log_sum = true;
  int skip_zero_entries = false;  // log show
  int purge_keys = false;
  int yes_i_really_mean_it = false;
  int delete_child_objects = false;
  int max_buckets = -1;

  std::string val;
  std::ostringstream errs;
  long long tmp = 0;
  for (std::vector<const char*>::iterator i = args.begin(); i != args.end(); ) {
    if (ceph_argparse_double_dash(args, i)) {
      break;
    } else if (ceph_argparse_flag(args, i, "-h", "--help", (char*)NULL)) {
      usage();
      return 0;
    } else if (ceph_argparse_witharg(args, i, &val, "-i", "--uid", (char*)NULL)) {
      user_id = val;
    } else if (ceph_argparse_witharg(args, i, &val, "--access-key", (char*)NULL)) {
      access_key = val;
    } else if (ceph_argparse_witharg(args, i, &val, "--subuser", (char*)NULL)) {
      subuser = val;
    } else if (ceph_argparse_witharg(args, i, &val, "--secret", (char*)NULL)) {
      secret_key = val;
    } else if (ceph_argparse_witharg(args, i, &val, "-e", "--email", (char*)NULL)) {
      user_email = val;
    } else if (ceph_argparse_witharg(args, i, &val, "-n", "--display-name", (char*)NULL)) {
      display_name = val;
    } else if (ceph_argparse_witharg(args, i, &val, "-b", "--bucket", (char*)NULL)) {
      bucket_name = val;
    } else if (ceph_argparse_witharg(args, i, &val, "-p", "--pool", (char*)NULL)) {
      pool_name = val;
    } else if (ceph_argparse_witharg(args, i, &val, "-o", "--object", (char*)NULL)) {
      object = val;
    } else if (ceph_argparse_witharg(args, i, &val, "--key-type", (char*)NULL)) {
      key_type_str = val;
      if (key_type_str.compare("swift") == 0) {
        key_type = KEY_TYPE_SWIFT;
      } else if (key_type_str.compare("s3") == 0) {
        key_type = KEY_TYPE_S3;
      } else {
        cerr << "bad key type: " << key_type_str << std::endl;
        return usage();
      }
    } else if (ceph_argparse_binary_flag(args, i, &gen_key, NULL, "--gen-access-key", (char*)NULL)) {
      implicit_gen_key = false;
    } else if (ceph_argparse_binary_flag(args, i, &gen_secret, NULL, "--gen-secret", (char*)NULL)) {
      implicit_gen_secret = false;
    } else if (ceph_argparse_binary_flag(args, i, &show_log_entries, NULL, "--show_log_entries", (char*)NULL)) {
      // do nothing
    } else if (ceph_argparse_binary_flag(args, i, &show_log_sum, NULL, "--show_log_sum", (char*)NULL)) {
      // do nothing
    } else if (ceph_argparse_binary_flag(args, i, &skip_zero_entries, NULL, "--skip_zero_entries", (char*)NULL)) {
      // do nothing
    } else if (ceph_argparse_withlonglong(args, i, &tmp, &errs, "-a", "--auth-uid", (char*)NULL)) {
      if (!errs.str().empty()) {
	cerr << errs.str() << std::endl;
	exit(EXIT_FAILURE);
      }
      auid = tmp;
    } else if (ceph_argparse_witharg(args, i, &val, "--max-buckets", (char*)NULL)) {
      max_buckets = atoi(val.c_str());
    } else if (ceph_argparse_witharg(args, i, &val, "--date", "--time", (char*)NULL)) {
      date = val;
      if (end_date.empty())
        end_date = date;
    } else if (ceph_argparse_witharg(args, i, &val, "--start-date", "--start-time", (char*)NULL)) {
      start_date = val;
    } else if (ceph_argparse_witharg(args, i, &val, "--end-date", "--end-time", (char*)NULL)) {
      end_date = val;
    } else if (ceph_argparse_witharg(args, i, &val, "--access", (char*)NULL)) {
      access = val;
      perm_mask = str_to_perm(access.c_str());
      specified_perm_mask = true;
    } else if (ceph_argparse_witharg(args, i, &val, "--bucket-id", (char*)NULL)) {
      bucket_id = val;
      if (bucket_id.empty()) {
        cerr << "bad bucket-id" << std::endl;
        return usage();
      }
    } else if (ceph_argparse_witharg(args, i, &val, "--format", (char*)NULL)) {
      format = val;
    } else if (ceph_argparse_binary_flag(args, i, &delete_child_objects, NULL, "--purge-objects", (char*)NULL)) {
      // do nothing
    } else if (ceph_argparse_binary_flag(args, i, &pretty_format, NULL, "--pretty-format", (char*)NULL)) {
      // do nothing
    } else if (ceph_argparse_binary_flag(args, i, &purge_data, NULL, "--purge-data", (char*)NULL)) {
      delete_child_objects = purge_data;
    } else if (ceph_argparse_binary_flag(args, i, &purge_keys, NULL, "--purge-keys", (char*)NULL)) {
      // do nothing
    } else if (ceph_argparse_binary_flag(args, i, &yes_i_really_mean_it, NULL, "--yes-i-really-mean-it", (char*)NULL)) {
      // do nothing
    } else {
      ++i;
    }
  }

  if (args.size() == 0) {
    return usage();
  }
  else {
    const char *prev_cmd = NULL;
    for (std::vector<const char*>::iterator i = args.begin(); i != args.end(); ++i) {
      opt_cmd = get_cmd(*i, prev_cmd, &need_more);
      if (opt_cmd < 0) {
	cerr << "unrecognized arg " << *i << std::endl;
	return usage();
      }
      if (!need_more)
	break;
      prev_cmd = *i;
    }
    if (opt_cmd == OPT_NO_CMD)
      return usage();
  }

  // default to pretty json
  if (format.empty()) {
    format = "json";
    pretty_format = true;
  }

  if (format ==  "xml")
    formatter = new XMLFormatter(pretty_format);
  else if (format == "json")
    formatter = new JSONFormatter(pretty_format);
  else {
    cerr << "unrecognized format: " << format << std::endl;
    return usage();
  }

  if (!subuser.empty()) {
    char *suser = strdup(subuser.c_str());
    char *p = strchr(suser, ':');
    if (p) {
      *p = '\0';
      if (!user_id.empty()) {
        if (user_id != suser) {
          cerr << "bad subuser " << subuser << " for uid " << user_id << std::endl;
          return 1;
        }
      } else {
        user_id = suser;
      }
      subuser = p + 1;
    }
    free(suser);
  }

  if (opt_cmd == OPT_KEY_RM && key_type == KEY_TYPE_S3 && access_key.empty()) {
    cerr << "error: access key was not specified" << std::endl;
    return usage();
  }

  user_modify_op = (opt_cmd == OPT_USER_MODIFY || opt_cmd == OPT_SUBUSER_MODIFY ||
                    opt_cmd == OPT_SUBUSER_CREATE || opt_cmd == OPT_SUBUSER_RM ||
                    opt_cmd == OPT_KEY_CREATE || opt_cmd == OPT_KEY_RM || opt_cmd == OPT_USER_RM);

  RGWStoreManager store_manager;
  store = store_manager.init(g_ceph_context, false);
  if (!store) {
    cerr << "couldn't init storage provider" << std::endl;
    return 5; //EIO
  }

  if (opt_cmd != OPT_USER_CREATE && 
      opt_cmd != OPT_LOG_SHOW && opt_cmd != OPT_LOG_LIST && opt_cmd != OPT_LOG_RM && 
      user_id.empty()) {
    bool found = false;
    string s;
    if (!found && (!user_email.empty())) {
      s = user_email;
      if (rgw_get_user_info_by_email(s, info) >= 0) {
	found = true;
      } else {
	cerr << "could not find user by specified email" << std::endl;
      }
    }
    if (!found && (!access_key.empty())) {
      s = access_key;
      if (rgw_get_user_info_by_access_key(s, info) >= 0) {
	found = true;
      } else {
	cerr << "could not find user by specified access key" << std::endl;
      }
    }
    if (found)
      user_id = info.user_id.c_str();
  }


  if (user_modify_op || opt_cmd == OPT_USER_CREATE ||
      opt_cmd == OPT_USER_INFO || opt_cmd == OPT_BUCKET_UNLINK || opt_cmd == OPT_BUCKET_LINK ||
      opt_cmd == OPT_USER_SUSPEND || opt_cmd == OPT_USER_ENABLE) {
    if (user_id.empty()) {
      cerr << "user_id was not specified, aborting" << std::endl;
      return usage();
    }

    bool found = (rgw_get_user_info_by_uid(user_id, info) >= 0);

    if (opt_cmd == OPT_USER_CREATE) {
      if (found) {
        if (info.display_name.compare(display_name) != 0 ||
            info.user_email.compare(user_email) != 0) {
          cerr << "error: user already exists with different display_name/email" << std::endl;
          return 1;
        }
        /* turn into OPT_USER_MODIFY */
        opt_cmd = OPT_USER_MODIFY;
        user_modify_op = true;
      }
    } else if (!found) {
      cerr << "error reading user info, aborting" << std::endl;
      return 1;
    }
  }

  bool subuser_found = false;

  if (!subuser.empty()) {
    map<string, RGWSubUser>::iterator iter = info.subusers.find(subuser);
    subuser_found = (iter != info.subusers.end());

    if (!subuser_found && opt_cmd != OPT_SUBUSER_CREATE && opt_cmd != OPT_USER_CREATE) {
      cerr << "subuser specified but was not found, aborting" << std::endl;
      return 1;
    }
  }

  if (opt_cmd == OPT_SUBUSER_CREATE || opt_cmd == OPT_SUBUSER_MODIFY ||
      opt_cmd == OPT_SUBUSER_RM) {
    if (subuser.empty()) {
      cerr << "subuser creation was requires specifying subuser name" << std::endl;
      return 1;
    }
    if (opt_cmd == OPT_SUBUSER_CREATE) {
      if (subuser_found) {
        cerr << "error: subuser already exists" << std::endl;
        return 1;
      }
      if (!key_type_str.empty() && key_type == KEY_TYPE_S3) {
        cerr << "error: subusers may not be created with an S3 key, aborting" << std::endl;
        return 1;
      }
    } else if (!subuser_found) {
      cerr << "error: subuser doesn't exist" << std::endl;
      return 1;
    }
  }

  bool keys_not_requested = (access_key.empty() && secret_key.empty() && !gen_secret && !gen_key &&
                             opt_cmd != OPT_KEY_CREATE);

  if (opt_cmd == OPT_USER_CREATE || (user_modify_op && !keys_not_requested)) {
    int ret;

    if (opt_cmd == OPT_USER_CREATE && display_name.empty()) {
      cerr << "display name was not specified, aborting" << std::endl;
      return 0;
    }

    if ((secret_key.empty() && implicit_gen_secret) || gen_secret) {
      ret = gen_rand_base64(g_ceph_context, secret_key_buf, sizeof(secret_key_buf));
      if (ret < 0) {
        cerr << "aborting" << std::endl;
        return 1;
      }
      secret_key = secret_key_buf;
    }
    if ((access_key.empty() && implicit_gen_key) || gen_key) {
      RGWUserInfo duplicate_check;
      string duplicate_check_id;
      do {
	ret = gen_rand_alphanumeric_upper(g_ceph_context, public_id_buf, sizeof(public_id_buf));
	if (ret < 0) {
	  cerr << "aborting" << std::endl;
	  return 1;
	}
	access_key = public_id_buf;
	duplicate_check_id = access_key;
      } while (!rgw_get_user_info_by_access_key(duplicate_check_id, duplicate_check));
    }
  }

  map<string, RGWAccessKey>::iterator kiter;
  map<string, RGWSubUser>::iterator uiter;
  RGWUserInfo old_info = info;

  if (!bucket_name.empty()) {
    string bucket_name_str = bucket_name;
    RGWBucketInfo bucket_info;
    int r = rgwstore->get_bucket_info(NULL, bucket_name_str, bucket_info);
    if (r < 0) {
      cerr << "could not get bucket info for bucket=" << bucket_name_str << std::endl;
      return r;
    }
    bucket = bucket_info.bucket;
  }

  int err;
  switch (opt_cmd) {
  case OPT_USER_CREATE:
  case OPT_USER_MODIFY:
  case OPT_SUBUSER_CREATE:
  case OPT_SUBUSER_MODIFY:
  case OPT_KEY_CREATE:
    if (!user_id.empty())
      info.user_id = user_id;
    if (max_buckets >= 0)
      info.max_buckets = max_buckets;
    if (key_type == KEY_TYPE_SWIFT) {
      access_key = info.user_id;
      access_key.append(":");
      access_key.append(subuser);
    }
    if ((!access_key.empty()) && (!secret_key.empty())) {
      if (key_type == KEY_TYPE_S3 && !validate_access_key(access_key)) {
        cerr << "access key contains illegal characters" << std::endl;
        return 1;
      }
      RGWAccessKey k;
      k.id = access_key;
      k.key = secret_key;
      if (!subuser.empty())
        k.subuser = subuser;
      if (key_type == KEY_TYPE_SWIFT)
        info.swift_keys[access_key] = k;
      else
        info.access_keys[access_key] = k;
   } else if (opt_cmd == OPT_KEY_CREATE && (access_key.empty() || secret_key.empty())) {
      if (key_type == KEY_TYPE_SWIFT)
        cerr << "swift key modification requires both subuser and secret key" << std::endl;
      else
        cerr << "access key modification requires both access key and secret key" << std::endl;
      return 1;
    }
    if (!display_name.empty())
      info.display_name = display_name;
    if (!user_email.empty())
      info.user_email = user_email;
    if (auid != (uint64_t)-1)
      info.auid = auid;
    if (!subuser.empty()) {
      RGWSubUser u = info.subusers[subuser];
      u.name = subuser;
      if (specified_perm_mask)
        u.perm_mask = perm_mask;

      info.subusers[subuser] = u;
    }
    if ((err = rgw_store_user_info(info, false)) < 0) {
      cerr << "error storing user info: " << cpp_strerror(-err) << std::endl;
      break;
    }

    remove_old_indexes(old_info, info);

    show_user_info(info, formatter);
    break;

  case OPT_SUBUSER_RM:
    uiter = info.subusers.find(subuser);
    assert (uiter != info.subusers.end());
    info.subusers.erase(uiter);
    if (purge_keys) {
      map<string, RGWAccessKey> *keys_map;
      access_key = info.user_id;
      access_key.append(":");
      access_key.append(subuser);
      keys_map = &info.swift_keys;
      kiter = keys_map->find(access_key);
      if (kiter != keys_map->end()) {
        rgw_remove_key_index(kiter->second);
        keys_map->erase(kiter);
      }
    }
    if ((err = rgw_store_user_info(info, false)) < 0) {
      cerr << "error storing user info: " << cpp_strerror(-err) << std::endl;
      break;
    }
    remove_old_indexes(old_info, info);

    show_user_info(info, formatter);
    break;

  case OPT_KEY_RM:
    {
      map<string, RGWAccessKey> *keys_map;
      if (key_type == KEY_TYPE_SWIFT) {
        access_key = info.user_id;
        access_key.append(":");
        access_key.append(subuser);
        keys_map = &info.swift_keys;
      } else {
        keys_map = &info.access_keys;
      }
      kiter = keys_map->find(access_key);
      if (kiter == keys_map->end()) {
        cerr << "key not found" << std::endl;
      } else {
        rgw_remove_key_index(kiter->second);
        keys_map->erase(kiter);
        if ((err = rgw_store_user_info(info, false)) < 0) {
          cerr << "error storing user info: " << cpp_strerror(-err) << std::endl;
          break;
        }
      }
    }
    show_user_info(info, formatter);
    break;

  case OPT_USER_INFO:
    show_user_info(info, formatter);
    break;
  }

  if (opt_cmd == OPT_POLICY) {
    bufferlist bl;
    rgw_obj obj(bucket, object);
    int ret = store->get_attr(NULL, obj, RGW_ATTR_ACL, bl);

    RGWAccessControlPolicy_S3 policy(g_ceph_context);
    if (ret >= 0) {
      bufferlist::iterator iter = bl.begin();
      try {
        policy.decode(iter);
      } catch (buffer::error& err) {
        dout(0) << "ERROR: caught buffer::error, could not decode policy" << dendl;
        return -EIO;
      }
      policy.to_xml(cout);
      cout << std::endl;
    }
  }

  if (opt_cmd == OPT_BUCKETS_LIST) {
    RGWAccessHandle handle;

    formatter->reset();
    formatter->open_array_section("buckets");
    if (!user_id.empty()) {
      RGWUserBuckets buckets;
      if (rgw_read_user_buckets(user_id, buckets, false) < 0) {
        cerr << "list buckets: could not get buckets for uid " << user_id << std::endl;
      } else {
        map<string, RGWBucketEnt>& m = buckets.get_buckets();
        map<string, RGWBucketEnt>::iterator iter;

        for (iter = m.begin(); iter != m.end(); ++iter) {
          RGWBucketEnt obj = iter->second;
	  formatter->dump_string("bucket", obj.bucket.name);
        }
      }
    } else {
      if (store->list_buckets_init(&handle) < 0) {
        cerr << "list buckets: no buckets found" << std::endl;
      } else {
        RGWObjEnt obj;
        while (store->list_buckets_next(obj, &handle) >= 0) {
          formatter->dump_string("bucket", obj.name);
        }
      }
    }
    formatter->close_section();
    formatter->flush(cout);
    cout << std::endl;
  }

  if (opt_cmd == OPT_BUCKET_LINK) {
    if (bucket_name.empty()) {
      cerr << "bucket name was not specified" << std::endl;
      return usage();
    }
    string uid_str(user_id);
    
    string no_oid;
    bufferlist aclbl;
    rgw_obj obj(bucket, no_oid);

    int r = rgwstore->get_attr(NULL, obj, RGW_ATTR_ACL, aclbl);
    if (r >= 0) {
      RGWAccessControlPolicy policy;
      ACLOwner owner;
      try {
       bufferlist::iterator iter = aclbl.begin();
       ::decode(policy, iter);
       owner = policy.get_owner();
      } catch (buffer::error& err) {
	dout(10) << "couldn't decode policy" << dendl;
	return -EINVAL;
      }
      //cout << "bucket is linked to user '" << owner.get_id() << "'.. unlinking" << std::endl;
      r = rgw_remove_user_bucket_info(owner.get_id(), bucket);
      if (r < 0) {
        cerr << "could not unlink policy from user '" << owner.get_id() << "'" << std::endl;
        return r;
      }

      // now update the user for the bucket...
      if (info.display_name.empty()) {
        cerr << "WARNING: user " << info.user_id << " has no display name set" << std::endl;
      } else {
        policy.create_default(info.user_id, info.display_name);

        // ...and encode the acl
        aclbl.clear();
        policy.encode(aclbl);

        r = rgwstore->set_attr(NULL, obj, RGW_ATTR_ACL, aclbl);
        if (r < 0)
          return r;

        r = rgw_add_bucket(info.user_id, bucket);
        if (r < 0)
          return r;
      }
    } else {
      // the bucket seems not to exist, so we should probably create it...
      r = create_bucket(bucket_name.c_str(), uid_str, info.display_name, info.auid);
      if (r < 0)
        cerr << "error linking bucket to user: r=" << r << std::endl;
      return -r;
    }
  }

  if (opt_cmd == OPT_BUCKET_UNLINK) {
    if (bucket_name.empty()) {
      cerr << "bucket name was not specified" << std::endl;
      return usage();
    }

    int r = rgw_remove_user_bucket_info(user_id, bucket);
    if (r < 0)
      cerr << "error unlinking bucket " <<  cpp_strerror(-r) << std::endl;
    return -r;
  }

  if (opt_cmd == OPT_TEMP_REMOVE) {
    if (date.empty()) {
      cerr << "date wasn't specified" << std::endl;
      return usage();
    }
    string parsed_date, parsed_time;
    parse_date(date, NULL, &parsed_date, &parsed_time);
    int r = store->remove_temp_objects(parsed_date, parsed_time);
    if (r < 0) {
      cerr << "failure removing temp objects: " << cpp_strerror(r) << std::endl;
      return 1;
    }
  }

  if (opt_cmd == OPT_LOG_LIST) {
    // filter by date?
    if (date.size() && date.size() != 10) {
      cerr << "bad date format for '" << date << "', expect YYYY-MM-DD" << std::endl;
      return -EINVAL;
    }

    formatter->reset();
    formatter->open_array_section("logs");
    RGWAccessHandle h;
    int r = store->log_list_init(date, &h);
    if (r == -ENOENT) {
      // no logs.
    } else {
      if (r < 0) {
	cerr << "log list: error " << r << std::endl;
	return r;
      }
      while (true) {
	string name;
	int r = store->log_list_next(h, &name);
	if (r == -ENOENT)
	  break;
	if (r < 0) {
	  cerr << "log list: error " << r << std::endl;
	  return r;
	}
	formatter->dump_string("object", name);
      }
    }
    formatter->close_section();
    formatter->flush(cout);
    cout << std::endl;
  }

  if (opt_cmd == OPT_LOG_SHOW || opt_cmd == OPT_LOG_RM) {
    if (object.empty() && (date.empty() || bucket_name.empty() || bucket_id.empty())) {
      cerr << "object or (at least one of date, bucket, bucket-id) were not specified" << std::endl;
      return usage();
    }

    string oid;
    if (!object.empty()) {
      oid = object;
    } else {
      oid = date;
      oid += "-";
      oid += bucket_id;
      oid += "-";
      oid += string(bucket.name);
    }

    if (opt_cmd == OPT_LOG_SHOW) {
      RGWAccessHandle h;

      int r = store->log_show_init(oid, &h);
      if (r < 0) {
	cerr << "error opening log " << oid << ": " << cpp_strerror(-r) << std::endl;
	return -r;
      }

      formatter->reset();
      formatter->open_object_section("log");

      struct rgw_log_entry entry;
      
      // peek at first entry to get bucket metadata
      r = store->log_show_next(h, &entry);
      if (r < 0) {
	cerr << "error reading log " << oid << ": " << cpp_strerror(-r) << std::endl;
	return -r;
      }
      formatter->dump_string("bucket_id", entry.bucket_id);
      formatter->dump_string("bucket_owner", entry.bucket_owner);
      formatter->dump_string("bucket", entry.bucket);

      uint64_t agg_time = 0;
      uint64_t agg_bytes_sent = 0;
      uint64_t agg_bytes_received = 0;
      uint64_t total_entries = 0;

      if (show_log_entries)
        formatter->open_array_section("log_entries");

      do {
	uint64_t total_time =  entry.total_time.sec() * 1000000LL * entry.total_time.usec();

        agg_time += total_time;
        agg_bytes_sent += entry.bytes_sent;
        agg_bytes_received += entry.bytes_received;
        total_entries++;

        if (skip_zero_entries && entry.bytes_sent == 0 &&
            entry.bytes_received == 0)
          goto next;

        if (show_log_entries) {
	  formatter->open_object_section("log_entry");
	  formatter->dump_string("bucket", entry.bucket);
	  entry.time.gmtime(formatter->dump_stream("time"));      // UTC
	  entry.time.localtime(formatter->dump_stream("time_local"));
	  formatter->dump_string("remote_addr", entry.remote_addr);
	  if (entry.object_owner.length())
	    formatter->dump_string("object_owner", entry.object_owner);
	  formatter->dump_string("user", entry.user);
	  formatter->dump_string("operation", entry.op);
	  formatter->dump_string("uri", entry.uri);
	  formatter->dump_string("http_status", entry.http_status);
	  formatter->dump_string("error_code", entry.error_code);
	  formatter->dump_int("bytes_sent", entry.bytes_sent);
	  formatter->dump_int("bytes_received", entry.bytes_received);
	  formatter->dump_int("object_size", entry.obj_size);
	  formatter->dump_int("total_time", total_time);
	  formatter->dump_string("user_agent",  entry.user_agent);
	  formatter->dump_string("referrer",  entry.referrer);
	  formatter->close_section();
	  formatter->flush(cout);
        }
next:
	r = store->log_show_next(h, &entry);
      } while (r > 0);

      if (r < 0) {
      	cerr << "error reading log " << oid << ": " << cpp_strerror(-r) << std::endl;
	return -r;
      }
      if (show_log_entries)
        formatter->close_section();

      if (show_log_sum) {
        formatter->open_object_section("log_sum");
	formatter->dump_int("bytes_sent", agg_bytes_sent);
	formatter->dump_int("bytes_received", agg_bytes_received);
	formatter->dump_int("total_time", agg_time);
	formatter->dump_int("total_entries", total_entries);
        formatter->close_section();
      }
      formatter->close_section();
      formatter->flush(cout);
      cout << std::endl;
    }
    if (opt_cmd == OPT_LOG_RM) {
      int r = store->log_remove(oid);
      if (r < 0) {
	cerr << "error removing log " << oid << ": " << cpp_strerror(-r) << std::endl;
	return -r;
      }
    }
  }
  
  if (opt_cmd == OPT_USER_RM) {
    RGWUserBuckets buckets;
    int ret;

    if (rgw_read_user_buckets(user_id, buckets, false) >= 0) {
      map<string, RGWBucketEnt>& m = buckets.get_buckets();

      if (m.size() > 0 && purge_data) {
        for (std::map<string, RGWBucketEnt>::iterator it = m.begin(); it != m.end(); it++) {
          ret = remove_bucket(((*it).second).bucket, true);

          if (ret < 0)
            return ret;
        }
      }

      if (m.size() > 0 && !purge_data) {
        cerr << "ERROR: specify --purge-data to remove a user with a non-empty bucket list" << std::endl;
        return 1;
      }
    }
    rgw_delete_user(info);
  }
  
  if (opt_cmd == OPT_POOL_ADD) {
    if (pool_name.empty()) {
      cerr << "need to specify pool to add!" << std::endl;
      return usage();
    }

    int ret = rgwstore->add_bucket_placement(pool_name);
    if (ret < 0)
      cerr << "failed to add bucket placement: " << cpp_strerror(-ret) << std::endl;
  }

  if (opt_cmd == OPT_POOL_RM) {
    if (pool_name.empty()) {
      cerr << "need to specify pool to remove!" << std::endl;
      return usage();
    }

    int ret = rgwstore->remove_bucket_placement(pool_name);
    if (ret < 0)
      cerr << "failed to remove bucket placement: " << cpp_strerror(-ret) << std::endl;
  }

  if (opt_cmd == OPT_POOLS_LIST) {
    set<string> pools;
    int ret = rgwstore->list_placement_set(pools);
    if (ret < 0) {
      cerr << "could not list placement set: " << cpp_strerror(-ret) << std::endl;
      return ret;
    }
    formatter->reset();
    formatter->open_array_section("pools");
    set<string>::iterator siter;
    for (siter = pools.begin(); siter != pools.end(); ++siter) {
      formatter->open_object_section("pool");
      formatter->dump_string("name",  *siter);
      formatter->close_section();
    }
    formatter->close_section();
    formatter->flush(cout);
    cout << std::endl;
  }

  if (opt_cmd == OPT_BUCKET_STATS) {
    if (bucket_name.empty() && user_id.empty()) {
      cerr << "either bucket or uid needs to be specified" << std::endl;
      return usage();
    }
    formatter->reset();
    if (user_id.empty()) {
      bucket_stats(bucket, formatter);
    } else {
      RGWUserBuckets buckets;
      if (rgw_read_user_buckets(user_id, buckets, false) < 0) {
	cerr << "could not get buckets for uid " << user_id << std::endl;
      } else {
	formatter->open_array_section("buckets");
	map<string, RGWBucketEnt>& m = buckets.get_buckets();
	for (map<string, RGWBucketEnt>::iterator iter = m.begin(); iter != m.end(); ++iter) {
	  RGWBucketEnt obj = iter->second;
	  bucket_stats(obj.bucket, formatter);
	}
	formatter->close_section();
      }
    }
    formatter->flush(cout);
    cout << std::endl;
  }

  if (opt_cmd == OPT_USER_SUSPEND || opt_cmd == OPT_USER_ENABLE) {
    string id;
    __u8 disable = (opt_cmd == OPT_USER_SUSPEND ? 1 : 0);

    if (user_id.empty()) {
      cerr << "uid was not specified" << std::endl;
      return usage();
    }
    RGWUserBuckets buckets;
    if (rgw_read_user_buckets(user_id, buckets, false) < 0) {
      cerr << "could not get buckets for uid " << user_id << std::endl;
    }
    map<string, RGWBucketEnt>& m = buckets.get_buckets();
    map<string, RGWBucketEnt>::iterator iter;

    int ret;
    info.suspended = disable;
    ret = rgw_store_user_info(info, false);
    if (ret < 0) {
      cerr << "ERROR: failed to store user info user=" << user_id << " ret=" << ret << std::endl;
      return 1;
    }
     
    if (disable)
      dout(0) << "disabling user buckets" << dendl;
    else
      dout(0) << "enabling user buckets" << dendl;

    vector<rgw_bucket> bucket_names;
    for (iter = m.begin(); iter != m.end(); ++iter) {
      RGWBucketEnt obj = iter->second;
      bucket_names.push_back(obj.bucket);
    }
    ret = rgwstore->set_buckets_enabled(bucket_names, !disable);
    if (ret < 0) {
      cerr << "ERROR: failed to change pool" << std::endl;
      return 1;
    }
  } 

  if (opt_cmd == OPT_USAGE_SHOW) {
    uint64_t start_epoch = 0;
    uint64_t end_epoch = (uint64_t)-1;

    parse_date(start_date, &start_epoch);
    parse_date(end_date, &end_epoch);

    uint32_t max_entries = 1000;

    bool is_truncated = true;

    RGWUsageIter usage_iter;

    map<rgw_user_bucket, rgw_usage_log_entry> usage;

    formatter->open_object_section("usage");
    if (show_log_entries) {
      formatter->open_array_section("entries");
    }
    string last_owner;
    bool user_section_open = false;
    map<string, rgw_usage_log_entry> summary_map;
    while (is_truncated) {
      int ret = rgwstore->read_usage(user_id, start_epoch, end_epoch, max_entries,
                                     &is_truncated, usage_iter, usage);

      if (ret == -ENOENT) {
        ret = 0;
        is_truncated = false;
      }

      if (ret < 0) {
        cerr << "ERROR: read_usage() returned ret=" << ret << std::endl;
        break;
      }

      map<rgw_user_bucket, rgw_usage_log_entry>::iterator iter;
      for (iter = usage.begin(); iter != usage.end(); ++iter) {
        const rgw_user_bucket& ub = iter->first;
        const rgw_usage_log_entry& entry = iter->second;

        if (show_log_entries) {
          if (ub.user.compare(last_owner) != 0) {
            if (user_section_open) {
              formatter->close_section();
              formatter->close_section();
            }
            formatter->open_object_section("user");
            formatter->dump_string("owner", ub.user);
            formatter->open_array_section("buckets");
            user_section_open = true;
            last_owner = ub.user;
          }
          formatter->open_object_section("bucket");
          formatter->dump_string("bucket", ub.bucket);
          utime_t ut(entry.epoch, 0);
          ut.gmtime(formatter->dump_stream("time"));
          formatter->dump_int("epoch", entry.epoch);
          formatter->dump_int("bytes_sent", entry.bytes_sent);
          formatter->dump_int("bytes_received", entry.bytes_received);
          formatter->dump_int("ops", entry.ops);
          formatter->dump_int("successful_ops", entry.successful_ops);
          formatter->close_section(); // bucket
          formatter->flush(cout);
        }

        summary_map[ub.user].aggregate(entry);
      }
    }
    if (show_log_entries) {
      if (user_section_open) {
        formatter->close_section(); // buckets
        formatter->close_section(); //user
      }
      formatter->close_section(); // entries
    }

    if (show_log_sum) {
      formatter->open_array_section("summary");
      map<string, rgw_usage_log_entry>::iterator siter;
      for (siter = summary_map.begin(); siter != summary_map.end(); ++siter) {
        const rgw_usage_log_entry& entry = siter->second;
        formatter->open_object_section("user");
        formatter->dump_string("user", siter->first);
        formatter->dump_int("bytes_sent", entry.bytes_sent);
        formatter->dump_int("bytes_received", entry.bytes_received);
        formatter->dump_int("ops", entry.ops);
        formatter->dump_int("successful_ops", entry.successful_ops);
        formatter->close_section();
        formatter->flush(cout);
      }

      formatter->close_section(); // summary
    }

    formatter->close_section(); // usage
    formatter->flush(cout);
  }

  if (opt_cmd == OPT_USAGE_TRIM) {
    if (user_id.empty() && !yes_i_really_mean_it) {
      cerr << "usage trim without user specified will remove *all* users data" << std::endl;
      cerr << "do you really mean it? (requires --yes-i-really-mean-it)" << std::endl;
      return 1;
    }
    uint64_t start_epoch = 0;
    uint64_t end_epoch = (uint64_t)-1;

    parse_date(start_date, &start_epoch);
    parse_date(end_date, &end_epoch);

    int ret = rgwstore->trim_usage(user_id, start_epoch, end_epoch);
    if (ret < 0) {
      cerr << "ERROR: read_usage() returned ret=" << ret << std::endl;
      return 1;
    }   
  }

  if (opt_cmd == OPT_OBJECT_RM) {
    int ret = remove_object(bucket, object);

    if (ret < 0) {
      cerr << "ERROR: object remove returned: " << cpp_strerror(-ret) << std::endl;
      return 1;
    }
  }

  if (opt_cmd == OPT_BUCKET_RM) {
    int ret = remove_bucket(bucket, delete_child_objects);

    if (ret < 0) {
      cerr << "ERROR: bucket remove returned: " << cpp_strerror(-ret) << std::endl;
      return 1;
    }
  }

  if (opt_cmd == OPT_GC_LIST) {
    int ret;
    int index = 0;
    string marker;
    bool truncated;
    formatter->open_array_section("entries");

    do {
      list<cls_rgw_gc_obj_info> result;
      ret = rgwstore->list_gc_objs(&index, marker, 1000, result, &truncated);
      if (ret < 0) {
	cerr << "ERROR: failed to list objs: " << cpp_strerror(-ret) << std::endl;
	return 1;
      }


      list<cls_rgw_gc_obj_info>::iterator iter;
      for (iter = result.begin(); iter != result.end(); ++iter) {
	cls_rgw_gc_obj_info& info = *iter;
	formatter->open_object_section("chain_info");
	formatter->dump_string("tag", info.tag);
	formatter->dump_stream("time") << info.time;
	formatter->open_array_section("objs");
        list<cls_rgw_obj>::iterator liter;
	cls_rgw_obj_chain& chain = info.chain;
	for (liter = chain.objs.begin(); liter != chain.objs.end(); ++liter) {
	  cls_rgw_obj& obj = *liter;
	  formatter->dump_string("pool", obj.pool);
	  formatter->dump_string("oid", obj.oid);
	  formatter->dump_string("key", obj.key);
	}
	formatter->close_section(); // objs
	formatter->close_section(); // obj_chain
	formatter->flush(cout);
      }
    } while (truncated);
    formatter->close_section();
    formatter->flush(cout);
  }

  if (opt_cmd == OPT_GC_PROCESS) {
    int ret = rgwstore->process_gc();
    if (ret < 0) {
      cerr << "ERROR: gc processing returned error: " << cpp_strerror(-ret) << std::endl;
      return 1;
    }
  }
  return 0;
}
