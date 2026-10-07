#pragma once
#include <new>
#include <utility>

// WEB-ARCH-1: HTTP handlers enqueue long responses without executing them.
// WEB-ARCH-2: admission never waits; rejected jobs release their captured socket.
// One worker owns every accepted job until it completes. Queue capacity is
// fixed by the caller, so neither jobs nor sockets accumulate without a bound.
struct WebBulkWork {
    virtual ~WebBulkWork() = default;
    virtual void run() = 0;
};

template<class Work> struct WebBulkWorkItem final : WebBulkWork {
    Work work;
    explicit WebBulkWorkItem(Work &&value) : work(std::move(value)) {}
    void run() override { work(); }
};

template<class Work, class TrySend>
bool enqueueWebBulkWork(Work work, TrySend trySend) {
    auto *item = new (std::nothrow) WebBulkWorkItem<Work>(std::move(work));
    if (!item) return false;
    if (trySend(static_cast<WebBulkWork *>(item))) return true;
    delete item;
    return false;
}
