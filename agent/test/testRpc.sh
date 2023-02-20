path=$(cd `dirname $0`;pwd)
cd $path
g++ -std=c++17 -pthread ../rpc/rpc.cpp ./rpctest.cpp -I../include -s -Os -o test -lgtest -lgtest_main && ./test
file_size_kb=`du -k "./test" | cut -f1`
echo "binrary size: " $file_size_kb "kb"
rm ./test