#define XCPU
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "xis.h"
#include "xcpu.h"
#include "xdb.h"


/**************************************************************************
   Gracefully terminate the programme on unrecoverable error.
***************************************************************************/
void fatal(char *errmsg){
  fprintf(stderr, "%s\n", errmsg);
  exit(EXIT_FAILURE);  
  // some memory-freeing should happen here too...
}

/* **************************************************************************
   CREATE A JUMP TABLE TO THE INSTRUCTION FUNCTIONS
   =-=-=-=-=-=-=-=-=-=-=-==-=-=-=-=-=-=-=-=-=-=-=-=
   To be called only *once* per process. This should save time, as compared
   to letting the compiler construct a jump table (as its interpretation of
   a switch/case sequence) each time xcpu_execute is called.
   (Called from xsim.c, just prior to the first execution loop.)
**************************************************************************/
void* build_jump_table(void){
  IHandler *table = malloc(0x100 * sizeof(IHandler)); 
  if (table == NULL){
    fprintf(stderr, "FAILURE IN <build_jump_table>: OUT OF MEMORY\n");
    exit(EXIT_FAILURE);
  }
  int p;
  for (p = 0; p < 0x100; p++){
    table[p] = bad;
  }
  table[I_BAD]  = bad;     table[I_RET]   = ret;     table[I_STD]   = std;
  table[I_NEG]  = neg;     table[I_NOT]   = not;     table[I_PUSH]  = push;
  table[I_POP]  = pop;     table[I_JMPR]  = jmpr;    table[I_CALLR] = callr;
  table[I_OUT]  = out;     table[I_INC]   = inc;     table[I_DEC]   = dec;
  table[I_BR]   = br;      table[I_JR]    = jr;      table[I_ADD]   = add;
  table[I_SUB]  = sub;     table[I_MUL]   = mul;     table[I_DIV]   = divide;
  table[I_AND]  = and;     table[I_OR]    = or;      table[I_XOR]   = xor;
  table[I_SHR]  = shr;     table[I_SHL]   = shl;     table[I_TEST]  = test;
  table[I_CMP]  = cmp;     table[I_EQU]   = equ;     table[I_MOV]   = mov;
  table[I_LOAD] = load;    table[I_STOR]  = stor;    table[I_LOADB] = loadb;
  table[I_STORB]= storb;   table[I_JMP]   = jmp;     table[I_CALL]  = call;
  table[I_LOADI]= loadi;   table[I_CLD]   = cld;
  // Instructions for handling interrupts (added in Assignment 2)
  table[I_CLI]  = cli;     table[I_STI]   = sti;     table[I_IRET]  = iret;
  table[I_TRAP] = trap;    table[I_LIT]   = lit;
  // Instructions for handling threading (added in Assignment 3)
  table[I_CPUID] = cpuid;  table[I_CPUNUM]= cpunum;  table[I_LOADA] = loada;
  table[I_TNSET] = tnset;  table[I_STORA] = stora;
  return table;
}
/*************************************************
 * Free the memory allocated to the jump table.  *
 * To be called once, at the end of the process. *
 *************************************************/
void destroy_jump_table(void* table){
  free(table);
}
/******************************************
 Display information about the xcpu context
 (Legacy debugger.)
*******************************************/
void xcpu_print( xcpu *c ) {
  int i;
  unsigned int op1;
  int op2;

  static pthread_mutex_t lk = PTHREAD_MUTEX_INITIALIZER;
  if (pthread_mutex_lock(&lk)){
    printf("Failure to acquire lock!");
    abort();
  }

  fprintf( stdout, "%2.2d> PC: %4.4x, State: %4.4x\n" , c->id, c->pc, c->state );
  fprintf( stdout, "%2.2d> Registers: ", c->id );
  for( i = 0; i < X_MAX_REGS; i++ ) {
    if( !( i % 8 ) ) {
      fprintf( stdout, "\n%2.2d>     ", c->id );
    }
    fprintf( stdout, " r%2.2d:%4.4x", i, c->regs[i] );
  }
  fprintf( stdout, "\n" );

  op1 = c->memory[c->pc];
  op2 = c->memory[c->pc + 1];
  for( i = 0; i < I_NUM; i++ ) {
    if( x_instructions[i].code == c->memory[c->pc] ) {
      fprintf( stdout, "%2.2d> Instruction: %s ", c->id,
               x_instructions[i].inst );
      break;
    }
  }

  switch( XIS_NUM_OPS( op1 ) ) {
  case 1:
    if( op1 & XIS_1_IMED ) {
      fprintf( stdout, "%d", op2 );
    } else {
      fprintf( stdout, "r%d", XIS_REG1( op2 ) );
    }
    break;
  case 2:
    fprintf( stdout, "r%d, r%d", XIS_REG1( op2 ), XIS_REG2( op2 ) );
    break;
  case XIS_EXTENDED:
    fprintf( stdout, "%u", (c->memory[c->pc + 2] << 8) | c->memory[c->pc + 3] );
    if( op1 & XIS_X_REG ) {
      fprintf( stdout, ", r%d", XIS_REG1( op2 ) );
    }
    break;
  }
  fprintf( stdout, "\n" );

  if( pthread_mutex_unlock( &lk ) ) {
    printf( "Failure to release lock!" );
    abort();
  }
}

/************************************************************
 Read the opcode, and use it as an index into the jump table. 
 Continue returning 1 until the programme halts (and the global
 _halt variable is set to 0), and which point return 0. The
 actual 'halting' is handled by xsim.c.
*************************************************************/
int xcpu_execute(xcpu *c, IHandler *table) {

  unsigned char opcode; //[c->num];
  unsigned short int instruction; //[c->num];
  
  instruction = FETCH_WORD(c->pc);
  opcode = (unsigned char)( (instruction >> 8) & 0x00FF); 
  c->pc += WORD_SIZE;  // extended instructions will increment pc a 2nd time
  (table[opcode])(c, instruction);
  if (c->state & 0x2) // check
    xcpu_print(c);
  //////////////////////
  //if (c->memory[0x0268] == c->id){
  //  printf("c->memory[0x0268] == %4.4x when c->id == %d @  c->pc == %4.4x !!\n", FETCH_WORD(0x0268), c->id,  c->pc);
  //  exit(EXIT_FAILURE);
  // }

  ///////////////////


  return (opcode != I_BAD); //_halt;
}


int xcpu_exception( xcpu *c, unsigned int ex ) {
  if (c->state & X_STATE_IN_EXCEPTION) {
    return 1; // return, doing nothing, but report success
  } else if (c->itr && (ex < X_E_LAST)) { // if itr loaded, and ex valid
    int i = ex * WORD_SIZE; // is this right?
    /* "i should be 0 if the exception is a regular interrupt; i should be
       2 if the exception is a trap; and i should be 4 if the exception is a
       fault." ex takes a value from the X_E_* enum defined in xcpu.h
       remember that WORD_SIZE = 2.
    */
    PUSHER(c->state);
    PUSHER(c->pc);
    c->state |= 0x0004;
    //LOCK(cpulock);
    c->pc = FETCH_WORD(c->itr+i); // re: i, see comment above
    //UNLOCK(cpulock);
    return 1; // but returns 0 when not successful. How is this gauged?
  }
  return 0;
}



/*********************************
 * THE INSTRUCTION SET FUNCTIONS *
 *********************************/

INSTRUCTION(bad){
  if ((unsigned char)( (instruction >> 8) & 0x00FF) != 0x00){ 
    printf("\n***** BAD INSTRUCTION ON CPU %d: 0x%4.4x at PC 0x%4.4x *****\n",
           c->id, instruction, (c->pc)-WORD_SIZE);
  }
}
INSTRUCTION(ret){
  POPPER(c->pc);
}
INSTRUCTION(cld){
  c->state &= 0xfffd;
  // clear the debug bit
}
INSTRUCTION(std){
  c->state |= 0x0002;
}
INSTRUCTION(neg){
  c->regs[XIS_REG1(instruction)] = ~c->regs[XIS_REG1(instruction)]+1;
  // assuming 2'sC
}
INSTRUCTION(not){
  c->regs[XIS_REG1(instruction)] = !c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(push){
  PUSHER(c->regs[XIS_REG1(instruction)]);
}
INSTRUCTION(pop){
  POPPER(c->regs[XIS_REG1(instruction)]);
}
INSTRUCTION(jmpr){
  c->pc = c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(callr){
  PUSHER(c->pc);
  c->pc = c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(out){
  fprintf(stdout, "%c", (char)(c->regs[XIS_REG1(instruction)] & 0xFF));
}
INSTRUCTION(inc){
  c->regs[XIS_REG1(instruction)]++;
}
INSTRUCTION(dec){
  c->regs[XIS_REG1(instruction)]--;
}
INSTRUCTION(br){
  signed char leap = instruction & 0x00FF;
  if (c->state & 0x0001) // if condition bit of state == 1
    c->pc = (c->pc-WORD_SIZE)+leap;
}
INSTRUCTION(jr){
  signed char leap = instruction & 0x00FF;
  c->pc = (c->pc-WORD_SIZE)+leap; // second byte of instruction = Label
}
INSTRUCTION(add){
  c->regs[XIS_REG2(instruction)] =
    (c->regs[XIS_REG1(instruction)] + c->regs[XIS_REG2(instruction)]) % 0x10000;
}
INSTRUCTION(sub){
  c->regs[XIS_REG2(instruction)] = (unsigned short)
    c->regs[XIS_REG2(instruction)] + ~c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(mul){
  c->regs[XIS_REG2(instruction)] =
    (c->regs[XIS_REG2(instruction)] * c->regs[XIS_REG1(instruction)]) % 0x10000;
}
INSTRUCTION(divide){  
  c->regs[XIS_REG2(instruction)] =
    c->regs[XIS_REG2(instruction)] / c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(and){
  c->regs[XIS_REG2(instruction)] =
    c->regs[XIS_REG1(instruction)] & c->regs[XIS_REG2(instruction)];
}
INSTRUCTION(or){
  c->regs[XIS_REG2(instruction)] =
    c->regs[XIS_REG2(instruction)] | c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(xor){
  c->regs[XIS_REG2(instruction)] =
    c->regs[XIS_REG2(instruction)] ^ c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(shr){
  c->regs[XIS_REG2(instruction)] = (unsigned short)
    c->regs[XIS_REG2(instruction)] >> c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(shl){
  c->regs[XIS_REG2(instruction)] = (unsigned short)
    c->regs[XIS_REG2(instruction)] << c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(test){
  c->state = (c->regs[XIS_REG1(instruction)] & c->regs[XIS_REG2(instruction)])?
    (c->state | 0x0001) : (c->state & 0xFFFE);
}
INSTRUCTION(cmp){
  c->state = (c->regs[XIS_REG1(instruction)] < c->regs[XIS_REG2(instruction)])?
    (c->state | 0x0001) : (c->state & 0xFFFE);
  /** NB: cmp is only defined for unsigned integer values **/
}
INSTRUCTION(equ){
  c->state =
    (c->regs[XIS_REG1(instruction)]==c->regs[XIS_REG2(instruction)])?
    (c->state | 0x0001) :
    (c->state & 0xFFFE);
}
INSTRUCTION(mov){
  c->regs[XIS_REG2(instruction)] = c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(load){
  c->regs[XIS_REG2(instruction)] =
    FETCH_WORD(c->regs[XIS_REG1(instruction)]);
}
INSTRUCTION(stor){
  c->memory[c->regs[XIS_REG2(instruction)] % MEMSIZE] =
    (unsigned char) ((c->regs[XIS_REG1(instruction)] >> 8));
  c->memory[(c->regs[XIS_REG2(instruction)]+1) % MEMSIZE] =
    (unsigned char) ((c->regs[XIS_REG1(instruction)]) & 0xFF);
}
INSTRUCTION(loadb){
  c->regs[XIS_REG2(instruction)] =
    c->memory[c->regs[XIS_REG1(instruction)] % MEMSIZE];
}
INSTRUCTION(storb){
  c->memory[c->regs[XIS_REG2(instruction)] % MEMSIZE] =
    c->regs[XIS_REG1(instruction)] & 0x00FF; // low byte mask
}
/*************************
 * extended instructions *
 *************************/
INSTRUCTION(jmp){
  c->pc = FETCH_WORD(c->pc);
}
INSTRUCTION(call){
  unsigned short int label = FETCH_WORD(c->pc);
  c->pc += WORD_SIZE;
  PUSHER(c->pc);
  c->pc = label; 
}
INSTRUCTION(loadi){
  unsigned short int value = FETCH_WORD(c->pc);
  c->pc += WORD_SIZE;
  c->regs[XIS_REG1(instruction) ] = value;
}
/*** INTERRUPT-HANDLING INSTRUCTIONS ***/
INSTRUCTION(cli){
  c->state &= 0xFFFB; // 0xFFFB == ~0x0004
}
INSTRUCTION(sti){
  c->state |= 0x0004;
}
INSTRUCTION(iret){
  POPPER(c->pc);
  POPPER(c->state);
}
INSTRUCTION(trap){
  if (!(c->state & 0x0004)){
    xcpu_exception(c, X_E_TRAP); 
  }
}
INSTRUCTION(lit){
  c->itr = c->regs[XIS_REG1(instruction)];
}
/*** THREADING INSTRUCTIONS ***/
INSTRUCTION(cpuid){
  c->regs[XIS_REG1(instruction)] = c->id;
}
INSTRUCTION(cpunum){
  c->regs[XIS_REG1(instruction)] = c->num;
}
INSTRUCTION(loada){
  LOCK(elk);  
  c->regs[XIS_REG2(instruction)] = FETCH_WORD(c->regs[XIS_REG1(instruction)]);
  UNLOCK(elk);
}
INSTRUCTION(stora){
  LOCK(elk);
  c->memory[c->regs[XIS_REG2(instruction)] % MEMSIZE] =
    (unsigned char) ((c->regs[XIS_REG1(instruction)] >> 8));
  c->memory[(c->regs[XIS_REG2(instruction)]+1) % MEMSIZE] =
    (unsigned char) ((c->regs[XIS_REG1(instruction)]) & 0xFF);  
  UNLOCK(elk);
}
INSTRUCTION(tnset){
  LOCK(elk);
  c->regs[XIS_REG2(instruction)] = FETCH_WORD(c->regs[XIS_REG1(instruction)]);
  c->memory[c->regs[XIS_REG1(instruction)] % MEMSIZE] = 0;
  c->memory[(c->regs[XIS_REG1(instruction)]+1) % MEMSIZE] = 1;
  UNLOCK(elk);
}

/** That's all, folks! **/
char *sig="\xde\xba\x5e\x12";
