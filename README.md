# CppSignal
PubSub pattern implemented in C++

- Based only platform independent modern C++, no external library dependency
- Lockfree implementation (no mutexes)
- Signals are strongly typed
- The signal publisher must be pointer by a shared_ptr
- Callbacks can be anything that can be wrapped in a std::function (functions, lambdas, functors)
- Subscription is a RAII object = when the subscription object is destructed the callback is destroyed immediately (captures in the callback are also released)
- Unit tests demonstrate the intended usage

Safety:
- Safe unsubscription even if the signal publisher is gone
- Emission at the same time from multiple threads is safe
- Emission parallel with unsubscription is safe
