#include <coroutine>
#include <thread>
#include <vector>
#include <atomic>
#include <infiniband/verbs.h>
#include <sched.h>      // CPU_SET, sched_setaffinity
#include "zipf.hpp"     // ZipfGenerator, TOTALOP, key[]

/* 1) Scheduler 정의 (이전 예제와 같음) */
class Scheduler {
  ibv_cq* cq_;
  std::deque<std::coroutine_handle<>> ready_;
  std::unordered_map<uint64_t, std::coroutine_handle<>> waiters_;
public:
  Scheduler(ibv_cq* cq): cq_(cq) {}
  void schedule(std::coroutine_handle<> h) { ready_.push_back(h); }
  void suspend(uint64_t wr_id, std::coroutine_handle<> h) {
    waiters_[wr_id] = h;
  }
  void run() {
    // 1) 초기 ready 실행
    while (!ready_.empty()) {
      ready_.front().resume();
      ready_.pop_front();
    }
    // 2) CQ 폴링 루프
    while (!waiters_.empty()) {
      ibv_wc wc;
      if (ibv_poll_cq(cq_, 1, &wc) > 0) {
        auto it = waiters_.find(wc.wr_id);
        if (it != waiters_.end()) {
          schedule(it->second);
          waiters_.erase(it);
        }
      }
      while (!ready_.empty()) {
        ready_.front().resume();
        ready_.pop_front();
      }
    }
  }
};

/* 2) Awaitable: post_send 후 suspend */
struct RdmaAwaitable {
  uint64_t wr_id;
  Scheduler& sched;
  bool await_ready() const noexcept { return false; }
  void await_suspend(std::coroutine_handle<> h) noexcept {
    sched.suspend(wr_id, h);
  }
  void await_resume() const noexcept {}
};

/* 3) Task/Promise 정의 */
struct Task {
  struct promise_type {
    Task get_return_object() {
      return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_never final_suspend()   noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() { std::terminate(); }
  };
  std::coroutine_handle<promise_type> h;
  Task(std::coroutine_handle<promise_type> h_) : h(h_) {}
  ~Task() { if (h) h.destroy(); }
};

/* 4) Worker 코루틴: read_key()로 받은 key 배열 순회하며 CAS & read */
Task worker_coroutine(int thread_id, Scheduler& sched,int coro_id, int total_ops) {
  uint64_t wr_id = coro_id;
  for (int i = 0; i < TOTALOP; i+=20) {
    // 1) read 요청
    int suc=rdma_read_nopoll((key[i] % (ALLOCSIZE / SIZEOFNODE)) * SIZEOFNODE, SIZEOFNODE, 0, id);
    // 2) 메인에 완료 대기 등록
    co_await RdmaAwaitable{wr_id, sched};
    // 3) 완료된 후, 필요시 응답 처리
    //기존에는 여기서 read sth
  }
}

/* 5) 스레드 함수: CPU 바인딩 후 read_key, Scheduler/코루틴 생성 */
void thread_main(int cpu_id,int thread_id) {
  // CPU affinity
  cpu_set_t cpus;
  CPU_ZERO(&cpus);
  CPU_SET(cpu_id, &cpus);
  sched_setaffinity(0, sizeof(cpus), &cpus);

  // Workload 키 생성
  read_key();  // key[] 배열 채움

  Scheduler sched(cq);
  const int coro_cnt = 20;           // 예시
  const int total_ops = TOTALOP/coro_cnt;

  // Worker 코루틴들 스케줄 등록
  for (int id = 0; id < coro_cnt; ++id) {
    auto t = worker_coroutine(thread_id, sched, coro_id, total_ops);
    sched.schedule(t.h);
  }

  // 이벤트 루프 실행 (메인 코루틴 역할)
  sched.run();
}

// int main(int argc, char** argv) {
//   // RDMA 초기화: ctx, pd, qp, cq, mr 세팅...
//   ibv_context* ctx = /* ... */;
//   ibv_pd* pd = /* ... */;
//   ibv_qp* qp = /* ... */;
//   ibv_cq* cq = /* ... */;
//   ibv_mr* mr = /* ... */;

//   // 1개 스레드 생성
//   std::thread worker_thr(int cpu_id,int thread_id);
//   worker_thr.join();
//   return 0;
// }
