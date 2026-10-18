#include "../adapters/dns_lifecycle.hpp"

#include <atomic>
#include <cassert>
#include <thread>

using mosaic::features::provisioning::adapters::portal::DnsLifecycle;

int main() {
    DnsLifecycle dns;
    assert(dns.reap());
    for (int cycle = 0; cycle < 100; ++cycle) {
        assert(dns.begin(42));
        std::atomic_bool release { false };
        std::atomic_bool entered { false };
        std::atomic_int live_sockets { 1 };
        std::thread worker([&] {
            assert(dns.address() == 42);
            entered.store(true);
            while (!release.load())
                std::this_thread::yield();
            assert(!dns.running());
            assert(dns.address() == 42);
            // Socket close precedes completion, just as in the adapter.
            live_sockets.store(0);
            dns.complete(); // No lifecycle/context access after this point.
        });
        while (!entered.load())
            std::this_thread::yield();
        dns.request_stop();
        // Timeout: restart must neither replace the address nor close the socket.
        for (int attempt = 0; attempt < 100; ++attempt) {
            assert(!dns.reap());
            assert(!dns.begin(99));
            assert(live_sockets.load() == 1);
        }
        release.store(true);
        while (!dns.reap())
            std::this_thread::yield();
        assert(live_sockets.load() == 0);
        // Restart is safe even before the previous thread's self-delete/return.
        assert(dns.begin(99));
        worker.join();
        assert(dns.address() == 99);
        dns.request_stop();
        dns.complete();
        assert(dns.reap());
        assert(dns.reap()); // Reaping is idempotent, not a consumed notification.
    }
    // Creation failure: owner closes the socket; there was never a worker.
    assert(dns.begin(7));
    dns.creation_failed();
    assert(dns.reap());
    assert(dns.begin(8));
    dns.request_stop();
    dns.complete();
    assert(dns.reap());

    // Permanently pending: keep the static handshake/context and reject restart.
    assert(dns.begin(123));
    dns.request_stop();
    for (int attempt = 0; attempt < 1000; ++attempt) {
        assert(!dns.reap());
        assert(!dns.begin(456));
        assert(dns.address() == 123);
        assert(!dns.running());
    }
}
