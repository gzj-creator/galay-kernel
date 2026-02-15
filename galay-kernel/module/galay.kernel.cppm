module;

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

export module galay.kernel;

export namespace galay::kernel {
using ::galay::kernel::Coroutine;
using ::galay::kernel::spawn;
using ::galay::kernel::Scheduler;
using ::galay::kernel::IOScheduler;
using ::galay::kernel::ComputeScheduler;
using ::galay::kernel::Runtime;
using ::galay::kernel::TimerScheduler;

using ::galay::kernel::IOError;
using ::galay::kernel::IOErrorCode;
using ::GHandle;
using ::IOEventType;
using ::galay::kernel::IPType;
using ::galay::kernel::Host;
using ::galay::kernel::HandleOption;
using ::galay::kernel::Bytes;
using ::galay::kernel::Buffer;
using ::galay::kernel::RingBuffer;
using ::galay::kernel::sleep;

using ::galay::kernel::MpscChannel;
using ::galay::kernel::UnsafeChannel;
using ::galay::kernel::AsyncMutex;
using ::galay::kernel::AsyncWaiter;
}  // namespace galay::kernel

export namespace galay::async {
using ::galay::async::TcpSocket;
using ::galay::async::UdpSocket;
using ::galay::async::FileWatcher;

#if defined(USE_KQUEUE) || defined(USE_IOURING)
using ::galay::async::AsyncFile;
using ::galay::async::FileOpenMode;
#endif

#ifdef USE_EPOLL
using ::galay::async::AioFile;
using ::galay::async::AioOpenMode;
#endif
}  // namespace galay::async
