module;

#include "galay-kernel/module/ModulePrelude.hpp"

export module galay.kernel;

export {
#include "galay-kernel/common/Defn.hpp"
#include "galay-kernel/common/Error.h"
#include "galay-kernel/common/Host.hpp"
#include "galay-kernel/common/HandleOption.h"
#include "galay-kernel/common/Bytes.h"
#include "galay-kernel/common/Buffer.h"
#include "galay-kernel/common/Sleep.hpp"

#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/kernel/Scheduler.hpp"
#include "galay-kernel/kernel/IOScheduler.hpp"
#include "galay-kernel/kernel/ComputeScheduler.h"
#include "galay-kernel/kernel/Runtime.h"
#include "galay-kernel/kernel/TimerScheduler.h"

#include "galay-kernel/concurrency/MpscChannel.h"
#include "galay-kernel/concurrency/UnsafeChannel.h"
#include "galay-kernel/concurrency/AsyncMutex.h"
#include "galay-kernel/concurrency/AsyncWaiter.h"

#include "galay-kernel/async/TcpSocket.h"
#include "galay-kernel/async/UdpSocket.h"
#include "galay-kernel/async/FileWatcher.h"

#if defined(USE_KQUEUE) || defined(USE_IOURING)
#include "galay-kernel/async/AsyncFile.h"
#endif

#ifdef USE_EPOLL
#include "galay-kernel/async/AioFile.h"
#endif
}
