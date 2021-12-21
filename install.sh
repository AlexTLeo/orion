#!/bin/bash
# make sure install path is provided
if [ $# -eq 0 ]
  then
    echo "No arguments supplied: installation path required"
    exit 1
fi

echo Installing...
# unzip sources.zip archive to specified pathname
unzip sources.zip -d $1
# make bin/ directory for executables
mkdir $1/bin
mkdir $1/logs
touch $1/logs/errors.log
touch $1/logs/info.log
# compile source files & create executables, link math library
gcc $1/src/producer.c -o $1/bin/producer -lrt -pthread -lm &>> logs/errors.log
gcc $1/src/consumer.c -o $1/bin/consumer -lrt -pthread -lm &>> logs/errors.log
gcc $1/src/master.c -o $1/bin/master -lrt -pthread -lm &>> logs/errors.log
touch $1/run.sh
chmod +x $1/run.sh;
# main executable script: run.sh
echo "#!/bin/bash" > $1/run.sh
echo "gnome-terminal -- sh -c \"./bin/master; bash\"" >> $1/run.sh
echo "echo return value: \$?" >> $1/run.sh

echo Installation complete. Executable created: $1/run.sh
