#include "rdma_verb.h"
#include "rdma_common.h"
#include <boost/coroutine/symmetric_coroutine.hpp>
#include <vector>
#include <functional>
#include <atomic>
#include <infiniband/verbs.h>

void run_coroutine(int thread_id,
                          int coro_cnt,
                          int* key_arr,
                          int threadcount,
                          int total_ops
                        );
