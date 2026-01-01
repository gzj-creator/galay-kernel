#include "Scheduler.h"
#include "Coroutine.h"

namespace galay::kernel 
{

void Scheduler::resume(Coroutine& co) {
    co.resume();
}

}