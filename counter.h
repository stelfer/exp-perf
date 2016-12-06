// Copyright (C) 2016 by Soren Telfer - MIT License. See LICENSE.txt

#ifndef _EXP_PERF_COUNTER_H
#define _EXP_PERF_COUNTER_H

#include <asm/unistd.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <system_error>
#include <vector>

namespace exp_perf {
//
// Wraps results to a perf_event_counter syscall
//
class counter
{
  public:
    using counts_t = std::vector<long long>;

    // Constructor is called with lists of PERF_COUNT* events
    // see linux/perf_event.h for the list
    //
    // For example:
    //
    // counter c({PERF_COUNT_SW_TASK_CLOCK, PERF_COUNT_SW_CPU_CLOCK},
    //   {PERF_COUNT_HW_INSTRUCTIONS, PERF_COUNT_HW_REF_CPU_CYCLES});
    //
    // At runtime this will hold an array of dimension of the number of these
    // types that were successfully opened. For portabilityt we try to open
    // whatever is requested on every system. but not all systems support all
    // counters, especially HW/SW counters.
    counter(std::initializer_list<int> sw_evts,
            std::initializer_list<int> hw_evts);

    ~counter();

    // Get the collected counts as a vector
    const counts_t& get_counts() const;

    // Get the status of each counter
    bool get_status(int i) const;

    // Start and stop the counter, can be called many times after initialization
    void start();
    void stop();

    // get the dimension of the counts vector
    size_t get_counts_size() const;

  private:
    // Initialize an event
    void init_event(int type, int config);

    // Wrapper around the syscall
    static long perf_event_open(struct perf_event_attr* hw_event,
                                pid_t pid,
                                int cpu,
                                int group_fd,
                                unsigned long flags);

    std::vector<perf_event_attr> m_events;
    std::vector<int> m_fds;
    counts_t m_counts;
};

inline counter::counter(std::initializer_list<int> sw_evts,
                        std::initializer_list<int> hw_evts)
{
    for (auto x : sw_evts) {
        init_event(PERF_TYPE_SOFTWARE, x);
    }
    for (auto x : hw_evts) {
        init_event(PERF_TYPE_HARDWARE, x);
    }
}

inline counter::~counter()
{
    // we only have to close the group fd
    ::close(m_fds[0]);
}

inline const counter::counts_t&
counter::get_counts() const
{
    return m_counts;
}

inline bool
counter::get_status(int i) const
{
    if (i > 0 && i < m_fds.size()) {
        return m_fds[i] > -1;
    }
    return false;
}

inline void
counter::start()
{
    for (int i = 0; i < m_fds.size(); ++i) {
        if (m_fds[i] > -1) {
            if (ioctl(m_fds[i], PERF_EVENT_IOC_RESET, 0) < 0) {
                throw std::system_error(errno, std::system_category());
            }
            if (ioctl(m_fds[i], PERF_EVENT_IOC_ENABLE, 0) < 0) {
                throw std::system_error(errno, std::system_category());
            }
        }
        m_counts[i] = 0;
    }
}

inline void
counter::stop()
{
    for (int i = 0; i < m_fds.size(); ++i) {
        if (m_fds[i] > -1) {
            if (ioctl(m_fds[i], PERF_EVENT_IOC_DISABLE, 0) < 0) {
                throw std::system_error(errno, std::system_category());
            }
            if (::read(m_fds[i], &m_counts[i], sizeof(long long)) < 0) {
                throw std::system_error(errno, std::system_category());
            }
        }
    }
}

inline size_t
counter::get_counts_size() const
{
    return m_counts.size();
}

inline void
counter::init_event(int type, int config)
{
    perf_event_attr pe;
    memset(&pe, 0, sizeof(struct perf_event_attr));

    pe.type           = type;
    pe.size           = sizeof(struct perf_event_attr);
    pe.config         = config;
    pe.disabled       = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv     = 0;

    const int group = m_fds.size() == 0 ? -1 : m_fds[0];
    int fd          = perf_event_open(&pe, 0, -1, group, 0);
    if (fd > -1) {
        m_events.emplace_back(pe);
        m_fds.push_back(fd);
        m_counts.push_back(0);
    }
}

inline long
counter::perf_event_open(struct perf_event_attr* hw_event,
                         pid_t pid,
                         int cpu,
                         int group_fd,
                         unsigned long flags)
{
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}
}

#endif //_EXP_PERF_COUNTER_H
