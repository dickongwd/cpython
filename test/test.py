import os
import sys
import threading

print("Python program start", file=sys.stderr, flush=True)

if sys._is_gil_enabled:
    print("GIL is enabled!", file=sys.stderr, flush=True)
else:
    print("GIL is not enabled", file=sys.stderr, flush=True)

thread_count = 10
n_count = 10000000

count = [0 for _ in range(thread_count)]

def inc(thread_id):
    global count
    for _ in range(n_count):
        count[thread_id] += 1
    print(f"Count: {count}", file=sys.stderr, flush=True)

threads = []
for i in range(thread_count):
    t = threading.Thread(target=inc, args=(i,))
    threads.append(t)

for t in threads:
    t.start()

for t in threads:
    t.join()

print(f"Final state of count = {count}", file=sys.stderr, flush=True)
