#include "jms_coord.hpp"
#include "pipe.hpp"
#include "poll.hpp"
#include <chrono>

using namespace jms;

PoolHandle PoolHandle::make_pool(const store::FileStore &store, PoolId id,
                                 size_t njobs) {
  auto substore = store.substore(sutil::concat<'-'>("pool", id));

  auto fr = substore.create_fifo("in");
  auto fw = substore.create_fifo("out");

  psx::Process proc([&](auto) {
    auto r = fr.into_read(O_CLOEXEC);
    auto w = fw.into_write(O_CLOEXEC);

    psx::DuplexConnection conn(std::move(r), std::move(w));
    int res = pool_main(id, njobs, store, conn);
    std::quick_exit(res);
    return 1;
  });

  auto w = fr.into_write(O_CLOEXEC);
  auto r = fw.into_read(O_CLOEXEC);

  if (fcntl(w.get_fd(), F_SETFL, O_NONBLOCK | O_CLOEXEC) < 0 ||
      fcntl(r.get_fd(), F_SETFL, O_NONBLOCK | O_CLOEXEC) < 0)
    throw psx::ErrnoException(
        sutil::concat("failed to set communication channel with pool",
                      proc.get_pid(), "to non-blocking"));

  return PoolHandle{
      id,
      std::move(proc),
      {std::move(r), std::move(w)},
  };
}

CoordExecutor::CoordExecutor(size_t jobs_per_pool, store::FileStore &store)
    : jobs_per_pool(jobs_per_pool), store(store), last_pool(nullptr),
      next_job{0}, next_pool{0} {}

PoolHandle *CoordExecutor::pool_for_job(JobId id) {
  auto iter = pools.find(id / jobs_per_pool);
  if (iter == pools.end())
    return nullptr;

  return &iter->second;
}

PoolHandle &CoordExecutor::new_pool() {
  PoolId id = next_pool++;

  PoolHandle pool(store, id, this->jobs_per_pool);
  pools.try_emplace(id, std::move(pool));

  PoolHandle &res = pools.at(id);
  last_pool = &res;
  by_pid.try_emplace(res.get_proc().get_pid(), &res);

  return res;
}

psx::Dynamic<std::pair<JobId, pid_t>>
CoordExecutor::spawn_on_last_pool(JobId job_id, std::string path,
                                  std::vector<std::string> args) {
  submission_times.emplace(chr::system_clock::now(), job_id);

  return jms::send_command(
             last_pool->get_conn(),
             sutil::concat("submit", job_id, path, sutil::concat_iter(args)))
      .map([job_id](std::string &&res) {
        std::istringstream is(std::move(res));

        // grab the pid from the response
        pid_t pid;
        is >> pid;

        return std::make_pair(job_id, pid);
      });
}

void CoordExecutor::receive_sigchld() {
  for (siginfo_t si{};
       waitid(P_ALL, -1, &si, WNOHANG | WNOWAIT | WEXITED) == 0 &&
       si.si_signo == SIGCHLD;
       si = {}) {
    pid_t pool_pid = si.si_pid;
    auto it = by_pid.find(pool_pid);

    if (it == by_pid.end()) {
      int exit;
      if (waitpid(pool_pid, &exit, WNOHANG) < 0) {
        std::cerr << "WARN: failed to wait on unknown pool child with pid "
                  << pool_pid << ": " << strerror(errno) << std::endl;
      }

      std::cerr << "WARN: spurious exit of unknown pool child with pid "
                << pool_pid << ". Exit code: " << WEXITSTATUS(exit) << std::endl;
      continue;
    }

    auto pool_id = it->second->pool_id();
    // otherwise it is a known pool, so attempt to terminate it
    terminate_pool(pool_id);
  }
}

size_t CoordExecutor::terminate_pool(PoolId pool_id) {
  {
    // locate the correct pool
    auto it = pools.find(pool_id);
    if (it == pools.end()) {
      std::cerr << "WARN: unknown pool attempted to shutdown" << std::endl;
      return 0;
    }
  }

  // this removes the pool right here
  auto node = pools.extract(pool_id);
  by_pid.extract(node.mapped().get_proc().get_pid());

  // send termination signal
  node.mapped().get_proc().signal(SIGTERM);
  size_t res;
  try {
    // collect number of active jobs (should be 0...)
    auto str = psx::await(ReadString(node.mapped().get_conn().r));
    res = std::stoi(str);
  } catch (const psx::PosixException &e) {
    std::cerr << "WARN: encountered problem while attempting to read "
                 "response from pool "
              << pool_id << ": " << e.what() << std::endl;
    // if we fail to do that, send SIGKILL
    node.mapped().get_proc().signal(SIGKILL);
  }

  // after here we can guarantee that the process will be ready to die or
  // better yet dead. Block and wait for process to exit
  node.mapped().get_proc().wait();

  // set last pool to nullptr if it was removed
  if (&node.mapped() == last_pool)
    last_pool = nullptr;

  return res;
}

psx::Dynamic<std::pair<JobId, pid_t>>
CoordExecutor::spawn_job(std::string path, std::vector<std::string> args) {
  JobId job_id = next_job++;

  if (!last_pool || job_id % jobs_per_pool == 0)
    new_pool();

  return spawn_on_last_pool(job_id, std::move(path), std::move(args));
}
