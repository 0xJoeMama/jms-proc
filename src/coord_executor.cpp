#include "jms_coord.hpp"
#include "poll.hpp"

using namespace jms;

PoolHandle *CoordExecutor::pool_for_job(JobId id) {
  auto iter = pools.find(id / jobs_per_pool);
  if (iter == pools.end())
    return nullptr;

  return &iter->second;
}

PoolHandle &CoordExecutor::new_pool() {
  PoolId id = next_pool++;

  PoolHandle pool(store, id);
  pools.try_emplace(id, std::move(pool));

  PoolHandle &res = pools.at(id);
  last_pool = &res;

  return res;
}

psx::Dynamic<std::pair<JobId, pid_t>>
CoordExecutor::spawn_on_last_pool(JobId job_id, std::string path,
                                  std::vector<std::string> args) {
  return jms::send_command(
             last_pool->get_proc().get_conn(),
             sutil::concat("submit", job_id, path, sutil::concat_iter(args)))
      .map([job_id](std::string &&res) {
        std::istringstream is(std::move(res));

        // grab the pid from the response
        pid_t pid;
        is >> pid;

        return std::make_pair(job_id, pid);
      });
}
