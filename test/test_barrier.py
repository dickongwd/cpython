import threading

thread_count = 8
n_count = 10

count = [0 for _ in range(thread_count)]

barrier = threading.Barrier(thread_count + 1)
A = threading.Lock()

def inc(thread_id):
    global count
    barrier.wait()
    for _ in range(n_count):
        with A:
            count[thread_id] += 1
    print(f"Count: {count}")

threads = []
for i in range(thread_count):
    t = threading.Thread(target=inc, args=(i,))
    threads.append(t)

for t in threads:
    t.start()

barrier.wait()

for t in threads:
    t.join()

print(f"Final state of count = {count}")
