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
  Dir(std::string_view root, bool delete_if_exists = false);
  ~Dir() noexcept;

  const std::string &get_root_path() const { return root; }

  int get_dirfd() const { return dirfd(dir); }
};

class FileStore {
  const Dir root_dir;

  FileStore(bool delete_if_exists, std::string_view root_path);

public:
  explicit FileStore(std::string_view root_path);
  psx::Fifo create_fifo(std::string_view name) const;
  FileStore substore(std::string_view name) const;
};
} // namespace store
