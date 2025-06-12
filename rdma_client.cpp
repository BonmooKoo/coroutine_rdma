#include "rdma_common.h"
#include "rdma_verb.h"
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
#define SIZEOFNODE 1024 
//#define SIZEOFNODE 4096 
int* key=new int[TOTALOP];
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

int
test_smallread (int id)
{
  timespec t1,t2;
  printf ("[%d]START\n", id);
  int count=0;
  for (int i = id; i < TOTALOP; i += threadcount)
    {
      clock_gettime(CLOCK_MONOTONIC_RAW, &t1);
      //rdma_read(uint64_t serveraddress, uint32_t datalength,int server,int thread)
      int suc = rdma_read((key[i] % (ALLOCSIZE / SIZEOFNODE)) * SIZEOFNODE,8, 0, id);   //return current value
      clock_gettime(CLOCK_MONOTONIC_RAW, &t2);    
      uint64_t elapsed_ns = (t2.tv_sec - t1.tv_sec) * 1e9 + (t2.tv_nsec - t1.tv_nsec);
      smallread_lat[id][count++]=elapsed_ns;
     }
  printf ("[%d]END\n", id);  
}

int
test_cas (int id)
{
  timespec t1,t2;
  int result;
  int count=0;
  printf ("[%d]START\n", id);
  for (int i = id; i < TOTALOP; i += threadcount)
    {
      clock_gettime(CLOCK_MONOTONIC_RAW, &t1);
      while(true){
      //rdma_CAS(compare,swap,serveradd,datalength,server,threadid)
      int result = rdma_CAS_returnvalue (0, 1, (key[i] % (ALLOCSIZE / SIZEOFNODE)) * SIZEOFNODE, 8, 0, id);
      if (result == 0)
	{
	  //success so unlock it 
          clock_gettime(CLOCK_MONOTONIC_RAW, &t2);    
          uint64_t elapsed_ns = (t2.tv_sec - t1.tv_sec) * 1e9 + (t2.tv_nsec - t1.tv_nsec);
	  cas_try[id][count]++; 
          cas_lat[id][count++]=elapsed_ns;
	  int result = rdma_CAS_returnvalue (1, 0, (key[i] % (ALLOCSIZE / SIZEOFNODE)) * SIZEOFNODE, 8, 0, id);
	  //int a = 0;
          //int suc = rdma_write(&a,(key[i] % (ALLOCSIZE / SIZEOFNODE)) * SIZEOFNODE,8, 0, id);
	  break;
	}
      else
	{
	  //fail
	  cas_try[id][count]++; 
	}
      }
    }
  printf ("[%d]END\n", id);
}

int
test_read (int id)
{
  timespec t1,t2;
  int count=0;
  printf ("[%d]START\n", id);
  for (int i = id; i < TOTALOP; i += threadcount)
    {
      //rdma_read(uint64_t serveraddress, uint32_t datalength,int server,int thread)
      clock_gettime(CLOCK_MONOTONIC_RAW, &t1);
      int suc = rdma_read ((key[i] % (ALLOCSIZE / SIZEOFNODE)) * SIZEOFNODE, SIZEOFNODE, 0, id);	//return current value
      clock_gettime(CLOCK_MONOTONIC_RAW, &t2);    
      uint64_t elapsed_ns = (t2.tv_sec - t1.tv_sec) * 1e9 + (t2.tv_nsec - t1.tv_nsec);
      read_lat[id][count++]=elapsed_ns;
    }
  printf ("[%d]END\n", id);
}
int test_mix(int id)
{
  timespec t1,t2;
  int result;
  int count=0;
  printf ("[%d]START\n", id);
  for (int i = id; i < TOTALOP; i += threadcount)
    {
      clock_gettime(CLOCK_MONOTONIC_RAW, &t1);
      while(true){
      //rdma_CAS(compare,swap,serveradd,datalength,server,threadid)
      int result = rdma_CAS_returnvalue (0, 1, (key[i] % (ALLOCSIZE / SIZEOFNODE)) * SIZEOFNODE, 8, 0, id);
      if (result == 0)
	{
	  //success so unlock it 
          clock_gettime(CLOCK_MONOTONIC_RAW, &t2);    
          uint64_t elapsed_ns = (t2.tv_sec - t1.tv_sec) * 1e9 + (t2.tv_nsec - t1.tv_nsec);
	  cas_try[id][count]++; 
          cas_lat[id][count++]=elapsed_ns;
	  int result = rdma_CAS_returnvalue (1, 0, (key[i] % (ALLOCSIZE / SIZEOFNODE)) * SIZEOFNODE, 8, 0, id);
	  break;
	}
      else
	{
	  //fail
	  cas_try[id][count]++; 
	}
      }
    }
  printf ("[%d]END\n", id);
}


void print_usage(const char* prog_name) {
    printf("Usage: %s -c <client_num> -t <test_type>\n", prog_name);
    printf("  -c <client_num>     : Set client number (e.g., 0, 1, 2...)\n");
    printf("  -t <test_type>      : Select test type:\n");
    printf("                        0: 24 READ\n");
    printf("                        1: 24 READ & 8 READ (small)\n");
    printf("                        2: 24 READ & 8 CAS\n");
    printf("                        3: 32 READ\n");
    printf("If no options are provided, defaults will be used: cs_num=0, reader=1, caser=0, smallreader=0\n");
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
  int reader=1,caser=0,smallreader=0;
  cs_num=0;
  while ((option = getopt (argc, argv, "c:t:")) != -1)
    {
      // alloc dst
      switch (option)
	{
	case 'c':
	  cs_num = atoi (optarg);
	  break;
	case 't'://A:24READ|B : 24READ & 8 READ | C : 24READ & 8 CAS | D : 32READ
	  test = atoi (optarg);
          switch(test){
		case 0 : 
			reader = 24;
			break;
		case 1 : 
			reader = 24;
			smallreader = 8;
			break;
		case 2 : 
			reader = 24;
			caser = 8;
			break;
		case 3 :
			reader = 16;
			caser = 16;
   			break;
		case 4 :
			reader = 8;
			caser =24;
   			break;
		case 5 : 
			reader = 32;
			break;
		case 6 : 
			caser = 32;
			reader = 0;
			smallreader = 0;
			break;
		default : 
			fprintf(stderr, "Invalid test type: %d\n", test);
                	print_usage(argv[0]);
			break;
	  }
	  break;
	default:
	  print_usage(argv[0]);
	  break;
	}
    }
  signal (SIGINT, sigint_handler);
  read_key();
  
  printf("read key end\n");
  threadcount = reader + smallreader + caser;
  printf("reader : %d, small reader : %d, caser : %d\n",reader,smallreader,caser);
  thread threadlist[threadcount];
  //setup RDMA connection
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
  for (int i = reader+smallreader; i < threadcount; i++)
    {
	  threadlist[i] = thread (&test_cas, i);
    }
  for (int i = 0; i < reader; i++)
    {
	  threadlist[i] = thread (&test_read, i);
    }
  for (int i = reader; i < reader+smallreader; i++)
    {
	  threadlist[i] = thread (&test_smallread, i);
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

if (reader != 0) {
    filter_and_analyze(read_lat, "READ", reader);
}

  if (caser != 0) {
    int fail;
    std::vector<int> cas_count;
    filter_and_analyze(cas_lat, "CAS", caser);
    for (int i = 0; i < threadcount; i++) {
        //printf("[%d] Success :  %d, Fail: %d\n", i, cas_success[i], cas_fail[i]);
        for (int j=0;j<TOTALOP/MAXTHREAD;j++){
        	fail += cas_try[i][j];
                if(cas_try[i][j]!=0)
                	cas_count.push_back(cas_try[i][j]);
    	}
    }
    if(!cas_count.empty()){
    std::sort(cas_count.begin(),cas_count.end());
    for(int i=0;i<cas_count.size();i++)
	printf("%d\n",cas_count[i]);
    }
    printf("TOTAL Try : %d\n", fail);
    
}

if (smallreader != 0) {
    filter_and_analyze(smallread_lat, "SmallREAD", smallreader);
}

  client_disconnect_and_clean (threadcount);
  return 0;
}
