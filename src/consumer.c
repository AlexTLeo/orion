#include "../include/common.h"

// Different functions to read data using different IPC mechanisms

double readUnnamedPipe(int sizeDataMiB, int messages[], int fd_read);

// If fildes is a non negative integer then uses it as file descriptor, otherwise
// generates its own file descriptor and name as /tmp/arpassign2
double readNamedPipe(int sizeDataMiB, int messages[], int fildes);

// The consumer acts as the CLIENT
double readSocket(int sizeDataMiB, int messages[], char* hostname, int portno);

// Uses a circular buffer to read data through shared memory
double readSharedMemory(int sizeDataMiB, int messages[], int circularBufferSize);

// Max possible size of data to transfer, specified here, in MiB
const int MAX_SIZE_MIB = 100;
const int MIB_TO_B_CONSTANT = 1049000;
const int MESSAGE_SIZE_B = 4; // size of one message in bytes (int = 4 bytes)
const int CIRC_BUFFER_SIZE = 4096; // max buffer size for circular buffer in shared memory
const int DEFAULT_PORTNO = 4000; // default port number for sockets
// Log file descriptors
int fdlog_err;
int fdlog_info;
// Function to use, specified by user to master process as argv[1]
int choiceIPC;

int main (int argc, char** argv) {
  char* logMessage;
  double timeToTransfer = 0;
  // Amount of data to be transferred, specified by user to master process and
  // passed over as second user argument
  int sizeDataMiB;
  // Messages to be transferred, pre-initialized to be of maximum dimension MAX_SIZE_MiB but only a portion will be used
  int* messages;
  pid_t myPID;

  if (argc < 3) {
    fprintf(stderr, "ERROR: expecting at least 3 arguments!");
    exit(-1);
  }

  // Open logs
  fdlog_err = openErrorLog();
  fdlog_info = openInfoLog();

  logMessage = malloc(sizeof(char) * 128);
  // User input
  choiceIPC = atoi(argv[1]);
  sizeDataMiB = atoi(argv[2]);

  // Input checks
  if (choiceIPC < 0 || choiceIPC > 3) {
    fprintf(stderr, "ERROR: first argument should be between 0 and 3");
    writeErrorLog(fdlog_err, "[Producer] Invalid user-specified IPC choice number", 0);
    exit(-1);
  }

  if (sizeDataMiB > MAX_SIZE_MIB) {
    fprintf(stderr, "ERROR: maximum data transfer size is %d MiB", MAX_SIZE_MIB);
    writeErrorLog(fdlog_err, "[Consumer] Invalid user-specified transfer size", 0);
    exit(-1);
  }

  // Initialize size of messages as specified by args
  messages = calloc(sizeDataMiB*MIB_TO_B_CONSTANT/MESSAGE_SIZE_B, MESSAGE_SIZE_B);

  sprintf(logMessage, "[Consumer] Total data transfer size: %dMiB", sizeDataMiB);
  writeInfoLog(fdlog_info, logMessage);
  switch(choiceIPC) {
    case 0:
      ;
      // Unnamed pipes
      // This case should only be invoked by producer process, which forks and
      // passes its file descriptors via args
      int fd_read = atoi(argv[3]);
      timeToTransfer = readUnnamedPipe(sizeDataMiB, messages, fd_read);
      break;
    case 1:
      ;
      // Named pipes
      timeToTransfer = readNamedPipe(sizeDataMiB, messages, -1);
      break;
    case 2:
      // Sockets
      ;
      int portno;
      if (argc >= 3) {
        portno = atoi(argv[3]);
      } else {
        portno = DEFAULT_PORTNO;
      }

      timeToTransfer = readSocket(sizeDataMiB, messages, "localhost", portno);
      break;
    default:
      // Shared Memory
      timeToTransfer = readSharedMemory(sizeDataMiB, messages, CIRC_BUFFER_SIZE);
      break;
  }

  printf("%.3f", timeToTransfer);
  fflush(stdout);
  sprintf(logMessage, "[Consumer] Total transfer time: %.3f seconds", timeToTransfer);
  writeInfoLog(fdlog_info, logMessage);

  myPID = getpid();
  return myPID;
}

double readUnnamedPipe(int sizeDataMiB, int messages[], int fd_read) {
  double timeToTransfer_ms;

  // Pipe has already been created so from here on it works just as a named pipe
  // but passing our unnamed pipe's file descriptor
  timeToTransfer_ms = readNamedPipe(sizeDataMiB, messages, fd_read);
  return timeToTransfer_ms;
}

double readNamedPipe(int sizeDataMiB, int messages[], int fildes) {
  sem_t* semConsumer;
  sem_t* semProducer;
  int numReads;
  int fd;
  double timerStart_ms, timerEnd_ms;
  double timeToTransfer_ms; // milliseconds
  void* ptrShmTimer;

  numReads = (sizeDataMiB*MIB_TO_B_CONSTANT)/MESSAGE_SIZE_B; // mebibytes to bytes

  // Semaphore to ensure correct usage of shared memory
  semConsumer = semOpen("/arp2_sem_consumer", 0, fdlog_err);
  semProducer = semOpen("/arp2_sem_producer", 0, fdlog_err);

  // Open pipe and read from it
  if (fildes < 0) {
    // Named pipe
    writeInfoLog(fdlog_info, "[Consumer] Opening pipe");
    fd = pipeStart("/tmp/arpassign2", false, fdlog_err);
  } else {
    // Unnamed pipe
    fd = fildes;
  }

  writeInfoLog(fdlog_info, "[Consumer] Starting pipe read");

  for (int i = 0; i < numReads; i++) {
    messages[i] = pipeRead(fd, fdlog_err);
  }

  // Timer end
  timerEnd_ms = getCurrrentTimeMS();
  writeInfoLog(fdlog_info, "[Consumer] Ending transfer timer");
  writeInfoLog(fdlog_info, "[Consumer] Pipe read complete");

  // Wait for producer to have actually written to the shared memory!
  writeInfoLog(fdlog_info, "[Consumer] Accessing semaphore arp2_sem_consumer");
  semWait(semConsumer, fdlog_err);

  // Calculating total transfer time
  writeInfoLog(fdlog_info, "[Consumer] Reading transfer start time from shared memory");
  timerStart_ms = shmReadOnce_double("/shm_timerStart", &ptrShmTimer, fdlog_err);

  // Let the producer know I'm done reading
  writeInfoLog(fdlog_info, "[Consumer] Posting semaphore arp2_sem_producer");
  semPost(semProducer, fdlog_err);

  writeInfoLog(fdlog_info, "[Consumer] Calculating total transfer time");
  timeToTransfer_ms = (timerEnd_ms - timerStart_ms)/1000;

  // Cleanup
  writeInfoLog(fdlog_info, "[Consumer] Closing pipe");
  pipeClose(fd, fdlog_err);
  writeInfoLog(fdlog_info, "[Consumer] Pipe closed");

  writeInfoLog(fdlog_info, "[Consumer] Unlinking shared memory");
  shmUnlinkUnmap("/shm_timerStart", &ptrShmTimer, sizeof(double), fdlog_err);
  writeInfoLog(fdlog_info, "[Consumer] Shared memory unlinked");

  writeInfoLog(fdlog_info, "[Consumer] Unlinking semaphores");
  semUnlink("/arp2_sem_consumer", fdlog_err);
  writeInfoLog(fdlog_info, "[Consumer] Semaphores unlinked");

  return timeToTransfer_ms;
}

double readSocket(int sizeDataMiB, int messages[], char* hostname, int portno) {
  sem_t* semConsumer;
  sem_t* semProducer;
  int numReads;
  int sockfd;
  int messageIndex;
  int numBlocks;
  int remainder;
  int numReadsPerBlock;
  int numReadsRemainder;
  struct sockaddr_in servAddr;
  struct hostent* server;
  char* logMessage;
  double timerStart_ms, timerEnd_ms;
  double timeToTransfer_ms; // milliseconds
  void* ptrShmTimer;

  logMessage = malloc(sizeof(char) * 256);
  numReads = (sizeDataMiB*MIB_TO_B_CONSTANT)/MESSAGE_SIZE_B; // mebibytes to bytes

  // Semaphore to ensure correct usage of shared memory
  semConsumer = semOpen("/arp2_sem_consumer", 0, fdlog_err);
  semProducer = semOpen("/arp2_sem_producer", 0, fdlog_err);

  // Socket creation
  sprintf(logMessage, "[Consumer] Opening socket on %s:%d", hostname, portno);
  writeInfoLog(fdlog_info, logMessage);
  sockfd = socketCreate(AF_INET, SOCK_STREAM, 0, fdlog_err);

  // Socket configuration
  writeInfoLog(fdlog_info, "[Consumer] Configuring socket");
  server = getHostFromName(hostname, fdlog_err);
  bzero((char *) &servAddr, sizeof(servAddr));
  servAddr.sin_family = AF_INET;
  bcopy((char *) server->h_addr, (char *)&servAddr.sin_addr.s_addr, server->h_length);
  servAddr.sin_port = htons(portno);

  // Connect to server
  writeInfoLog(fdlog_info, "[Consumer] Connecting to server");
  socketConnect(sockfd, (struct sockaddr *) &servAddr, sizeof(servAddr), fdlog_err);

  // Request packets in blocks of 2MB to avoid buffer overflow
  numBlocks = (int) sizeDataMiB / 2; // Rounded down
  if (sizeDataMiB > 2) {
    remainder = sizeDataMiB % 2;
  } else {
    // If sizeDataMiB < 2, then numBlocks is 0 and remainder would be 0 unless
    // we specifically assign it here
    remainder = sizeDataMiB;
  }

  if (numBlocks != 0) {
    numReadsRemainder = (remainder*MIB_TO_B_CONSTANT)/MESSAGE_SIZE_B;
    numReadsPerBlock = numReads/numBlocks - numReadsRemainder;
  } else {
    numReadsPerBlock = 0;
  }

  // First, tell server how many blocks we need
  socketWrite(sockfd, numBlocks, MESSAGE_SIZE_B, fdlog_err);
  // Then, server know the remainder
  socketWrite(sockfd, remainder, MESSAGE_SIZE_B, fdlog_err);

  messageIndex = 0;
  for (int i = 0; i < numBlocks; i++) {
    for (int j = 0; j < numReadsPerBlock; j++) {
      // Read the packets of one block
      messages[messageIndex] = socketRead(sockfd, MESSAGE_SIZE_B, fdlog_err);
      messageIndex++;
    }

    // Now let the server know we are done reading, so the next block can be sent
    socketWrite(sockfd, 1, MESSAGE_SIZE_B, fdlog_err);
  }

  // Read final data, if it wasn't already read (if there is a remainder)
  if (remainder != 0) {
    for (int i = 0; i < numReadsRemainder; i++) {
      messages[messageIndex] = socketRead(sockfd, MESSAGE_SIZE_B, fdlog_err);
      messageIndex++;
    }
  } else {
    // Otherwise, tell server there is no remainder and everything is OK
    socketWrite(sockfd, 0, MESSAGE_SIZE_B, fdlog_err);
  }

  // Timer end
  timerEnd_ms = getCurrrentTimeMS();
  writeInfoLog(fdlog_info, "[Consumer] Ending transfer timer");
  writeInfoLog(fdlog_info, "[Consumer] Data transfer complete");

  // Wait for producer to have actually written to the shared memory!
  writeInfoLog(fdlog_info, "[Consumer] Accessing semaphore arp2_sem_consumer");
  semWait(semConsumer, fdlog_err);

  // Calculating total transfer time
  writeInfoLog(fdlog_info, "[Consumer] Reading transfer start time from shared memory");
  timerStart_ms = shmReadOnce_double("/shm_timerStart", &ptrShmTimer, fdlog_err);

  // Let the producer know I'm done reading
  writeInfoLog(fdlog_info, "[Consumer] Posting semaphore arp2_sem_producer");
  semPost(semProducer, fdlog_err);

  writeInfoLog(fdlog_info, "[Consumer] Calculating total transfer time");
  timeToTransfer_ms = (timerEnd_ms - timerStart_ms)/1000;

  // Cleanup
  writeInfoLog(fdlog_info, "[Consumer] Closing socket");
  socketClose(sockfd, fdlog_err);
  writeInfoLog(fdlog_info, "[Consumer] Socket closed");

  writeInfoLog(fdlog_info, "[Consumer] Unlinking shared memory");
  shmUnlinkUnmap("/shm_timerStart", &ptrShmTimer, sizeof(double), fdlog_err);
  writeInfoLog(fdlog_info, "[Consumer] Shared memory unlinked");

  writeInfoLog(fdlog_info, "[Consumer] Unlinking semaphores");
  semUnlink("/arp2_sem_consumer", fdlog_err);
  writeInfoLog(fdlog_info, "[Consumer] Semaphores unlinked");

  return timeToTransfer_ms;
}

double readSharedMemory(int sizeDataMiB, int messages[], int circularBufferSize) {
  sem_t* mutexCircBuffer;
  sem_t* semConsumer;
  sem_t* semProducer;
  sem_t* semCircBufferProducer;
  sem_t* semCircBufferConsumer;
  int cbufferTail; // Keeps track of position in circular buffer
  int numReads;
  double timerStart_ms, timerEnd_ms;
  double timeToTransfer_ms; // milliseconds
  void* ptrShmTimer;
  void* ptrShmCBuffer;

  numReads = (sizeDataMiB*MIB_TO_B_CONSTANT)/MESSAGE_SIZE_B; // mebibytes to bytes

  // Initialise shared memory
  writeInfoLog(fdlog_info, "[Consumer] Initialising shared memory");
  ptrShmCBuffer = shmInit("/shm_arpassign2", NULL, circularBufferSize,
      PROT_WRITE, MAP_SHARED, 0, fdlog_err);

  // Semaphore to ensure correct usage of shared memory
  writeInfoLog(fdlog_info, "[Consumer] Initializing semaphores");
  cbufferTail = 0;
  mutexCircBuffer = semOpen("arp2_mutex_cbuffer", 1, fdlog_err);
  semConsumer = semOpen("/arp2_sem_consumer", 0, fdlog_err);
  semProducer = semOpen("/arp2_sem_producer", 0, fdlog_err);
  semCircBufferProducer = semOpen("/arp2_sem_cbuffer_producer",
      circularBufferSize/MESSAGE_SIZE_B, fdlog_err);
  semCircBufferConsumer = semOpen("/arp2_sem_cbuffer_consumer", 0, fdlog_err);

  writeInfoLog(fdlog_info, "[Consumer] Reading from shared memory");

  for (int i = 0; i < numReads; i++) {
    semWait(semCircBufferConsumer, fdlog_err);
    semWait(mutexCircBuffer, fdlog_err);
    // Dividing cbufferTail by size of message because the ptr already increments by sizeof message
    messages[i] = shmReadInteger(&ptrShmCBuffer, cbufferTail/MESSAGE_SIZE_B, fdlog_err);
    semPost(mutexCircBuffer, fdlog_err);
    cbufferTail = (++cbufferTail % (circularBufferSize - 1));
    semPost(semCircBufferProducer, fdlog_err);
  }

  // Timer end
  timerEnd_ms = getCurrrentTimeMS();
  writeInfoLog(fdlog_info, "[Consumer] Ending transfer timer");
  writeInfoLog(fdlog_info, "[Consumer] Read complete");

  // Wait for producer to have actually written to the shared memory!
  writeInfoLog(fdlog_info, "[Consumer] Accessing semaphore arp2_sem_consumer");
  semWait(semConsumer, fdlog_err);

  // Calculating total transfer time
  writeInfoLog(fdlog_info, "[Consumer] Reading transfer start time from shared memory");
  timerStart_ms = shmReadOnce_double("/shm_timerStart", &ptrShmTimer, fdlog_err);

  // Let the producer know I'm done reading
  writeInfoLog(fdlog_info, "[Consumer] Posting semaphore arp2_sem_producer");
  semPost(semProducer, fdlog_err);

  writeInfoLog(fdlog_info, "[Consumer] Calculating total transfer time");
  timeToTransfer_ms = (timerEnd_ms - timerStart_ms)/1000;

  // Cleanup
  writeInfoLog(fdlog_info, "[Consumer] Unlinking shared memory");
  shmUnlinkUnmap("/shm_timerStart", &ptrShmTimer, sizeof(double), fdlog_err);
  shmUnlinkUnmap("/shm_arpassign2", &ptrShmCBuffer, sizeof(double), fdlog_err);
  writeInfoLog(fdlog_info, "[Consumer] Shared memory unlinked");

  writeInfoLog(fdlog_info, "[Consumer] Unlinking semaphores");
  semUnlink("/arp2_mutex_cbuffer", fdlog_err);
  semUnlink("/arp2_sem_consumer", fdlog_err);
  semUnlink("/arp2_sem_cbuffer_consumer", fdlog_err);
  writeInfoLog(fdlog_info, "[Consumer] Semaphores unlinked");

  return timeToTransfer_ms;
}
