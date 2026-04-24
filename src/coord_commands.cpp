#include <algorithm>
#include <numeric>

#include "async_ops.hpp"
#include "except.hpp"
#include "executor.hpp"
#include "jms_coord.hpp"
#include "poll.hpp"
#include "util.hpp"

using namespace jms;
using namespace command::coord;

psx::Dynamic<std::string> ShowActive::execute(CoordExecutor &ex) {
  psx::Join<std::string> active;
  for (auto &[_, pool] : ex.get_pools()) {
    // send message to all pools
    active.push(jms::send_command(pool.get_conn(), pool::mnem::SHOW_ACTIVE)
                    .except<psx::PosixException>([](auto) { return ""; }));
  }

  return active.map([](auto actives) {
    std::ostringstream os{};

    os << "Active jobs:\n";
    for (const auto &i : actives)
      os << i;

    return os.str();
  });
}

namespace {
void imitate_pool_response(CoordExecutor &ex, PoolId id,
                           std::function<void(JobId)> f) {
  size_t njobs = ex.get_pool_size();
  for (JobId j = id * njobs; j < (id + 1) * njobs && j < ex.get_total_jobs();
       j++)
    f(j);
}

auto fake_job(std::ostream &os) {
  return [&](JobId id) { os << "JobID " << id << std::endl; };
}

auto fake_job_status(std::ostream &os, ProcStatus status) {
  return [&os, status](JobId id) {
    os << "JobID " << id << " Status: ";
    switch (status) {
    case ProcStatus::Finished:
      os << "Finished";
      break;
    case ProcStatus::Active:
      os << "Active";
      break;
    case ProcStatus::Suspended:
      os << "Suspended";
      break;
    default:
      todo("unknown job status");
    }

    os << std::endl;
  };
}
} // namespace

psx::Dynamic<std::string> ShowFinished::execute(CoordExecutor &ex) {
  psx::Join<std::string> active;

  for (PoolId i = 0; i < ex.get_next_pool(); i++) {
    auto iter = ex.get_pools().find(i);

    if (iter == ex.get_pools().end()) {
      // not in the map, so the pool is already finished
      std::ostringstream os{};

      imitate_pool_response(ex, i, fake_job(os));
      active.push(psx::Completed(os.str()));
    } else {
      // send message to all pools
      active.push(
          jms::send_command(iter->second.get_conn(), pool::mnem::SHOW_FINISHED)
              .except<psx::PosixException>([pool_id = i, &ex](auto) {
                std::ostringstream os{};
                imitate_pool_response(ex, pool_id, fake_job(os));
                return os.str();
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
  if (!target_pool)
    return psx::Completed<std::string>(
        sutil::concat("Sent resume signal to JobID", job_id));

  return jms::send_command(target_pool->get_conn(),
                           sutil::concat(pool::mnem::RESUME, job_id))
      .except<psx::PosixException>([job_id = this->job_id](auto) {
        return sutil::concat("Sent resume signal to JobID", job_id);
      });
}

psx::Dynamic<std::string> Suspend::execute(CoordExecutor &ex) {
  if (PoolHandle *target_pool = ex.pool_for_job(job_id)) {
    return jms::send_command(target_pool->get_conn(),
                             sutil::concat(pool::mnem::SUSPEND, job_id))
        .except<psx::PosixException>([job_id = this->job_id](auto) {
          return sutil::concat("Sent suspend signal to JobID", job_id);
        });
  }

  // if the pool does not exist, we just make a fake message
  return psx::Completed<std::string>(
      sutil::concat("Sent suspend signal to JobID", job_id));
}

psx::Dynamic<std::string> Submit::execute(CoordExecutor &ex) {
  return ex.spawn_job(std::move(path), std::move(args)).map([](auto pair) {
    auto [job_id, pid] = pair;

    std::ostringstream os{};
    os << "JobID: " << job_id << ", PID: " << pid << std::endl;

    return os.str();
  });
}

psx::Dynamic<std::string> Shutdown::execute(CoordExecutor &ex) {
  psx::Join<size_t> statuses;

  for (auto &[pool, handle] : ex.get_pools()) {
    handle.get_proc().signal(SIGTERM);
    auto curr = ReadString(handle.get_conn().r)
                    .map([](std::string &&res) {
                      try {
                        return size_t{std::stoull(std::move(res))};
                      } catch (...) {
                        return size_t{0};
                      }
                    })
                    .except<psx::PosixException>([pool_id = pool](auto) {
                      std::cerr << "WARN: failed to properly get amount of "
                                   "active jobs for pool "
                                << pool_id << std::endl;

                      return size_t{0};
                    });

    statuses.push(std::move(curr));
  }

  auto res = psx::await(statuses.map([](auto active) {
    return std::accumulate(active.cbegin(), active.cend(), size_t{0});
  }));

  throw ShutdownException{
      res,
      ex.get_total_jobs() - res,
  };
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

namespace {
std::string fake_status_response(CoordExecutor &ex, JobId job_id) {
  std::ostringstream os{};

  if (job_id < ex.get_total_jobs())
    fake_job_status(os, ProcStatus::Finished)(job_id);
  else
    os << "Job not submitted yet\n";

  return os.str();
}
} // namespace

psx::Dynamic<std::string> Status::execute(CoordExecutor &ex) {
  if (PoolHandle *handle = ex.pool_for_job(job_id)) {
    return jms::send_command(handle->get_conn(),
                             sutil::concat(pool::mnem::STATUS, job_id))
        .except<psx::PosixException>([&ex, job_id = job_id](auto) {
          return fake_status_response(ex, job_id);
        });
  }

  return psx::Completed(fake_status_response(ex, job_id));
}

namespace {
void advance_to_end_of_pool(CoordExecutor &ex, PoolId id,
                            std::multimap<Timepoint, JobId>::const_iterator &it,
                            const std::multimap<Timepoint, JobId> &subm) {
  while (it != subm.cend()) {
    PoolId curr_pool = it->second / ex.get_pool_size();
    if (curr_pool != id)
      break;

    it++;
  }
}
} // namespace

psx::Dynamic<std::string> StatusAll::execute(CoordExecutor &ex) {
  psx::Join<std::string> pool_responses;

  auto &subm = ex.get_submission_times();
  for (auto it = subm.lower_bound(since); it != subm.cend();) {
    const auto &[_, job_id] = *it;

    if (PoolHandle *pool = ex.pool_for_job(job_id)) {
      PoolId pool_id = pool->pool_id();

      advance_to_end_of_pool(ex, pool_id, it, subm);
      pool_responses.push(
          jms::send_command(pool->get_conn(),
                            sutil::concat(pool::mnem::STATUS_AFTER, job_id))
              .except<psx::PosixException>([&ex, pool_id](const auto &) {
                std::ostringstream os{};

                // this pool has already exited
                imitate_pool_response(
                    ex, pool_id, fake_job_status(os, ProcStatus::Finished));
                return os.str();
              }));
    } else {
      PoolId pool_id = job_id / ex.get_pool_size();
      std::ostringstream os{};

      // this pool has already exited
      imitate_pool_response(ex, pool_id,
                            fake_job_status(os, ProcStatus::Finished));

      advance_to_end_of_pool(ex, pool_id, it, subm);
      pool_responses.push(psx::Completed(os.str()));
    }
  }

  return pool_responses.map([](auto &&responses) {
    std::ostringstream os{};
    for (const auto &res : responses)
      os << res;

    return os.str();
  });
}
