#ifndef CEPH_CLS_LOCK_CLIENT_H
#define CEPH_CLS_LOCK_CLIENT_H


#include "include/types.h"
#include "include/rados/librados.hpp"

#include "cls/lock/cls_lock_types.h"


namespace rados {
  namespace cls {
    namespace lock {

      extern void lock(librados::ObjectWriteOperation& rados_op,
                       std::string& name, ClsLockType type,
                       std::string& cookie, std::string& tag,
                       std::string description, utime_t& duration, uint8_t flags);

      extern int lock(librados::IoCtx& ioctx,
                      std::string& oid,
                      std::string& name, ClsLockType type,
                      std::string& cookie, std::string& tag,
                      std::string description, utime_t& duration, uint8_t flags);

      extern void unlock(librados::ObjectWriteOperation& rados_op,
                         std::string& name, std::string& cookie);

      extern int unlock(librados::IoCtx& ioctx, std::string& oid,
                        std::string& name, std::string& cookie);

      extern void break_lock(librados::ObjectWriteOperation& op,
                             std::string& name, std::string& cookie,
                             entity_name_t& locker);

      extern int break_lock(librados::IoCtx& ioctx, std::string& oid,
                            std::string& name, std::string& cookie,
                            entity_name_t& locker);

      extern int list_locks(librados::IoCtx& ioctx, std::string& oid, list<std::string> *locks);
      extern int get_lock_info(librados::IoCtx& ioctx, std::string& oid, std::string& lock,
                               map<locker_id_t, locker_info_t> *lockers,
                               ClsLockType *lock_type,
                               std::string *tag);

      class Lock {
        std::string name;
        std::string cookie;
        std::string tag;
        std::string description;
        utime_t duration;
        uint8_t flags;

      public:

        Lock(const std::string& _n) : name(_n), flags(0) {}

        void set_cookie(const std::string& c) { cookie = c; }
        void set_tag(const std::string& t) { tag = t; }
        void set_description(const std::string& desc) { description = desc; }
        void set_duration(const utime_t& e) { duration = e; }
        void set_renew(bool renew) {
          if (renew) {
            flags |= LOCK_FLAG_RENEW;
          } else {
            flags &= ~LOCK_FLAG_RENEW;
          }
        }

        /* ObjectWriteOperation */
        void lock_exclusive(librados::ObjectWriteOperation& ioctx);
        void lock_shared(librados::ObjectWriteOperation& ioctx);
        void unlock(librados::ObjectWriteOperation& ioctx);
        void break_lock(librados::ObjectWriteOperation& ioctx, entity_name_t& locker);

        /* IoCtx*/
        int lock_exclusive(librados::IoCtx& ioctx, std::string& oid);
        int lock_shared(librados::IoCtx& ioctx, std::string& oid);
        int unlock(librados::IoCtx& ioctx, std::string& oid);
        int break_lock(librados::IoCtx& ioctx, std::string& oid, entity_name_t& locker);
      };

    } // namespace lock
  }  // namespace cls
} // namespace rados

#endif

