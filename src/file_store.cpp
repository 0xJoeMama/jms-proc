#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fts.h>
#include <iostream>
#include <sstream>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

#include "except.hpp"
#include "file_store.hpp"

using namespace store;
using namespace psx;

namespace {
struct Fts {
  FTS *fts;

  Fts(const char *root) {
    char *roots[] = {const_cast<char *>(root), nullptr};
    fts = fts_open(roots, FTS_PHYSICAL, nullptr);

    if (!fts)
      throw psx::PosixException("failed to create file tree traversal handle: ");
  }

  FTSENT *operator++(int) {
    FTSENT *res = fts_read(fts);

    if (!res && errno)
      throw psx::PosixException("failed to get next file tree traversal object: ");

    return res;
  }

  ~Fts() noexcept {
    if (fts_close(fts) < 0)
      std::cerr << "WARN: failed to close file tree traversal handle: "
                << strerror(errno) << std::endl;
  }
};

class AlreadyThere {};

DIR *mk_open_dir(const char *path) {
  DIR *dir = opendir(path);
  if (dir) {
    closedir(dir);
    throw AlreadyThere();
  }

  int res = mkdir(path, 0744);

  if (res < 0 && errno == EEXIST)
    throw AlreadyThere();
  else if (res < 0)
    throw psx::PosixException("failed to create directory");

  dir = opendir(path);

  if (!dir)
    throw psx::PosixException("failed to open directory");

  return dir;
}
} // namespace

Dir::Dir(std::string_view root) : root(root) {
  try {
    dir = mk_open_dir(this->root.c_str());
  } catch (const AlreadyThere &) {
    // dir exists here, thus we need to its contents
    Fts fts(this->root.c_str());

    FTSENT *ent;

    while ((ent = fts++)) {
      switch (ent->fts_info) {
      case FTS_DP:
        if (rmdir(ent->fts_accpath) < 0) {
          std::ostringstream os{};
          os << "failed to remove directory '" << ent->fts_accpath << '\''
             << ": ";

          throw psx::PosixException(os.str());
        }

        break;
      case FTS_F:
      case FTS_SL:
      case FTS_SLNONE:
      case FTS_DEFAULT:
      case FTS_NSOK:
        if (unlink(ent->fts_accpath) < 0) {
          std::ostringstream os{};
          os << "failed to unlink path '" << ent->fts_accpath << '\'' << ": ";

          throw psx::PosixException(os.str());
        }

        break;
      case FTS_DNR:
      case FTS_ERR:
      case FTS_NS: {
        std::ostringstream os{};
        os << "invalid file tree entry '" << ent->fts_accpath << "'" << ": ";

        throw psx::PosixException(os.str());
      } break;
      default:
        break;
      };
    }

    dir = mk_open_dir(this->root.c_str());
  }
}

Dir::~Dir() noexcept {
  if (closedir(dir) < 0)
    std::cerr << strerror(errno) << std::endl;
}

FileStore::FileStore(std::string_view root_path) : root_dir(root_path) {}

Fifo FileStore::create_fifo(std::string_view subpath) const {
  std::string path = root_dir.get_root_path();

  path.push_back('/');
  path.append(subpath);

  return Fifo(std::move(path));
}

FileStore FileStore::substore(std::string_view name) const {
  std::ostringstream os{};
  os << this->root_dir.get_root_path() << "/" << name;
  return FileStore(os.str());
}
