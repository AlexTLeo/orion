#include "../include/common.h"

/**
* The master process prompts users to select the transfer method and opens the
* appropriate processes.
*/

// Print decorative text
void printStartOfTransmission(char* transmissionProtocol);

int TEXT_DELAY = 25000; // Delay in "typing" text to terminal
bool DEBUG_MODE = false;
const int MAX_SIZE_MIB = 100; // Amount of data to transfer

int main (int argc, char** argv) {
  bool isInputCorrect;
  bool isRunning;
  bool hasOneChild;
  int sizeDataMiB;
  char* sizeDataMiB_str;
  int input;
  int retStatus;
  pid_t childPID;
  pid_t waitPID;

  if (argc == 1) {
    DEBUG_MODE = false;
  } else if (argc == 2 && !strcmp(argv[1], "debug")) {
    DEBUG_MODE = true;
  } else {
    printf("ERROR: bad arguments. Please see README.\n");
    fflush(stdout);
    exit(-1);
  }

  if (DEBUG_MODE) {
    TEXT_DELAY = 0;
  }

  isRunning = true;
  while (isRunning) {
    hasOneChild = false;
    isInputCorrect = false;
    sizeDataMiB_str = malloc(sizeof(char)*8); // Just some extra room!
    sizeDataMiB = 1;

    clearTerminal();
    displayText("Welcome to the Orion satellite. I am your AI assistant.\n", TEXT_DELAY);
    displayText("Please specify the amount of data to be transmitted between 1 and ", TEXT_DELAY);
    printf("%d", MAX_SIZE_MIB);
    displayText("MiB (MebiBytes): ", TEXT_DELAY);
    fflush(stdout);
    while (!isInputCorrect) {
      // Get input from user
      if (fgets(sizeDataMiB_str, 8, stdin) < 0) {
        perror("ERROR in sizeDataMiB_str fgets");
        exit(-1);
      }

      sizeDataMiB = atoi(sizeDataMiB_str);

      // Input checks
      if (sizeDataMiB > MAX_SIZE_MIB || sizeDataMiB < 1) {
        clearTerminal();
        terminalColor(41, true);
        displayText("Invalid input.\n", TEXT_DELAY);
        usleep(100000);
        terminalColor(37, true);
        displayText("Please specify an amount between 1 and 100 MiB: ", TEXT_DELAY);
        fflush(stdout);
      } else {
        isInputCorrect = true;
        sprintf(sizeDataMiB_str, "%d", sizeDataMiB);
      }
    }

    clearTerminal();
    terminalColor(36, true);
    printf("Data size: %dMiB\n", sizeDataMiB);
    terminalColor(37, true);
    displayText("Select the transmission protocol: \n", TEXT_DELAY);
    terminalColor(32, true);
    displayText("1) Unnamed Pipes\n", TEXT_DELAY);
    displayText("2) Named Pipes\n", TEXT_DELAY);
    displayText("3) Sockets\n", TEXT_DELAY);
    displayText("4) Shared Memory\n\n", TEXT_DELAY);
    displayText("Or press any other key to power down Orion.", TEXT_DELAY);
    fflush(stdout);

    input = (int) detectKeyPress();

    clearTerminal();

    // Spawn processes with user-specified configuration
    switch (input) {
      case 49: {
        // Key Pressed "1" : Unnamed Pipes
        // Only need to spawn producer in this case, which spawns the consumer on its own!
        char* argListProducer[] = {"./bin/producer", "0", sizeDataMiB_str, NULL};

        hasOneChild = true; // Special case, only spawned producer

        printStartOfTransmission("Unnamed Pipes");

        // Forking so master process can remain in control
        childPID = fork();
        if (childPID == 0) {
          // Child
          if (execvp("./bin/producer", argListProducer) < 0) {
            perror("ERROR in case 49 execvp");
          }
        } else if (childPID < 0) {
          perror("ERROR in case 49 fork");
          exit(-1);
        }
        break;
      }

      case 50: {
        // Key pressed "2": Named Pipes
        char* argListProducer[] = {"./bin/producer", "1", sizeDataMiB_str, NULL};
        char* argListConsumer[] = {"./bin/consumer", "1", sizeDataMiB_str, NULL};

        printStartOfTransmission("Named Pipes");

        // Forking so master process can remain in control
        // PRODUCER
        childPID = fork();
        if (childPID == 0) {
          // Child
          if (execvp("./bin/producer", argListProducer) < 0) {
            perror("ERROR in case 50 execvp 1");
          }
        } else if (childPID < 0) {
          perror("ERROR in case 50 fork 1");
          exit(-1);
        }

        // CONSUMER
        childPID = fork();
        if (childPID == 0) {
          // Child
          if (execvp("./bin/consumer", argListConsumer) < 0) {
            perror("ERROR in case 50 execvp 2");
          }
        } else if (childPID < 0) {
          perror("ERROR in case 50 fork 2");
          exit(-1);
        }

        break;
      }

      case 51: {
        // Key pressed "3": Sockets
        char* portno_str = malloc(sizeof(char) * 5);

        // Get port number from user
        clearTerminal();
        displayText("Insert port number for transmission: ", TEXT_DELAY);

        if (fgets(portno_str, sizeof(portno_str), stdin) < 0) {
          perror("ERROR in portno fgets");
          exit(-1);
        }

        char* argListProducer[] = {"./bin/producer", "2", sizeDataMiB_str, portno_str, NULL};
        char* argListConsumer[] = {"./bin/consumer", "2", sizeDataMiB_str, portno_str, NULL};

        clearTerminal();
        terminalColor(36, true);
        printf("Port Number: %s", portno_str);
        fflush(stdout);
        printStartOfTransmission("Sockets");

        // Forking so master process can remain in control
        childPID = fork();
        if (childPID == 0) {
          // Child
          if (execvp("./bin/producer", argListProducer) < 0) {
            perror("ERROR in case 51 execvp 1");
          }
        } else if (childPID < 0) {
          perror("ERROR in case 51 fork 1");
          exit(-1);
        }

        // CONSUMER
        childPID = fork();
        if (childPID == 0) {
          // Child
          if (execvp("./bin/consumer", argListConsumer) < 0) {
            perror("ERROR in case 50 execvp 2");
          }
        } else if (childPID < 0) {
          perror("ERROR in case 50 fork 2");
          exit(-1);
        }

        break;
      }

      case 52: {
        // Key pressed "4": Shared Memory
        char* argListProducer[] = {"./bin/producer", "3", sizeDataMiB_str, NULL};
        char* argListConsumer[] = {"./bin/consumer", "3", sizeDataMiB_str, NULL};

        printStartOfTransmission("Shared Memory");

        // Forking so master process can remain in control
        childPID = fork();
        if (childPID == 0) {
          // Child
          if (execvp("./bin/producer", argListProducer) < 0) {
            perror("ERROR in case 52 execvp 1");
          }
        } else if (childPID < 0) {
          perror("ERROR in case 52 fork 1");
          exit(-1);
        }

        // CONSUMER
        childPID = fork();
        if (childPID == 0) {
          // Child
          if (execvp("./bin/consumer", argListConsumer) < 0) {
            perror("ERROR in case 50 execvp 2");
          }
        } else if (childPID < 0) {
          perror("ERROR in case 50 fork 2");
          exit(-1);
        }

        break;
      }

      default: {
        clearTerminal();
        displayText("Powering down the Orion satellite...\n", TEXT_DELAY);
        sleep(2);

        return 0;
      }
    }


    if (hasOneChild) {
      waitPID = wait(&retStatus);
    } else {
      waitPID = wait(&retStatus);
      waitPID = wait(&retStatus);
    }

    if (WEXITSTATUS(retStatus) < 0) {
      clearTerminal();
      terminalColor(31, true);
      displayText("Transmission error. Please consult error logs.\nSatellite powering off...", TEXT_DELAY);
    }

    displayText(" seconds.", TEXT_DELAY);
    displayText("\n\nPress any key to continue...", TEXT_DELAY);
    getchar();
  }

  return 0;
}

void printStartOfTransmission(char* transmissionProtocol) {
  if (DEBUG_MODE) {
    terminalColor(32, true);
    printf("Transmitting...\n");
    terminalColor(37, true);
    printf("Total transmission time: ");
    terminalColor(32, true);
    fflush(stdout);
  } else {
    displayText("Chosen protocol: ", TEXT_DELAY);
    terminalColor(32, true);
    displayText(transmissionProtocol, TEXT_DELAY);
    terminalColor(37, true);
    printf(".\n\n");
    displayText("Rotating antenna...\n\n", TEXT_DELAY);
    displayText("Establishing link with Starfleet Headquarters...\n\n", TEXT_DELAY);
    displayText("Transmission in progress...\n", TEXT_DELAY);
    terminalColor(37, true);
    displayText("\nThe total transmission time is: ", TEXT_DELAY);
    terminalColor(32, true);
  }
}
