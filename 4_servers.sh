#!/usr/bin/bash

#uncomment the topolgy you want. The simple two-server topology is uncommented here.

# Change the SERVER variable below to point your server executable.
SERVER=./server

SERVER_NAME=`echo $SERVER | sed 's#.*/\(.*\)#\1#g'`

# Generate a triangle topology
$SERVER localhost 4000 localhost 4001 localhost 4002 &
$SERVER localhost 4001 localhost 4000 localhost 4002 &
$SERVER localhost 4002 localhost 4001 localhost 4000 &


echo "Press ENTER to quit"
read
pkill $SERVER_NAME
