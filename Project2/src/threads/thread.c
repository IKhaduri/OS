#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "fixed_point.h"
#include "thread.h"
#include "../../tests/p1/src/threads/thread.h"
#include "interrupt.h"

#ifdef USERPROG
#include "userprog/process.h"
#endif
/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

static struct list sleepers; //////////////////////////

static int load_avg = 0;

// Array storing lists of 'ready' threads with priorities from MIN to MAX
static struct list lists_of_equiprior_threads[PRI_MAX - PRI_MIN + 1];

/* Initializes data structure storing lists of threads
   differentiaded by thethread_mlfqsir priorities */
static void init_priority_lists(void){
  int priority;
  for (priority = PRI_MIN; priority <= PRI_MAX; ++priority){
    int index_of_list = priority - PRI_MIN;
    list_init(lists_of_equiprior_threads + index_of_list);
  }
};

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void update_recent_cpu_of_thread(struct thread *t, void *aux UNUSED);
static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *get_next_thread_to_run();
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);
/*
 * compares threads according to their priority.
 * It returns true if a is less than b
 *
 * */
static bool thread_cmp(const struct list_elem *a, const struct list_elem *b, void *aux);


/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void)
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init(&sleepers);
  list_init (&all_list);

  /* Initialize contatiner of ready threads, in case 'mlfqs'
     flag is on we operate in advanced scheduler mode with
     efficiently storing data as array of lists of threads 
     having equal priority. Otherwise opt for single list
     strategy with O(n) time complexity. */
  //if (thread_mlfqs)
  init_priority_lists();
  //else
  list_init (&ready_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main",(thread_mlfqs)? PRI_MAX : PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void)
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void)
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
} 
/* Prints thread statistics. */
void
thread_print_stats (void)
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux)
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock (t);

  thread_yield();

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void)
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;

  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t)
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  // Decide on the container for 'ready threads'
  if(t != idle_thread) {
    if (thread_mlfqs) {
      int thread_priority = t->prior_don; //BSD
      struct list *list_with_given_priority =
              lists_of_equiprior_threads + (thread_priority - PRI_MIN);
      list_push_back(list_with_given_priority, &t->elem);
    } else {
      list_push_back(&ready_list, &t->elem); //basic
    }
  }
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void)
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void)
{
  struct thread *t = running_thread ();

  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void)
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void)
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void)
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;

  ASSERT (!intr_context ());

  if(cur == idle_thread) return;
  old_level = intr_disable ();
  if (cur != idle_thread){
    //if (!is_thread(cur)) return;
    if (thread_mlfqs){
      int thread_priority = cur->prior_don;
      struct list *list_with_given_priority = 
        lists_of_equiprior_threads + (thread_priority - PRI_MIN);
      list_push_back (list_with_given_priority, &cur->elem);
    }else{
      list_push_back (&ready_list, &cur->elem);
    }
  }
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority)
{
  //if(thread_mlfqs) return;
  ASSERT(new_priority >= PRI_MIN && new_priority <= PRI_MAX);
  enum intr_level old_level = intr_disable();
  struct thread *t = thread_current ();
  //int old_prior = t->prior_don;
  t->base_priority = new_priority;
  if(!thread_mlfqs) {
    if (t->prior_don < new_priority)
      t->prior_don = new_priority;
    else thread_update_donations(t);
  }else{
    t->prior_don = t->base_priority;
  }
  thread_yield_if_needed();
  intr_set_level (old_level);
}

/* Returns the current thread's priority. */
int
thread_get_priority (void)
{
  return thread_current ()->prior_don;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice)
{
  enum intr_level old_level = intr_disable();
  struct thread *t = thread_current();
  ASSERT(t != NULL);
  t->nice = nice;
  thread_priority_update(t);
  thread_yield_if_needed();
  intr_set_level(old_level);
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void)
{

  return thread_current()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void)
{
  /* Not yet implemented. */
  return fixed_round_to_closest_int(fixed_int_mul(load_avg, 100));
  //return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void)
{
  /* Not yet implemented. */
  return fixed_round_to_closest_int(fixed_int_mul(thread_current()->recent_cpu, 100));
  //return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED)
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;)
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux)
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void)
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->base_priority = priority;
  t->prior_don = priority;
  t->magic = THREAD_MAGIC;

  list_init(&t->lock_list);
  t->locked_on = NULL;
  lock_init(&t->prior_lock);

  old_level = intr_disable ();
  if(thread_mlfqs){
    struct thread *cur = running_thread();
    if(is_thread(cur) && (t != initial_thread) && (cur != idle_thread)){
      t->nice = cur->nice;
      t->recent_cpu = cur->recent_cpu;
    }else{
      t->nice = 0;
      t->recent_cpu = 0;
    }
    thread_priority_update(t);
  }

  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size)
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

static struct thread *get_next_thread_to_run(){
  ASSERT(intr_get_level() == INTR_OFF);
  if (!thread_mlfqs && list_empty(&ready_list))
    return idle_thread;

  struct list_elem *elem = NULL;
  if (!thread_mlfqs){
    elem = list_max (&ready_list, thread_cmp, NULL);
  }else{
    int priority;
    for (priority = PRI_MAX; priority >= PRI_MIN; --priority){
      struct list *list_with_cur_priority = lists_of_equiprior_threads + (priority - PRI_MIN);
      if (!list_empty(list_with_cur_priority)){
        //*
        elem = list_front(list_with_cur_priority);
        /*/
        elem = list_back(list_with_cur_priority);
        //*/
        break;
      }
    }
    if (elem == NULL)
      return idle_thread;
  }
  ASSERT (elem != NULL);
  struct thread *ret_thread = list_entry(elem, struct thread, elem);
  ASSERT (ret_thread != NULL);
  return ret_thread;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void)
{
  struct thread *next = get_next_thread_to_run();
  if(next != idle_thread && next->status == THREAD_READY) list_remove(&next->elem);
  return next;
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();

  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread)
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void)
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void)
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);


//////////////////////\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\////////////////////
//////////////////////\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\////////////////////

static bool sleepers_is_less_cmp(const struct list_elem *first, const struct list_elem *second, void *aux UNUSED) {
  //ASSERT(0);
  ASSERT(first);
  ASSERT(second);

  return list_entry (first, struct thread, elem)->ticks_left_to_sleep <
        list_entry (second, struct thread, elem)->ticks_left_to_sleep;
}

static void reset_sleepers_ticks(struct thread *t) {
  //ASSERT(0);
  ASSERT(t);

  t->ticks_left_to_sleep = timer_ticks();
}

void sleep_the_thread(int64_t ticks) {
  //*
  enum intr_level before = intr_disable();
  thread_current()->ticks_left_to_sleep = ticks + timer_ticks();

  list_insert_ordered(&sleepers, &thread_current()->elem,
                            sleepers_is_less_cmp, NULL);

  thread_block();

  intr_set_level(before);
  /*/
  ASSERT(0);
  //*/
}

void handle_tick_for_sleep_queue(void) {
  //*
  if (list_empty (&sleepers))
    return;

  struct list_elem *e = list_begin (&sleepers);
  while (e != list_end (&sleepers)) {
    struct thread *curr = list_entry (e, struct thread, elem);
    struct list_elem* r = list_next (e);
    if (curr->ticks_left_to_sleep <= timer_ticks()) {
      reset_sleepers_ticks(curr);

      enum intr_level before = intr_disable();

      list_remove(e);


      thread_unblock(curr);

      intr_set_level(before);

      e = r;
    } else break;
  }
  intr_yield_on_return();
  /*/
  ASSERT(0);
  //*/
}


static bool thread_cmp(const struct list_elem *a, const struct list_elem *b, void *aux){
	const struct thread *thread_a = list_entry(a, struct thread, elem);
	const struct thread *thread_b = list_entry(b, struct thread, elem);
	bool rv = (thread_a->prior_don < thread_b->prior_don);
  int prior = (rv ? thread_b->prior_don : thread_a->prior_don);
	if (aux!=NULL)
    if ((*((int*)aux)) < prior) (*((int*)aux)) = prior;
	return rv;
}


void thread_donate(struct thread *t, int priority){
  ASSERT(intr_get_level () == INTR_OFF);
  ASSERT(t != NULL);
  if(!is_thread (t)) return;
  if (is_thread (t)) {
    if (t->prior_don < priority) {
      t->prior_don = priority;
      if (t->locked_on != NULL) thread_donate(t->locked_on->holder, priority);
    }
  }
}
void thread_update_donations(struct thread *t){
  ASSERT(intr_get_level () == INTR_OFF);
  ASSERT(t != NULL);
  if(!is_thread (t)) return;
  int start_priority = t->prior_don;
  t->prior_don = t->base_priority;
  if (!list_empty(&t->lock_list)){
    struct list_elem *cursor = list_begin(&t->lock_list);
    struct list_elem *end = list_end(&t->lock_list);
    while(cursor != end){
      struct semaphore *sem = &(list_entry(cursor, struct lock, elem)->semaphore);
      struct thread *max = list_entry(list_max(&sem->waiters, thread_cmp, NULL), struct thread, elem);
      if(max->prior_don > t->prior_don) t->prior_don = max->prior_don;
      cursor = list_next(cursor);
    }
  }
  if (t->locked_on != NULL && t->prior_don != start_priority)
    thread_update_donations(t->locked_on->holder);
  struct thread *cur = thread_current();
}



static void update_recent_cpu_of_thread(struct thread *t, void *aux UNUSED){
  t->recent_cpu = fixed_int_sum(fixed_mul(fixed_div(fixed_int_mul(load_avg, 2), fixed_int_sum(fixed_int_mul(load_avg, 2), 1)), t->recent_cpu), t->nice);
}
void update_recent_cpu(){
  thread_foreach(update_recent_cpu_of_thread, NULL);
}

static void count_threads(struct thread *t UNUSED, void *cnt){
  if((t != idle_thread) && ((t->status == THREAD_RUNNING) || (t->status == THREAD_READY)))
    (*((int*)cnt))++;
}

/*
 * counts load average, every
 * multiple of a second
 * */
void count_load_avg(void){
  if(!thread_mlfqs) return;
  int cnt = 0;
  /*
  thread_foreach(count_threads, &cnt);
  /*/
  int priority;
  for (priority = PRI_MIN; priority <= PRI_MAX; ++priority){
    struct list *list_with_cur_priority = lists_of_equiprior_threads + (priority - PRI_MIN);
    cnt += list_size(list_with_cur_priority);
  }
  struct thread *cur = thread_current();
  if ((cur != idle_thread) && (cur->status == THREAD_RUNNING)) cnt++;
  //*/
  load_avg = fixed_sum(fixed_mul (fixed_int_div (int_to_fixed (59), 60), load_avg),
                       fixed_int_mul (fixed_int_div (int_to_fixed (1), 60), cnt) );
}

/*
 * updates thread priority
 * by going trough the heavy
 * process of counting everything
 * necessary described in pintOS
 *
 * */

void thread_priority_update(struct thread *t){
  ASSERT(is_thread(t));
  //enum intr_level before = intr_disable();
  if(t == idle_thread) return;

  t->base_priority = PRI_MAX - fixed_round_to_closest_int(fixed_int_div (t->recent_cpu, 4)) - t->nice * 2;
  if (t->base_priority>PRI_MAX)
    t->base_priority=PRI_MAX;
  if (t->base_priority<PRI_MIN)
    t->base_priority=PRI_MIN;
  t->prior_don = t->base_priority;
  //intr_set_level(before);
}

static void thread_priority_update_wrap(struct thread *t, void *aux){
  thread_priority_update(t);
}

void thread_priority_update_all(){
  thread_foreach(thread_priority_update_wrap, NULL);
}

void thread_yield_if_needed(){
  struct thread *cur = thread_current();
  struct thread *next = get_next_thread_to_run();
  if(cur == idle_thread && cur->status == THREAD_RUNNING) thread_yield();
  if(next == idle_thread) return;
  ASSERT(cur != NULL);
  ASSERT(next != NULL);
  if((cur->status != THREAD_RUNNING)) return;
  if((cur != next) && ((cur ->prior_don < next->prior_don) || (next->status != THREAD_READY))) thread_yield();
}