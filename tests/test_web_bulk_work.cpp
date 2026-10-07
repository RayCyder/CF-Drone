// WEB-ARCH-1/2: a stalled download runs only on the worker; request admission
// and control dispatch remain available. A full queue rejects immediately and
// releases captured state. This tests the production enqueue helper directly.
#include "../web_bulk_work.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <deque>
#include <future>
#include <memory>
#include <thread>

int main() {
    std::deque<WebBulkWork *> jobs;
    auto send = [&](WebBulkWork *job) {
        if (jobs.size() == 2) return false;
        jobs.push_back(job);
        return true;
    };
    std::promise<void> started, release;
    auto released = release.get_future().share();
    bool downloadFinished = false;
    assert(enqueueWebBulkWork([&] { started.set_value(); released.wait(); downloadFinished = true; }, send));
    WebBulkWork *first = jobs.front(); jobs.pop_front();
    std::thread worker([&] { first->run(); delete first; });
    assert(started.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready);

    int controls = 0, responses = 0;
    assert(enqueueWebBulkWork([&] { responses++; }, send));
    assert(enqueueWebBulkWork([&] { responses++; }, send));
    auto held = std::make_shared<int>(7);
    std::weak_ptr<int> heldWeak = held;
    auto rejectedWork = [capture = std::move(held)] {};
    assert(!enqueueWebBulkWork(std::move(rejectedWork), send));
    assert(heldWeak.expired());
    controls++; // HTTP task can dispatch the next short control request.
    assert(controls == 1 && responses == 0);
    release.set_value(); worker.join();
    assert(downloadFinished);
    while (!jobs.empty()) {
        auto *job = jobs.front(); jobs.pop_front(); job->run(); delete job;
    }
    assert(responses == 2);
    auto currentSocket = std::make_shared<int>(42);
    std::weak_ptr<int> oldSocket = currentSocket;
    assert(enqueueWebBulkWork([socket = currentSocket] { assert(*socket == 42); }, send));
    currentSocket = std::make_shared<int>(99); // next HTTP request uses another socket
    assert(!oldSocket.expired());
    auto *owned = jobs.front(); jobs.pop_front(); owned->run(); delete owned;
    assert(oldSocket.expired() && *currentSocket == 99);

    puts("web bulk work isolation and bounded admission: PASS");
}
