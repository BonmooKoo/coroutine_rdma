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
#include <chrono>
#include <vector>
#include <algorithm>
#include <atomic>
#include <thread>
#include <signal.h>
using namespace std;
#define MAXTHREAD 32
#define TOTALOP 32000000//32M
//#define SIZEOFNODE 4096 
static int* key=new int[TOTALOP];
int cs_num;
int threadcount;
uint64_t read_lat[MAXTHREAD][TOTALOP/MAXTHREAD]={0};
uint64_t smallread_lat[MAXTHREAD][TOTALOP/MAXTHREAD]={0};
uint64_t cas_lat[MAXTHREAD][TOTALOP/MAXTHREAD]={0};
int cas_try[MAXTHREAD][TOTALOP/MAXTHREAD]={0};

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

int
thread_setup (int id)
{
  int ret;
  client_connection (cs_num, threadcount, id);
  return 0;
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
  int reader=1,caser=0,smallreader=0,cs_num=0;
  while ((option = getopt (argc, argv, "c:t:")) != -1){
      // alloc dst
    switch (option)
        {
        case 'c':
          cs_num = atoi (optarg);
          break;
        case 't':
          threadcount = atoi (optarg);
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
  printf ("Start test\n");
  timespec t1, t2;
  clock_gettime (CLOCK_MONOTONIC_RAW, &t1);
  for (int i = 0; i < threadcount; i++)
  {
   //run_coroutine(int thread_id,int coro_cnt,int* key_arr,int threadcount,int total_ops);
    threadlist[i] = thread (&run_coroutine,i,5,key,threadcount,TOTALOP);
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
