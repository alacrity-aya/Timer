#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <utility>

struct TimerNodeBase {
    time_t expire;
    uint64_t id;
};

struct TimerNode : public TimerNodeBase {
    using CallBack = std::function<void(const TimerNode& node)>;
    CallBack func;

    explicit TimerNode(uint64_t id, time_t expire, CallBack func)
        : TimerNodeBase { expire, id }
        , func { std::move(func) }
    {
        this->expire = expire;
        this->id = id;
    }
    int add(int aaa, int bbbb)
    {
    }
};

bool operator<(const TimerNodeBase& lhd, const TimerNodeBase& rhd)
{
    if (lhd.expire < rhd.expire) {
        return true;
    } else if (lhd.expire > rhd.expire) {
        return false;
    } else {
        return lhd.id < rhd.id;
    }
}

class Timer {
public:
    static inline time_t get_tick()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    TimerNodeBase add_timer(int msec, TimerNode::CallBack func)
    {
        time_t expire = get_tick() + msec;
        if (_timeouts.empty() || expire <= _timeouts.crbegin()->expire) {
            auto pair = _timeouts.emplace(
                gen_id(), expire, func);
            return static_cast<TimerNodeBase>(*pair.first);
        }
        auto ele = _timeouts.emplace_hint(_timeouts.crbegin().base(), gen_id(), expire, func);
        // reserve-itertor.base() -> iteator
        return static_cast<TimerNodeBase>(*ele);
    }

    void del_timer(TimerNodeBase& node)
    {
        auto iter = _timeouts.find(node);
        if (iter != _timeouts.end()) {
            _timeouts.erase(iter);
        }
    }

    void handle_timer(time_t now)
    {
        // 如果有线程阻塞了，这就处理不了了把
        auto iter = _timeouts.begin();
        while (iter != _timeouts.end() && iter->expire <= now) {
            iter->func(*iter);
            iter = _timeouts.erase(iter);
        }
        // 为什么要用while：可能有多个定时任务到期了
    }

    virtual void update_timerfd(const int fd)
    {
        std::timespec abstime {};
        auto iter = _timeouts.begin();
        if (iter != _timeouts.end()) {
            abstime.tv_sec = iter->expire / 1000;
            abstime.tv_nsec = (iter->expire & 1000) * 1000000;
        } else {
            abstime.tv_sec = 0;
            abstime.tv_nsec = 0;
        }
        struct itimerspec its = {
            {},
            abstime
        };
        timerfd_settime(fd, TFD_TIMER_ABSTIME, &its, nullptr);
    }

private:
    static inline uint64_t gen_id()
    {
        return gid++;
    }

    std::set<TimerNode, std::less<>> _timeouts;
    static uint64_t gid;
};
uint64_t Timer::gid = 0;

int main(int argc, char* argv[])
{

    using namespace std;

    int epfd = epoll_create(1);
    int timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
    struct epoll_event event {
        EPOLLIN | EPOLLET
    };
    epoll_ctl(epfd, EPOLL_CTL_ADD, timerfd, &event);

    unique_ptr<Timer> timer = std::make_unique<Timer>();

    int i = 0;
    timer->add_timer(1000, [&](const TimerNode& node) {
        cout << Timer::get_tick() << " node id:" << node.id << " revoked times:" << ++i << endl;
    });

    timer->add_timer(2000, [&](const TimerNode& node) {
        cout << Timer::get_tick() << " node id:" << node.id << " revoked times:" << ++i << endl;
    });

    timer->add_timer(3000, [&](const TimerNode& node) {
        cout << Timer::get_tick() << " node id:" << node.id << " revoked times:" << ++i << endl;
    });

    cout << "now time: " << Timer::get_tick() << endl;
    struct epoll_event evs[64] = { 0 };
    while (true) {
        timer->update_timerfd(timerfd);
        int nready = epoll_wait(epfd, evs, 64, -1);
        time_t now = Timer::get_tick();

        for (int i = 0; i < nready; i++) {
            // if (evs[i].events & EPOLLIN) {
            //     cout << evs[i].data.fd << " is ready\n";
            // }
        }
        timer->handle_timer(now);
    }
    epoll_ctl(epfd, EPOLL_CTL_DEL, timerfd, &event);
    close(timerfd);
    close(epfd);

    return 0;
}
