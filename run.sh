#!/bin/bash
gcc src/producer.c -o bin/producer -lrt -pthread -lm &>> logs/errors.log
gcc src/consumer.c -o bin/consumer -lrt -pthread -lm &>> logs/errors.log
gcc src/master.c -o bin/master -lrt -pthread -lm &>> logs/errors.log

# gnome-terminal -- sh -c "./bin/producer 3 5;bash"
# gnome-terminal -- sh -c "./bin/consumer 3 5;bash"
gnome-terminal -- sh -c "./bin/master $1;bash"
