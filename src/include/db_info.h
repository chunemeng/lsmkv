#pragma once

#include <string>

namespace LSMKV {

  static inline LSMKV::status mk_store_dir(const std::string &dir, const std::string &vlog) {
      if (!utils::dirExists(dir)) {
          utils::_mkdir(dir);
      }
      std::string level_dir = dir + "/" + "level-0";
      size_t size = level_dir.size();
      for (int i = 0; i < 8; ++i) {
          level_dir[size - 1] = std::to_string(i)[0];
          if (!utils::dirExists(level_dir)) {
              utils::_mkdir(level_dir);
          }
      }

      if (!LSMKV::FileExists(vlog)) {
          LSMKV::WritableFile *file;
          LSMKV::NewWritableFile(vlog, &file);
          file->Close();
          delete file;
      }

      return LSMKV::status::OK();
  }

  struct DB_Info {
      DB_Info(const std::string &dbname, const std::string &vlog) : dbname(dbname), vlog(vlog) {
          mk_store_dir(dbname, vlog);
      }

      std::string dbname;
      std::string vlog;
  };


} // namespace LSMKV
