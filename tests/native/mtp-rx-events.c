/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#include "y2_rx.h"

static volatile int cancel, stop;
static int received;
static long switches;
static void interrupted(int sig) { (void)sig; }
static void pause_ms(long ms)
{
 struct timespec delay={ms/1000,(ms%1000)*1000000};nanosleep(&delay,NULL);
}
static void join_bounded(pthread_t thread)
{
 struct timespec deadline;clock_gettime(CLOCK_REALTIME,&deadline);deadline.tv_sec+=2;
 assert(!pthread_timedjoin_np(thread,NULL,&deadline));
}
static void *consumer(void *unused)
{
 (void)unused;unsigned char bytes[64];struct rusage a,b;
 assert(!getrusage(RUSAGE_THREAD,&a));
 received=y2_rx_read(bytes,sizeof(bytes),&cancel,&stop);
 assert(!getrusage(RUSAGE_THREAD,&b));switches=b.ru_nvcsw-a.ru_nvcsw;
 if(received>0) assert(received==4 && !memcmp(bytes,"data",4));
 return NULL;
}
int main(void)
{
 struct sigaction action={.sa_handler=interrupted};sigemptyset(&action.sa_mask);
 assert(!sigaction(SIGUSR1,&action,NULL));
 for(unsigned mode=0;mode<4;mode++) {
  int fds[2];assert(!pipe(fds));cancel=stop=0;received=99;
  assert(!y2_rx_start(fds[0],64));pthread_t thread;
  assert(!pthread_create(&thread,NULL,consumer,NULL));pause_ms(300);
  if(mode==0) assert(write(fds[1],"data",4)==4);
  if(mode==1) y2_rx_set_flag(&cancel,2);
  if(mode==2) y2_rx_set_flag(&stop,1);
  if(mode==3) y2_rx_stop();
  join_bounded(thread);assert(received==(mode==0?4:-1));
  /* A 20-ms timeout would cause about 15 sleeps during this quiet wait. */
  assert(switches<=3);printf("mode=%u idle_switches=%ld\n",mode,switches);
  y2_rx_stop();close(fds[0]);close(fds[1]);
 }
 /* Cancellation already published before waiting cannot be missed. */
 int fds[2];assert(!pipe(fds));assert(!y2_rx_start(fds[0],64));
 cancel=stop=0;y2_rx_set_flag(&cancel,1);unsigned char bytes[64];
 assert(y2_rx_read(bytes,sizeof(bytes),&cancel,&stop)==-1);
 y2_rx_set_flag(&cancel,0);assert(write(fds[1],"data",4)==4);
 assert(y2_rx_read(bytes,sizeof(bytes),&cancel,&stop)==4);
 y2_rx_stop();close(fds[0]);close(fds[1]);
 /* Endpoint error wakes its consumer rather than leaving it asleep. */
 assert(!y2_rx_start(-1,64));cancel=stop=0;
 assert(y2_rx_read(bytes,sizeof(bytes),&cancel,&stop)==-1);y2_rx_stop();
 puts("PASS: idle wait, data, cancel, stop, restart and endpoint failure");return 0;
}
