/**************************************************************/
/* CS/COE 1541
   Project 1 Superscalar Pipeline Simulator
   Collaborators: Zachary Whitney, Albert Yang, Jeremy Kato
   IDS: zdw9, aly31, jdk81					 			
   compile with gcc -o pipeline five_stage.c			
   and execute using							
   ./pipeline  /afs/cs.pitt.edu/courses/1541/short_traces/sample.tr	0  
***************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include <string.h>
#include "CPU.h" 

unsigned int check_load_use_hazard(struct instruction *first_instr, struct instruction *x);
unsigned int check_data_hazard(struct instruction *first_instr, struct instruction *x);
unsigned int check_control_hazard(struct instruction *first_instr, struct instruction *x);
void insert_noop(struct instruction *loc);
void insert_squashed(struct instruction *loc);
unsigned int is_branch_taken(struct instruction *branch_instr, struct instruction *next_instr);
void printtrace(struct instruction *instr);

struct instruction PACKING[2];
struct instruction PREFETCH[2];
int cycle_number = -2;  //start at -2 to ignore filling the PREFETCH QUEUE

int main(int argc, char **argv)
{
  struct instruction *tr_entry;
  struct instruction *tr_entry2;
  struct instruction IF_A, ID_A, EX_A, MEM_A, WB_A; //the ALU/branch/jump/special pipe
  struct instruction IF_B, ID_B, EX_B, MEM_B, WB_B; //the load/store pipe
  size_t size = 1;
  char *trace_file_name;
  int trace_view_on = 0;
  int prediction_method = 0;
  int instructions_packed = 2;  //to track whether to advance pipe 0, 1 or 2 instructions each cycle
  int flush_counter = 6; //5 stage pipeline and 2-part buffer, so we have to move 7 instructions once trace is done

  if (argc == 1) {
    fprintf(stdout, "\nUSAGE: tv <trace_file> <prediction method> <switch - any character> \n");
    fprintf(stdout, "\n(prediction method) 0 to predict NOT TAKEN, 1 to predict TAKEN.\n\n");
    fprintf(stdout, "\n(switch) to turn on or off individual item view.\n\n");
    exit(0);
  }
    
  trace_file_name = argv[1];
  if (argc > 2) prediction_method = atoi(argv[2]); 
  if (argc > 3) trace_view_on = atoi(argv[3]);

  fprintf(stdout, "\n ** opening file %s\n", trace_file_name);

  trace_fd = fopen(trace_file_name, "rb");

  if (!trace_fd) {
    fprintf(stdout, "\ntrace file %s not opened.\n\n", trace_file_name);
    exit(0);
  }

  trace_init();

  size = trace_get_item(&tr_entry); 
  if(size)
  	size = trace_get_item(&tr_entry2);

  while(1) {
    
    instructions_packed = 0;
   
    if (!size && flush_counter==0) {       /* no more instructions (instructions) to simulate */
      printf("+ Simulation terminates at cycle : %u\n", cycle_number);
      break;
    }
    else{              /* move the pipeline forward */
      cycle_number++;

      /* move instructions one stage ahead */
      WB_A = MEM_A;
      WB_B = MEM_B;
      MEM_A = EX_A;
      MEM_B = EX_B;
      EX_A = ID_A;
      EX_B = ID_B;
      ID_A = IF_A;
      ID_B = IF_B;
      IF_A = PACKING[0];
      IF_B = PACKING[1];

      if(cycle_number < 0)
      {
      	memcpy(&PREFETCH[0], tr_entry, sizeof(struct instruction));
       	memcpy(&PREFETCH[1], tr_entry2, sizeof(struct instruction));
      }
      else if(IF_A.type == ti_JRTYPE || IF_A.type == ti_JTYPE || (IF_A.type == ti_BRANCH && is_branch_taken(&IF_A, &PREFETCH[0])))
      {
        insert_squashed(&PACKING[0]);
        insert_squashed(&PACKING[1]);
      }
      else if(PREFETCH[0].type == ti_LOAD || PREFETCH[0].type == ti_STORE)
      {
        PACKING[1] = PREFETCH[0];
        instructions_packed++;
        if(PREFETCH[1].type == ti_LOAD || PREFETCH[1].type == ti_STORE || check_data_hazard(&PREFETCH[0], &PREFETCH[1]))
        {
          insert_noop(&PACKING[0]);
        }
        else
        {
          PACKING[0] = PREFETCH[1];
          instructions_packed++;
        }
      }
      else 
      {
        PACKING[0] = PREFETCH[0];
        instructions_packed++;

        if(PREFETCH[0].type == ti_BRANCH || PREFETCH[0].type == ti_JTYPE || PREFETCH[0].type == ti_JRTYPE)
        {
          insert_noop(&PACKING[1]);
        }
        else
        {
          if (!check_data_hazard(&PREFETCH[0], &PREFETCH[1]) && (PREFETCH[1].type == ti_LOAD || PREFETCH[1].type == ti_STORE))
          {
            PACKING[1] = PREFETCH[1];
            instructions_packed++;
          }
          else
          {
            insert_noop(&PACKING[1]);
          }
        }
        
      }

      //adjust results for possible load/use hazard
      if(IF_B.type == ti_LOAD)
      {
        //need a complete no-op cycle if first instruction is dependent
        if (instructions_packed > 0 && check_load_use_hazard(&IF_B, &PREFETCH[0]) )
        {
          insert_noop(&PACKING[0]);
          insert_noop(&PACKING[1]);
          instructions_packed = 0;
        }
        //no-op the second instruction only if it's the dependent one  
        else if (instructions_packed == 2 && check_load_use_hazard(&IF_B, &PREFETCH[1]) )
        {
          if(PREFETCH[1].type == ti_LOAD || PREFETCH[1].type == ti_STORE)
            insert_noop(&PACKING[1]);
          else
            insert_noop(&PACKING[0]);
          instructions_packed--;
        }
        
      }

      if(size && instructions_packed > 0)
      	size = trace_get_item(&tr_entry); /* put the instruction into a buffer */
      if(size && instructions_packed > 1)
        size = trace_get_item(&tr_entry2); /* put the second instruction into a buffer */

      if(!size){    /* if no more instructions in trace, feed NOOPS and reduce flush_counter */
        if(instructions_packed > 0)
        {
          flush_counter--;
          insert_noop(&PREFETCH[0]);
         }
        if(instructions_packed > 1)
          insert_noop(&PREFETCH[1]);
           
      }
      else{
        if(instructions_packed == 1)
        {
          PREFETCH[0] = PREFETCH[1];
          memcpy(&PREFETCH[1], tr_entry, sizeof(struct instruction));
        }
        if(instructions_packed == 2)
        {
          memcpy(&PREFETCH[0], tr_entry, sizeof(struct instruction));
          memcpy(&PREFETCH[1], tr_entry2, sizeof(struct instruction));
        }
      }

      //printf("==============================================================================\n");
    }   

    if (trace_view_on && cycle_number>=5) {
	
      switch(WB_A.type) {
        case ti_NOP:
          printf("[cycle %d] NOP:\n",cycle_number) ;
          break;
        case ti_RTYPE: 
          printf("[cycle %d] RTYPE:",cycle_number) ;
          printf(" (PC: %d)(sReg_a: %d)(sReg_b: %d)(dReg: %d) \n", WB_A.PC, WB_A.sReg_a, WB_A.sReg_b, WB_A.dReg);
          break;
        case ti_ITYPE:
          printf("[cycle %d] ITYPE:",cycle_number) ;
          printf(" (PC: %d)(sReg_a: %d)(dReg: %d)(addr: %d)\n", WB_A.PC, WB_A.sReg_a, WB_A.dReg, WB_A.Addr);
          break;
        case ti_LOAD:
          printf("[cycle %d] LOAD:",cycle_number) ;      
          printf(" (PC: %d)(sReg_a: %d)(dReg: %d)(addr: %d)\n", WB_A.PC, WB_A.sReg_a, WB_A.dReg, WB_A.Addr);
          break;
        case ti_STORE:
          printf("[cycle %d] STORE:",cycle_number) ;      
          printf(" (PC: %d)(sReg_a: %d)(sReg_b: %d)(addr: %d)\n", WB_A.PC, WB_A.sReg_a, WB_A.sReg_b, WB_A.Addr);
          break;
        case ti_BRANCH:
          printf("[cycle %d] BRANCH:",cycle_number) ;
          printf(" (PC: %d)(sReg_a: %d)(sReg_b: %d)(addr: %d)\n", WB_A.PC, WB_A.sReg_a, WB_A.sReg_b, WB_A.Addr);
          break;
        case ti_JTYPE:
          printf("[cycle %d] JTYPE:",cycle_number) ;
          printf(" (PC: %d)(addr: %d)\n", WB_A.PC, WB_A.Addr);
          break;
        case ti_SPECIAL:
          printf("[cycle %d] SPECIAL:\n",cycle_number) ;      	
          break;
        case ti_JRTYPE:
          printf("[cycle %d] JRTYPE:",cycle_number) ;
          printf(" (PC: %d) (sReg_a: %d)(addr: %d)\n", WB_A.PC, WB_A.dReg, WB_A.Addr);
          break;
        case ti_SQUASHED:
          printf("[cycle %d] SQUASHED\n",cycle_number) ;
          break;
		
      }
	
      switch(WB_B.type) {
        case ti_NOP:
          printf("[cycle %d] NOP:\n",cycle_number) ;
          break;
        case ti_RTYPE:
          printf("[cycle %d] RTYPE:",cycle_number) ;
          printf(" (PC: %d)(sReg_a: %d)(sReg_b: %d)(dReg: %d) \n", WB_B.PC, WB_B.sReg_a, WB_B.sReg_b, WB_B.dReg);
          break;
        case ti_ITYPE:
          printf("[cycle %d] ITYPE:",cycle_number) ;
          printf(" (PC: %d)(sReg_a: %d)(dReg: %d)(addr: %d)\n", WB_B.PC, WB_B.sReg_a, WB_B.dReg, WB_B.Addr);
          break;
        case ti_LOAD:
          printf("[cycle %d] LOAD:",cycle_number) ;      
          printf(" (PC: %d)(sReg_a: %d)(dReg: %d)(addr: %d)\n", WB_B.PC, WB_B.sReg_a, WB_B.dReg, WB_B.Addr);
          break;
        case ti_STORE:
          printf("[cycle %d] STORE:",cycle_number) ;      
          printf(" (PC: %d)(sReg_a: %d)(sReg_b: %d)(addr: %d)\n", WB_B.PC, WB_B.sReg_a, WB_B.sReg_b, WB_B.Addr);
          break;
        case ti_BRANCH:
          printf("[cycle %d] BRANCH:",cycle_number) ;
          printf(" (PC: %d)(sReg_a: %d)(sReg_b: %d)(addr: %d)\n", WB_B.PC, WB_B.sReg_a, WB_B.sReg_b, WB_B.Addr);
          break;
        case ti_JTYPE:
          printf("[cycle %d] JTYPE:",cycle_number) ;
          printf(" (PC: %d)(addr: %d)\n", WB_B.PC, WB_B.Addr);
          break;
        case ti_SPECIAL:
          printf("[cycle %d] SPECIAL:\n",cycle_number) ;        
          break;
        case ti_JRTYPE:
          printf("[cycle %d] JRTYPE:",cycle_number) ;
          printf(" (PC: %d) (sReg_a: %d)(addr: %d)\n", WB_B.PC, WB_B.dReg, WB_B.Addr);
          break;
        case ti_SQUASHED:
          printf("[cycle %d] SQUASHED\n",cycle_number) ;
          break;

      }
    }
    
  }

  trace_uninit();

  exit(0);
}

//returns 1 if a load/use hazard is found, else returns 0
//x is assumed to be the possibly dependent instruction following the LOAD
unsigned int check_load_use_hazard(struct instruction *first_instr, struct instruction *x)
{
  if(first_instr->type != ti_LOAD)
    return 0;
  int hazard_register = first_instr->dReg;
  if(x->sReg_a == hazard_register && x->type != ti_JTYPE && x->type != ti_SPECIAL && x->type != ti_SQUASHED)
    return 1;
  if(x->sReg_b == hazard_register && (x->type == ti_RTYPE || x->type == ti_STORE || x->type == ti_BRANCH))
    return 1;

  return 0;
}

//returns 1 if a data hazard is found, else returns 0
//x should be the other candidate instruction to pack along with first_instr
unsigned int check_data_hazard(struct instruction *first_instr, struct instruction *x)
{
  //we can only have write/read hazard if first instruction writes a register
  if(first_instr->type != ti_LOAD && first_instr->type != ti_RTYPE && first_instr->type != ti_ITYPE)
    return 0;
  int hazard_register = first_instr->dReg;
  if(x->sReg_a == hazard_register && x->type != ti_JTYPE && x->type != ti_SPECIAL && x->type != ti_SQUASHED)
    return 1;
  if(x->sReg_b == hazard_register && (x->type == ti_RTYPE || x->type == ti_STORE || x->type == ti_BRANCH))
    return 1;

  return 0;
}

//returns 1 if a control hazard is found, else returns 0
//x is the instruction following the possible BRANCH
unsigned int check_control_hazard(struct instruction *first_instr, struct instruction *next_instr)
{
  if(first_instr->type != ti_BRANCH)
    return 0;
  return is_branch_taken(first_instr, next_instr);
}

//puts a NO_OP in the desired location
void insert_noop(struct instruction *loc)
{
  struct instruction NO_OP;
  memset(&NO_OP, 0, sizeof(struct instruction));
  *loc = NO_OP;
}

//puts a NO_OP which prints in trace as "SQUASHED" in the desired location
void insert_squashed(struct instruction *loc)
{
  struct instruction SQUASHED_OP;
  memset(&SQUASHED_OP, 0, sizeof(struct instruction));
  SQUASHED_OP.type = ti_SQUASHED;
  *loc = SQUASHED_OP;
}

//returns 0 if not taken and 1 if taken
unsigned int is_branch_taken(struct instruction *branch_instr, struct instruction *next_instr)
{
  return (branch_instr->PC != next_instr->PC - 4);
}

void printtrace(struct instruction *instr)
{
	struct instruction W = *instr;
	switch(W.type) {
        case ti_NOP:
          printf("[cycle %d] NOP:\n",cycle_number) ;
          break;
        case ti_RTYPE: /* registers are translated for printing by subtracting offset  */
          printf("[cycle %d] RTYPE:",cycle_number) ;
          printf(" (PC: %d)(sReg_a: %d)(sReg_b: %d)(dReg: %d) \n", W.PC, W.sReg_a, W.sReg_b, W.dReg);
          break;
        case ti_ITYPE:
          printf("[cycle %d] ITYPE:",cycle_number) ;
          printf(" (PC: %d)(sReg_a: %d)(dReg: %d)(addr: %d)\n", W.PC, W.sReg_a, W.dReg, W.Addr);
          break;
        case ti_LOAD:
          printf("[cycle %d] LOAD:",cycle_number) ;      
          printf(" (PC: %d)(sReg_a: %d)(dReg: %d)(addr: %d)\n", W.PC, W.sReg_a, W.dReg, W.Addr);
          break;
        case ti_STORE:
          printf("[cycle %d] STORE:",cycle_number) ;      
          printf(" (PC: %d)(sReg_a: %d)(sReg_b: %d)(addr: %d)\n", W.PC, W.sReg_a, W.sReg_b, W.Addr);
          break;
        case ti_BRANCH:
          printf("[cycle %d] BRANCH:",cycle_number) ;
          printf(" (PC: %d)(sReg_a: %d)(sReg_b: %d)(addr: %d)\n", W.PC, W.sReg_a, W.sReg_b, W.Addr);
          break;
        case ti_JTYPE:
          printf("[cycle %d] JTYPE:",cycle_number) ;
          printf(" (PC: %d)(addr: %d)\n", W.PC, W.Addr);
          break;
        case ti_SPECIAL:
          printf("[cycle %d] SPECIAL:\n",cycle_number) ;        
          break;
        case ti_JRTYPE:
          printf("[cycle %d] JRTYPE:",cycle_number) ;
          printf(" (PC: %d) (sReg_a: %d)(addr: %d)\n", W.PC, W.dReg, W.Addr);
          break;
        case ti_SQUASHED:
          printf("[cycle %d] SQUASHED\n",cycle_number) ;
          break;
	}
}
