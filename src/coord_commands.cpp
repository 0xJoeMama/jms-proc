#include <algorithm>

#include "jms_coord.hpp"
#include "poll.hpp"

using namespace jms;
using namespace command::coord;

psx::Dynamic<std::string> ShowActive::execute(CoordExecutor &ex) {
  psx::Join<std::string> active;
  for (auto &[_, pool] : ex.get_pools()) {
    auto &[r, w] = pool.get_proc().get_conn();

    // send message to all pools
    active.push(WriteString("show-active", w).then([&r = r](auto) {
      return ReadString(r);
    }));
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
      auto &[r, w] = iter->second.get_proc().get_conn();

      // send message to all pools
      active.push(WriteString("show-finished", w).then([&r = r](auto) {
        return ReadString(r);
      }));
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
  if (!target_pool) {
    std::ostringstream os{};
    os << "Sent resume signal to job " << job_id;

    return psx::Completed<std::string>(os.str());
  }

  // otherwise we sent a command to the pool on the other and to ask it to do
  // the thing
  auto &[r, w] = target_pool->get_proc().get_conn();
  std::ostringstream os{};
  os << "resume " << job_id;

  // first send the command
  return WriteString(os.str(), w)
      // then receive it
      .then([&r = r](auto) { return ReadString(r); })
      .map([this](auto) {
        std::ostringstream os{};
        os << "Sent resume signal to job " << job_id;

        return os.str();
      }); // finally write a message back to whoever called this
}

psx::Dynamic<std::string> Suspend::execute(CoordExecutor &ex) {
  PoolHandle *target_pool = ex.pool_for_job(job_id);

  // if the pool does not exist, we just make a fake message
  if (!target_pool) {
    std::ostringstream os{};
    os << "Sent suspend signal to job " << job_id;

    return psx::Completed<std::string>(os.str());
  }

  // otherwise we sent a command to the pool on the other and to ask it to do
  // the thing
  auto &[r, w] = target_pool->get_proc().get_conn();
  std::ostringstream os{};
  os << "suspend " << job_id;

  // first send the command
  return WriteString(os.str(), w)
      // then receive it
      .then([&r = r](auto) { return ReadString(r); })
      .map([this](auto) {
        std::ostringstream os{};
        os << "Sent resume signal to job " << job_id;

        return os.str();
      }); // finally write a message back to whoever called this
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
