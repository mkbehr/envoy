#include "common/signal/fatal_error_handler.h"

#include <list>

#include "absl/base/attributes.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace FatalErrorHandler {

namespace {

ABSL_CONST_INIT static absl::Mutex failure_mutex(absl::kConstInit);
// Since we can't grab the failure mutex on fatal error (snagging locks under
// fatal crash causing potential deadlocks) access the handler list as an atomic
// operation, to minimize the chance that one thread is operating on the list
// while the crash handler is attempting to access it.
// This basically makes edits to the list thread-safe - if one thread is
// modifying the list rather than crashing in the crash handler due to accessing
// the list in a non-thread-safe manner, it simply won't log crash traces.
using FailureFunctionList = std::list<const FatalErrorHandlerInterface*>;
ABSL_CONST_INIT std::atomic<FailureFunctionList*> fatal_error_handlers{nullptr};

} // namespace

void registerFatalErrorHandler(const FatalErrorHandlerInterface& handler) {
#ifdef ENVOY_OBJECT_TRACE_ON_DUMP
  absl::MutexLock l(&failure_mutex);
  FailureFunctionList* list = fatal_error_handlers.exchange(nullptr, std::memory_order_relaxed);
  if (list == nullptr) {
    list = new FailureFunctionList;
  }
  list->push_back(&handler);
  fatal_error_handlers.store(list, std::memory_order_release);
#else
  UNREFERENCED_PARAMETER(handler);
#endif
}

void removeFatalErrorHandler(const FatalErrorHandlerInterface& handler) {
#ifdef ENVOY_OBJECT_TRACE_ON_DUMP
  absl::MutexLock l(&failure_mutex);
  FailureFunctionList* list = fatal_error_handlers.exchange(nullptr, std::memory_order_relaxed);
  if (list == nullptr) {
    // removeFatalErrorHandler() may see an empty list of fatal error handlers
    // if it's called at the same time as callFatalErrorHandlers(). In that case
    // Envoy is in the middle of crashing anyway, but don't add a segfault on
    // top of the crash.
    return;
  }
  list->remove(&handler);
  if (list->empty()) {
    delete list;
  } else {
    fatal_error_handlers.store(list, std::memory_order_release);
  }
#else
  UNREFERENCED_PARAMETER(handler);
#endif
}

void callFatalErrorHandlers(std::ostream& os) {
  FailureFunctionList* list = fatal_error_handlers.exchange(nullptr, std::memory_order_relaxed);
  if (list) {
    for (const auto* handler : *list) {
      handler->onFatalError(os);
    }
    delete list;
  }
}

} // namespace FatalErrorHandler
} // namespace Envoy
