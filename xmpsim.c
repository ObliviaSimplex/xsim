#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h> // check for multiple def errors
#include <assert.h>
#include "xcpu.h"
#include "xdb.h"

/**
 * MOREDEBUG turns on a host of helpful debugging features, which I 
 * gradually pieced together for my own benefit, in trying to get 
 * this programme to run. Features actived by the MOREDEBUG macro
 * switch are: a pretty and much-improved xcpu_print called 
 * xcpu_pretty_print, on-the-fly natural language disassembly and
 * running commentary on the active instruction in each cycle, and
 * a cycle counter. The default channel for all of this is stderr,
 * leaving stdout unsullied, and still usable for comparing with the
 * output of xsim_gold. To temporarily turn off the MOREDEBUG features,
 * just redirect stderr to /dev/null on the command line. To more 
 * permanently turn it off, just switch it to 0 below and recompile. 
 * For an optimised version of xcpu, I may consider trimming the fat
 * and commenting out all of the MOREDEBUG features, since, taken
 * together, they linearly increase the runtime relative to the number
 * of cycles (since, even when deactived, they mean another if-statement
 * or two to evaluate for every machine instruction). Since this cpu is
 * being developed for experimentation rather than for speed, however, 
 * it seems advisable to leave the MOREDEBUG features intact.  
 * 
 * UPDATE: MOREDEBUG is no longer interpreted as boolean. It selects the 
 *         CPU for which debugging info is desired. If set to -1, all CPUs
 *         will be attended to (-1 is the new 1). If set to -2, no CPUs will.
 **/

// There seems to be a data corruption issue when interrupts are frequent and
// multiple threads are being used. This is probably due to stack overflow.
// check to make sure that the lock functions are clearing the stack? but gold
// doesn't have this problem... so it's probably in the C code. 

#define MOREDEBUG -2

#define CYCLE_ARG 1
#define IMAGE_ARG 2
#define INTERRUPT_ARG 3
#define CPU_ARG 4
#define EXPECTED_ARGC 5

#define DEFAULT_CPU 1
#define DEFAULT_INTERRUPT 0
#define DEFAULT_CYCLES 0

void init_cpu(xcpu *c);
FILE* load_file(char *filename);
int load_programme(unsigned char *mem, FILE *fd);
void shutdown(xcpu *c);
static void * execution_loop(void *);

/** GLOBAL VARIABLES (NECESSARY EVILS) **/

// build the jump table of instruction-function pointers 
IHandler *table;
int cycles, interrupt_freq, cpu_num;

// The memory to be shared among all CPUs/threads. 
unsigned char *mem; 

/***************************************************************************/

int main(int argc, char *argv[]){
  // parse command-line options
  cycles = (argc >= CYCLE_ARG+2)? atoi(argv[CYCLE_ARG]) : DEFAULT_CYCLES;
  interrupt_freq = (argc >= INTERRUPT_ARG+1)? atoi(argv[INTERRUPT_ARG])
    : DEFAULT_INTERRUPT;
  cpu_num = (argc >= CPU_ARG+1)? atoi(argv[CPU_ARG]) : DEFAULT_CPU;
  if (argc == 1){
    fprintf(LOG,"Usage: %s <cycles> <filename> <interrupt frequency>"
            " <number of CPUs>\n",
            argv[0]);
    exit(EXIT_FAILURE);
  } else if (argc != EXPECTED_ARGC){
    char cyc[15];
    sprintf(cyc, "%d", cycles);
    fprintf(LOG,"USING SOME DEFAULT SETTINGS:\n\nCYCLES = %s\n",
            (cycles)? cyc : "unlimited");
    fprintf(LOG,"INTERRUPT FREQUENCY = %d\nNUMBER OF CPUS = %d\n",
            interrupt_freq, cpu_num);
    fprintf(LOG,"\nHit <ENTER> to continue...\n");
    getchar();
  }

  FILE *fd= (argc >= IMAGE_ARG+1)? load_file(argv[IMAGE_ARG]) :
    load_file(argv[IMAGE_ARG-1]);

  /**** Now, the interesting modification: create cpu_num different cpu
        contexts, and spin a separate thread to execute each one, in a loop. 
        You might want a separate function for this. ****/

  table = build_jump_table();
  //xcpu c[cpu_num];
  xcpu * c;
  c = (xcpu *) calloc(cpu_num, sizeof(xcpu));
  mem = calloc(MEMSIZE, sizeof(unsigned char));
  load_programme(mem, fd);

  pthread_t threads[cpu_num];
  int tsignal;  
  int u;

  /** Now initialize each CPU, in sequence. **/
  for (u = 0; u < cpu_num; u++){
    c[u].memory = mem;
    c[u].num = cpu_num;
    c[u].id = u;
    }

  /** Now spin the threads. Each CPU gets one. **/
  for (u = 0; u < cpu_num; u++){
    if (pthread_create(&threads[u], NULL, execution_loop, (void *) (c+u))){
      fprintf(stderr, "Thread %d not okay. Error signal: %d\n", u, tsignal);
      exit(EXIT_FAILURE);
    }
  }

  /** Wait for the threads to come home. **/
  int join_count = 0;
  for (u = cpu_num-1; u >= 0; u--){
    join_count += !pthread_join(threads[u], NULL);
  }

  // Check for !errors:
  if (join_count == cpu_num){
    free(mem);
  } else {
    fprintf(LOG, "join count = %d of %d expected\n",join_count, cpu_num);
  }
  
  /** Now free the jump table. **/
  destroy_jump_table(table);
  pthread_exit(NULL);
}

/**************************************************************
 * The central execution loop
 **************************************************************/
static void * execution_loop(void * cpu){ // expects pointer to an xcpu struct
  xcpu *c = (xcpu *) cpu; // we need our parameter back in the form of an xcpu*
  char graceful[40];    
  char out_of_time[40];
  sprintf(graceful, "CPU %d has halted", c->id);
  sprintf(out_of_time, "CPU ran %d out of time", c->id);
  int halted[c->num], j, i[c->num]; // deprecate halted?
  // let's have each thread keep its own cycle counter.
  for (j=0; j < c->num; j++){
    i[j]=0;
    halted[j] = 0;
  }

  int oldpc[c->num]; // holds previous programme counter
  
  while ( i[c->id] < cycles || !cycles){
     if (MOREDEBUG == -1 || MOREDEBUG == c->id)
       fprintf(LOG, "<CYCLE %d> <CPU %d>\n",i[c->id],c->id);
     // if not in 1st cycle, & interrupts are set, then interrupt periodically
    if (i[c->id] != 0 && interrupt_freq != 0 && i[c->id] % interrupt_freq == 0){
      if (!xcpu_exception(c, X_E_INTR)){
        fprintf(stderr, "Exception error at 0x%4.4x. CPU has halted.\n",
                c->pc);
        return NULL;
      }
    }
    
    oldpc[c->id] = c->pc; // save current instruction for error reporting

    // Now, call xcpu_execute function to perform the instruction at c->pc.

    halted[c->id] = !xcpu_execute(c,table); 
       
    if (MOREDEBUG == c->id || MOREDEBUG == -1){
      LOCK(elk);
      xcpu_pretty_print(c);
      UNLOCK(elk);
    }

    if (halted[c->id]) break;
    i[c->id] ++;
  }
  
  char *exit_msg = (halted[c->id])? graceful : out_of_time;
  fprintf(LOG, "\n<%s after %d cycles at PC = %4.4x : %4.4x>\n",
          exit_msg, i[c->id], oldpc[c->id], FETCH_WORD(oldpc[c->id]));
  //  disas(c);
  return NULL;
}

FILE* load_file(char* filename){
  FILE *fd;
  if ((fd = fopen(filename, "rb")) == NULL){
    char msg[80]="error: could not stat image file ";
    strncat(msg, filename,30);
    fatal(msg);
  }
  return fd;
}

/**************************************************************************
   Load a programme into memory, given a file descriptor. 
 **************************************************************************/
int load_programme(unsigned char *mem, FILE *fd){
  unsigned int addr = 0;
  do{
    mem[addr++] = fgetc(fd);
  }
  while (!feof(fd) && addr < MEMSIZE);
  fclose(fd);
  if (addr >= MEMSIZE){
    char msg[70]; 
    sprintf(msg, "Programme larger than %d bytes. Too big to fit in memory.",
            MEMSIZE);
    fatal(msg);
  }
  return addr;
}

void shutdown(xcpu *c){
  free(c->memory);
  free(c);
}
