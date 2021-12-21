**Institution:** Università di Genova<br>
**Course:** MSc in Robotics Engineering<br>
**Subject:** Advanced and Robot Programming<br>
**Author:** ***Alex Thanaphon Leonardi***<br>

# ORION: Data Satellite (assignment 2)
This program, written in C, comprises 3 different processes that work together to transmit data via 4 selectable IPC mechanisms (**unnamed pipes**, **named pipes**, **sockets** or **shared memory**).
(For my own entertainment, I gave the UI a slightly sci-fi feel).

## Running The Program
To run the program, gcc is required to compile the source code.
```
sudo apt-get install gcc
```

Then, navigate to the ***orion*** directory and run the install script specifying directory.
```
cd example/orion/
sudo chmod +x install.sh
./install.sh ./
```

Then, simply execute the **run** file:
1. **Standard mode**
```
./run.sh
```
2. **Debug mode** (fast output, no decorative text)
```
./run.sh debug
```

## Behind The Scenes...
The program consists of 3 processes that work together:
1. master
2. producer
3. consumer

### Master
The master process is mostly UI, with some error control sprinkled in. It asks the user for relevant input (IPC protocol, size of data to be transferred, port number for sockets) before executing produer and consumer with the correct arguments.

### Producer
The producer process generates random data (integers) which is then sent to the consumer process via the selected IPC protocol. The transmission start-time is recorded and sent via shared memory to the consumer process.

### Consumer
The consumer receives the data sent by the producer through the selected IPC protocol, reads the start timer written in shared memory, and records the end-time once it is done reading. It then calculates total transmission time and prints it.

## Behind The Scenes: IPC mechanisms
Let's see some interesting details about each implementation
1. **Unnamed pipes**
2. **Named pipes**
3. **Sockets**
4. **Shared memory**

### Unnamed Pipes
The master only executes the producer, which opens the pipe, forks and in turn executes the conumser and passes it the pipe file descriptors.

### Named pipes
Just as you would expect: a named pipe is opened and used to transmit data

### Sockets
A **TCP client/server handshaking architecture** is used. The producer acts as the **server** while the consumer acts as the **client**. The total data to be sent is divided down into groups of up to 2MiB, to avoid problems with buffer overflow. The consumer sends the producer a request for packets, and the producer responds with the requested packets. The consumer then informs the producer that the packets have been correctly received, and the handshake repeats until all required data is sent. Then, the connection terminates.

### Shared memory
Shared memory and a **circular buffer** system is used. **Semaphores** in this case are used to guarantee a correct circular buffer mechanism.

## Conclusion
This project highlighted the different transfer speeds of the aforementioned 4 IPC mechanisms. Improvements can definitely be made to vastly improve the transfer speed of each mechanism, for example through the **bufferisation** of data, perhaps sending/reading entire blocks of information rather than just one value at a time.
In the end, unnamed pipes and sockets (even with TCP) resulted in **much faster communication speeds** than named pipes and shared memory.
