import sys
import threading

if sys._is_gil_enabled:
    print("GIL is enabled!")
else:
    print("GIL is not enabled")

count = 0

def inc():
    global count
    for _ in range(100000):
        count += 1

threads = []
for _ in range(10):
    t = threading.Thread(target=inc)
    threads.append(t)

for t in threads:
    t.start()

for t in threads:
    t.join()

print(f"Final value of count = {count}")

