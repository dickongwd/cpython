import time
import threading

thread_count = 4
n_count = 10

count = [0 for _ in range(thread_count)]

def inc(thread_id):
    global count
    for _ in range(n_count):
        count[thread_id] += 1
        time.sleep(0.1)
    print(f"Count: {count}")

threads = []
for i in range(thread_count):
    t = threading.Thread(target=inc, args=(i,))
    threads.append(t)

for t in threads:
    t.start()

for t in threads:
    t.join()

print(f"Final state of count = {count}")
