#include <unistd.h>
#include <stdio.h>
#include <getopt.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <iostream>
#include <netinet/in.h>
#include <string.h>
#include <fstream>
#include <pthread.h> 
#include <semaphore.h>
#include <arpa/inet.h>
#include <sstream>
#define PORT 80

const size_t memorySize = 16384;
int threadsInUse;
void* putRequest(void* arg);
void* getRequest(void* arg);
char *validChars = (char *)"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";
int testSum = 0;
int threadIndex = 0;
bool isGetRequest = false;
int logFileDescriptor; 
char *logFileName;
bool logFilePresent = false;
pthread_mutex_t offsetLock = PTHREAD_MUTEX_INITIALIZER;
size_t logFileOffset = 0;
sem_t semaphoreThread;

//***********************************************************************/
// request_Thread_Struct - used to pass multiple arguments to threads
//***********************************************************************/
struct request_Thread_Struct
{
    char *headerBuffer;
    int request_Socket;
};


//***********************************************************************/
// convertStringToHex - args - string: string to be converted
//                    - returns - string converted to hex
// Will take a given string and return a string converted to hex
// adapted from : https://www.tutorialspoint.com/stringstream-in-cplusplus-for-decimal-to-hexadecimal-and-back
//***********************************************************************/
std::string convertStringToHex(std::string stringConvert)
{
    std::stringstream hexString;
    for(int i = 0; i < (int)stringConvert.length(); ++i)
    {
        hexString << std::hex << (int)stringConvert[i];
    }
    std::string mystr = hexString.str();
    return mystr; 
}


//***********************************************************************/
// returnNumDigits - args - num: integer
//                 - returns - amount of digits in a given int
//***********************************************************************/
int returnNumDigits(int num)
{
    int count = 0;
    while (num != 0)
    {
        count++;
        num /= 10;
    }
    return count;
}


int main(int argc, char *argv[])
{

    char headerBuffer[1024] = {0};
    int server_fd;
    int new_socket;
    int headerReader;
    int numThreads = 4;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    int optimalValue = 1;
    char *addressName;
    char *portNumber;
    int opt;

    // parsing user input
    // For some reason getopt return -1 if flags came after specifying host/port
    
    while((opt = getopt(argc, argv, "?N:l:")) != -1)
    {
        switch(opt)
        {
            case 'N':
                numThreads = atoi(optarg);
                break;
            case 'l':
                logFileName = (char*) optarg;
                logFilePresent = true;
                logFileDescriptor = open(logFileName, O_RDONLY);

                if(logFileDescriptor > 0)
                {
                    remove(logFileName);
                    logFileDescriptor = open(logFileName, O_WRONLY | O_CREAT);

                }else{

                    logFileDescriptor = open(logFileName, O_CREAT | O_WRONLY);

                }
                break;
            case '?':
                std::cout << "unknown: " << optind;
                break;
        }
    }

    if(opt == -1 && argc > 3)
    {
        if(argc == 7)
        {
            if((std::string)argv[3] == "-N")
            {
                numThreads = atoi(argv[4]);
            }
            if((std::string)argv[3] == "-l")
            {
                logFileName = (char*) argv[4];
                logFilePresent = true;
                logFileDescriptor = open(logFileName, O_WRONLY);
                if(logFileDescriptor > 0)
                {
                    remove(logFileName);
                    logFileDescriptor = open(logFileName, O_CREAT | O_WRONLY);

                }else{
                    logFileDescriptor = open(logFileName, O_CREAT | O_WRONLY);

                }
            }
            if((std::string)argv[5] == "-N")
            {
                numThreads = atoi(argv[6]);
            }
            if((std::string)argv[5] == "-l")
            {
                logFileName = (char*) argv[6];
                logFilePresent = true;
                logFileDescriptor = open(logFileName, O_WRONLY);
                if(logFileDescriptor > 0)
                {
                    remove(logFileName);
                    logFileDescriptor = open(logFileName, O_CREAT | O_WRONLY);

                }else{
                    logFileDescriptor = open(logFileName, O_CREAT | O_WRONLY);

                }
            }
        }
        if(argc == 5)
        {
            if((std::string)argv[3] == "-N")
            {
                numThreads = atoi(argv[4]);
            }
            if((std::string)argv[3] == "-l")
            {
                logFileName = (char*) argv[4];
                logFilePresent = true;
                logFileDescriptor = open(logFileName, O_WRONLY);
                if(logFileDescriptor > 0)
                {
                    remove(logFileName);
                    logFileDescriptor = open(logFileName, O_CREAT | O_WRONLY);

                }else{
                    logFileDescriptor = open(logFileName, O_CREAT | O_WRONLY);

                }
            }
        }
    }
    

    // assigning "extra input" to address and port if present
    int extraArgCount = 0;
    address.sin_port = htons(PORT);
    address.sin_addr.s_addr = INADDR_ANY;

    for(; optind < argc; optind++)
    {      
        if (extraArgCount == 0)
        {
            addressName = (char*) argv[optind];
            address.sin_family = atoi(addressName);
        }
        else if (extraArgCount == 1)
        {
            portNumber = (char*) argv[optind];
            address.sin_port = htons(atoi(portNumber));
        }
        extraArgCount++;

    }

    pthread_t *threadPool = new pthread_t[numThreads];
    sem_init(&semaphoreThread, 0, numThreads);

    // server setup adapted from https://www.geeksforgeeks.org/socket-programming-cc/
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    { 
        perror("socket failed");
        exit(EXIT_FAILURE);
    } 
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optimalValue, sizeof(optimalValue))) 
    { 
        perror("setsockopt"); 
        exit(EXIT_FAILURE); 
    } 
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &optimalValue, sizeof(optimalValue))) 
    { 
        perror("setsockopt"); 
        exit(EXIT_FAILURE); 
    } 

    address.sin_family = AF_INET;

    if (bind(server_fd, (struct sockaddr *)&address,
             sizeof(address)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    // we infinite loop on the accept stage to be able to accept multiple requests
    while(true)
    {
        isGetRequest = false;
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address,
                                (socklen_t *)&addrlen)) < 0)
        {
            perror("accept");
            exit(EXIT_FAILURE);
        }

        memset(headerBuffer, 0, sizeof(headerBuffer));
        headerReader = read(new_socket, headerBuffer, sizeof(headerBuffer));

        struct request_Thread_Struct argStruct;

        argStruct.request_Socket = new_socket;
        argStruct.headerBuffer = headerBuffer;
        

        // need to implement semaphores, to put threads to sleep until in use
        // 
        std::string getOrPutHeader = "";
        getOrPutHeader += headerBuffer[0];
        getOrPutHeader += headerBuffer[1];
        getOrPutHeader += headerBuffer[2];
        // determine whether it is a GET or PUT request
        if (getOrPutHeader.compare("GET") == 0)
        {
            isGetRequest = true;
        }

        if (isGetRequest == false) 
        {   
            sem_wait(&semaphoreThread);
            pthread_create(&threadPool[threadIndex], NULL, putRequest, &argStruct);
            threadIndex++;
        }
        else
        {
            sem_wait(&semaphoreThread);
            pthread_create(&threadPool[threadIndex], NULL, getRequest, &argStruct);
            threadIndex++;
        }

        for(int i = 0; i < numThreads; i++)
        {   
            pthread_join(threadPool[i], NULL);
        }
        
        close(new_socket);
        
    }
    close(logFileDescriptor);
    close(server_fd);
   
    return 0;
}


//***********************************************************************/
// writeToLogFile - args - string: string to write to log file
//                - returns - void
// Will take the fully formatted log string and pwrite to global log file
//***********************************************************************/

void writeToLogFile(std::string str)
{
    if(logFilePresent)
    {
        char *charArray = new char[str.length() + 1];
        strcpy(charArray, str.c_str());

        std::cout << "char array: " << charArray << "\n";

        // for offset, need to have a counter for amount of characters, offset for where to start
        pwrite(logFileDescriptor, charArray, strlen(charArray), logFileOffset);

        pthread_mutex_lock(&offsetLock);
        logFileOffset += str.length();
        pthread_mutex_unlock(&offsetLock);


    }
}

//***********************************************************************/
// getRequest - args - headerbuffer: buffer returned from client
//                   - new_socket: socket connection initialized in main
//            - returns - void
// Will get file if exists in server, and send servertype, error/success codes,
// contents back to the client, and the content length
//***********************************************************************/

void* getRequest(void *arg)
{
    struct request_Thread_Struct *arg_struct = 
        (struct request_Thread_Struct*) arg;
    char *headerBuffer = arg_struct->headerBuffer;
    int new_socket = arg_struct->request_Socket;
    char str[500] = {0};
    char serverType[20] = {0};
    // content length of the file
    // char contentLength[20] = {0};
    // char contentLengthExists[20] = {0};
    char *contentLengthString = (char *)"Content-Length: ";
    char textFileName[500] = {0};

    //uint_8t 
    // uint8_t *fileBuffer = (uint8_t*) calloc(memorySize, sizeof(char));
    uint8_t *fileBuffer = (uint8_t*) malloc(memorySize * sizeof(uint8_t));

    char *successString = (char *)" 200 OK\r\n";
    char *fileNotFoundString = (char *)" 404 Not Found\r\n";
    char *permissionDenied = (char *)" 400 Forbidden\r\n";
    char *badRequest = (char *)" 400 Bad Request\r\n";
    char *endResponse = (char *)"\r\n";
    int textFileLengthCount = 0;
    bool validTextName = true;
    int contentLength = 0;

    // LOG variables
    char* requestType = (char* )"GET ";
    char* failString = (char*)"FAIL: ";
    char *lengthString = (char*)" length ";
    char* endLineLogFile = (char*)"========\n";
    std::string failedRequestString = ""; 
    failedRequestString += failString;
    failedRequestString += requestType;

    // parse header for file name and server type
    sscanf(headerBuffer, "%*s %s", str, NULL);
    sscanf(headerBuffer, "%*s %*s %s", serverType, NULL);
    send(new_socket, serverType, strlen(serverType), 0);

    // delete the '/' character
    int j = 0;
    for (size_t i = 0; i < sizeof(str); ++i)
    {

        if (str[i] != 0 && str[i] != '/')
        {
            textFileName[j] = str[i];
            textFileLengthCount++;
        }
        j++;
    }

    
    char *namingConvention = textFileName;
    
    // adapted from source: https://stackoverflow.com/questions/6605282/how-can-i-check-if-a-string-has-special-characters-in-c-effectively
    // checks for special characters excluding '-' and '_'
    if (namingConvention[strspn(namingConvention, validChars)] != 0)
    {
        validTextName = false;
    }

    failedRequestString += namingConvention; 
    failedRequestString += " ";
    failedRequestString += serverType;
    failedRequestString += " --- response ";

    // if the length of the textfile is greater than 27 chars 
    if (textFileLengthCount != 27 || validTextName == false)
    {
        send(new_socket, badRequest, strlen(badRequest), 0);
        failedRequestString += "400\n";
        writeToLogFile(failedRequestString);
    }
    else
    {
        
        int fileDescriptor = open(textFileName, O_RDONLY);


        if (fileDescriptor < 0)
        {
            // permissions error
            if (errno == EACCES)
            {
                send(new_socket, permissionDenied, strlen(permissionDenied), 0);
                failedRequestString += "400\n";
                writeToLogFile(failedRequestString);
            }
            // file not found error
            else
            {
                send(new_socket, fileNotFoundString, strlen(fileNotFoundString), 0);
                failedRequestString += "404\n";
                writeToLogFile(failedRequestString);
            }
        }
        else
        {
            // concatenate these two strings
            // std::string tempSuccess (successString);
            // std::string tempContent (contentLengthString);
            // std::string tempEnd (endResponse);
            // std::string combined = tempSuccess + endResponse +  tempContent;

            // char charArray[combined.length() + 1];
            // strcpy(charArray, combined.c_str());
            // send(new_socket, charArray, strlen(charArray), 0);
            // end 

            send(new_socket, successString, strlen(successString), 0);
            send(new_socket, contentLengthString, strlen(contentLengthString), 0);


            int byteCount = 0;
            std::string fileToString = ""; 

            // read through file once to get content length, then read again to send
            // contents to client
            while (true) 
            {
                int readFileLength = read(fileDescriptor, fileBuffer, sizeof(fileBuffer));
                
                if (readFileLength == 0)
                {
                    break;
                }
                contentLength += readFileLength;
                fileToString += (char*)fileBuffer;
                memset(fileBuffer, 0, memorySize);
            }


            std::string contentLengthToString = std::to_string(contentLength);


            // writing to log file

            std::string stringToHex = convertStringToHex(fileToString); 

            std::string stringToLogFile = "";
            stringToLogFile += requestType;
            stringToLogFile += namingConvention;
            stringToLogFile += lengthString;
            stringToLogFile += contentLengthToString;
            stringToLogFile += "\n";
            for (int i = 0; i < (int)stringToHex.length()-1; i+=2)
            {
                std::string leadingZeroString = "";
                int numLeadingZeros = 8;
                if(byteCount == 0)
                {
                    stringToLogFile += "00000000 ";
                }
                if(byteCount != 0 && byteCount % 20 == 0) 
                {
                    numLeadingZeros = numLeadingZeros - returnNumDigits(byteCount);
                    for(int k = 0; k < numLeadingZeros; k++)
                    {
                        leadingZeroString += "0";
                    }
                    leadingZeroString += std::to_string(byteCount);

                    char firstChar = stringToHex[i];
                    char secondChar = stringToHex[i+1];
                    stringToLogFile += "\n";
                    stringToLogFile += leadingZeroString;
                    stringToLogFile += " ";
                    stringToLogFile += firstChar;
                    stringToLogFile += secondChar;
                    stringToLogFile += " ";
                }
                else
                {
                    char firstChar = stringToHex[i];
                    char secondChar = stringToHex[i+1];
                    stringToLogFile += firstChar;
                    stringToLogFile += secondChar;
                    stringToLogFile += " ";
                }
                
                byteCount++;
            }
            stringToLogFile += "\n";
            stringToLogFile += endLineLogFile;
            writeToLogFile(stringToLogFile);



            //send content length value
            // std::string tempSuccess (contentLengthToString);
            // std::string tempEnd (endResponse);
  
            // std::string combined = tempSuccess + tempEnd;

            // char *charArray = new char[combined.length() + 1];
            // strcpy(charArray, combined.c_str());
            // send(new_socket, charArray, strlen(charArray), 0);

            send(new_socket, contentLengthToString.c_str(), strlen(contentLengthToString.c_str()), 0);
            send(new_socket, endResponse, strlen(endResponse), 0);  
            send(new_socket, endResponse, strlen(endResponse), 0);
          

            close(fileDescriptor);

            fileDescriptor = open(textFileName, O_RDONLY);
            while (true) 
            {
                int readFileLength = read(fileDescriptor, fileBuffer, sizeof(fileBuffer));                
                if (readFileLength == 0)
                {
                    break;
                }
                send(new_socket , fileBuffer, sizeof(fileBuffer), 0); 
                memset(fileBuffer, 0, memorySize);
            }
            // send(new_socket, endResponse, strlen(endResponse), 0);
            
        }
        close(fileDescriptor);
    }
    pthread_mutex_lock(&offsetLock);
    threadIndex--;
    pthread_mutex_unlock(&offsetLock);
    sem_post(&semaphoreThread);
    return NULL;
    //send buffer back to client (curl)
}



//***********************************************************************/
// putRequest - args - headerbuffer: buffer returned from client
//                   - new_socket: socket connection initialized in main
//            - returns - void
// Will create file in server passed in by client, and read contents of file 
// into the new created file. Will send back server type, error/success codes,
// and content length
//***********************************************************************/

void* putRequest(void* arg)
{
    struct request_Thread_Struct *arg_struct = 
        (struct request_Thread_Struct*) arg;
    char *headerBuffer = arg_struct->headerBuffer;
    int new_socket = arg_struct->request_Socket;   
    
    char str[500] = {0};
    char serverType[20] = {0};

    // content length of the file
    char contentLength[20] = {0};
    char contentLengthExists[20] = {0};

    char *contentLengthString = (char *)"Content-Length: 0\r\n";
    char *contentLengthCheck = (char *)"Content-Length:";
    char textFileName[500] = {0};
    int readFileLength;
    bool writeFailed = false;
    char *successString = (char *)" 200 OK\r\n";
    char *permissionDeniedString = (char *)" 400 Forbidden\r\n";
    char *createdFileString = (char *)" 201 Created\r\n";
    char *badRequest = (char *)" 400 Bad Request\r\n";
    char *fileBuffer = (char *)calloc(memorySize, sizeof(char));
    char *endResponse = (char *)"\r\n";
    bool permissionDenied = false;
    bool fileCreated = false;
    int textFileLength = 0;
    bool validTextName = true;
    int intContentLength;
    std::string nameTemp = "";
    std::string headerError = "";
    bool missingContentLength = false;
    
    // LOG variables
    char* requestType = (char* )"PUT ";
    char* failString = (char*)"FAIL: ";
    char *lengthString = (char*)" length ";
    char* endLineLogFile = (char*)"========\n";
    std::string failedRequestString = ""; 
    failedRequestString += failString;
    failedRequestString += requestType;

    // parse header for textfile name, server type, content length, and for content length string
    sscanf(headerBuffer, "%*s %s", str, NULL);
    sscanf(headerBuffer, "%*s %*s %s", serverType, NULL);
    sscanf(headerBuffer, "%*s %*s %*s %*s %*s %*s %*s %*s %*s %s", contentLengthExists, NULL);
    sscanf(headerBuffer, "%*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %s", contentLength, NULL);
    int j = 0;

    // need to check to make sure there is a contentLength in the header, or else it is an invalid put 
    for (size_t i = 0; i < sizeof(str); ++i)
    {
        if (str[i] != 0 && str[i] != '/')
        {
            textFileName[j] = str[i];
            textFileLength++;            
        }
        j++;
    }

    // converting char contentLength
    for(size_t i = 0; i < sizeof(contentLength); i++)
    {
        if (contentLength[i] != 0)
        {
            nameTemp += contentLength[i];
        }
    }

    // checking if contentLength exists in header
    for(size_t i = 0; i < sizeof(contentLengthExists); i++)
    {
        if (contentLengthExists[i] != 0)
        {
            headerError += contentLengthExists[i];
        }
    }
    
    // if missing contentLength in header
    if (headerError.compare(contentLengthCheck) != 0)
    {
       missingContentLength = true;
    }

    intContentLength = std::stoi(nameTemp);



    // cited above, determines if any special characters present not specified in assignment
    char *namingConvention = textFileName;
    if (namingConvention[strspn(namingConvention, validChars)] != 0)
    {
        validTextName = false;
    }

    failedRequestString += namingConvention; 
    failedRequestString += " ";
    failedRequestString += serverType;
    failedRequestString += " --- response ";


    

    send(new_socket, serverType, strlen(serverType), 0);

    if(missingContentLength == true || textFileLength != 27 || validTextName == false)
    {
        send(new_socket, badRequest, strlen(badRequest), 0);
        failedRequestString += "400\n";
        writeToLogFile(failedRequestString);

    }
    else
    {
        int fileDescriptor = open(textFileName, O_WRONLY);
        if (fileDescriptor < 0)
        { 
            // send error code 400 forbidden
            if (errno == EACCES) 
            {
                send(new_socket, permissionDeniedString, strlen(permissionDeniedString), 0);
                permissionDenied = true;
                failedRequestString += "400\n";
                writeToLogFile(failedRequestString);

            } 
            else 
            { 
                fileDescriptor = open(textFileName, O_CREAT | O_WRONLY); 
                fileCreated = true; 
            } 
        } 
        else 
        {
            // if the file already exists, we need to override existing file
            remove(textFileName); 
            fileDescriptor = open(textFileName, O_CREAT | O_WRONLY); 
        } 

        if (permissionDenied == false) 
        { 
            readFileLength = recv(new_socket, fileBuffer, intContentLength, 0); 
            if (logFilePresent == true) { 

                std::string fileToString = ""; 
                fileToString.append(fileBuffer); 
                std::string stringToHex = convertStringToHex(fileToString); 

                int byteCount = 0;

                std::string stringToLogFile = "";
                stringToLogFile += requestType;
                stringToLogFile += namingConvention;
                stringToLogFile += lengthString;
                stringToLogFile += contentLength;
                stringToLogFile += "\n";
                
                for (int i = 0; i < (int) stringToHex.length()-1; i+=2)
                {
                    std::string leadingZeroString = "";
                    int numLeadingZeros = 8;
                    if(byteCount == 0)
                    {
                        stringToLogFile += "00000000 ";
                    }
                    if(byteCount != 0 && byteCount % 20 == 0) 
                    {
                        numLeadingZeros = numLeadingZeros - returnNumDigits(byteCount);
                        for(int k = 0; k < numLeadingZeros; k++)
                        {
                            leadingZeroString += "0";
                        }
                        leadingZeroString += std::to_string(byteCount);

                        char firstChar = stringToHex[i];
                        char secondChar = stringToHex[i+1];
                        stringToLogFile += "\n";
                        stringToLogFile += leadingZeroString;
                        stringToLogFile += " ";
                        stringToLogFile += firstChar;
                        stringToLogFile += secondChar;
                        stringToLogFile += " ";
                    }
                    else
                    {
                        char firstChar = stringToHex[i];
                        char secondChar = stringToHex[i+1];
                        stringToLogFile += firstChar;
                        stringToLogFile += secondChar;
                        stringToLogFile += " ";
                    }
                    
                    byteCount++;
                }
                stringToLogFile += "\n";
                stringToLogFile += endLineLogFile;
                
                // stringToLogFile += "\n";
                
                
                // for offset, need to have a counter for amount of characters, offset for where to start
                writeToLogFile(stringToLogFile);

                
            }
            int writeToFile = write(fileDescriptor, fileBuffer, readFileLength);
            if (writeToFile < 0)
            {
                if (errno == EACCES)
                {
                    writeFailed = true;
                }
            }
            
            // send error code 400 forbidden
            if (writeFailed == true)
            {
                send(new_socket, permissionDeniedString, strlen(permissionDeniedString), 0);
                failedRequestString += "400\n"; 
                writeToLogFile(failedRequestString);
            }
            // send error code 201 created
            else if (fileCreated == true)
            {   
                send(new_socket, createdFileString, strlen(createdFileString), 0);
                send(new_socket, contentLengthString, strlen(contentLengthString), 0);
                send(new_socket, endResponse, strlen(endResponse), 0);
            } 
            // send success code 200 OK
            else
            {
                send(new_socket, successString, strlen(successString), 0);
                send(new_socket, contentLengthString, strlen(contentLengthString), 0);
                send(new_socket, endResponse, strlen(endResponse), 0);
            }
            
        }  
        close(fileDescriptor);
    }
    pthread_mutex_lock(&offsetLock);
    threadIndex--;
    pthread_mutex_unlock(&offsetLock);
    sem_post(&semaphoreThread);

    return NULL;
    
}