#include "../include/common.h"

// Different functions to send data using different IPC mechanisms. These functions
// all return the transfer time measured in seconds and milliseconds

void sendUnnamedPipe(int sizeDataMiB, int messages[]);

// If fildes is a non negative integer then uses it as file descriptor, otherwise
// generates its own file descriptor and name as /tmp/arpassign2
void sendNamedPipe(int sizeDataMiB, int messages[], int fildes);

// The producer acts as the CLIENT
void sendSocket(int sizeDataMiB, int messages[], int portno);

// Uses a circular buffer to send data through shared memory
void sendSharedMemory(int sizeDataMiB, int messages[], int circularBufferSize);

// Generates random messages up to specified max size in MiB and fills array
void generateMessages(int sizeDataMiB, int *messages);

const int MAX_SIZE_MIB = 100; // Amount of data to transfer
const int MIB_TO_B_CONSTANT = 1049000;
const int MESSAGE_SIZE_B = 4; // size of messages in bytes (int = 4 bytes)
const int CIRC_BUFFER_SIZE = 4096; // max buffer size for circular buffer in shared memory
const int DEFAULT_PORTNO = 4000; // default port number for sockets
// Log file descriptors
int fdlog_err;
int fdlog_info;
// Function to use, specified by user to master process as argv[1]
int choiceIPC;

int main (int argc, char** argv) {
  bool isInputCorrect = false;
  // Amount of data to be transferred, specified by user to the master process
  // passed over as second user argument
  int sizeDataMiB = atoi(argv[2]);
  // Array of messages to be transferred;
  int* messages;

  if (argc < 3) {
    fprintf(stderr, "ERROR: expecting at least 2 arguments!");
    exit(-1);
  }

  // Open logs
  fdlog_err = openErrorLog();
  fdlog_info = openInfoLog();

  // IPC chosen by user
  choiceIPC = atoi(argv[1]);

  // Input checks
  if (choiceIPC < 0 || choiceIPC > 3) {
    fprintf(stderr, "ERROR: first argument should be between 0 and 3");
    writeErrorLog(fdlog_err, "[Producer] Invalid user-specified IPC choice number", 0);
    exit(-1);
  }

  if (sizeDataMiB > MAX_SIZE_MIB) {
    fprintf(stderr, "ERROR: maximum data transfer size is %d MiB", MAX_SIZE_MIB);
    writeErrorLog(fdlog_err, "[Producer] Invalid user-specified transfer size", 0);
    exit(-1);
  }

  writeInfoLog(fdlog_info, "================"); // new line

  // Initialize size of messages as specified by args
  messages = calloc(sizeDataMiB*MIB_TO_B_CONSTANT/MESSAGE_SIZE_B, MESSAGE_SIZE_B);

  // Randomly generate data to be transferred and store it in "messages"
  generateMessages(sizeDataMiB, messages);

  switch(choiceIPC) {
    case 0:
      // Unnamed pipes
      sendUnnamedPipe(sizeDataMiB, messages);
      break;
    case 1:
      // Named pipes
      sendNamedPipe(sizeDataMiB, messages, -1);
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

      sendSocket(sizeDataMiB, messages, portno);
      break;
    default:
      // Shared Memory
      sendSharedMemory(sizeDataMiB, messages, CIRC_BUFFER_SIZE);
      break;
  }

  return 0;
}

void sendUnnamedPipe(int sizeDataMiB, int messages[]) {
  int fildes[2];

  // Create pipe
  writeInfoLog(fdlog_info, "[Producer] Opening pipe");
  if (pipe(fildes) < 0) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("producer.c sendUnnamedPipe pipe()");
    writeErrorLog(fdlog_err, "producer.c: sendUnnamedPipe pipe() failed", errno);
    exit(-1);
  }

  // Fork into reader/writer
  writeInfoLog(fdlog_info, "[Producer] Forking process");
  switch (fork()) {
    case -1: // Error
      printf("Error %d in ", errno);
      fflush(stdout);
      perror("producer.c sendUnnamedPipe fork");
      writeErrorLog(fdlog_err, "producer.c: sendUnnamedPipe fork failed", errno);
      exit(-1);
      break;

    case 0: // Consumer (reader)
      pipeClose(fildes[1], fdlog_err);

      // Runs consumer script specifying unnamed pipe behaviour, passing file
      // descriptors for read and write ends
      char* choiceIPC_str = malloc(sizeof(char)); // only 1 int
      char* sizeDataMiB_str = malloc(sizeof(char) * 16); // can be multiple digits
      char* fd_read_str = malloc(sizeof(char) * 2); // just in case

      sprintf(choiceIPC_str, "%d", choiceIPC);
      sprintf(sizeDataMiB_str, "%d", sizeDataMiB);
      sprintf(fd_read_str, "%d", fildes[0]);

      char* arg_list[] = {"./bin/consumer", choiceIPC_str, sizeDataMiB_str,
          fd_read_str, NULL};
      execvp("./bin/consumer", arg_list);
      exit(1);

    default: // Producer (writer)
      pipeClose(fildes[0], fdlog_err);

      // Pipe has already been created so from here on it works just as a named pipe
      // but passing our unnamed pipe's file descriptor
      sendNamedPipe(sizeDataMiB, messages, fildes[1]);
      break;
  }

  wait(NULL);
}

void sendNamedPipe(int sizeDataMiB, int messages[], int fildes) {
  sem_t *semConsumer;
  sem_t* semProducer;
  int numWrites;
  int fd;
  double timerStart_ms;
  void *ptrShmTimer;

  numWrites = (sizeDataMiB*MIB_TO_B_CONSTANT)/MESSAGE_SIZE_B; // mebibytes to bytes

  // Semaphore to ensure correct usage of shared memory
  semConsumer = semOpen("/arp2_sem_consumer", 0, fdlog_err);
  semProducer = semOpen("/arp2_sem_producer", 0, fdlog_err);

  // Create/open pipe and write all the data to it
  if (fildes < 0) {
    // Named pipe
    writeInfoLog(fdlog_info, "[Producer] Opening pipe");
    fd = pipeStart("/tmp/arpassign2", true, fdlog_err);
  } else {
    // Unnamed pipe
    fd = fildes;
  }

  writeInfoLog(fdlog_info, "[Producer] Starting data transfer via pipe");

  // Timer start (epoch time)
  writeInfoLog(fdlog_info, "[Producer] Recording transfer start time");
  timerStart_ms = getCurrrentTimeMS();

  for (int i = 0; i < numWrites; i++) {
    pipeWrite(fd, messages[i], fdlog_err);
  }

  writeInfoLog(fdlog_info, "[Producer] Data transfer via pipe complete");

  // Send timer start time over to the consumer
  writeInfoLog(fdlog_info, "[Producer] Writing transfer start time to shared memory");
  shmWriteOnce_double("/shm_timerStart", timerStart_ms, &ptrShmTimer, fdlog_err);

  // Let the consumer know the shared memory is ready to be read
  writeInfoLog(fdlog_info, "[Producer] Posting semaphore arp2_sem_consumer");
  semPost(semConsumer, fdlog_err);

  // Wait for the consumer to have finished reading before cleaning up
  writeInfoLog(fdlog_info, "[Producer] Accessing semaphore arp2_sem_producer");
  semWait(semProducer, fdlog_err);

  // Cleanup
  writeInfoLog(fdlog_info, "[Producer] Closing pipe");
  pipeClose(fd, fdlog_err);
  writeInfoLog(fdlog_info, "[Producer] Pipe closed");

  writeInfoLog(fdlog_info, "[Producer] Unlinking semaphores");
  semUnlink("/arp2_sem_producer", fdlog_err);
  writeInfoLog(fdlog_info, "[Producer] Semaphores unlinked");
}

void sendSocket(int sizeDataMiB, int messages[], int portno) {
  sem_t *semConsumer;
  sem_t* semProducer;
  int sockfd;
  int sockfdAccept;
  int clilen;
  int messageIndex;
  int response;
  int numBlocks;
  int numWrites;
  int numWritesPerBlock;
  int remainder;
  int numWritesRemainder;
  struct sockaddr_in servAddr;
  struct sockaddr_in cliAddr;
  char* logMessage;
  double timerStart_ms;
  void *ptrShmTimer;
  const int optVal = 1;
  const socklen_t optLen = sizeof(optVal);

  logMessage = malloc(sizeof(char) * 256);
  numWrites = (sizeDataMiB*MIB_TO_B_CONSTANT)/MESSAGE_SIZE_B; // mebibytes to bytes

  // Semaphore to ensure correct usage of shared memory
  semConsumer = semOpen("/arp2_sem_consumer", 0, fdlog_err);
  semProducer = semOpen("/arp2_sem_producer", 0, fdlog_err);

  // Socket creation
  sprintf(logMessage, "[Producer] Opening socket on port %d", portno);
  writeInfoLog(fdlog_info, logMessage);
  sockfd = socketCreate(AF_INET, SOCK_STREAM, 0, fdlog_err);

  // Socket configuration
  writeInfoLog(fdlog_info, "[Producer] Configuring socket");
  // Make sure port is reusable (for running multiple times quickly, otherwise
  // waiting for system to clean up the port takes too long)
  socketSetOpt(sockfd, SOL_SOCKET, SO_REUSEPORT, (void*) &optVal, optLen, fdlog_err);
  bzero((char *) &servAddr, sizeof(servAddr));
  servAddr.sin_family = AF_INET;
  servAddr.sin_port = htons(portno);
  servAddr.sin_addr.s_addr = INADDR_ANY; // IP of current machine

  // Bind socket and listen for connections
  writeInfoLog(fdlog_info, "[Producer] Binding socket");
  socketBind(sockfd, (struct sockaddr *) &servAddr, sizeof(servAddr), fdlog_err);
  writeInfoLog(fdlog_info, "[Producer] Listening on socket");

  socketListen(sockfd, 5, fdlog_err);

  // Accept incoming connections
  writeInfoLog(fdlog_info, "[Producer] Accepting incoming connection");
  clilen = sizeof(cliAddr);
  sockfdAccept = socketAccept(sockfd, (struct sockaddr *) &cliAddr, &clilen, fdlog_err);

  // Client tells us how many blocks of data to send and how big each block is in MiB
  writeInfoLog(fdlog_info, "[Producer] Reading packet structure information");
  numBlocks = socketRead(sockfdAccept, MESSAGE_SIZE_B, fdlog_err);

  // Check if there is any remainder to send over
  remainder = socketRead(sockfdAccept, MESSAGE_SIZE_B, fdlog_err);

  if (numBlocks != 0) {
    numWritesRemainder = (remainder*MIB_TO_B_CONSTANT)/MESSAGE_SIZE_B;
    numWritesPerBlock = numWrites/numBlocks - numWritesRemainder;
  } else {
    numWritesPerBlock = 0;
  }

  messageIndex = 0;

  // Transfer all data
  writeInfoLog(fdlog_info, "[Producer] Starting packet transfer");

  // Timer start (epoch time)
  writeInfoLog(fdlog_info, "[Producer] Recording transfer start time");
  timerStart_ms = getCurrrentTimeMS();

  for (int i = 0; i < numBlocks; i++) {
    for (int j = 0; j < numWritesPerBlock; j++) {
      socketWrite(sockfdAccept, messages[messageIndex], MESSAGE_SIZE_B, fdlog_err);
      messageIndex++;
    }

    response = socketRead(sockfdAccept, MESSAGE_SIZE_B, fdlog_err);

    if (response != 1) {
      perror("ERROR in packet transfer");
      writeErrorLog(fdlog_err, "producer.c: packet transfer response negative", errno);
      exit(-1);
    }
  }

  if (remainder != 0) {
    // There is remaining data, send it!
    numWritesRemainder = (remainder*MIB_TO_B_CONSTANT)/MESSAGE_SIZE_B;
    for (int i = 0; i < numWritesRemainder; i++) {
      socketWrite(sockfdAccept, messages[messageIndex], MESSAGE_SIZE_B, fdlog_err);
      messageIndex++;
    }
  }

  writeInfoLog(fdlog_info, "[Producer] Data transfer complete");

  // Send timer start time over to the consumer
  writeInfoLog(fdlog_info, "[Producer] Writing transfer start time to shared memory");
  shmWriteOnce_double("/shm_timerStart", timerStart_ms, &ptrShmTimer, fdlog_err);

  // Let the consumer know the shared memory is ready to be read
  writeInfoLog(fdlog_info, "[Producer] Posting semaphore arp2_sem_consumer");
  semPost(semConsumer, fdlog_err);

  // Wait for the consumer to have finished reading before cleaning up
  writeInfoLog(fdlog_info, "[Producer] Accessing semaphore arp2_sem_producer");
  semWait(semProducer, fdlog_err);

  // Cleanup
  writeInfoLog(fdlog_info, "[Producer] Closing socket");
  socketClose(sockfd, fdlog_err);
  writeInfoLog(fdlog_info, "[Producer] Socket closed");

  writeInfoLog(fdlog_info, "[Producer] Unlinking semaphores");
  semUnlink("/arp2_sem_producer", fdlog_err);
  writeInfoLog(fdlog_info, "[Producer] Semaphores unlinked");
}

void sendSharedMemory(int sizeDataMiB, int messages[], int circularBufferSize) {
  sem_t* mutexCircBuffer;
  sem_t* semConsumer;
  sem_t* semProducer;
  sem_t* semCircBufferProducer;
  sem_t* semCircBufferConsumer;
  int cbufferHead; // Keeps track of position in circular buffer
  int numWrites;
  double timerStart_ms;
  void *ptrShmTimer;
  void *ptrShmCBuffer;

  numWrites = (sizeDataMiB*MIB_TO_B_CONSTANT)/MESSAGE_SIZE_B; // mebibytes to bytes

  // Initialise shared memory
  writeInfoLog(fdlog_info, "[Producer] Initialising shared memory");
  ptrShmCBuffer = shmInit("/shm_arpassign2", NULL, circularBufferSize,
      PROT_WRITE, MAP_SHARED, 0, fdlog_err);

  // Semaphore to ensure correct usage of shared memory and bounded buffer
  writeInfoLog(fdlog_info, "[Producer] Initializing semaphores");
  cbufferHead = 0;
  mutexCircBuffer = semOpen("arp2_mutex_cbuffer", 1, fdlog_err);
  semConsumer = semOpen("/arp2_sem_consumer", 0, fdlog_err);
  semProducer = semOpen("/arp2_sem_producer", 0, fdlog_err);
  semCircBufferProducer = semOpen("/arp2_sem_cbuffer_producer",
      circularBufferSize/MESSAGE_SIZE_B, fdlog_err);
  semCircBufferConsumer = semOpen("/arp2_sem_cbuffer_consumer", 0, fdlog_err);

  writeInfoLog(fdlog_info, "[Producer] Starting data transfer via shared memory");

  // Timer start (epoch time)
  writeInfoLog(fdlog_info, "[Producer] Recording transfer start time");
  timerStart_ms = getCurrrentTimeMS();

  for (int i = 0; i < numWrites; i++) {
    semWait(semCircBufferProducer, fdlog_err);
    semWait(mutexCircBuffer, fdlog_err);
    // Dividing cbufferHead by size of message because the ptr already increments by sizeof message
    shmWriteInteger(&ptrShmCBuffer, messages[i], cbufferHead/MESSAGE_SIZE_B, fdlog_err);
    semPost(mutexCircBuffer, fdlog_err);
    cbufferHead = (++cbufferHead % (circularBufferSize - 1));
    semPost(semCircBufferConsumer, fdlog_err);
  }

  writeInfoLog(fdlog_info, "[Producer] Data transfer complete");

  // Send timer start time over to the consumer
  writeInfoLog(fdlog_info, "[Producer] Writing transfer start time to shared memory");
  shmWriteOnce_double("/shm_timerStart", timerStart_ms, &ptrShmTimer, fdlog_err);

  // Let the consumer know the shared memory is ready to be read
  writeInfoLog(fdlog_info, "[Producer] Posting semaphore arp2_sem_consumer");
  semPost(semConsumer, fdlog_err);

  // Wait for the consumer to have finished reading before cleaning up
  writeInfoLog(fdlog_info, "[Producer] Accessing semaphore arp2_sem_producer");
  semWait(semProducer, fdlog_err);

  // Cleanup
  writeInfoLog(fdlog_info, "[Producer] Unlinking semaphores");
  semUnlink("/arp2_sem_producer", fdlog_err);
  semUnlink("/arp2_sem_cbuffer_producer", fdlog_err);
  writeInfoLog(fdlog_info, "[Producer] Semaphores unlinked");
}

void generateMessages(int sizeDataMiB, int *messages) {
  int numWrites = (sizeDataMiB*MIB_TO_B_CONSTANT)/MESSAGE_SIZE_B; // mebibytes to bytes
  srand(time(NULL));

  writeInfoLog(fdlog_info, "[Producer] Generating data to be transferred");

  // Generates random data and fills passed array messages
  for (int i = 0; i < numWrites; i++) {
    messages[i] = rand() % 100;
  }

  writeInfoLog(fdlog_info, "[Producer] Data generation complete");
}
