#include <algorithm>

#include "async_ops.hpp"
#include "jms_coord.hpp"
#include "poll.hpp"

using namespace jms;
using namespace command::coord;

psx::Dynamic<std::string> ShowActive::execute(CoordExecutor &ex) {
  psx::Join<std::string> active;
  for (auto &[_, pool] : ex.get_pools()) {
    // send message to all pools
    active.push(jms::send_command(pool.get_conn(), "show-active"));
  }

  return active.map([](auto &&actives) {
    std::ostringstream os{};

    os << "Active jobs:\n";
    for (const auto &i : actives)
      os << i;

    return os.str();
  });
}

psx::Dynamic<std::string> ShowFinished::execute(CoordExecutor &ex) {
  psx::Join<std::string> active;

  for (PoolId i = 0; i < ex.get_next_pool(); i++) {
    auto iter = ex.get_pools().find(i);

    if (iter == ex.get_pools().end()) {
      // not in the map, so the pool is already finished
      std::ostringstream os{};

      for (JobId j = i * ex.get_pool_size(); j < ex.get_pool_size(); j++)
        os << "JobID " << i << std::endl;

      active.push(psx::Completed(os.str()));
    } else {
      // send message to all pools
      active.push(jms::send_command(iter->second.get_conn(), "show-finished"));
    }
  }

  return active.map([](auto &&actives) {
    std::ostringstream os{};

    os << "Finished jobs:\n";
    for (const auto &i : actives)
      os << i;

    return os.str();
  });
}
psx::Dynamic<std::string> Resume::execute(CoordExecutor &ex) {
  PoolHandle *target_pool = ex.pool_for_job(job_id);

  // if the pool does not exist, we just make a fake message
  if (!target_pool)
    return psx::Completed<std::string>(
        sutil::concat("Sent resume signal to JobID", job_id));

  return jms::send_command(target_pool->get_conn(),
                           sutil::concat("resume", job_id))
      .map([this](auto) {
        return sutil::concat("Sent resume signal to JobID", job_id);
      });
}

psx::Dynamic<std::string> Suspend::execute(CoordExecutor &ex) {
  PoolHandle *target_pool = ex.pool_for_job(job_id);

  // if the pool does not exist, we just make a fake message
  if (!target_pool)
    return psx::Completed<std::string>(
        sutil::concat("Sent suspend signal to JobID", job_id));

  return jms::send_command(target_pool->get_conn(),
                           sutil::concat("suspend", job_id))
      .map([this](auto) {
        return sutil::concat("Sent suspend signal to JobID", job_id);
      });
}

psx::Dynamic<std::string> Submit::execute(CoordExecutor &ex) {
  return ex.spawn_job(std::move(path), std::move(args)).map([](auto pair) {
    auto [job_id, pid] = pair;

    std::ostringstream os{};
    os << "JobID: " << job_id << ", PID: " << pid << std::endl;

    return os.str();
  });
}

psx::Dynamic<std::string> Shutdown::execute(CoordExecutor &) {
  throw ShutdownException();
}

psx::Dynamic<std::string> ShowPools::execute(CoordExecutor &ex) {
  psx::Join<std::pair<pid_t, size_t>> pool_cnts;

  for (auto &[pool_id, pool] : ex.get_pools()) {
    pool_cnts.push(pool.get_proc_count().map([&pool = pool](auto &&it) {
      return std::make_pair<pid_t, size_t>(pool.get_proc().get_pid(),
                                           std::move(it));
    }));
  }

  return pool_cnts.map([](auto &&pairs) {
    std::ostringstream os{};
    os << "Pools & NumOfJobs:" << std::endl;

    for (const auto &[pid, cnt] : pairs)
      os << pid << " " << cnt << std::endl;

    return os.str();
  });
}

psx::Dynamic<std::string> Status::execute(CoordExecutor &ex) {
  PoolHandle *handle = ex.pool_for_job(job_id);
  if (handle)
    return jms::send_command(handle->get_conn(),
                             sutil::concat("status", job_id));

  if (job_id / ex.get_pool_size() < ex.get_next_pool()) {
    return psx::Completed<std::string>(
        sutil::concat("JobID", job_id, "Status:", "Finished"));
  } else {
    return psx::Completed<std::string>("Id does not match any submitted job");
  }
}
