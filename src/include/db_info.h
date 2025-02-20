#pragma once

#include <string>

namespace LSMKV {

  static inline LSMKV::Status InitDBDir(const std::string &dir, const std::string &vlog) {
      if (!utils::dirExists(dir)) {
          utils::_mkdir(dir);
      }

      return LSMKV::Status::OK();
  }

  struct DB_Info {
      DB_Info(const std::string &dbname, const std::string &vlog) : dbname(dbname), vlog(vlog) {
          InitDBDir(dbname, vlog);
      }

      std::string dbname;
      std::string vlog;
  };


} // namespace LSMKV
