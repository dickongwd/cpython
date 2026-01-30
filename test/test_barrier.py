import threading

thread_count = 8
n_count = 10_000_000

count = [0 for _ in range(thread_count)]

barrier = threading.Barrier(thread_count + 1)

def inc(thread_id):
    barrier.wait()
    for _ in range(n_count):
        count[thread_id] += 1
        # print(f"Thread {thread_id} ran")
    print(f"Count: {count}")


def main():
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

if __name__ == "__main__":
    main()
