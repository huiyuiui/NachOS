// scheduler.cc 
//	Routines to choose the next thread to run, and to dispatch to
//	that thread.
//
// 	These routines assume that interrupts are already disabled.
//	If interrupts are disabled, we can assume mutual exclusion
//	(since we are on a uniprocessor).
//
// 	NOTE: We can't use Locks to provide mutual exclusion here, since
// 	if we needed to wait for a lock, and the lock was busy, we would 
//	end up calling FindNextToRun(), and that would put us in an 
//	infinite loop.
//
// 	Very simple implementation -- no priorities, straight FIFO.
//	Might need to be improved in later assignments.
//
// Copyright (c) 1992-1996 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "debug.h"
#include "scheduler.h"
#include "main.h"

//----------------------------------------------------------------------
// Scheduler::Scheduler
// 	Initialize the list of ready but not running threads.
//	Initially, no ready threads.
//----------------------------------------------------------------------

int
comp_L1(Thread* x,Thread* y){
    if(x->remain_burst==0 && y->remain_burst==0){
        if(x->priority > y->priority) return -1;
        if(x->priority == y->priority) return 0;
        if(x->priority < y->priority) return 1;
    }else{
        if(x->remain_burst > y->remain_burst) return 1;
        if(x->remain_burst == y->remain_burst) return 0;
        if(x->remain_burst < y->remain_burst) return -1;
    }
}

int
comp_L2(Thread* x,Thread* y){
    if(x->priority > y->priority) return -1;
    if(x->priority == y->priority) return 0;
    if(x->priority < y->priority) return 1;
}

Scheduler::Scheduler()
{ 
    readyList = new List<Thread *>; 
    L1 = new SortedList<Thread *>(comp_L1);
    L2 = new SortedList<Thread *>(comp_L2);
    L3 = new List<Thread *>;
    toBeDestroyed = NULL;
} 

//----------------------------------------------------------------------
// Scheduler::~Scheduler
// 	De-allocate the list of ready threads.
//----------------------------------------------------------------------

Scheduler::~Scheduler()
{ 
    delete readyList; 
} 

//----------------------------------------------------------------------
// Scheduler::ReadyToRun
// 	Mark a thread as ready, but not running.
//	Put it on the ready list, for later scheduling onto the CPU.
//
//	"thread" is the thread to be put on the ready list.
//----------------------------------------------------------------------

void
Scheduler::ReadyToRun (Thread *thread)
{
    ASSERT(kernel->interrupt->getLevel() == IntOff);
    DEBUG(dbgThread, "Putting thread on ready list: " << thread->getName());
	//cout << "Putting thread on ready list: " << thread->getName() << endl ;
    thread->setStatus(READY);
    readyList->Append(thread);
}

//----------------------------------------------------------------------
// Scheduler::FindNextToRun
// 	Return the next thread to be scheduled onto the CPU.
//	If there are no ready threads, return NULL.
// Side effect:
//	Thread is removed from the ready list.
//----------------------------------------------------------------------

Thread *
Scheduler::FindNextToRun ()
{
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    if (readyList->IsEmpty()) {
		return NULL;
    } else {
    	return readyList->RemoveFront();
    }
}

//----------------------------------------------------------------------
// Scheduler::Run
// 	Dispatch the CPU to nextThread.  Save the state of the old thread,
//	and load the state of the new thread, by calling the machine
//	dependent context switch routine, SWITCH.
//
//      Note: we assume the state of the previously running thread has
//	already been changed from running to blocked or ready (depending).
// Side effect:
//	The global variable kernel->currentThread becomes nextThread.
//
//	"nextThread" is the thread to be put into the CPU.
//	"finishing" is set if the current thread is to be deleted
//		once we're no longer running on its stack
//		(when the next thread starts running)
//----------------------------------------------------------------------

void
Scheduler::Run (Thread *nextThread, bool finishing)
{
    Thread *oldThread = kernel->currentThread;
    
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    if (finishing) {	// mark that we need to delete current thread
        ASSERT(toBeDestroyed == NULL);
	    toBeDestroyed = oldThread;
    }
    
    if (oldThread->space != NULL) {	// if this thread is a user program,
        oldThread->SaveUserState(); 	// save the user's CPU registers
	    oldThread->space->SaveState();
    }
    
    oldThread->CheckOverflow();		    // check if the old thread
					    // had an undetected stack overflow

    kernel->currentThread = nextThread;  // switch to the next thread

    nextThread->StartRunning(); 
    nextThread->setStatus(RUNNING);      // nextThread is now running
    
    DEBUG(dbgThread, "Switching from: " << oldThread->getName() << " to: " << nextThread->getName());

    DEBUG(dbgScheduler, "[E] Tick ["<<kernel->stats->totalTicks<<"]: Thread ["<<nextThread->getID()<<"] is now selected for execution, thread ["<<oldThread->getID()<<"] is replaced, and it has executed ["<<oldThread->true_burst<<"] ticks");

    // This is a machine-dependent assembly language routine defined 
    // in switch.s.  You may have to think
    // a bit to figure out what happens after this, both from the point
    // of view of the thread and from the perspective of the "outside world".

    SWITCH(oldThread, nextThread);

    // we're back, running oldThread
      
    // interrupts are off when we return from switch!
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    DEBUG(dbgThread, "Now in thread: " << oldThread->getName());

    CheckToBeDestroyed();		// check if thread we were running
					// before this one has finished
					// and needs to be cleaned up
    
    if (oldThread->space != NULL) {	    // if there is an address space
        oldThread->RestoreUserState();     // to restore, do it.
	    oldThread->space->RestoreState();
    }
}

//----------------------------------------------------------------------
// Scheduler::CheckToBeDestroyed
// 	If the old thread gave up the processor because it was finishing,
// 	we need to delete its carcass.  Note we cannot delete the thread
// 	before now (for example, in Thread::Finish()), because up to this
// 	point, we were still running on the old thread's stack!
//----------------------------------------------------------------------

void
Scheduler::CheckToBeDestroyed()
{
    if (toBeDestroyed != NULL) {
        delete toBeDestroyed;
	toBeDestroyed = NULL;
    }
}
 
//----------------------------------------------------------------------
// Scheduler::Print
// 	Print the scheduler state -- in other words, the contents of
//	the ready list.  For debugging.
//----------------------------------------------------------------------
void
Scheduler::Print()
{
    cout << "Ready list contents:\n";
    readyList->Apply(ThreadPrint);
}

void
Scheduler::Aging(){
    int cur_time = kernel->stats->totalTicks;  
    if(!L1->IsEmpty()){
        ListIterator<Thread *> *iter;
        iter = new ListIterator<Thread *>(L1);
        while(!iter->IsDone()){
            Thread* t = iter->Item();
            t->total_ready_time = kernel->stats->totalTicks - t->insert_ready_time;
            if(t->total_ready_time >= 1500){
                int old_priority = t->priority;
                t->priority = (t->priority+10 > 149 ? 149 : t->priority + 10);
                DEBUG(dbgScheduler, "[C] Tick ["<<kernel->stats->totalTicks<<"]: Thread ["<<t->getID()<<"] changes its priority from ["<<old_priority<<"] to ["<<t->priority<<"]");
                t->total_ready_time -= 1500;
                t->insert_ready_time += 1500; 
            }
            iter->Next();
        }
    }
    if(!L2->IsEmpty()){
        ListIterator<Thread *> *iter;
        iter = new ListIterator<Thread *>(L2);
        while(!iter->IsDone()){
            Thread* t = iter->Item();
            t->total_ready_time = kernel->stats->totalTicks - t->insert_ready_time;
            if(t->total_ready_time >= 1500){
                int old_priority = t->priority;
                t->priority += 10;
                DEBUG(dbgScheduler, "[C] Tick ["<<kernel->stats->totalTicks<<"]: Thread ["<<t->getID()<<"] changes its priority from ["<<old_priority<<"] to ["<<t->priority<<"]");
                t->total_ready_time -= 1500;
                t->insert_ready_time += 1500;
                if(t->priority >= 100){
                    iter->Next();
                    DEBUG(dbgScheduler, "[B] Tick ["<<kernel->stats->totalTicks<<"]: Thread ["<<t->getID()<<"] is removed from queue L[2]");
                    DEBUG(dbgScheduler, "[A] Tick ["<<kernel->stats->totalTicks<<"]: Thread ["<<t->getID()<<"] is inserted into queue L[1]");
                    L1->Insert(t);
                    L2->Remove(t);
                    continue;
                }
            }
            iter->Next();
        }
    }
    if(!L3->IsEmpty()){
        ListIterator<Thread *> *iter;
        iter = new ListIterator<Thread *>(L3);
        while(!iter->IsDone()){ 
            Thread* t = iter->Item();
            t->total_ready_time = kernel->stats->totalTicks - t->insert_ready_time;
            if(t->total_ready_time >= 1500){
                int old_priority = t->priority;
                t->priority += 10;
                DEBUG(dbgScheduler, "[C] Tick ["<<kernel->stats->totalTicks<<"]: Thread ["<<t->getID()<<"] changes its priority from ["<<old_priority<<"] to ["<<t->priority<<"]");
                t->total_ready_time -= 1500;
                t->insert_ready_time += 1500; 
                if(t->priority >= 50){
                    iter->Next();
                    DEBUG(dbgScheduler, "[B] Tick ["<<kernel->stats->totalTicks<<"]: Thread ["<<t->getID()<<"] is removed from queue L[3]");
                    DEBUG(dbgScheduler, "[A] Tick ["<<kernel->stats->totalTicks<<"]: Thread ["<<t->getID()<<"] is inserted into queue L[2]");
                    L2->Insert(t);
                    L3->Remove(t);
                    continue;
                }
            }
            iter->Next();
        }
    }
}

Thread *
Scheduler::ScheduleNext ()
{
    ASSERT(kernel->interrupt->getLevel() == IntOff);
    if(!L1->IsEmpty()){
        Thread* nextThread = L1->RemoveFront();
        DEBUG(dbgScheduler, "[B] Tick ["<<kernel->stats->totalTicks<<"]: Thread ["<<nextThread->getID()<<"] is removed from queue L["<<nextThread->WhichQueue()<<"]");
        return nextThread;
    } else if(!L2->IsEmpty()){
        Thread* nextThread = L2->RemoveFront();
        DEBUG(dbgScheduler, "[B] Tick ["<<kernel->stats->totalTicks<<"]: Thread ["<<nextThread->getID()<<"] is removed from queue L["<<nextThread->WhichQueue()<<"]");
        return nextThread;
    } else if(!L3->IsEmpty()){
        Thread* nextThread = L3->RemoveFront();
        DEBUG(dbgScheduler, "[B] Tick ["<<kernel->stats->totalTicks<<"]: Thread ["<<nextThread->getID()<<"] is removed from queue L["<<nextThread->WhichQueue()<<"]");
        return nextThread;
    } else{
        return NULL;
    }
}

void
Scheduler::PutToReady (Thread *thread)
{
    ASSERT(kernel->interrupt->getLevel() == IntOff);
    DEBUG(dbgThread, "Putting thread on ready list: " << thread->getName());
	//cout << "Putting thread on ready list: " << thread->getName() << endl ;
    if(thread->getStatus() == BLOCKED){
        thread->remain_burst = thread->approx_burst; //update remain burst to new approximate burst when thread go from wait to ready
        thread->true_burst = 0; //update old thread's true burst to 0
    } 
    thread->setStatus(READY);
    thread->StartReady();
    int queue = thread->WhichQueue();
    if(queue == 1){
        L1->Insert(thread);
        DEBUG(dbgScheduler, "[A] Tick ["<<kernel->stats->totalTicks<<"]: Thread ["<<thread->getID()<<"] is inserted into queue L[1]");
    }
    if(queue == 2){
        L2->Insert(thread);
        DEBUG(dbgScheduler, "[A] Tick ["<<kernel->stats->totalTicks<<"]: Thread ["<<thread->getID()<<"] is inserted into queue L[2]");
    }
    if(queue == 3){
        L3->Append(thread);
        DEBUG(dbgScheduler, "[A] Tick ["<<kernel->stats->totalTicks<<"]: Thread ["<<thread->getID()<<"] is inserted into queue L[3]");
    }
}

bool
Scheduler::CheckPreempt(){
    Thread* thread = kernel->currentThread;
    if(thread->WhichQueue() == 1){
        if(!L1->IsEmpty()){
            if(L1->Front()->remain_burst < thread->remain_burst) return true;
            else return false;
        }
    } else if(thread->WhichQueue() == 2){
        if(!L1->IsEmpty()) return true;
        else return false;
    } else if(thread->WhichQueue() == 3){
        if(L3->IsEmpty()) return false;
        return true;
    }
}


