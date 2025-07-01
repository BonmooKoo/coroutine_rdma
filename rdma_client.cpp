#include "rdma_common.h"
#include "rdma_verb.h"
#include "rdma_coroutine.hpp"
#include "zipf.hpp"
#include <iostream>
#include <fstream>
#include <cstring>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <cmath>
#include <climits>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <vector>
#include <algorithm>
#include <atomic>
#include <thread>
#include <signal.h>
using namespace std;
#define MAXTHREAD 32
#define TOTALOP 64000000//32M
//#define SIZEOFNODE 4096 
static int* key=new int[TOTALOP];
int cs_num;
int threadcount;
uint64_t read_lat[MAXTHREAD][TOTALOP/MAXTHREAD]={0};
uint64_t smallread_lat[MAXTHREAD][TOTALOP/MAXTHREAD]={0};
uint64_t cas_lat[MAXTHREAD][TOTALOP/MAXTHREAD]={0};
int cas_try[MAXTHREAD][TOTALOP/MAXTHREAD]={0};
static std::atomic<uint64_t> cur_ops{0};
int read_key(){
    const int key_range = 1600000;
    // 1) Zipf
    //*
    printf("Zipf\n");
    ZipfGenerator zipf(key_range, 0.99);
    for (int i = 0; i < TOTALOP; ++i) {
        key[i] = zipf.Next();
    }
    printf("key %d %d %d\n",key[50],key[100],key[20000]);
    //*/
    // 2) Uniform
    /*
    printf("Unif\n");
    std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, key_range - 1);
    for (int i = 0; i < TOTALOP; ++i) {
        key[i] = dist(rng);
    }
    //*/
    /*
    for (int i=0;i<key_range;i++){
	key[i] = i;
    }
    */
    return 0;
}
static std::atomic<uint64_t> g_ops_started{0};
static std::atomic<uint64_t> g_ops_finished{0};
static int get_key(int thread_id){
   uint64_t idx = g_ops_started.fetch_add(1, std::memory_order_relaxed);
   return key[(TOTALOP/threadcount)*thread_id+idx];
//	return key[cur_ops++];
}
void
cleanup_rdma ()
{
  client_disconnect_and_clean (threadcount);
}

void
sigint_handler (int sig)
{
  printf ("\n[INFO] Ctrl+C 감지. 자원 정리 중...\n");
  cleanup_rdma ();
}
void
bind_cpu(int thread_id){
  pthread_t this_thread = pthread_self();
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(thread_id,&cpuset);
  int ret = pthread_setaffinity_np(this_thread, sizeof(cpu_set_t), &cpuset);
  if(ret!=0){
	perror("pthread_setaffinity_np");
  }
}
int
thread_setup (int id)
{
  int ret;
  client_connection (cs_num, threadcount, id);
  return 0;
}

int
test_read (int id)
{
  bind_cpu(id);
  int count=0;
  printf ("[%d]START\n", id);
  while (cur_ops<TOTALOP)
    {
      int suc = rdma_read ((get_key(id) % (ALLOCSIZE / SIZEOFNODE)) * SIZEOFNODE, SIZEOFNODE, 0, id);	//return current value
    }
  printf ("[%d]END\n", id);
}

static 
void thread_worker(int thread_id,int worker_num,std::mutex* my_mtx,std::condition_variable* my_cv)
{
   bind_cpu(thread_id);
  printf("[Thread %d - %d] start op\n",thread_id,worker_num);
  std::mutex& q_mtx = *my_mtx;
  std::condition_variable& q_cv = *my_cv;
  //printf("here?\n");
   while(g_ops_started < TOTALOP){
    printf("[Thread %d - %d] start op\n",thread_id,worker_num);
    rdma_read_nopoll((get_key(thread_id) % (ALLOCSIZE / SIZEOFNODE)) * SIZEOFNODE,8, 0,thread_id,worker_num);//(master의 WQ를 사용) worker_num를 coro_id처럼 사용
    {
      std::unique_lock<std::mutex> lock(q_mtx);
      q_cv.notify_one();//wakeup master
      q_cv.wait(lock); // master가 notify할 때까지 sleep
    }
    //poll 끝났음 do sth 여기선 그냥 nothing
    printf("[Thread %d - %d] wakeup\n",thread_id,worker_num);
    ++g_ops_finished;
  }
}

static void thread_master(int thread_id,int worker_count)
{
  bind_cpu(thread_id);
  std::vector<std::condition_variable*> cv_list;
  std::vector<std::mutex*> mtx_list;
  std::vector<std::thread> worker;
  for (int i = 0; i < worker_count; i++) {
        mtx_list.push_back(new std::mutex());
        cv_list.push_back(new std::condition_variable());
  }
  printf("[Master %d] Make workers \n",thread_id);
  //1)worker thread 생성 ( 바로 실행됨 )
  for (int i = 0; i < worker_count; i++) {
      worker.emplace_back(&thread_worker, thread_id, i, mtx_list[i], cv_list[i]);   
      //worker[i] = thread(&thread_worker,thread_id,i, mtx_list[i], cv_list[i]);
  }
  for (int i=0;i<worker_count;i++){
       std::lock_guard<std::mutex> lock(*mtx_list[i]);
       cv_list[i]->notify_one();
       cv_list[i]->wait(lock);
  }

  //2)poll 수행
  while (g_ops_finished < TOTALOP) {
    //printf("[Master : poll]\n");
    int next_id=poll_coroutine(thread_id); // ret : -1 : failed / 0~ : coro_id
    if(next_id<0){
      continue;
    }
    else{
      {
         std::lock_guard<std::mutex> lock(*mtx_list[next_id]);
         cv_list[next_id]->notify_one();
      }
    }
  }
  for (auto& t : worker) {
    t.join();
  }
}

auto filter_and_analyze = [](uint64_t lat_arr[][TOTALOP / MAXTHREAD], const char* label, int count) {
    std::vector<uint64_t> merged;
    for (int i = 0; i < MAXTHREAD; ++i) {
        for (int j = 0; j < TOTALOP / MAXTHREAD; ++j) {
            if (lat_arr[i][j] != 0)
                merged.push_back(lat_arr[i][j]);
        }
    }

    if (merged.empty()) {
        printf("%s: No latency data collected.\n", label);
        return;
    }

    std::sort(merged.begin(), merged.end());
    size_t idx;

    idx = merged.size() * 0.50;
    printf("%s tail(us): %.2f,", label, merged[idx] / 1000.0);

    idx = merged.size() * 0.99;
    printf("%.2f,", merged[idx] / 1000.0);

    idx = merged.size() * 0.999;
    printf("%.2f\n",merged[idx] / 1000.0);
    
   //print all tail latency
   if (strcmp(label, "CAS") == 0) {
    for(int j=0;j<merged.size();j++){
     printf("%.2f\n",merged[j]/1000.0);
    }
   }
};

int
main (int argc, char **argv)
{
  int option;
  int test;
  int coroutine;
  int reader=1,caser=0,smallreader=0,cs_num=0;
  while ((option = getopt (argc, argv, "n:t:c:")) != -1){
      // alloc dst
    switch (option)
        {
        case 'n':
          cs_num = atoi (optarg);
          break;
        case 't':
          threadcount = atoi (optarg);
          break;
	case 'c':
	  coroutine = atoi (optarg);
	  break;
        default:
          break;
        }
  }
  signal (SIGINT, sigint_handler);
  read_key();
  printf("read key end\n");
  thread threadlist[threadcount];
  printf("InitRDMA %d\n",threadcount);
  for (int i = 0; i < threadcount; i++)
    {
      threadlist[i] = thread (&thread_setup, i);
    }
  for (int i = 0; i < threadcount; i++)
    {
      threadlist[i].join ();
    }
 sleep(10);
  printf ("Start test\n");
  timespec t1, t2;
  clock_gettime (CLOCK_MONOTONIC_RAW, &t1);
  for (int i = 0; i < threadcount; i++)
  {
   //run_coroutine(int thread_id,int coro_cnt,int* key_arr,int threadcount,int total_ops);
   if(coroutine!=0){
	 threadlist[i] = thread (&run_coroutine,i,coroutine,key,threadcount,TOTALOP/threadcount);
   }
   else{
	threadlist[i] = thread (&thread_master,i,20);
	//threadlist[i] = thread(&test_read,i);
   }
  }
  for (int i = 0; i < threadcount; i++)
  {
    threadlist[i].join ();
  }
  clock_gettime (CLOCK_MONOTONIC_RAW, &t2);
  //end time
  printf ("End test\n");
  unsigned long timer =(t2.tv_sec - t1.tv_sec) * 1000000000UL + t2.tv_nsec - t1.tv_nsec;
  printf ("Time : %lu msec\n", timer / 1000);
  //Get Tail latency

  client_disconnect_and_clean (threadcount);
  return 0;
}
