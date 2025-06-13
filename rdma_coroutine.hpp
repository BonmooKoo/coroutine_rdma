#include "rdma_verb.h"
#include "rdma_common.h"
#include <boost/coroutine/symmetric_coroutine.hpp>
#include <vector>
#include <functional>
#include <atomic>
#include <infiniband/verbs.h>


int get_key();
void coro_worker(CoroYield &yield,
                        RequestGen gen,
                        int coro_id);

 void coro_master(CoroYield &yield,
                        int coro_cnt,
                        std::vector<CoroCall> &worker);
void run_coroutine(int thread_id,
                          int coro_cnt,
                          int* key_arr,
                          int threadcount,
                          int total_ops
                        );
