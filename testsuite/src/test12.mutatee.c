#include <stdio.h>
#include <sys/types.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>

#include "test12.h"
#define TRUE 1
#define FALSE 0

#if defined(i386_unknown_nt4_0)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <pthread.h>
#endif

#if defined(sparc_sun_solaris2_4)  || defined(i386_unknown_solaris2_5) || \
    defined(i386_unknown_linux2_0) || defined(x86_64_unknown_linux2_4) || \
    defined(mips_sgi_irix6_4) || defined(alpha_dec_osf4_0) || \
    defined(rs6000_ibm_aix4_1) || defined(ia64_unknown_linux2_4)
#include <dlfcn.h>
#endif

#ifdef __cplusplus
int mutateeCplusplus = 1;
#else
int mutateeCplusplus = 0;
#endif

#ifndef COMPILER
#define COMPILER ""
#endif
const char *Builder_id=COMPILER; /* defined on compile line */

int runTest[MAX_TEST+1];
int passedTest[MAX_TEST+1];

#define dprintf if (debugPrint) printf
int debugPrint = 0;
int isAttached = 0;
int mutateeIdle = 0;
int mutateeXLC = 0;

int subtest2counter = 0;
int subtest2err = 0;

/*
 * Stop the process (in order to wait for the mutator to finish what it's
 * doing and restart us).
 */
void stop_process()
{
#ifdef i386_unknown_nt4_0
    DebugBreak();
#else

#if !defined(bug_irix_broken_sigstop)
    kill(getpid(), SIGSTOP);
#else
    kill(getpid(), SIGEMT);
#endif

#endif
}

#if defined (os_windows)
#error
#else
typedef void *(*ThreadMain_t)(void *);
typedef pthread_t Thread_t;
typedef pthread_mutex_t Lock_t;
  /*
    createThreads()
    createThreads creates specified number of threads and returns
    a pointer to an allocated buffer that contains their handles
    caller is responsible for free'ing the result
  */

pthread_attr_t attr;

Thread_t *createThreads(unsigned int num, ThreadMain_t tmain, Thread_t *tbuf)
{
  unsigned int i;
  int err = 0;
  Thread_t *threads;
  if (tbuf == NULL)
    threads = (Thread_t *)malloc(num * sizeof(Thread_t));
  else 
    threads = tbuf;
    
  if (!threads) {
    fprintf(stderr, "%s[%d]:  could not alloc space for %d thread handles\n",
            __FILE__, __LINE__, num);
    return NULL;
  }

  if (0 != pthread_attr_init(&attr)) {
    err = 1;
    perror("pthread_attr_init");
    goto cleanup;
  }

  if (0 != pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED)) {
    err = 1;
    perror("pthread_attr_setdetachstate");
    goto cleanup;
  }

  if (0 != pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM)) {
    err = 1;
    perror("pthread_attr_setscope");
    goto cleanup;
  }

  /* start a bunch of threads */
  for (i = 0; i < num; ++i) {
    /*fprintf(stderr, "%s[%d]:  PTHREAD_CREATE\n", __FILE__, __LINE__); */
    if (0 != pthread_create(&(threads[i]), &attr, (void *(*)(void*))tmain, NULL)) {
      err = 1;
      fprintf(stderr, "%s[%d]:pthread_create\n", __FILE__, __LINE__);
      goto cleanup;
    }
  }

  cleanup:

  if (err) {
    free (threads);
    return NULL;
  }

  return threads;
}

int createLock(Lock_t *lock)
{
  if (debugPrint)
    fprintf(stderr, "%s[%d]:  creating lock on thread %lu\n", __FILE__, __LINE__, (unsigned long) pthread_self());
  if (0 != pthread_mutex_init(lock, NULL)) {
    perror("pthread_mutex_init");
    return FALSE;
  }
  return TRUE;
}

int destroyLock(Lock_t *lock)
{
  if (debugPrint)
    fprintf(stderr, "%s[%d]:  destroying lock on thread %lu\n", __FILE__, __LINE__, (unsigned long) pthread_self());
  if (0 != pthread_mutex_destroy(lock)) {
    perror("pthread_mutex_destroy");
    return FALSE;
  }
  return TRUE;
}

int lockLock(Lock_t *lock)
{
  if (debugPrint)
    fprintf(stderr, "%s[%d]:  locking lock on thread %lu\n", __FILE__, __LINE__, (unsigned long) pthread_self());
  if (0 != pthread_mutex_lock(lock)) {
    perror("pthread_mutex_lock");
    return FALSE;
  }
  return TRUE;
}

int unlockLock(Lock_t *lock)
{
  if (debugPrint)
    fprintf(stderr, "%s[%d]:  unlocking lock on thread %lu\n", __FILE__, __LINE__, (unsigned long) pthread_self());
  if (0 != pthread_mutex_unlock(lock)) {
    perror("pthread_mutex_unlock");
    return FALSE;
  }
  return TRUE;
}

#endif

/*
 * Check to see if the mutator has attached to us.
 */
int checkIfAttached()
{
    return isAttached;
}
/********************************************************************/
/********************************************************************/
/********************************************************************/
#define NUM_DYN_CALLS 8 
typedef int (*intFuncArg) (int);
int call1_zero() {return 0;}

int call1_1(int arg) {return arg+1;}
int call1_2(int arg) {return arg+2;}
int call1_3(int arg) {return arg+3;}
int call1_4(int arg) {return arg+4;}

int call1_dispatch(intFuncArg callme, int arg) 
{
  /*fprintf(stderr, "%s[%d]:  inside call1_dispatch\n", __FILE__, __LINE__);*/
  static int callsite_selector = 0;
  int ret = -1;
  intFuncArg tocall = (intFuncArg) callme;

  ret = call1_zero(); /* lets have a non-dynamic call site here too */

  if (!tocall) {
    fprintf(stderr, "%s[%d]:  FIXME!\n", __FILE__, __LINE__);
    return -1;
  }

  /*  3 dynamic call sites */
  switch (callsite_selector) {
  case 0: ret = (tocall)(arg); callsite_selector++; break;
  case 1: ret = (tocall)(arg+1); callsite_selector++; break;
  case 2: ret = (tocall)(arg+2); callsite_selector = 0; break;
  }

  if (ret) 
    ret = call1_zero(); /* lets have a non-dynamic call site here too */

  return ret;

}

void func1_1()
{

  /*  want to trigger a lot of dynamic calls, and then stop the process. */
  /*  to make sure we test possible race in event handling. */

  int nextfunc = 1;
  unsigned int i;
  for (i = 0; i < NUM_DYN_CALLS; ++i) {
    switch (nextfunc) {
    case 1: call1_dispatch(call1_1, i); nextfunc++; break;
    case 2: call1_dispatch(call1_2, i); nextfunc++; break;
    case 3: call1_dispatch(call1_3, i); nextfunc++; break;
    case 4: call1_dispatch(call1_4, i); nextfunc = 1; break;
    }; 
  }
  
  mutateeIdle = 1;
  while (mutateeIdle);
  /*  stop the process (mutator will restart us) */
  /*stop_process(); */

}


unsigned long current_locks[TEST2_THREADS];
/*Thread_t  *test2threads; */
pthread_t test2threads[TEST2_THREADS];
pthread_mutex_t real_lock;

void register_my_lock(unsigned long id, unsigned int val)
{
  unsigned int i;
  int found = 0;
  for (i = 0; i < TEST2_THREADS; ++i) {
    if (pthread_equal(test2threads[i],(pthread_t)id)) {
      found = 1;
      current_locks[i] = (unsigned)val;
      break;
    }
  }
  if (!found)
    fprintf(stderr, "%s[%d]: FIXME\n", __FILE__, __LINE__);
}
int is_only_one() {
  unsigned int i;
  int foundone = 0;
  for (i = 0; i < TEST2_THREADS; ++i) {
    if (0 != current_locks[i]) {
      if (foundone) return 0; /*false*/
      foundone++;
    }
  }
  return 1; /*true */
}

void (*DYNINSTlock_thelock)(void);
void (*DYNINSTunlock_thelock)(void);

void sleep_ms(int _ms) 
{
  struct timespec ts,rem;
  ts.tv_sec = 0;
  ts.tv_nsec = _ms * 1000 /*us*/ * 1000/*ms*/;
  assert(_ms < 1000);

 sleep:
 if (0 != nanosleep(&ts, &rem)) {
    if (errno == EINTR) {
      fprintf(stderr, "%s[%d]:  sleep interrupted\n", __FILE__, __LINE__);
      ts.tv_sec = rem.tv_sec;
      ts.tv_nsec = rem.tv_nsec;
      goto sleep;
    }
    assert(0);
  }

}

void *thread_main2 (void *arg)
{
   (DYNINSTlock_thelock)();

   register_my_lock((unsigned long)pthread_self(),1);
   pthread_mutex_lock(&real_lock);

  sleep_ms(1);

   if (!is_only_one()) {
     fprintf(stderr, "FAIL subtest2: more than one lock has been obtained\n");
     subtest2err = 1;
   }
   pthread_mutex_unlock(&real_lock);
   register_my_lock((unsigned long)pthread_self(),0);
   subtest2counter++;

   (DYNINSTunlock_thelock)(); 
   return NULL;
}

void func2_1()
{

  /*pthread_attr_t attr;*/
  void *RTlib;

#if !defined (os_windows) && !defined(os_irix)

  RTlib = dlopen("libdyninstAPI_RT.so.1", RTLD_LAZY);
  if (!RTlib) {
    fprintf(stderr, "%s[%d]:  could not open dyninst RT lib: %s\n", __FILE__, __LINE__, dlerror());
    exit(1);
  }

  DYNINSTlock_thelock = (void (*)(void))dlsym(RTlib, "DYNINSTlock_thelock");
  DYNINSTunlock_thelock = (void (*)(void))dlsym(RTlib, "DYNINSTunlock_thelock");
  if (!DYNINSTlock_thelock) {
    fprintf(stderr, "%s[%d]:  could not DYNINSTlock_thelock: %s\n", __FILE__, __LINE__, dlerror());
    exit(1);
  }
  if (!DYNINSTunlock_thelock) {
    fprintf(stderr, "%s[%d]:  could not DYNINSTunlock_thelock:%s\n", __FILE__, __LINE__, dlerror());
    exit(1);
  }

  pthread_mutex_init(&real_lock, NULL);

#if !defined(os_solaris)
   /*  XXX this is nasty */
   /*  The way this is supposed to work is that we get a lock, then start a bunch of
       threads, which all try to get the same lock, pretty much as soon as they start.
       Then, after starting all the threads, we release the lock and let the threads
       compete for it, checking to make sure that all threads get the lock at some point
       and that no two threads have it at the same time.  
       The problem is that solaris is having problems with this system when the lock is 
       obtained before the threads are spawned (pthread_create hangs) -- it is still ok
       to just start all the threads and have the system run, its just not quite as clean.
       This might be bad asm programming on my behalf, or it might be some idiosyncracy
       with solaris libpthreads.  This worked, incidentally, when this stuff was all in
       the mutator, but that might've been because the asm that was imported to implement
       the locks was the gnu asm, not the solaris-cc asm, which is the stuff that gets
       compiled, by default into the runtime lib*/
   (DYNINSTlock_thelock)();
#endif

  createThreads(TEST2_THREADS, thread_main2,test2threads);
  assert(test2threads);


  sleep_ms(5);

#if !defined(os_solaris)
   (DYNINSTunlock_thelock)(); 
#endif

#endif

  mutateeIdle = 1;
  while (mutateeIdle);

  /*free (test2threads);*/
}

void *thread_main(void *arg)
{
  int x, i;
  x = 0;
  for (i = 0; i < 0xffffff; ++i) {
    x = x + i;
  }
  /*fprintf(stderr, "%s[%d]:  PTHREAD_DESTROY\n", __FILE__, __LINE__); */
  return (void *) x;
}

void func3_1()
{
  Thread_t *threads;

  threads = createThreads(TEST3_THREADS, thread_main, NULL);
  assert (threads);

  mutateeIdle = 1;
  while (mutateeIdle);

  free (threads);
}

void func4_1()
{
  Thread_t *threads;

  threads = createThreads(TEST4_THREADS, thread_main, NULL);
  assert (threads);

  mutateeIdle = 1;
  while (mutateeIdle) {}

  free (threads);
}

void *thread_main5(void *arg)
{
  /*  The mutator will patch in messaging primitives to signal events at mutex creation,
      deletion, locking and unlocking.  Thus far, we are only considering reporting of events
      so actual contention is meaningless */
  Lock_t newmutex;
  if (!createLock(&newmutex)) {
     fprintf(stderr, "%s[%d]:  createLock failed\n", __FILE__, __LINE__);
     return NULL;
  }
  sleep_ms(100);
  if (!lockLock(&newmutex)) {
     fprintf(stderr, "%s[%d]:  lockLock failed\n", __FILE__, __LINE__);
     return NULL;
  }
  sleep_ms(100);
  if (!unlockLock(&newmutex)) {
     fprintf(stderr, "%s[%d]:  unlockLock failed\n", __FILE__, __LINE__);
     return NULL;
  }
  sleep_ms(100); 
  if (!destroyLock(&newmutex)) {
     fprintf(stderr, "%s[%d]:  destroyLock failed\n", __FILE__, __LINE__);
     return NULL;
  }

  sleep(1);
  return NULL;
}

Thread_t test5threads[TEST5_THREADS];
void func5_1()
{
  Thread_t *threads = test5threads;

  threads = createThreads(TEST5_THREADS, thread_main5,threads);
  assert (threads);

  mutateeIdle = 1;
  while (mutateeIdle);

  /*free (threads);*/
}

void func8_1()
{
  mutateeIdle = 1;
  while (mutateeIdle) {}
}

/********************************************************************/
/********************************************************************/
/********************************************************************/
#ifdef i386_unknown_nt4_0
#define USAGE "Usage: test12.mutatee [-attach] [-verbose] -run <num> .."
#else
#define USAGE "Usage: test12.mutatee [-attach <fd>] [-verbose] -run <num> .."
#endif

int main(int iargc, char *argv[])
{                                       /* despite different conventions */
    unsigned argc=(unsigned)iargc;      /* make argc consistently unsigned */
    unsigned int i, j;
#if !defined(i386_unknown_nt4_0)
    int pfd;
#endif
    int useAttach = FALSE;

    for (j=0; j <= MAX_TEST; j++) runTest[j] = FALSE;

    for (i=1; i < argc; i++) {
        if (!strcmp(argv[i], "-verbose")) {
            debugPrint = 1;
        } else if (!strcmp(argv[i], "-attach")) {
            useAttach = TRUE;
#ifndef i386_unknown_nt4_0
            if (++i >= argc) {
                printf("attach usage\n");
                fprintf(stderr, "%s\n", USAGE);
                exit(-1);
            }
            pfd = atoi(argv[i]);
#endif
        } else if (!strcmp(argv[i], "-run")) {
            for (j=i+1; j < argc; j++) {
                unsigned int testId;
                if (argv[j] && isdigit(*argv[j]) && (testId = atoi(argv[j]))) {
                    if ((testId > 0) && (testId <= MAX_TEST)) {
                        dprintf("selecting test %d\n", testId);
                        runTest[testId] = TRUE;
                    } else {
                        printf("%s[%d]: invalid test %d requested\n", __FILE__, __LINE__, testId);
                        exit(-1);
                    }
                } else {
                    /* end of test list */
                    break;
                }
            }
            i = j-1;
       } else if (!strcmp(argv[i], "-runall")) {
          for (j=0; j <= MAX_TEST; j++) runTest[j] = TRUE;
       } else {
            printf("unexpected parameter '%s'\n", argv[i]);
            fprintf(stderr, "%s\n", USAGE);
            exit(-1);
        }
    }

    if ((argc==1) || debugPrint)
        printf("Mutatee %s [%s]:\"%s\"\n", argv[0],
                mutateeCplusplus ? "C++" : "C", Builder_id);
    if (argc==1) exit(0);

     /* set xlc flag if appropriate */
     mutateeXLC = 0;
     if (strstr(argv[0], "xlc") || strstr(argv[0], "xlC"))
        mutateeXLC = 1;

    /* see if we should wait for the attach */
    if (useAttach) {
#ifndef i386_unknown_nt4_0
        char ch = 'T';
        if (write(pfd, &ch, sizeof(char)) != sizeof(char)) {
            fprintf(stderr, "*ERROR*: Writing to pipe\n");
            exit(-1);
        }
        close(pfd);
#endif
        printf("Waiting for mutator to attach...\n"); fflush(stdout);
        while (!checkIfAttached()) ;
        printf("Mutator attached.  Mutatee continuing.\n");
    }

    if (runTest[1]) func1_1();
    if (runTest[2]) func2_1();
    if (runTest[3]) func3_1();
    if (runTest[4]) func4_1();
    /*if (runTest[5]) func5_1();*/

    while(1);

    return(0);
}

