/***************** Transaction class **********************/
/*** Implements methods that handle Begin, Read, Write, ***/
/*** Abort, Commit operations of transactions. These    ***/
/*** methods are passed as parameters to threads        ***/
/*** spawned by Transaction manager class.              ***/
/**********************************************************/

/* Required header files */
#include <stdio.h>
#include <stdlib.h>
#include <sys/signal.h>
#include "zgt_def.h"
#include "zgt_tm.h"
#include "zgt_extern.h"
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <pthread.h>


extern void *start_operation(long, long);  //starts opeartion by doing conditional wait
extern void *finish_operation(long);       // finishes abn operation by removing conditional wait
extern void *open_logfile_for_append();    //opens log file for writing
extern void *do_commit_abort(long, char);   //commit/abort based on char value (the code is same for us)

extern zgt_tm *ZGT_Sh;			// Transaction manager object

FILE *logfile; //declare globally to be used by all

/* Transaction class constructor */
/* Initializes transaction id and status and thread id */
/* Input: Transaction id, status, thread id */

zgt_tx::zgt_tx( long tid, char Txstatus,char type, pthread_t thrid){
  this->lockmode = (char)' ';  //default
  this->Txtype = type; //Fall 2014[jay] R = read only, W=Read/Write
  this->sgno =1;
  this->tid = tid;
  this->obno = -1; //set it to a invalid value
  this->status = Txstatus;
  this->pid = thrid;
  this->head = NULL;
  this->nextr = NULL;
  this->semno = -1; //init to  an invalid sem value
}

/* Method used to obtain reference to a transaction node      */
/* Inputs the transaction id. Makes a linear scan over the    */
/* linked list of transaction nodes and returns the reference */
/* of the required node if found. Otherwise returns NULL      */

zgt_tx* get_tx(long tid1){  
  zgt_tx *txptr, *lastr1;
  
  if(ZGT_Sh->lastr != NULL){	// If the list is not empty
      lastr1 = ZGT_Sh->lastr;	// Initialize lastr1 to first node's ptr
      for  (txptr = lastr1; (txptr != NULL); txptr = txptr->nextr)
	    if (txptr->tid == tid1) 		// if required id is found									
	       return txptr; 
      return (NULL);			// if not found in list return NULL
   }
  return(NULL);				// if list is empty return NULL
}

/* Method that handles "BeginTx tid" in test file     */
/* Inputs a pointer to transaction id, obj pair as a struct. Creates a new  */
/* transaction node, initializes its data members and */
/* adds it to transaction list */

void *begintx(void *arg){
  //Initialize a transaction object. Make sure it is
  //done after acquiring the semaphore for the tm and making sure that 
  //the operation can proceed using the condition variable. when creating
  //the tx object, set the tx to TR_ACTIVE and obno to -1; there is no 
  //semno as yet as none is waiting on this tx.
  
    struct param *node = (struct param*)arg;// get tid and count
    start_operation(node->tid, node->count); 
    zgt_tx *tx = new zgt_tx(node->tid,TR_ACTIVE, node->Txtype, pthread_self());	// Create new tx node
    open_logfile_for_append();
    fprintf(logfile, "T%d\t%c \tBeginTx\n", node->tid, node->Txtype);	// Write log record and close
    fflush(logfile);
  
    zgt_p(0);				// Lock Tx manager; Add node to transaction list
  
    tx->nextr = ZGT_Sh->lastr;
    ZGT_Sh->lastr = tx;   
    zgt_v(0); 			// Release tx manager

  
  finish_operation(node->tid);
  pthread_exit(NULL);				// thread exit

}








/*
This function is a recursive function which checks all the conditions and does the appropriate action 
based on whether it gets a lock or it doesnt get a lock
*/

void *readRecursive(void *arg)
{


  struct param *node = (struct param*)arg;// get tid and objno and count
  zgt_tx *presentTX = get_tx(node->tid);
  zgt_hlink *translinkwithlock = ZGT_Ht->find(1, node->obno);  // gets the transaction which has the lock on given object

  zgt_p(0);  // Lock the TM table

  if(translinkwithlock==NULL) //if object is not there in the table then set lock on the object by the given transaction
  {
    ZGT_Ht->add(presentTX,1,node->obno,'S');
    presentTX->perform_readWrite(presentTX->tid,node->obno,'S');  //USAGE perform_readWrite(long tid,long obno, char lockmode)
    zgt_v(0);
  }
  else  //Lock hash table contains that object
    {
        zgt_tx *transwithlock = get_tx(translinkwithlock->tid);
    if(transwithlock->tid==presentTX->tid || transwithlock->Txtype=='R')  //object held by same transaction
    {

        presentTX->perform_readWrite(presentTX->tid,node->obno,'S');  //USAGE perform_readWrite(long tid,long obno, char lockmode)
        zgt_v(0);  


    } // Inner If Close
    
    else  //Object held by a different transaction in write mode
    {  


      presentTX->status = TR_WAIT;   //We make the present Transaction to wait for the object
      presentTX->lockmode = 'S';
      open_logfile_for_append();
      fprintf(logfile, "T%d\t\tWaiting\t\t%d\n", node->tid,node->obno); // Write the waiting status of the Tx to the output log
      fflush(logfile);


      presentTX->setTx_semno(transwithlock->tid,transwithlock->tid);  //Change the semno of the tx which is holding the lock to same as TID
      zgt_v(0);   // Release the lock before going to sleep
      zgt_p(transwithlock->semno);  // Lock the current tx on the semaphor of the tx which has the lock
      // Thread sleeps till here
      zgt_p(0);  //wakes up and locks the table
      presentTX->status  = TR_ACTIVE;  //Changes the state to Active
      zgt_v(0);  // Lock the table 

       readRecursive(arg);  //Call the readrecursive method



    }// Inner Else Close
     
    }//Outer Else close


}// END OF READ RECUSIVE FUNCTION



/*
This function is a recursive function which checks all the conditions and does the appropriate action 
based on whether it gets a lock or it doesnt get a lock
*/
void *writeRecursive(void *arg)
{

  struct param *node = (struct param*)arg;  // struct parameter that contains tid, obno, count
  zgt_tx *presentTX = get_tx(node->tid); //Get the current tx
  zgt_hlink *translinkwithlock = ZGT_Ht->find(1, node->obno);  // gets the transaction which has the lock on given object
  zgt_p(0);  // lock the table

  if(translinkwithlock==NULL) //if object is not there in the table then set lock on the object by the given transaction
  {
    ZGT_Ht->add(presentTX,1,node->obno,'X');
        zgt_v(0);
    presentTX->perform_readWrite(presentTX->tid,node->obno,'X');  //USAGE perform_readWrite(long tid,long obno, char lockmode)

  }
  else  //Lock table contains that object
    {
        zgt_tx *transwithlock = get_tx(translinkwithlock->tid);
    if(transwithlock->tid==presentTX->tid)  //object held by same transaction
    {
    translinkwithlock->lockmode = 'X';
        zgt_v(0);  //change this if code doesnt work
    presentTX->perform_readWrite(presentTX->tid,node->obno,'X');  //USAGE perform_readWrite(long tid,long obno, char lockmode)
    } // Inner If
    else  //Object held by a different transaction
    {  
      presentTX->status = TR_WAIT;   //We make the present Transaction to wait for the object
      open_logfile_for_append();
      fprintf(logfile, "T%d\t\tWaiting\t%d\n", node->tid,node->obno); //Write to the output file
      fflush(logfile);
      presentTX->lockmode = 'X';   
      presentTX->obno = node->obno;     
      presentTX->setTx_semno(transwithlock->tid,transwithlock->tid);   //Set the semno of the trans which is holding the lock to its value of tid
      zgt_v(0);   //release the lock table
      zgt_p(transwithlock->semno);  //make the tx wait on the tx's semno which is holding the lock
     
      zgt_p(0);   // Lock the table
      presentTX->status  = TR_ACTIVE;   //Change the status to ACTIVE 
      zgt_v(0);   //Release Table
      writeRecursive(arg);  //Recall the method to execute because once the thread wakes up execution starts from next line

    }// Inner Else

    }//Outer Else close

} //END OF WRITE RECURSIVE METHOD






/* Method to handle Readtx action in test file    */
/* Inputs a pointer to structure that contans     */
/* tx id and object no to read. Reads the object  */
/* if the object is not yet present in hash table */
/* or same tx holds a lock on it. Otherwise waits */
/* until the lock is released */

void *readtx(void *arg){
  struct param *node = (struct param*)arg;// get tid and objno and count
  start_operation(node->tid,node->count); //start the operation to block other threads from same Tx to execute
  
  readRecursive(arg); // Call the read recursive function

  finish_operation(node->tid);  //finish the operation for other threads in the same tx to start 
  pthread_exit(NULL);       // thread exit


} //END OF READTX METHOD




/* Method to handle Writetx action in test file    */
/* Inputs a pointer to structure that contans     */
/* tx id and object no to read. Reads the object  */
/* if the object is not yet present in hash table */
/* or same tx holds a lock on it. Otherwise waits */
/* until the lock is released */

void *writetx(void *arg){ //do the operations for writing; similar to readTx
    struct param *node = (struct param*)arg;	// struct parameter that contains tid, obno, count
    start_operation(node->tid,node->count); //start the operation to block other threads from same Tx to execute
  
    writeRecursive(arg);  // Call the write recursive function
    
    finish_operation(node->tid);  //finish the operation for other threads in the same tx to start 
    pthread_exit(NULL);       // thread exit

}



void *aborttx(void *arg)
{
  struct param *node = (struct param*)arg;// get tid and count  
  start_operation(node->tid, node->count);
    zgt_tx* curTX = get_tx(node->tid);            //get TX which is being aborted
      open_logfile_for_append();

  if (curTX != NULL)
  {
    do_commit_abort(node->tid,'A');         // Set status of the transaction to TR_ABORT
        fprintf(logfile, "T%d\t\tAbortTx\t\n", node->tid);
        fflush(logfile);

    } 
  
  else 
   {
    
      fprintf(logfile, "\n Transaction does not exist \n");

    }
   finish_operation(node->tid); //Completed the operation successfully
   pthread_exit(NULL);			// thread exit
}

void *committx(void *arg)
{
 //remove the locks before committing
  struct param *node = (struct param*)arg;// get tid and count


      start_operation(node->tid, node->count);
               
    zgt_tx* curTX = get_tx(node->tid);            //get TX which is being commited
      
      open_logfile_for_append();

  if (curTX != NULL)    //If tx exists
  {
    do_commit_abort(node->tid,'E');         // Set status of the transaction to TR_COMMIT
        fprintf(logfile, "T%d\t\tCommitTx\t\n", node->tid);
        fflush(logfile);

    } 
  
  else 
   {
    
      fprintf(logfile, "\n Transaction does not exist \n");

    }
  
    finish_operation(node->tid);
    pthread_exit(NULL);			// stop the thread 
}

// called from commit/abort with appropriate parameter to do the actual
// operation. Make sure you give error messages if you are trying to
// commit/abort a non-existant tx

void *do_commit_abort(long t, char status){

  zgt_p(0);   
  int i, numberofwaitingTxs;
  open_logfile_for_append(); 
  zgt_tx* curTx = get_tx(t);              // Get the tx being commited
    curTx->status = status;              //set its status to E or A i.e., TR_COMMIT or TR_ABORT
    curTx->free_locks();                // Freeing all the locks held by this transaction   


    // Releasing the transactions which are waiting on this semaphore
    if (curTx->semno != -1) 
  {
        numberofwaitingTxs = zgt_nwait(curTx->semno);        // Retrieving the number transactions which are waiting on this semaphore
      for (i=1; i<=numberofwaitingTxs; i++)
        {zgt_v(curTx->semno);    }      //Releasing each semaphore waiting on this semaphore

    }
        curTx->remove_tx();                 // Remove transaction from the table
    zgt_v(0);  

}

int zgt_tx::remove_tx ()
{
  //remove the transaction from the TM
  
  zgt_tx *txptr, *lastr1;
  lastr1 = ZGT_Sh->lastr;
  for(txptr = ZGT_Sh->lastr; txptr != NULL; txptr = txptr->nextr){	// scan through list
	  if (txptr->tid == this->tid){		// if req node is found          
		 lastr1->nextr = txptr->nextr;	// update nextr value; done
		 //delete this;
         return(0);
	  }
	  else lastr1 = txptr->nextr;			// else update prev value
   }
  fprintf(logfile, "Trying to Remove a Tx:%d that does not exist\n", this->tid);
  fflush(logfile);
  printf("Trying to Remove a Tx:%d that does not exist\n", this->tid);
  fflush(stdout);
  return(-1);
}

/* this method sets lock on objno1 with lockmode1 for a tx in this*/

int zgt_tx::set_lock(long tid1, long sgno1, long obno1, int count, char lockmode1){
  //if the thread has to wait, block the thread on a semaphore from the
  //sempool in the transaction manager. Set the appropriate parameters in the
  //transaction list if waiting.
  //if successful  return(0);



}

// this part frees all locks owned by the transaction
// Need to free the thread in the waiting queue
// try to obtain the lock for the freed threads
// if the process itself is blocked, clear the wait and semaphores

int zgt_tx::free_locks()
{
  zgt_hlink* temp = head;  //first obj of tx
  
  open_logfile_for_append();
   
  for(temp;temp != NULL;temp = temp->nextp){	// SCAN Tx obj list

      fprintf(logfile, "%d : %d \t", temp->obno, ZGT_Sh->objarray[temp->obno]->value);
      fflush(logfile);
      
      if (ZGT_Ht->remove(this,1,(long)temp->obno) == 1){
	   printf(":::ERROR:node with tid:%d and onjno:%d was not found for deleting", this->tid, temp->obno);		// Release from hash table
	   fflush(stdout);
      }
      else {
#ifdef TX_DEBUG
	   printf("\n:::Hash node with Tid:%d, obno:%d lockmode:%c removed\n",
                            temp->tid, temp->obno, temp->lockmode);
	   fflush(stdout);
#endif
      }
    }
  fprintf(logfile, "\n");
  fflush(logfile);
  
  return(0);
}		

// CURRENTLY Not USED
// USED to COMMIT
// remove the transaction and free all associate dobjects. For the time being
// this can be used for commit of the transaction.

int zgt_tx::end_tx()  //2014: not used
{
  zgt_tx *linktx, *prevp;

  linktx = prevp = ZGT_Sh->lastr;
  
  while (linktx){
    if (linktx->tid  == this->tid) break;
    prevp  = linktx;
    linktx = linktx->nextr;
  }
  if (linktx == NULL) {
    printf("\ncannot remove a Tx node; error\n");
    fflush(stdout);
    return (1);
  }
  if (linktx == ZGT_Sh->lastr) ZGT_Sh->lastr = linktx->nextr;
  else {
    prevp = ZGT_Sh->lastr;
    while (prevp->nextr != linktx) prevp = prevp->nextr;
    prevp->nextr = linktx->nextr;    
  }
}

// currently not used
int zgt_tx::cleanup()
{
  return(0);
  
}

// check which other transaction has the lock on the same obno
// returns the hash node
zgt_hlink *zgt_tx::others_lock(zgt_hlink *hnodep, long sgno1, long obno1)
{
  zgt_hlink *ep;
  ep=ZGT_Ht->find(sgno1,obno1);
  while (ep)				// while ep is not null
    {
      if ((ep->obno == obno1)&&(ep->sgno ==sgno1)&&(ep->tid !=this->tid)) 
	return (ep);			// return the hashnode that holds the lock
      else  ep = ep->next;		
    }					
  return (NULL);			//  Return null otherwise 
  
}

// routine to print the tx list
// TX_DEBUG should be defined in the Makefile to print

void zgt_tx::print_tm(){
  
  zgt_tx *txptr;
  
#ifdef TX_DEBUG
  printf("printing the tx list \n");
  printf("Tid\tTxType\tThrid\t\tobjno\tlock\tstatus\tsemno\n");
  fflush(stdout);
#endif
  txptr=ZGT_Sh->lastr;
  while (txptr != NULL) {
#ifdef TX_DEBUG
    printf("%d\t%c\t%d\t%d\t%c\t%c\t%d\n", txptr->tid, txptr->Txtype, txptr->pid, txptr->obno, txptr->lockmode, txptr->status, txptr->semno);
    fflush(stdout);
#endif
    txptr = txptr->nextr;
  }
  fflush(stdout);
}

//currently not used
void zgt_tx::print_wait(){

  //route for printing for debugging
  
  printf("\n    SGNO        TxType       OBNO        TID        PID         SEMNO   L\n");
  printf("\n");
}
void zgt_tx::print_lock(){
  //routine for printing for debugging
  
  printf("\n    SGNO        OBNO        TID        PID   L\n");
  printf("\n");
  
}

// routine to perform the acutual read/write operation
// based  on the lockmode
void zgt_tx::perform_readWrite(long tid,long obno, char lockmode){
  
    open_logfile_for_append();
    int val = ZGT_Sh->objarray[obno]->value;
   for(int i=1;i<=ZGT_Sh->optime[tid];i++)
   {}// Wait till the specified optime of the TX

    if (lockmode == 'X') // WRITE OPERATION
    {

        ZGT_Sh->objarray[obno]->value = val + 1; //Increment the value of the objarray of that object  by 1
        fprintf(logfile, "T%d\t\tWriteTx\t\t\t%d:%d:%d\t\tWriteLock\tGranted\t\t %c\n",this->tid, obno, ZGT_Sh->objarray[obno]->value, ZGT_Sh->optime[tid], this->status);
        fflush(logfile);  //Write into log file
    }
    else  //READ OPERATION 
    { 
        ZGT_Sh->objarray[obno]->value = val - 1; //Decrement the value of the objarray of that object  by 1
        fprintf(logfile, "T%d\t\tReadTx\t\t\t%d:%d:%d\tReadLock\tGranted\t\t %c\n",this->tid, obno, ZGT_Sh->objarray[obno]->value, ZGT_Sh->optime[tid], this->status);
        fflush(logfile); //Write into log file
        
    }
}

// routine that sets the semno in the Tx when another tx waits on it.
// the same number is the same as the tx number on which a Tx is waiting
int zgt_tx::setTx_semno(long tid, int semno){
  zgt_tx *txptr;
  
  txptr = get_tx(tid);
  if (txptr == NULL){
    printf("\n:::ERROR:Txid %d wants to wait on sem:%d of tid:%d which does not exist\n", this->tid, semno, tid);
    fflush(stdout);
    return(-1);
  }
  if (txptr->semno == -1){
    txptr->semno = semno;
    return(0);
  }
  else if (txptr->semno != semno){
#ifdef TX_DEBUG
    printf(":::ERROR Trying to wait on sem:%d, but on Tx:%d\n", semno, txptr->tid);
    fflush(stdout);
#endif
    exit(1);
  }
  return(0);
}

// routine to start an operation by checking the previous operation of the same
// tx has completed; otherwise, it will do a conditional wait until the
// current thread signals a broadcast

void *start_operation(long tid, long count){
  
  pthread_mutex_lock(&ZGT_Sh->mutexpool[tid]);	// Lock mutex[t] to make other
  // threads of same transaction to wait
  
  while(ZGT_Sh->condset[tid] != count)		// wait if condset[t] is != count
    pthread_cond_wait(&ZGT_Sh->condpool[tid],&ZGT_Sh->mutexpool[tid]);
  
}

// Otherside of the start operation;
// signals the conditional broadcast

void *finish_operation(long tid){
  ZGT_Sh->condset[tid]--;	// decr condset[tid] for allowing the next op
  pthread_cond_broadcast(&ZGT_Sh->condpool[tid]);// other waiting threads of same tx
  pthread_mutex_unlock(&ZGT_Sh->mutexpool[tid]); 
}

void *open_logfile_for_append(){
  
  if ((logfile = fopen(ZGT_Sh->logfile, "a")) == NULL){
    printf("\nCannot open log file for append:%s\n", ZGT_Sh->logfile);
    fflush(stdout);
    exit(1);
  }
}
