#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/uio.h>
static void handler(int signal){(void)signal;}
static void *idle(void *arg){(void)arg;for(;;)pause();return 0;}
int main(int argc,char **argv){
    if(argc!=2)return 2;
    struct iovec v[]={{"diagnostic ",11},{"writev marker\n",14}};
    writev(2,v,2);write(2,"diagnostic write marker\n",24);
    if(!strcmp(argv[1],"caught")){signal(SIGABRT,handler);raise(SIGABRT);return 0;}
    if(!strcmp(argv[1],"abort")){abort();}
    if(!strcmp(argv[1],"threads")){pthread_t thread;if(pthread_create(&thread,0,idle,0))return 3;abort();}
    if(!strcmp(argv[1],"exit")){pthread_t thread;if(pthread_create(&thread,0,idle,0))return 3;exit(42);}
    if(!strcmp(argv[1],"fault")){*(volatile int*)0=1;}
    return 4;
}
