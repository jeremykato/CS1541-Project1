/**************************************************************/
/* CS/COE 1541				 			
   Project 1 5-Stage Pipeline Simulator
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

unsigned char get_prediction(struct instruction *instr);
void update_prediction(struct instruction *instr, unsigned char new_prediction);
unsigned int check_data_hazard(struct instruction *first_instr, struct instruction *x);
unsigned int check_control_hazard(struct instruction *first_instr, struct instruction *x);
void insert_noop(struct instruction *loc);
void insert_squashed(struct instruction *loc);
unsigned int is_branch_taken(struct instruction *branch_instr, struct instruction *next_instr);
unsigned char is_right_target(struct instruction *instr);

struct prediction ht[HASH_TABLE_SIZE];
struct instruction PREFETCH[2];

int main(int argc, char **argv)
{
  struct instruction *tr_entry;
  struct instruction IF, ID, EX, MEM, WB;
  size_t size;
  char *trace_file_name;
  int trace_view_on = 0;
  int prediction_method = 0;
  int squash_counter = 0;
  int flush_counter = 6; //5 stage pipeline and 2-part buffer, so we have to move 6 instructions once trace is done
  
  int cycle_number = -2;  //start at -2 to ignore filling the PREFETCH QUEUE

  memset(ht, 0, HASH_TABLE_SIZE * sizeof(struct prediction));

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

  while(1) {

    int has_data_hazard = check_data_hazard(&PREFETCH[0], &PREFETCH[1]);

    if(prediction_method == 0 && check_control_hazard(&PREFETCH[0], &PREFETCH[1]))
      squash_counter++;

    if(prediction_method == 1 && PREFETCH[0].type == ti_BRANCH)
    {
      int branch_result = is_branch_taken(&PREFETCH[0], &PREFETCH[1]);
      if (get_prediction(&PREFETCH[0]) != branch_result || (branch_result == TAKEN && !is_right_target(&PREFETCH[0])))
        squash_counter++;
      update_prediction(&PREFETCH[0], branch_result);
    }
    else if (prediction_method == 1 && (PREFETCH[0].type == ti_JRTYPE || PREFETCH[0].type == ti_JTYPE))
    {
      if(!is_right_target(&PREFETCH[0]))
        squash_counter++;
      update_prediction(&PREFETCH[0], TAKEN);
    }

    if(!has_data_hazard && !squash_counter)
      size = trace_get_item(&tr_entry); /* put the instruction into a buffer */
   
    if (!size && flush_counter==0) {       /* no more instructions (instructions) to simulate */
      printf("+ Simulation terminates at cycle : %u\n", cycle_number);
      break;
    }
    else{              /* move the pipeline forward */
      cycle_number++;

      /* move instructions one stage ahead */
      WB = MEM;
      MEM = EX;
      EX = ID;
      ID = IF;
      IF = PREFETCH[0];

      if(squash_counter)
        insert_squashed(&PREFETCH[0]);
      else if(has_data_hazard) 
        insert_noop(&PREFETCH[0]);
      else
        PREFETCH[0] = PREFETCH[1];

      if(!size){    /* if no more instructions in trace, feed NOOPS and reduce flush_counter */
        insert_noop(&PREFETCH[1]);
        flush_counter--;   
      }
      else{   
        if(!has_data_hazard && !squash_counter)
          memcpy(&PREFETCH[1], tr_entry , sizeof(IF));
      }

      if(squash_counter)
        squash_counter--;

      //printf("==============================================================================\n");
    }  


    if (trace_view_on && cycle_number>=5) {/* print the executed instruction if trace_view_on=1 */
      switch(WB.type) {
        case ti_NOP:
          printf("[cycle %d] NOP:\n",cycle_number) ;
          break;
        case ti_RTYPE: /* registers are translated for printing by subtracting offset  */
          printf("[cycle %d] RTYPE:",cycle_number) ;
          printf(" (PC: %d)(sReg_a: %d)(sReg_b: %d)(dReg: %d) \n", WB.PC, WB.sReg_a, WB.sReg_b, WB.dReg);
          break;
        case ti_ITYPE:
          printf("[cycle %d] ITYPE:",cycle_number) ;
          printf(" (PC: %d)(sReg_a: %d)(dReg: %d)(addr: %d)\n", WB.PC, WB.sReg_a, WB.dReg, WB.Addr);
          break;
        case ti_LOAD:
          printf("[cycle %d] LOAD:",cycle_number) ;      
          printf(" (PC: %d)(sReg_a: %d)(dReg: %d)(addr: %d)\n", WB.PC, WB.sReg_a, WB.dReg, WB.Addr);
          break;
        case ti_STORE:
          printf("[cycle %d] STORE:",cycle_number) ;      
          printf(" (PC: %d)(sReg_a: %d)(sReg_b: %d)(addr: %d)\n", WB.PC, WB.sReg_a, WB.sReg_b, WB.Addr);
          break;
        case ti_BRANCH:
          printf("[cycle %d] BRANCH:",cycle_number) ;
          printf(" (PC: %d)(sReg_a: %d)(sReg_b: %d)(addr: %d)\n", WB.PC, WB.sReg_a, WB.sReg_b, WB.Addr);
          break;
        case ti_JTYPE:
          printf("[cycle %d] JTYPE:",cycle_number) ;
          printf(" (PC: %d)(addr: %d)\n", WB.PC,WB.Addr);
          break;
        case ti_SPECIAL:
          printf("[cycle %d] SPECIAL:\n",cycle_number) ;      	
          break;
        case ti_JRTYPE:
          printf("[cycle %d] JRTYPE:",cycle_number) ;
          printf(" (PC: %d) (sReg_a: %d)(addr: %d)\n", WB.PC, WB.dReg, WB.Addr);
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

//predicts last choice for a given branch if stored in predictor, else predicts NOT TAKEN
unsigned char get_prediction(struct instruction *instr)
{
  struct prediction p = ht[HASH(instr->PC)];
  return p.taken;
}

//checks a jump or branch target address versus the target in the hash table
//returns 1 if they match, otherwise returns 0
unsigned char is_right_target(struct instruction *instr)
{
  struct prediction p = ht[HASH(instr->PC)];
  return instr->Addr == p.target;
}

//updates the BTB for given instruction
void update_prediction(struct instruction *instr, unsigned char new_prediction)
{
  int index = HASH(instr->PC);
  struct prediction p;
  p.taken = new_prediction;
  p.target =  instr->Addr;
  ht[index] = p;
}

//returns 1 if a data hazard is found, else returns 0
//x is assumed to be the possibly dependent instruction following the LOAD
unsigned int check_data_hazard(struct instruction *first_instr, struct instruction *x)
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

//returns 1 if a control hazard is found, else returns 0
//x is the instruction following the possible BRANCH
unsigned int check_control_hazard(struct instruction *first_instr, struct instruction *next_instr)
{
  if(first_instr->type == ti_JTYPE || first_instr->type == ti_JRTYPE)
    return 1;
  if(first_instr->type == ti_BRANCH)
    return is_branch_taken(first_instr, next_instr);
  return 0;
}

//puts a NO_OP in the desired location
void insert_noop(struct instruction *loc)
{
  struct instruction NO_OP;
  memset(&NO_OP, 0, sizeof(struct instruction));
  *loc = NO_OP;
}

//puts a NO_OP in the desired location
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
  if (branch_instr->PC != next_instr->PC - 4)
    return 1;

  return 0;
}
