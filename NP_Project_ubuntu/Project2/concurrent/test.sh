clear
g++ server.cpp -o server

if [ "$?" == "0" ]; then
    ./server
else
    echo "fail"
fi
