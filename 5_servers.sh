#!/usr/bin/bash

#uncomment the topolgy you want. The simple two-server topology is uncommented here.

# Change the SERVER variable below to point your server executable.
SERVER=./server

SERVER_NAME=`echo $SERVER | sed 's#.*/\(.*\)#\1#g'`

# Generate a 3x3 grid topology
$SERVER localhost 3120 localhost 3121 localhost 3123 &
$SERVER localhost 3121 localhost 3120 localhost 3122 localhost 3124 &
$SERVER localhost 3122 localhost 3121 localhost 3125 &
$SERVER localhost 3123 localhost 3120 localhost 3124 localhost 3126 &
$SERVER localhost 3124 localhost 3121 localhost 3123 localhost 3125 localhost 3127 &
$SERVER localhost 3125 localhost 3122 localhost 3124 localhost 3128 &
$SERVER localhost 3126 localhost 3123 localhost 3127 &
$SERVER localhost 3127 localhost 3126 localhost 3124 localhost 3128 &
$SERVER localhost 3128 localhost 3125 localhost 3127 &


echo "Press ENTER to quit"
read
pkill $SERVER_NAME
