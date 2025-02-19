#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h> // check for multiple def errors
#include "xcpu.c"
//#include "xcpu.h"
#include "xdb.c"

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
 **/
#define MOREDEBUG 0

#define CYCLE_ARG 1
#define IMAGE_ARG 2
#define INTERRUPT_ARG 3
#define CPU_ARG 4
#define EXPECTED_ARGC 5

#define DEFAULT_CPU 1
#define DEFAULT_INTERRUPT 0
#define DEFAULT_CYCLES 0

#define HANDLE_ERROR(msg) \
  perror(msg); \
  exit(EXIT_FAILURE);

void init_cpu(xcpu *c);
FILE* load_file(char *filename);
int load_programme(unsigned char *mem, FILE *fd);
void shutdown(xcpu *c);
static void * execution_loop(void *);

/** build the jump table of instruction-function pointers **/
IHandler *table;
/** some global integer variables **/
int cycles, interrupt_freq, cpu_num;

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
  xcpu c[cpu_num];
  unsigned char *mem = calloc(MEMSIZE, sizeof(char));
  load_programme(mem, fd);

  pthread_t threads[cpu_num];
  int tsignal;  
  int u,i;
  //xcpu *ptr;
  for (u = 0; u < cpu_num; u++){
    //ptr = calloc(1,sizeof(xcpu));
    //c[u] = *ptr;
    //init_cpu(c[u]);
    c[u].num = cpu_num;
    c[u].id = u;
    c[u].pc = 0;
    c[u].itr = 0;
    c[u].state = 0;
    c[u].memory = mem;
    for(i=0; i<=15; c[u].regs[i++]=0) // registers will need to be on the stack
      ;                             // so that each thread can maintain its own
     
    ////printf("c[u]->pc: %d %4.4x",&(c[u])->pc,FETCH_WORD(&(c[u])->pc));
    // now spin the threads...
    tsignal = pthread_create(&threads[u], NULL, execution_loop,
                             (void *) (c+u));
    if (tsignal){
      fprintf(stderr, "Thread %d not okay. Error signal: %d\n", u, tsignal);
      exit(EXIT_FAILURE);
    }
  } // NB: I think each thread is supposed to SHARE the same memory
  // right now, they each hold a duplicate. But we'll figure out how to
  // do this later. 
  //u = 0;
  void * end;
  int join_count = 0;
  for (u = cpu_num-1; u >= 0; u--){
    tsignal = pthread_join(threads[u], &end);
    fprintf(stdout, "Joining thread %d: signal %d\n",u, tsignal);
    join_count ++;
  }


  while (--u >= 0){
    fprintf(LOG,"\nShutting down CPU %d...\n",c[u].id);
    //shutdown(&(c[u])); // don't free the memory yet!
  }
 
 
  /* 
free (&(c[u]));
  */
  
  if (join_count == cpu_num){
    printf("Freeing memory.\n");
    free(mem);
    ///for (u = 0; u < cpu_num; u++)
    ///  free(&(c[u]));
  } else {
    printf("join count = %d of %d expected\n",join_count, cpu_num);
  }
 
  //while(!(tsignal = pthread_join(threads[++u], &end) && u < cpu_num))
  //  fprintf(LOG, "joining thread %d of %d ==> %x\n", u-1, cpu_num, threads[u-1]);
  //destroy_jump_table(table);

  while (--u >= 0){
    fprintf(LOG,"\nShutting down CPU %d...\n",c[u].id);
    //shutdown(&(c[u])); // don't free the memory yet!
  }
  destroy_jump_table(table);
  return 0;
}

// more or less works, at least in a rough sense, but segfaults at the end. 

static void * execution_loop(void * cpu){ // expects pointer to an xcpu struct
  xcpu *c = (xcpu *) cpu;
  char graceful[40];    
  char out_of_time[40];
  sprintf(graceful, "CPU %d has halted.", c->id);
  sprintf(out_of_time, "CPU %d out of time.", c->id);
  int halted, i=0;
  fprintf(LOG,"c->pc: %d %4.4x\n",c->pc,FETCH_WORD(c->pc));
  
  while (i++ < cycles || !cycles){
    if (MOREDEBUG) fprintf(LOG, "<CYCLE %d> <CPU %d>\n",i-1,c->id);
    if (i != 0 && interrupt_freq != 0 && i % interrupt_freq == 0)
      if (!xcpu_exception(c, X_E_INTR)){
        fprintf(stderr, "Exception error at 0x%4.4x. CPU has halted.\n",
                c->pc);
        return NULL;
        //pthread_exit(NULL);
        //exit(EXIT_FAILURE);
      }
    if ((halted = !xcpu_execute(c, table))) break;
    if (MOREDEBUG)
      xcpu_pretty_print(c);

  } // on halt, halted gets 0; otherwise halted remains 1
  char *exit_msg = (halted)? graceful : out_of_time;
  fprintf(stdout, "%s\n", exit_msg);
  fprintf(LOG, "(%d cycles completed.)\n", i-1);
  
  
  // pthread_exit(NULL);
  return NULL; //((void *) c);
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
  do
    mem[addr++] = fgetc(fd);
  while (!feof(fd) && addr < MEMSIZE);
  if (addr >= MEMSIZE){
    char msg[70]; 
    sprintf(msg, "Programme larger than %d bytes. Too big to fit in memory.",
            MEMSIZE);
    fatal(msg);
  }
  return addr;
}


void init_cpu(xcpu *c){
  c->memory = calloc(MEMSIZE, sizeof(unsigned char));
  unsigned char i;
  for(i=0; i<=15; c->regs[i++]=0) // registers will need to be on the stack
    ;                             // so that each thread can maintain its own
  c->pc     = 0x0;
  c->state  = 0x0;
  c->itr    = 0x0;
}

void shutdown(xcpu *c){
  free(c->memory);
  free(c);
}
