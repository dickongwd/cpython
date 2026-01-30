import threading

rlock = threading.RLock()

def work():
    print("Worker: acquiring rlock")
    for i in range(3):
        rlock.acquire()
    print("Worker: acquired rlock")
    print("Worker: releasing rlock")
    for i in range(3):
        rlock.release()
    print("Worker: released rlock")


def main():
    print("Main: Acquiring RLock")
    for i in range(3):
        rlock.acquire()

    print("Main: starting thread")
    t = threading.Thread(target=work, args=())
    t.start()
    
    print("Main: releasing rlock")
    for i in range(3):
        rlock.release()
    print("Main: released rlock")

    print("Main: joining thread")
    t.join()
    print("Main: joined thread")


if __name__ == "__main__":
    main()
