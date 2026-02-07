
./python.exe ./test/test_barrier.py > ./test/out.txt 2> ./test/err.txt
for i in {1..100000}; do
    ./python.exe ./test/test_barrier.py > ./test/out2.txt 2> ./test/err2.txt
    if ! diff -q ./test/out.txt ./test/out2.txt > /dev/null; then
        echo "Iteration $i failed"
        diff ./test/out.txt ./test/out2.txt
        break
    fi
    if ! diff -q ./test/err.txt ./test/err.txt > /dev/null; then
        echo "Iteration $i failed"
        diff ./test/err.txt ./test/err2.txt
        break
    fi
    echo "Progress: $i/100000" 
done