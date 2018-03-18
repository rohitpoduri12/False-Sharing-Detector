#include "pin.H"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <unordered_map>
#include <string>

#define MAX_THREAD_ID 32

using namespace std;

typedef unsigned int uint;

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "sharing.out", "file name for falsely-shared cache block list");

int coun3t = 0;    //Used to count the number of false shared blocks
int max_tid = 0;   //Used to calculate the the maximum thread_id seen

PIN_LOCK lock;     //Used for locking a thread in the MemRef routine

class Thread	   //This class is used to keep track of the indices accessed by the threads for a particular block
{
  public:	   //Created the variables in public to avoid any complications
  unsigned long words_accessed[MAX_THREAD_ID+1];  //Creating an array of size MAX_THREAD_ID+1 which tells us the indices accessed by a particular thread
						  // 1 extra thread to take into consideration of main()
  Thread()	//Constructor which sets the initial value of words_accessed = 0
  {
   
     memset(words_accessed,0,(MAX_THREAD_ID+1)*sizeof(unsigned long));  //Using memset is faster as compared to setting each element to 0 in a loop
  }

};

tr1::unordered_map<long, class Thread > Maptoaddress; //Unordered map used to keep track of words accessed by threads corresponding to a unique block address

std::tr1::unordered_map<long, class Thread >::const_iterator got;  //Iterator of the unordered map used for searching a block address

long block_address, index;

// This analysis routine is called on every memory reference.
VOID MemRef(THREADID tid, VOID* addr) {

  PIN_GetLock(&lock, tid);  // Lock the thread to prevent accesses by multiple threads at the same time

  if(int(tid)>max_tid)	    // Used for updating the maximum thread id seen
	max_tid = int(tid);	
	
  block_address = ((unsigned long)addr)>>6;  //Block address is the address excluding the last 6 bits
  index = (((unsigned long)addr) & 0x3F)>>2; // Index/Word number is the last 6 bits of the address, excluding the last 2 bits
  
  got = Maptoaddress.find(block_address);  // Find if the specified block address is there in the unordered map

  if(got == Maptoaddress.end())		   // If the block addresss is not found, insert a new entry
  {
    Thread t1;
    t1.words_accessed[int(tid)] = 0 | 1<<index; //We set the bit corresponding to the word number
    Maptoaddress[block_address] = t1;
  }

  else					   // If found then simply update the corresponding entry
  {
    Maptoaddress[block_address].words_accessed[int(tid)] |= 1<<index; //We set the bit corresponding to the word number
  }
  
  PIN_ReleaseLock(&lock);    //Release the lock at the end of the routine
}

// Note: Instrumentation function adapted from ManualExamples/pinatrace.cpp
// It is called for each Trace and instruments reads and writes
VOID Trace(TRACE trace, VOID *v) {
  // Visit every basic block  in the trace
  for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
      for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {

	UINT32 memOperands = INS_MemoryOperandCount(ins);
	//    cout << "memOperands=" << memOperands << endl;
	for (UINT32 memOp = 0; memOp < memOperands; memOp++){
	  if (INS_MemoryOperandIsRead(ins, memOp)) {
	    //	  cout << "INS: " << INS_Disassemble(ins) << endl;
	    INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)MemRef, 
				     IARG_THREAD_ID,
				     IARG_MEMORYOP_EA, memOp,
				     IARG_END);
	  }
	  if (INS_MemoryOperandIsWritten(ins, memOp)) {
	    INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)MemRef, 
				     IARG_THREAD_ID,
				     IARG_MEMORYOP_EA, memOp,
				     IARG_END);
	  }
	}
      }
  }
}

VOID Fini(INT32 code, VOID *v) {

  string filename;
  std::ofstream out;
  filename =  KnobOutputFile.Value();
  out.open(filename.c_str());
  
  tr1::unordered_map<long, class Thread>:: iterator itr;  // Define an iterator
  for (itr = Maptoaddress.begin(); itr != Maptoaddress.end(); itr++) // Iteratate over the unordered map
   {
	int not_false_sharing=0, non_zero=0;  // Flag to check for not false sharing and counter to count number of non-zero elements in the array words_accessed

	   for(int i = 0; i< max_tid + 1 ; i++)
	   {

	   if(itr->second.words_accessed[i]!=0) //If word access info corresponding to an thread is non-zero
	   {
		unsigned long check_val = itr->second.words_accessed[i];

	   	for(int k = i+1; k< max_tid + 1; k++)  //Analyze all the remaining array elements with this array element
	        {

		    if((itr->second.words_accessed[k]!=0)&&((itr->second.words_accessed[k]&check_val)!=0))
		    // This condition above is valid only when there is no false sharing
		    // A Non-zero AND result between the 2 non-zero array elements implies that atleast one index was accessed by both the threads, so there is true sharing
		    {
			not_false_sharing = 1; //Not a case of false sharing if the above condition is satisfied
			break; //Break out of the loop as the remaining computations are useless 
		    }
	        }
	   }

		if(not_false_sharing==1)
		break; //Break out of the loop as the remaining computations are useless
	   }


	   if(not_false_sharing==0) //If not_false_sharing flag was not set to 1
	    {

		for(int n = 0; n< max_tid + 1 ; n++) //Count number of non-zero elements in the array
	   	{
			if(itr->second.words_accessed[n]!=0)
				non_zero++;
		}

		if(non_zero>1) // If it is greater than 1, it is a case of false sharing
		{
		  	coun3t++;  // Update the false sharing counter
			out<<std::hex<<itr->first<<std::endl; // Print the address of that block
		}
		//If the number of non-zero elements is not greater than 1 it implies that only 1 thread or no threads have accessed the block, which can never cause flase sharing
		
	    }
  }

  out.close();

}

INT32 Usage()
{
    PIN_ERROR("This Pintool prints list of falsely-shared cache blocks\n"
              + KNOB_BASE::StringKnobSummary() + "\n");
    return -1;
}

int main(int argc, char *argv[])
{
    if (PIN_Init(argc, argv)) return Usage();

    TRACE_AddInstrumentFunction(Trace, 0);
    PIN_AddFiniFunction(Fini, 0);

    // Never returns
    PIN_StartProgram();
    
    return 0;
}
