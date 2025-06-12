#pragma once
#include "rdma_verb.h"
#include <boost/coroutine2/symmetric_coroutine.hpp>
#include <vector>
#include <functional>
#include <atomic>
#include <infiniband/verbs.h>

// 1) Boost.Coroutine2 타입 정의
using coro_t    = boost::coroutines2::symmetric_coroutine<void>;
using CoroCall  = coro_t::call_type;
using CoroYield = coro_t::yield_type;

int* key;

// 전역 카운터들
static uint64_t g_total_ops   = 0;
static std::atomic<uint64_t> g_ops_started{0};
static std::atomic<uint64_t> g_ops_finished{0};

// 2) Worker 코루틴 본체
static void coro_worker(CoroYield &yield,
                        RequestGen gen,
                        int coro_id,
                        bool lock_bench)
{
  // 예시: RequestGen 인터페이스에 `next()` 가 있다고 가정
  // auto r = reinterpret_cast<YourGenType*>(gen)->next();

  while (g_ops_started < g_total_ops) {
    // 1) next request
    // auto req = reinterpret_cast<YourGenType*>(gen)->next();
    ++g_ops_started;

    // 2) RDMA post (pseudo code)
    // uint64_t wr_id = post_send(req);
    uint64_t wr_id = coro_id * 100000 + g_ops_started; // 예시 wr_id

    // 3) 완료 대기: master 로 제어권 넘기기
    yield();

    // (resume 후) 후처리
    ++g_ops_finished;
  }

  // 루프 종료 후에도 master에게 복귀
  yield();
}

// 3) Master 코루틴 본체
static void coro_master(CoroYield &yield,
                        int coro_cnt,
                        std::vector<CoroCall> &worker)
{
  // 3-1) 초기 실행: 각 워커를 한 번 깨워서 시작하게 만듦
  for (int i = 0; i < coro_cnt; ++i) {
    yield(worker[i]);
  }

  // 3-2) 이벤트 루프: CQ 폴링 대신 here pseudo‐poll
  while (g_ops_finished < g_total_ops) {
    // 실제 코드: PollRdmaCqOnce(wr_id)
    // wr_id 를 받아서, 어떤 워커인지 mapping 후 resume
    int next_id = g_ops_finished % coro_cnt; // 예시 순환
    yield(worker[next_id]);
  }
}
// run_coroutine(int thread_id,int coro_cnt, int* key[],int threadcount)
// 4) run_coroutine 함수: Tree::run_coroutine 과 동일한 형태
static void run_coroutine(int id,
                          int coro_cnt,
                          int* key_arr,
                          int threadcount)
{
//0.key
  key=key_arr;
  //1. coroutine vector 생성
  std::vector<CoroCall> worker(coro_cnt);

  //2. Client 생성
  for (int i = 0; i < coro_cnt; ++i) {
    worker[i] = CoroCall(std::bind(&coro_worker, _1, gen, i, lock_bench));
  }

  // 4-2) 마스터 코루틴 생성
  CoroCall master = CoroCall(
    std::bind(&coro_master, _1, coro_cnt, std::ref(worker))
  );

  // 4-3) 최초 진입
  master();
}
