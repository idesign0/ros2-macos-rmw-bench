// Cross-process wake latency: futex-style wait on a 32-bit word in POSIX shm (os_sync_wait_on_address, SHARED).
#include <os/os_sync_wait_on_address.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <sys/resource.h>
typedef struct {_Atomic uint32_t seq; _Atomic uint64_t t_ns;} shm_t;
static uint64_t now(){return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);}
static int cmp(const void*a,const void*b){uint64_t x=*(uint64_t*)a,y=*(uint64_t*)b;return x<y?-1:x>y;}
int main(){
  const char*name="/oswait_bench"; shm_unlink(name); int fd=shm_open(name,O_CREAT|O_RDWR,0600); ftruncate(fd,4096);
  shm_t*s=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0); s->seq=0; enum{N=4000};
  pid_t p=fork();
  if(p==0){ // reader: separate process, blocks in the kernel until the word changes
    static uint64_t lat[N]; uint32_t seen=0;
    for(int i=0;i<N;i++){
      while(atomic_load(&s->seq)==seen) os_sync_wait_on_address(&s->seq,seen,4,OS_SYNC_WAIT_ON_ADDRESS_SHARED);
      seen=atomic_load(&s->seq); lat[i]=now()-atomic_load(&s->t_ns);}
    qsort(lat,N,8,cmp); struct rusage ru; getrusage(RUSAGE_SELF,&ru);
    printf("os_sync_wait_on_address(SHARED), 2 processes, %d wakes at 2 kHz: latency p50=%.1f us p99=%.1f us max=%.1f us, reader CPU=%.3f s over %.1f s\n",
      N,lat[N/2]/1e3,lat[N*99/100]/1e3,lat[N-1]/1e3,ru.ru_utime.tv_sec+ru.ru_utime.tv_usec/1e6+ru.ru_stime.tv_sec+ru.ru_stime.tv_usec/1e6,N/2000.0); fflush(stdout); _exit(0);}
  usleep(100000);
  for(int i=0;i<N;i++){usleep(500); atomic_store(&s->t_ns,now()); atomic_fetch_add(&s->seq,1); os_sync_wake_by_address_any(&s->seq,4,OS_SYNC_WAKE_BY_ADDRESS_SHARED);}
  waitpid(p,0,0); shm_unlink(name); return 0;}
