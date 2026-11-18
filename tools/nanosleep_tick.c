#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>
static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e3+t.tv_nsec/1e6;}
int main(){long tck=sysconf(_SC_CLK_TCK);unsigned long tick_ns=999999999ul/tck+1ul;struct timespec r={0,tick_ns/2};
 double s=0,mx=0;int n=400;for(int i=0;i<n;i++){double a=now();nanosleep(&r,0);double d=now()-a;s+=d;if(d>mx)mx=d;}
 printf("_SC_CLK_TCK=%ld -> boost tick=%.1f ms, thread_sleep_tick() requests %.1f ms, actual nanosleep mean=%.2f ms max=%.2f ms\n",tck,tick_ns/1e6,tick_ns/2e6,s/n,mx);}
