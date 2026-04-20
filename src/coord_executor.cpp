#include "jms_coord.hpp"
#include "pipe.hpp"
#include "poll.hpp"

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

  return res;
}

psx::Dynamic<std::pair<JobId, pid_t>>
CoordExecutor::spawn_on_last_pool(JobId job_id, std::string path,
                                  std::vector<std::string> args) {
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
