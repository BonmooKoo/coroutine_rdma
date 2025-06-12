#ifndef RDMA_COROUTINE_HPP
#define RDMA_COROUTINE_HPP

#include <coroutine>
#include <deque>
#include <unordered_map>
#include <infiniband/verbs.h>

// 1) 스케줄러
class Scheduler {
  ibv_cq* cq_;
  std::deque<std::coroutine_handle<>> ready_;
  std::unordered_map<uint64_t, std::coroutine_handle<>> waiters_;
public:
  explicit Scheduler(ibv_cq* cq) : cq_(cq) {}
  void schedule(std::coroutine_handle<> h);
  void suspend(uint64_t wr_id, std::coroutine_handle<> h);
  void run();
};

// 2) Awaitable
struct RdmaAwaitable {
  uint64_t wr_id;
  Scheduler& sched;
  bool await_ready() const noexcept;
  void await_suspend(std::coroutine_handle<> h) noexcept;
  void await_resume() const noexcept;
};

// 3) Task 타입
struct Task {
  struct promise_type {
    Task get_return_object();
    std::suspend_never initial_suspend() noexcept;
    std::suspend_never final_suspend() noexcept;
    void return_void() noexcept;
    void unhandled_exception();
  };
  std::coroutine_handle<promise_type> h;
  explicit Task(std::coroutine_handle<promise_type> h);
  ~Task();
};

// 4) Worker 코루틴
Task worker_coroutine(Scheduler& sched,
                      ibv_qp* qp, ibv_mr* mr,
                      int coro_id, int total_ops);

#endif // RDMA_COROUTINE_HPP
