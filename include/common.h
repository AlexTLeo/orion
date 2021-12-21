#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include<sys/wait.h>
#include <netinet/in.h>
#include <netdb.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <math.h>
#include <semaphore.h>
#include <termios.h>

/////////////////
//// LOGGING ////
/////////////////

// Opens error log
int openErrorLog() {
  int fd;

  fd = open("logs/errors.log", O_WRONLY | O_APPEND | O_CREAT, 0666);
  if (fd == -1) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h openErrorLog");
    exit(-1);
  }

  return fd;
}

// Opens info log
int openInfoLog() {
  int fd;

  fd = open("logs/info.log", O_WRONLY | O_APPEND | O_CREAT, 0666);
  if (fd == -1) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h openInfoLog");
    exit(-1);
  }

  return fd;
}

// Writes to info log
void writeInfoLog(int fd, char* string) {
  // get current time
  time_t rawtime;
  struct tm * timeinfo;
  char* currentTime = malloc(sizeof(timeinfo));
  time ( &rawtime );
  timeinfo = localtime ( &rawtime );

  // prettier format...
  sprintf(currentTime, "[%d-%d-%d %d:%d:%d]", timeinfo->tm_mday,
      timeinfo->tm_mon + 1, timeinfo->tm_year + 1900, timeinfo->tm_hour,
      timeinfo->tm_min, timeinfo->tm_sec);

  // make sure print is atomic
  flock(fd, LOCK_EX);
  if (dprintf(fd, "%s %s\n", currentTime, string) < 0) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h writeInfoLog");
    exit(-1);
  }
  flock(fd, LOCK_UN);
}

// Writes to error log
void writeErrorLog(int fd, char* string, int errorCode) {
  // get current time
  time_t rawtime;
  struct tm * timeinfo;
  char* currentTime = malloc(sizeof(timeinfo));
  time ( &rawtime );
  timeinfo = localtime ( &rawtime );

  // prettier format...
  sprintf(currentTime, "[%d-%d-%d %d:%d:%d]", timeinfo->tm_mday,
      timeinfo->tm_mon + 1, timeinfo->tm_year + 1900, timeinfo->tm_hour,
      timeinfo->tm_min, timeinfo->tm_sec);

  // make sure print is atomic
  flock(fd, LOCK_EX);
  if (dprintf(fd, "%s (code: %d) %s\n", currentTime, errorCode, string) < 0) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h writeErrorLog");
    exit(-1);
  }
  flock(fd, LOCK_UN);
}

// Closes log defined by fd
void closeLog(int fd) {
  if (close(fd) == -1) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h closeLog");
    exit(-1);
  }
}

//////////////
//// MISC ////
//////////////

// Returns current time in milliseconds
double getCurrrentTimeMS () {
  long ms; // Milliseconds
  time_t s;  // Seconds
  struct timespec spec;
  double currTime_ms;

  clock_gettime(CLOCK_REALTIME, &spec);

  s  = spec.tv_sec;
  ms = round(spec.tv_nsec / 1.0e6); // Convert nanoseconds to milliseconds
  if (ms > 999) {
    s++;
    ms = 0;
  }

  currTime_ms = (s * 1000) + ms;

  return currTime_ms;
}

// Changes terminal color
// colorCode - ANSI color code
void terminalColor(int colorCode, bool isBold) {
  char* specialCode = "";
  if (isBold) {
    specialCode = "1;";
  }

  printf("\033[%s%dm", specialCode, colorCode);
  fflush(stdout);
}

// Resets the terminal to initial state with title
void clearTerminal() {
  printf("\033c");
  system("clear");
  terminalColor(31, true);
  printf("========================\n");
  printf("= ORION DATA SATELLITE =\n");
  printf("========================\n\n");
  terminalColor(37, true);
  fflush(stdout);
}

// Decorative text print, with "typing" effect
// delay - in microseconds
void displayText (char* str, int delay) {
  for (int i = 0; str[i] != '\0'; i++) {
    printf("%c", str[i]);
    fflush(stdout);
    usleep(delay);
  }
}

// Detects key presses, without waiting for newline (ENTER)
int detectKeyPress () {
  int input;
  struct termios orig_term_attr;
  struct termios new_term_attr;

  /* set the terminal to raw mode rather than canonical mode */
  tcgetattr(fileno(stdin), &orig_term_attr);
  memcpy(&new_term_attr, &orig_term_attr, sizeof(struct termios));
  new_term_attr.c_lflag &= ~(ECHO|ICANON);
  new_term_attr.c_cc[VTIME] = 0;
  new_term_attr.c_cc[VMIN] = 1;
  tcsetattr(fileno(stdin), TCSANOW, &new_term_attr);

  /* read a character from the stdin stream without blocking */
  /* returns EOF (-1) if no character is available */
  input = getchar();

  /* restore the original terminal attributes */
  tcsetattr(fileno(stdin), TCSANOW, &orig_term_attr);

  return input;
}

///////////////
//// PIPES ////
///////////////

// Creates and opens the COMMANDER pipe
int pipeStart(char* pipeName, bool isWriting, int fdlog_err) {
  int fd;

  // (ignore "file already exists", errno 17)
  if (mkfifo(pipeName, 0666) == -1 && errno != 17) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h pipeStart mkfifo");
    writeErrorLog(fdlog_err, "common.h: pipeStart mkfifo failed", errno);
    exit(-1);
  }

  if (isWriting) {
    fd = open(pipeName, O_WRONLY);
  } else {
    fd = open(pipeName, O_RDONLY);
  }

  if (fd == -1) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h pipeStart open");
    writeErrorLog(fdlog_err, "common.h: pipeStart open failed", errno);
    exit(-1);
  }

  return fd;
}

// Writes to pipe
void pipeWrite (int fd, int message, int fdlog_err) {
  if (write(fd, &message, sizeof(message)) == -1) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h pipeWrite");
    writeErrorLog(fdlog_err, "common.h: pipeWrite failed", errno);
    exit(-1);
  }
}

// Writes to pipe
void pipeWriteString (int fd, char message[], int messageLength, int fdlog_err) {
  if (write(fd, &message, messageLength) == -1) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h pipeWrite");
    writeErrorLog(fdlog_err, "common.h: pipeWrite failed", errno);
    exit(-1);
  }
}

// Reads from pipe
int pipeRead (int fd, int fdlog_err) {
  int message;

  if (read(fd, &message, sizeof(message)) == -1) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h pipeRead");
    writeErrorLog(fdlog_err, "common.h: pipeRead failed", errno);
    exit(-1);
  }

  return message;
}

// Reads from pipe
void pipeReadString (int fd, char *messageContainer, int messageLength, int fdlog_err) {
  if (read(fd, &messageContainer, messageLength) == -1) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h pipeRead");
    writeErrorLog(fdlog_err, "common.h: pipeRead failed", errno);
    exit(-1);
  }
}

// Closes the pipe
void pipeClose(int fd, int fdlog_err) {
  if (close(fd) == -1) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h pipeClose");
    writeErrorLog(fdlog_err, "common.h: pipeClose failed", errno);
    exit(-1);
  }
}

///////////////////////
//// SHARED MEMORY ////
///////////////////////

// Initialises shared memory for use
void* shmInit(char* shmPath, void* addr, size_t length, int prot, int flags, off_t offset, int fdlog_err) {
  int fdShm;
  void* ptr;

  fdShm = shm_open(shmPath, O_CREAT | O_RDWR, 0666);
  if (fdShm < 0) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h shmMMap shm_open");
    writeErrorLog(fdlog_err, "common.h: shmMMap shm_open failed", errno);
    exit(-1);
  }

  if (ftruncate(fdShm, length) < 0) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h shmMMap ftruncate");
    writeErrorLog(fdlog_err, "common.h: shmMMap ftruncate failed", errno);
    exit(-1);
  }

  ptr = mmap(NULL, length, PROT_WRITE, MAP_SHARED, fdShm, 0);
  if (ptr == MAP_FAILED) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h shmMMap mmap");
    writeErrorLog(fdlog_err, "common.h: shmMMap mmap failed", errno);
    exit(-1);
  }

  // close file descriptor
  if (close(fdShm) == -1) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h shmMMap close");
    writeErrorLog(fdlog_err, "common.h: shmMMap close failed", errno);
    exit(-1);
  }

  return ptr;
}

// Writes a double to shared memory
void shmWriteOnce_double(char* shmPath, double message, void** ptr, int fdlog_err) {
  int sharedSegSize;
  int fdShm;

  sharedSegSize = (sizeof(message));

  fdShm = shm_open(shmPath, O_CREAT | O_RDWR, 0666);
  if (fdShm < 0) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h shmWriteOnce_double shm_open");
    writeErrorLog(fdlog_err, "common.h: shmWriteOnce_double shm_open failed", errno);
    exit(-1);
  }

  if (ftruncate(fdShm, sharedSegSize) < 0) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h shmWriteOnce_double ftruncate");
    writeErrorLog(fdlog_err, "common.h: shmWriteOnce_double ftruncate failed", errno);
    exit(-1);
  }

  *ptr = mmap(NULL, sharedSegSize, PROT_WRITE, MAP_SHARED, fdShm, 0);
  if (*ptr == MAP_FAILED) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h shmWriteOnce_double mmap");
    writeErrorLog(fdlog_err, "common.h: shmWriteOnce_double mmap failed", errno);
    exit(-1);
  }

  // close file descriptor
  if (close(fdShm) == -1) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h shmWriteOnce_double close");
    writeErrorLog(fdlog_err, "common.h: shmWriteOnce_double close failed", errno);
    exit(-1);
  }

  sprintf(*ptr, "%f", message);
}

// Given a pointer to shared memory, writes the given integer to it
void shmWriteInteger(void** ptr, int message, int offset, int fdlog_err) {
  char* ptrNew = (char*) (((int*) *ptr) + offset);
  sprintf(ptrNew, "%d", message);
}

// Reads a double from shared memory
double shmReadOnce_double(char* shmPath, void** ptr, int fdlog_err) {
  int sharedSegSize;
  int fdShm;
  double message;

  sharedSegSize = (sizeof(message));
  fdShm = shm_open(shmPath, O_RDONLY, 0666);
  if (fdShm < 0) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h shmReadOnce_double shm_open");
    writeErrorLog(fdlog_err, "common.h: shmReadOnce_double shm_open failed", errno);
    exit(-1);
  }

  *ptr = mmap(NULL, sharedSegSize, PROT_READ, MAP_SHARED, fdShm, 0);
  if (*ptr == MAP_FAILED) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h shmReadOnce_double mmap");
    writeErrorLog(fdlog_err, "common.h: shmReadOnce_double mmap failed", errno);
    exit(-1);
  }

  // close file descriptor
  if (close(fdShm) == -1) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h shmReadOnce_double close");
    writeErrorLog(fdlog_err, "common.h: shmReadOnce_double close failed", errno);
    exit(-1);
  }

  message = strtod((char*) *ptr, NULL);

  return message;
}

// Reads an integer from shared memory
int shmReadInteger(void** ptr, int offset, int fdlog_err) {
  int message;
  int* ptrNew = ((int*) *ptr) + offset;
  message = strtod((char*) ptrNew, NULL);

  return message;
}

// Unlinks caller from shared memory
void shmUnlink(char* shmPath, int fdlog_err) {
  if (shm_unlink(shmPath) < 0) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h shmUnlink shm_unlink");
    writeErrorLog(fdlog_err, "common.h: shmUnlink shm_unlink failed", errno);
    exit(-1);
  }
}

// Unlinks caller from shared memory and also memory pointed to by void** ptr
void shmUnlinkUnmap(char* shmPath, void** ptr, size_t length, int fdlog_err) {
  if (shm_unlink(shmPath) < 0) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h shmUnlinkUnmap shm_unlink");
    writeErrorLog(fdlog_err, "common.h: shmUnlinkUnmap shm_unlink failed", errno);
    exit(-1);
  }

  if (munmap(*ptr, length) < 0) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h shmUnlinkUnmap munmap");
    writeErrorLog(fdlog_err, "common.h: shmUnlinkUnmap munmap failed", errno);
    exit(-1);
  }
}

////////////////////
//// SEMAPHORES ////
////////////////////

// Opens a semaphore with specified pathname.
sem_t* semOpen(char *pathname, int initValue, int fdlog_err) {
  sem_t* sem;

  sem = sem_open(pathname, O_CREAT | O_RDWR, 0666, initValue);

  if (sem < 0) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h sem_open");
    writeErrorLog(fdlog_err, "common.h: sem_open failed", errno);
    exit(-1);
  }
}

// Calls a sem_wait on given semaphore
void semWait(sem_t* sem, int fdlog_err) {
  if (sem_wait(sem) < 0) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h sem_wait");
    writeErrorLog(fdlog_err, "common.h: sem_wait failed", errno);
    exit(-1);
  }
}

// Calls a sem_post on given sempahore
void semPost(sem_t* sem, int fdlog_err) {
  if (sem_post(sem) < 0) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h sem_post");
    writeErrorLog(fdlog_err, "common.h: sem_post failed", errno);
    exit(-1);
  }
}

// Closes given semaphore
void semClose(sem_t* sem, int fdlog_err) {
  if (sem_close(sem) < 0) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h sem_close");
    writeErrorLog(fdlog_err, "common.h: sem_close failed", errno);
    exit(-1);
  }
}

// Unlinks given semaphore
void semUnlink(char* pathname, int fdlog_err) {
  if (sem_unlink(pathname) < 0) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h sem_unlink");
    writeErrorLog(fdlog_err, "common.h: sem_unlink failed", errno);
    exit(-1);
  }
}

/////////////////
//// SOCKETS ////
/////////////////

// Wrapper for socket()
int socketCreate(int domain, int type, int protocol, int fdlog_err) {
  int fd;

  fd = socket(domain, type, protocol);
  if (fd < 0) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h socketCreate");
    writeErrorLog(fdlog_err, "common.h: socketCreate failed", errno);
    exit(-1);
  }

  return fd;
}

// Wrapper for gethostbyname()
struct hostent* getHostFromName(const char* name, int fdlog_err) {
  struct hostent* ent;

  ent = gethostbyname(name);
  if (ent == NULL) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h getHostFromName");
    writeErrorLog(fdlog_err, "common.h: getHostFromName failed (no such host)", errno);
    exit(-1);
  }

  return ent;
}

// Wrapper for connect()
void socketConnect(int sockfd, const struct sockaddr* addr, socklen_t addrlen, int fdlog_err) {
  bool isFailed = true;
  int retConnect;

  // Keep trying to connect for a while before giving up
  for (int i = 0; i < 5; i++) {
    retConnect = connect(sockfd, addr, addrlen);
    if (retConnect < 0) {
      sleep(1);
    } else {
      isFailed = false;
      break;
    }
  }

  if (isFailed) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h socketConnect");
    writeErrorLog(fdlog_err, "common.h: socketConnect failed, connection timed out", errno);
    exit(-1);
  }
}

// Wrapper for write()
void socketWrite(int fd, int message, int messageLength, int fdlog_err) {
  if (write(fd, &message, messageLength) < 0) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h socketWrite");
    writeErrorLog(fdlog_err, "common.h: socketWrite failed", errno);
    exit(-1);
  }
}

// Read from socket and return value
int socketRead(int fd, int messageLength, int fdlog_err) {
  int message;

  if (read(fd, &message, messageLength) < 0) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h socketRead");
    writeErrorLog(fdlog_err, "common.h: socketRead failed", errno);
    exit(-1);
  }

  return message;
}

// Wrapper for bind()
void socketBind (int sockfd, const struct sockaddr* addr, socklen_t addrlen, int fdlog_err) {
  if (bind(sockfd, addr, addrlen) < 0) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h socketBind");
    writeErrorLog(fdlog_err, "common.h: socketBind failed", errno);
    exit(-1);
  }
}

// Wrapper for listen()
void socketListen (int sockfd, int backlog, int fdlog_err) {
  if (listen(sockfd, backlog) < 0) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h socketListen");
    writeErrorLog(fdlog_err, "common.h: socketListen failed", errno);
    exit(-1);
  }
}

// Wrapper for accept()
int socketAccept (int sockfd, struct sockaddr* cliAddr, socklen_t* addrlen, int fdlog_err) {
  if (accept(sockfd, cliAddr, addrlen) < 0) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h socketAccept");
    writeErrorLog(fdlog_err, "common.h: socketAccept failed", errno);
    exit(-1);
  }
}

// Wrapper for setsockopt()
void socketSetOpt (int sockfd, int level, int optname,
    const void* optval, socklen_t optlen, int fdlog_err) {
  if (setsockopt(sockfd, level, optname, optval, optlen) < 0) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h socketSetOpt");
    writeErrorLog(fdlog_err, "common.h: socketSetOpt failed", errno);
    exit(-1);
  }
}

// Closes the socket
void socketClose(int fd, int fdlog_err) {
  if (close(fd) == -1) {
    printf("Error %d in ", errno);
    fflush(stdout);
    perror("common.h socketClose");
    writeErrorLog(fdlog_err, "common.h: socketClose failed", errno);
    exit(-1);
  }
}
