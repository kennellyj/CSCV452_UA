 /* ------------------------------------------------------------------------
   phase1.c
   Justin Kennelly & Carlos Torres
   University of Arizona
   CSCV 452
   ------------------------------------------------------------------------ */
#include <stdlib.h>
#include <strings.h>
#include <stdio.h>
#include <phase1.h>
#include "kernel.h"

/* ------------------------- Prototypes ----------------------------------- */
void startup();
extern int start1 (char *);
int   sentinel (char *);
void  check_deadlock();
int   fork1(char *name, int(*func)(char *), char *arg, 
			int stacksize, int priority);
void	p1_fork(int pid);
int	join(int *status);
void	quit(int status);
void	p1_quit(int pid);
void  dispatcher(void);
void  insertRL(proc_ptr proc);
void	p1_switch(int old, int new);
void  launch();

int	zap(int pid);
int	is_zapped(void);
int	getpid(void);
void	dump_processes(void);
int   block_me(int block_status);
int   read_cur_start_time(void);
int   unblock_proc(int pid);
void  time_slice(void);
int	readtime(void);
void  disableInterrupts();
void  enableInterrupts();
void  check_kernel_mode(char *func_name);
void  proc_count_Update();

/* -------------------------- Globals ------------------------------------- */

/* Patrick's debugging global variable... */
int debugflag = 1;

/* the process table */
proc_struct ProcTable[MAXPROC];

/* keeps track of the number of processes */
static int proc_count;

/* Process lists  */
proc_ptr ReadyList;
proc_ptr ZappedList;
proc_ptr BlockedList;
proc_ptr QuitList;

/* current process ID */
proc_ptr Current;

/* the next pid to be assigned */
unsigned int next_pid = SENTINELPID;


/* -------------------------- Functions ----------------------------------- */
/* ------------------------------------------------------------------------
   Name - startup
   Purpose - Initializes process lists and clock interrupt vector.
	     Start up sentinel process and the test process.
   Parameters - none, called by USLOSS
   Returns - nothing
   Side Effects - lots, starts the whole thing
   ----------------------------------------------------------------------- */
void startup()
{
   int i;      /* loop index */
   int result; /* value returned by call to fork1() */

   /* initialize the process table */
   if (DEBUG && debugflag)
      console("startup(): initializing the Process Table\n");
   /*Loop for initializing the ProcessTable*/   
   for(i = 0; i < MAXPROC; i++){
      ProcTable[i].next_proc_ptr = NULL;
      ProcTable[i].child_proc_ptr = NULL;
      ProcTable[i].next_sibling_ptr = NULL;
      ProcTable[i].pid = -1;
      ProcTable[i].parent = -1;
      ProcTable[i].priority = -1;
      ProcTable[i].status = EMPTY;
      ProcTable[i].Kids = 0;
      ProcTable[i].CPUtime = -1;
      ProcTable[i].name[0] = '\0';
   } 

   /* Initialize the Ready list, etc. */
   if (DEBUG && debugflag)
      console("startup(): initializing the process lists\n");

   for (i = 1; i < SENTINELPRIORITY; i++) {
      ReadyList[i] = NULL;
   }   
   BlockedList = NULL;
   QuitList = NULL;
   ZappedList = NULL;

   /* Initialize the clock interrupt handler */
   int_vec[CLOCK_INT] = clock_handler;

   /* startup a sentinel process */
   if (DEBUG && debugflag)
       console("startup(): calling fork1() for sentinel\n");
   result = fork1("sentinel", sentinel, NULL, USLOSS_MIN_STACK, SENTINELPRIORITY);
   if (result < 0) {
      if (DEBUG && debugflag)
         console("startup(): fork1 of sentinel returned error, halting...\n");
      halt(1);
   }
  
   /* start the test process */
   if (DEBUG && debugflag)
      console("startup(): calling fork1() for start1\n");
   result = fork1("start1", start1, NULL, 2 * USLOSS_MIN_STACK, 1);
   if (result < 0) {
      console("startup(): fork1 for start1 returned an error, halting...\n");
      halt(1);
   }

   /* Call the dispatcher function */
   dispatcher();

   console("startup(): Should not see this message! ");
   console("Returned from fork1 call that created start1\n");

   return;
} /* startup */

/* ------------------------------------------------------------------------
   Name - sentinel
   Purpose - The purpose of the sentinel routine is two-fold.  One
            responsibility is to keep the system going when all other
	         processes are blocked.  The other is to detect and report
	         simple deadlock states.
   Parameters - none
   Returns - nothing
   Side Effects -  if system is in deadlock, print appropriate error
		               and halt.
   ----------------------------------------------------------------------- */
int sentinel (char * dummy)
{
   if (DEBUG && debugflag)
      console("sentinel(): called\n");
   while (1)
   {
      check_deadlock();
      waitint();
   }
} /* sentinel */

/* ------------------------------------------------------------------------
   Name - check_deadlock
   Purpose - Checks to see if all other processes have terminated
   Parameters - Nothing
   Returns - Nothing
   Side Effects - If processes are terminated then halt the simulator.
   ----------------------------------------------------------------------- */
static void check_deadlock() {
   /* test if in kernel mode; halt if in user mode */
   check_kernel_mode("fork1");
   /*disable interupts*/
   disableInterrupts(); 
   
   /* Update proc count for most accurate count of all processes*/
   proc_count_Update();
   /* Tracks specifically processes that are blocked or zapped*/
   int ZapBlock_procs_count;

   if (proc_count == 1) {
      for (int i = 1; i < MAXPROC; i++) {
         if (ProcTable[i].status = ZAP_BLOCK || ProcTable[i].status = JOIN_BLOCK) {
            ZapBlock_procs_count++;
         }
      }
      if (ZapBlock_procs_count >= 1) {
         console("check_deadlock(): Sentinel detected deadlock.\n");
         halt(1);
      }    
      else {
         console("check_deadlock(): All processes completed.\n");
         halt(0);     
      } 
   }


} /* check_deadlock */

/* ------------------------------------------------------------------------
   Name - fork1
   Purpose - Gets a new process from the process table and initializes
             information of the process.  Updates information in the
             parent process to reflect this child process creation.
   Parameters - the process procedure address, the size of the stack and
                the priority to be assigned to the child process.
   Returns - the process id of the created child or -1 if no child could
             be created or if priority is not between max and min priority.
   Side Effects - ReadyList is changed, ProcTable is changed, Current
                  process information changed
   ------------------------------------------------------------------------ */
int fork1(char *name, int (*f)(char *), char *arg, int stacksize, int priority)
{
   int proc_slot;
   int pid_count = 0;

   if (DEBUG && debugflag)
      console("fork1(): creating process %s\n", name);

   /* test if in kernel mode; halt if in user mode */
   check_kernel_mode("fork1");
   
   /*disable interupts*/
   disableInterrupts();

   /* Check if stacksize too small*/
   if (stacksize < USLOSS_MIN_STACK) {
      enableInterrupts();
      if(DEBUG && debugflag)
         console("fork1(): the stacksize is too small.\n");
      return(-2)
   }

   /* Check for valid priority */
   if (priority < MAXPRIORITY || priority > MINPRIORITY) {
      enableInterrupts();
      if(DEBUG && debugflag)
         console("fork1(): process priority is out of range.\n");
      return(-1);      
   }

   /* Check for valid function */
   if (f == NULL) {
      enableInterrupts();
      if(DEBUG && debugflag)
         console("fork1(): function is invalid.\n");
      return(-1);
   }

   /* check for valid name */
   if (name == NULL) {
      enableInterrupts();
      if(DEBUG && debugflag)
         console("fork1(): process name is invalid.\n");
      return(-1);      
   }

   /* find an empty slot */
   for (slot = 1; slot < MAXPROC; slot++) {
      if (ProcTable[slot].status = EMPTY) {
         pid_count++;
         proc_slot = slot;
      }
   }
   /* Return if Process table is full */
   if (pid_count >= MAXPROC) {
      enableInterrupts();
      if(DEBUG && debugflag)
         console("fork1(): process table full\n");
      return(-1);
   }

   /* fill-in entry in process table */
   /*------------------------------------------------------------*/
   
   /* Validate name by length */
   if ( strlen(name) >= (MAXNAME - 1) ) {
      console("fork1(): Process name is too long.  Halting...\n");
      halt(1);
   }

   /* Set process name & start_func*/
   strcpy(ProcTable[proc_slot].name, name);
   ProcTable[proc_slot].start_func = f;
   
   /* Validate argument & set */
   if ( arg == NULL )
      ProcTable[proc_slot].start_arg[0] = '\0';
   else if ( strlen(arg) >= (MAXARG - 1) ) {
      console("fork1(): argument too long.  Halting...\n");
      halt(1);
   }
   /* Set the rest of the process attributes */
   else
      strcpy(ProcTable[proc_slot].start_arg, arg);
      ProcTable[proc_slot].stacksize = stacksize;
      ProcTable[proc_slot].stack = (char *)(malloc(stacksize));
      ProcTable[proc_slot].pid = next_pid++;
      ProcTable[proc_slot].priority = priority;
      ProcTable[proc_slot].status = READY;
      ProcTable[proc_slot].Kids = 0;
      ProcTable[proc_slot].Parent = Current->pid;
      ProcTable[proc_slot].zapped = NULL;
      ProcTable[proc_slot].quit = NULL;
      ProcTable[proc_slot].CPUtime = NULL;

   /* add process to list */
   /* CREATE AN ADD TO LIST FUNCTION HERE*/
   insertRL(&ProcTable[proc_slot]);

   /* Initialize context for this process, but use launch function pointer for
    * the initial value of the process's program counter (PC)
    */
   context_init(&(ProcTable[proc_slot].state), psr_get(),
                ProcTable[proc_slot].stack, 
                ProcTable[proc_slot].stacksize, launch);
   
   /* for future phase(s) */
   p1_fork(ProcTable[proc_slot].pid);

   /* Update total number of processes */
   proc_count_Update();

   /* call dispatcher if not sentinel */
   if (strcmp(name, "sentinel"))
   dispatcher();

   /* enable interrupts */
   enableInterrupts("fork1");
   
   
   return(ProcTable[proc_slot].pid);
} /* fork1 */

/* ------------------------------------------------------------------------
   Name - p1_fork
   Purpose - 
   Parameters - 
   Returns - 
   Side Effects -  
   ----------------------------------------------------------------------- */
void	p1_fork(int pid) {

} /* p1_fork */

/* ------------------------------------------------------------------------
   Name - join
   Purpose - Wait for a child process (if one has been forked) to quit.  If 
             one has already quit, don't wait.
   Parameters - a pointer to an int where the termination code of the 
                quitting process is to be stored.
   Returns - the process id of the quitting child joined on.
		-1 if the process was zapped in the join
		-2 if the process has no children
   Side Effects - If no child process has quit before join is called, the 
                  parent is removed from the ready list and blocked.
   ------------------------------------------------------------------------ */
int join(int *code)
{
   /* test if in kernel mode; halt if in user mode */
   check_kernel_mode("join");
   /*disable interupts*/
   disableInterrupts();

   if (Current->Kids == 0)
   {
       enableInterrupts();
       if (DEBUG && debugflag)
           console("join(): the process has no children\n");
       return(-2);
   }
   
   /* Check if child(ren) quit before join() occurred*/
   if (Current->quit_child_ptr == NULL)
   {
       
   }
      /* return immediatly*/
      /* return PID of quit child(ren)*/
      /* Store child status passed to quit */
      /* dump child(ren) info in order of their quit */

   /* Check if no unjoined child(ren) has quit */
      /* wait */
      /* check if process zapped waiting for child to quit*/
         /* return -1*/
      /* when quit.. */
      /* return PID of quit child(ren) >= 0*/
      /* Store child status passed to quit */
      /* dump child(ren) info in order of their quit */

} /* join */

/* ------------------------------------------------------------------------
   Name - quit
   Purpose - Stops the child process and notifies the parent of the death by
             putting child quit info on the parents child completion code
             list.
   Parameters - the code to return to the grieving parent
   Returns - nothing
   Side Effects - changes the parent of pid child completion status list.
   ------------------------------------------------------------------------ */
void quit(int code)
{
   /* test if in kernel mode; halt if in user mode */
   check_kernel_mode("quit");
   
   /*disable interupts*/
   disableInterrupts();

   /* halt if process calls quit with active child(ren)*/
   if (Current->kids > 0) {
      console("quit(): The parent has active children. Halting..\n");
      halt(1);
   }
   else {
      Current->status = QUIT;
   }
   /* cleanup proc table*/
      /* parent already preformed join() */
      /* parent has not done a join() */
   /* unlock zapped processes */
   /* child(ren) have quit that have and will not join() */
      /* no error */

   p1_quit(Current->pid);
} /* quit */

/* ------------------------------------------------------------------------
   Name - p1_quit
   Purpose - 
   Parameters - 
   Returns - 
   Side Effects -  
   ----------------------------------------------------------------------- */
void	p1_quit(int pid) {

} /* p1_quit */

/* ------------------------------------------------------------------------
   Name - dispatcher
   Purpose - dispatches ready processes.  The process with the highest
             priority (the first on the ready list) is scheduled to
             run.  The old process is swapped out and the new process
             swapped in.
   Parameters - none
   Returns - nothing
   Side Effects - the context of the machine is changed
   ----------------------------------------------------------------------- */
void dispatcher(void)
{
   proc_ptr next_process;

   enableInterrupts();

   /* test if in kernel mode; halt if in user mode */
   check_kernel_mode("dispatcher");

   /* insertRL function? */

   /* decides which process runs next & execute */

   /* check if current proc can keep running */
      /* has it been time sliced? */
      /* has it been blocked? */
      /* is it highest priority in ready list? */
   
   /* context_switch(context *old, context *new); */

   p1_switch(Current->pid, next_process->pid);
} /* dispatcher */

/* ------------------------------------------------------------------------
   Name - insertRL
   Purpose - 
   Parameters - 
   Returns - 
   Side Effects -  
   ----------------------------------------------------------------------- */
static void insertRL(proc_ptr proc) {
   proc_ptr walker, previous; // pointers to PCB
   previous = NULL;
   walker = ReadyList;
   while (walker != NULL && walker->priority <= proc->priority) {
      previous = walker;
      walker = walker->next_proc_ptr;
   }
   if (previous == NULL) {
      /* process goes at front of ReadyList */
      proc->next_proc_ptr = ReadyList;
   }
   else {
      /* process goes after previous */
      previous->next_proc_ptr = proc;
      proc->next_proc_ptr = walker;
   }
   return;
} /* insertRL */

/* ------------------------------------------------------------------------
   Name - removeRL
   Purpose -
   Parameters -
   Returns -
   Side Effects -
   ----------------------------------------------------------------------- */
static void insertRL(proc_ptr proc) {
    proc_ptr walker; // pointers to PCB
    if (proc == ReadyList)
    {
        ReadyList = ReadyList->next_proc_ptr;
    }
    else
    {
        walker = ReadyList;
        while (walker->next_proc_ptr != proc)
        {
            walker = walker->next_proc_ptr;
        }
        walker->next_proc_ptr = walker->next_proc_ptr->next_proc_ptr;
    }
    return;
} /* removeRL */

/* ------------------------------------------------------------------------
   Name - p1_switch
   Purpose - 
   Parameters - 
   Returns - 
   Side Effects -  
   ----------------------------------------------------------------------- */
void	p1_switch(int old, int new) {

} /* p1_switch */

/* ------------------------------------------------------------------------
   Name - finish
   Purpose - Required by USLOSS
   Parameters - none
   Returns - nothing
   Side Effects - none
   ----------------------------------------------------------------------- */
void finish()
{
   if (DEBUG && debugflag)
      console("in finish...\n");
} /* finish */

/* ------------------------------------------------------------------------
   Name - launch
   Purpose - Dummy function to enable interrupts and launch a given process
             upon startup.
   Parameters - none
   Returns - nothing
   Side Effects - enable interrupts
   ------------------------------------------------------------------------ */
void launch()
{
    int result;

    if (DEBUG && debugflag)
        console("launch(): started\n");

    /* Enable interrupts */
    enableInterrupts();

    /* Call the function passed to fork1, and capture its return value */
    result = Current->start_func(Current->start_arg);

    if (DEBUG && debugflag)
        console("Process %d returned to launch\n", Current->pid);

    quit(result);

} /* launch */

/* ------------------------------------------------------------------------
   Name - zap
   Purpose -
   Parameters -
   Returns -
   Side Effects -
   ----------------------------------------------------------------------- */
int zap(int pid) {

} /* zap*/

/* ------------------------------------------------------------------------
   Name - is_zapped
   Purpose -
   Parameters -
   Returns -
   Side Effects -
   ----------------------------------------------------------------------- */
int is_zapped(void) {
    if (Current->status == ZAP_BLOCK) {
        return 1;
    }
    else {
        return 0;
    }
} /* is_zapped */

/* ------------------------------------------------------------------------
   Name - getpid
   Purpose - Return a PID number for a process
   Parameters - NONE
   Returns - PID number
   Side Effects - NONE
   ----------------------------------------------------------------------- */
int getpid(void) {
    /* Check if we ar in kernel mode*/
    check_kernel_mode("getpid");

    return Current->pid;
} /* getpid */

/* ------------------------------------------------------------------------
   Name - dump_processes
   Purpose -
   Parameters -
   Returns -
   Side Effects -
   ----------------------------------------------------------------------- */
void dump_processes(void) {
    char* ready = "READY";
    char* running = "RUNNING";
    char* blocked = "BLOCKED";
    char* join_blocked = "JOIN_BLOCKED";
    char* quit = "QUIT";
    char* zap_blocked = "ZAP_BLOCKED";
    console("\n     PID     NAME    Priority    Status  Parent\n");
    for (int i = 0; i < 50; i++)
    {
        char buf[30];
        char* status = buf;
        char* parent;
        if (ProcTable[i].status != EMPTY) {
            switch (ProcTable[i].status)
            {
            case READY: status = ready;
                break;
            case RUNNING: status = running;
                break;
            case BLOCKED: status = blocked;
                break;
            case JOIN_BLOCKED: status = join_blocked;
                break;
            case QUIT: status = quit;
                break;
            case ZAP_BLOCKED: status = zap_blocked;
                break;
            default: sprintf(status, "%d", ProcTable[i].status);
            }
            if ProcTable[i].parent != NULL)
            {
            parent = ProcTable[i].parent->name;

            }
            else
            {
                parent = "NULL"
            }
            console("%8d %8s %8d %8s %8s\n", ProcTable[i].pid, ProcTable[i].name,
                ProcTable[i].priority, status, parent);
        }
    }
} /* dump_processes */

/* ------------------------------------------------------------------------
   Name - block_me
   Purpose - 
   Parameters - 
   Returns - 
   Side Effects -  
   ----------------------------------------------------------------------- */
int block_me(int block_status) {
   /* test if in kernel mode; halt if in user mode */
   check_kernel_mode("fork1");
   
   /*disable interupts*/
   disableInterrupts();

} /* block_me */

/* ------------------------------------------------------------------------
   Name - unblock_proc
   Purpose - 
   Parameters - 
   Returns - 
   Side Effects -  
   ----------------------------------------------------------------------- */
int unblock_proc(int pid) {

} /* unblock_proc */

/* ------------------------------------------------------------------------
   Name - read_cur_start_time
   Purpose - 
   Parameters - 
   Returns - 
   Side Effects -  
   ----------------------------------------------------------------------- */
int read_cur_start_time(void) {

} /* read_cur_start_time */

/* ------------------------------------------------------------------------
   Name - time_slice
   Purpose - 
   Parameters - 
   Returns - 
   Side Effects -  
   ----------------------------------------------------------------------- */
void time_slice(void) {

} /* time_slice */

/* ------------------------------------------------------------------------
   Name - readtime
   Purpose - 
   Parameters - 
   Returns - 
   Side Effects -  
   ----------------------------------------------------------------------- */
int readtime(void) {

} /* readtime */

/* ------------------------------------------------------------------------
   Name - disableInterrupts
   Purpose - 
   Parameters - 
   Returns - 
   Side Effects -  
   ----------------------------------------------------------------------- */
void disableInterrupts()
{
  /* turn the interrupts OFF iff we are in kernel mode */
  if((PSR_CURRENT_MODE & psr_get()) == 0) {
    //not in kernel mode
    console("Kernel Error: Not in kernel mode, may not disable interrupts\n");
    halt(1);
  } else
    /* We ARE in kernel mode */
    psr_set( psr_get() & ~PSR_CURRENT_INT );
} /* disableInterrupts */

/* --------------------------------------------------------------------------------
   Name - enableInterupts
   Purpose - Turn the interupts on
   Parameters - none
   Returns - nothing
   Side Effects -  if system is in usermode, print appropriate error and halt.
   -------------------------------------------------------------------------------- */
void enableInterrupts() {
   /* turn the interrupts OFF iff we are in kernel mode */
  if((PSR_CURRENT_MODE & psr_get()) == 0) {
    //not in kernel mode
    console("Kernel Error: Not in kernel mode, may not disable interrupts\n");
    halt(1);
  } else
    /* We ARE in kernel mode */
    psr_set( psr_get() | PSR_CURRENT_INT );
   
} /* enableInterupts */

/* --------------------------------------------------------------------------------
   Name - check_kernel_mode
   Purpose - Checks to make sure functions are being called in Kernel mode
   Parameters - *func_name keeps track of where the mode checking is being invoked.
   Returns - nothing
   Side Effects -  if system is in usermode, print appropriate error and halt.
   -------------------------------------------------------------------------------- */
static void check_kernel_mode(char *func_name) {
   union psr_values caller_psr;  /*Holds the caller's PSR values*/
   char buffer[200];

   if (DEBUG && debugflag) {
      sprintf(buffer, "check_kernel_mode(): called for function %s\n", func_name);
      console("%s", buffer);
   }

   /*test if in kernel mode and halts if in user mode*/
   caller_psr.integer_part = psr_get();
   if(caller_psr.bits.cur_mode == 0) {
      sprintf(buffer, "%s(): called while in user mode, process %d. Halting...\n", func_name, Current->pid);
      console("%s", buffer);
      halt(1);
   }
   
} /* check_kernel_mode */

/* <<<<<<<<<<<< FUNCTIONS TO ADD >>>>>>>>>>>>>*/

// ADD PROCESS TO LIST

// UPDATE GLOBAL PROCESS COUNT
/* --------------------------------------------------------------------------------
   Name - proc_count_Update
   Purpose - To keep track of the number of processes in the process table
   Parameters - Nothing
   Returns - Nothing
   Side Effects -  Increments the count of processes in the table when called, 
                     and there are new processes.
   -------------------------------------------------------------------------------- */
static void proc_count_Update() {
   /* loop index*/
   int i = 0;

   /* Loop through process in the process table*/
   for (i; i <= MAXPROC; i++) {
      if (ProcTable[i].pid != EMPTY) {
         i++;
      }
   /* if process found increment the count*/
   int proc_count = i;
   }
} /* proc_count_Update */