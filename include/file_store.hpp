#pragma once

#include <dirent.h>
#include <string>
#include <string_view>

#include "pipe.hpp"

namespace store {
class Dir {
  std::string root;
  DIR *dir;

public:
  Dir(std::string_view root);
  ~Dir() noexcept;

  const std::string &get_root_path() const { return root; }

  int get_dirfd() const { return dirfd(dir); }
};

class FileStore {
  Dir root_dir;

public:
  FileStore(std::string_view root_path);
  pipes::Fifo create_fifo(std::string_view name);
};
} // namespace store
