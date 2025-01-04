/*
 * Project Name: FinalAssignment - web_ized word without friends
 * Author: Zhisong Chen
 * Version: 1.0.7
 * Description: Implements a multithreaded web server that handles multiple client connections simultaneously.
 *              The server listens for incoming requests, creates a new thread for each connection, and processes GET requests.
 *              It allows users to play a word-guessing game via the web browser, with dynamic updates of the game state.
 *              To play, navigate to the URL: localhost:8000/filename.c (replace 'filename.c' with the appropriate file name).
 *              Use the cheat code "110" in the game input to reveal all words.
 *              The server retrieves requested files or sends an appropriate error message if the file is not found.
 *              Proper thread management ensures resource cleanup and efficient handling of multiple clients.
 */
#include <stdio.h>
#include <dirent.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>
#include <time.h>

//structure 
struct myThread{
	pthread_t id;
	int isDone;
};
//word List Node Structure 
struct wordListNode{
	char str[30];
	struct wordListNode *next;
};
//game List Node Structure
struct gameListNode{
	char str[30];
	int isFound;
	struct gameListNode *next;
};

//function prototype 
int isThreadAvailable();
int serverSocketCreate();
int initialization();
int compareCounts(int *choiceCount, int *userInputCount);
int *getLetterDistribution(char *strInput);
int isDone();
char *displayGameList(struct gameListNode *root);
char *acceptInput(char *input);
char *displayWord(char *masterWordStr);
struct wordListNode *createWordList(char *word);
struct gameListNode *createGameList(char *word);
struct wordListNode *getRandomWord(struct wordListNode* root, int wordCount);
struct gameListNode *findWords(char *masterWord);
void capitalizedWordInGameList(struct gameListNode *root);
void addWordNode(char *word, struct wordListNode *root);
void *findFile (void *value);
void addGameListNode(char *word, struct gameListNode *root);
void gameLoop(int wordPositionInDictionary);
void displayList(struct wordListNode *root);
void tearDown();
void cheat();
void setAllWordsToNotFound();
void cleanupWordListNode();
void cleanupGameListNode();

//Global variable 
int BUFFER_SIZE = 1024;
int BACKLOG = 8;
struct myThread thread[8];
const char *PORT_NUMBER = "8000";
char PATH[100];
char *serverFullMsg = "Sorry, Web Server is Full!";
char *wordBuffer = NULL;
char *masterWordHolder = NULL;
char fileName[40];
//Create Root for Word List Node
struct wordListNode *wordRoot = NULL;
//Create Root for Game List Node
struct gameListNode *gameRoot = NULL;
//Pointer points to Master Word
struct wordListNode *masterWord = NULL;
//Prevent Program get into infinte loop
int findBugHelper = 0;

//main
int main (int argc, char **argv){
	//local variable 
	int serverSocket, clientSocket;
	struct sockaddr clientSocketAddress;
	socklen_t clientSocketAddressSize = sizeof(clientSocketAddress);

	//set all thread's status to available
	for (int i = 0; i < 8; i++){
		thread[i].isDone = 1;
	}
	//check if Path exist as parameter 
	if (argc < 2){
		//usage message 
		fprintf(stderr, "Usage: %s <path>\n", argv[0]);
		return 1;
	}
	//assign directory's path to PATH
	strcpy(PATH, argv[1]);
	
	//Initialize the WordGuess Game
	int wordPositionInDictionary = initialization();
	//get the master word from dictionary randomly
	masterWord = getRandomWord(wordRoot, wordPositionInDictionary);
	//find all possible word that can formed by uses the letters of master word
	findWords(masterWord->str);
	// Set the 'found' status of all words in Game List to 'not found' in preparation for the game
	setAllWordsToNotFound();

	//Server socket create, Server Setup
	serverSocket = serverSocketCreate();
	//keep receiving client connect request, and assign a new thread to each requestor 
	while (1){
		//if All words are guessed 
		if (isDone() == 1){
			//cleanUp the GameList
			cleanupGameListNode();
			//generate a new master word
			masterWord = getRandomWord(wordRoot, wordPositionInDictionary);
			//find all possible word that can formed by uses the letters of master word
			findWords(masterWord->str);
			setAllWordsToNotFound();
		}

		//accept client's connection 
		clientSocket = accept(serverSocket, (struct sockaddr *)&clientSocketAddress, &clientSocketAddressSize);
		//error check
		if (clientSocket == -1){
			printf("Client socket error\n");
		}

		//if there exist available thread to handle client
		if (isThreadAvailable() == 1){
			for (int i = 0; i < 8; i++){
				if (thread[i].isDone == 1){
					//create a new thread to handle client's request 
					pthread_create(&thread[i].id, NULL, findFile, (void *)(intptr_t)clientSocket);
					//set current thread status to busy
					thread[i].isDone = 0;
					//thread will clean up after thread is complete 
					pthread_detach(thread[i].id);
					break;
				}
			}
		}
		//send msg to client, let client know all threads are currently busy
		else{
			send(clientSocket, serverFullMsg, strlen(serverFullMsg), 0);
		}
	}
	close(serverSocket);
	return 0;
}

/*
 * Function: serverSocketCreate
 * ----------------------------
 * Initializes and creates a server socket, configures it to use IPv4 and TCP,
 * binds it to a specific port, and sets it to listen for incoming connections.
 *
 * Return:
 *      int - Returns the server socket descriptor if successful, or -1 if an error occurs.
 */
int serverSocketCreate(){
	// Local Variables 
	int serverSocket;
	struct addrinfo hint, *result, *currentPointer;

	// Initialize server socket settings
	memset(&hint, 0, sizeof(hint));
	hint.ai_family = AF_INET; 	//IPv4
	hint.ai_socktype = SOCK_STREAM; //TCP
	hint.ai_flags = AI_PASSIVE; 	//use own ip address
	
	// Get address information for the server
	if (getaddrinfo(NULL, PORT_NUMBER, &hint, &result) != 0){
		printf("Error getaddrinfo\n");
	}
	
	// Create server socket using the specified protocol
	serverSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (serverSocket == -1){
		printf("Server Socket Error\n");
	}

	// Bind the socket to the provided address
	if (bind(serverSocket, result->ai_addr, result->ai_addrlen) == -1){
		perror("Binding Error\n");
	}
	
	//free allicated memory	
	freeaddrinfo(result);

	// Set the socket to listen for incoming connections
	if (listen(serverSocket, BACKLOG) == -1){
		printf("Listen Error\n");
	}
	return serverSocket;
}

/*
 * Function: findFile
 * ------------------
 * Handles a client request to locate a file or process a word-guessing game query.
 * Runs in a new thread for each client connection, processes the request, and sends a response.
 *
 * If a file is requested, it checks if the file exists and sends it. If not, returns a 404 error.
 * For game requests, it processes user input, updates the game, and sends an updated HTML page.
 *
 * Parameters:
 *      value - void pointer to the client socket (passed as an integer)
 *
 * Return:
 *      void* - Returns NULL when the thread completes.
 *
 * Steps:
 * 1. Retrieve client socket.
 * 2. Open directory (PATH).
 * 3. Receive client request.
 * 4. Parse "GET" request.
 * 5. Handle file or game request.
 * 6. Construct and send response.
 * 7. Mark thread complete and clean up.
 */
void *findFile(void *value) {
	// Local variables
	pthread_t currentThreadId = pthread_self();
	DIR *dir;
	int clientSocket, fileExist = 0, userInputDetected = 0;
	char buffer[BUFFER_SIZE], *token, *tokSavePtr;
	char fileNotFoundMsg[100] = "HTTP/1.1 404 Not Found\r\nContent-Length: 13\r\n\r\n404 Not Found";
	char badRequestMsg[100] = "HTTP/1.0 400 Bad Request\r\nContent-Length: 15\r\n\r\n400 Bad Request";
	struct dirent *filePtr = NULL;
	struct stat fileStat;

	// Retrieve client socket from passing value
	clientSocket = (int)(intptr_t)value;
	if (clientSocket == -1) {
		printf("Client socket error\n");
		return NULL;
	}

	// Open the directory specified by PATH
	dir = opendir(PATH);
	if (dir == NULL) {
		printf("Directory open error\n");
		send(clientSocket, fileNotFoundMsg, strlen(fileNotFoundMsg), 0);
		close(clientSocket);
		return NULL;
	}

	// Receive request message from the client
	if (recv(clientSocket, buffer, BUFFER_SIZE, 0) == -1) {
		printf("Recv Error\n");
	}

	// Get the first part of the request (e.g., "GET")
	token = strtok_r(buffer, " ", &tokSavePtr);
	// Terminate the thread if the message does not start with "GET"
	if (strcmp(token, "GET") != 0 && strcmp(token, "get") != 0) {
		send(clientSocket, badRequestMsg, strlen(badRequestMsg), 0);
		close(clientSocket);
		closedir(dir);
		return NULL;
	}

	// Assign the second part of the request (file path) to token
	token = strtok_r(NULL, " ", &tokSavePtr);
	// Remove leading '/' from the file path if it exists
	if (token[0] == '/') {
		token++; // Increment pointer to skip the first character
	}

	// Split the file path and query part if there is a '?'(user enter their guess)
	char *query = strchr(token, '?');

	//during user look for specific file phase 
	if (query == NULL){
		// Loop over files in the directory to check if the requested file exists
		while ((filePtr = readdir(dir)) != NULL) {
			if (strcmp(filePtr->d_name, token) == 0) {
				//if string is empty string
				if (fileName[0] == '\0'){
					strcpy(fileName, token);
				}
				//get targer file information 
				stat(token, &fileStat);
				fileExist = 1;
				break;
			}
		}
	}

	//during user guessing phase 
	if (query != NULL) {
		*query = '\0'; // split into two parts by "?"
		query++;       // Move to the beginning of the 2nd part
		// Parse the query parameters
		char *key = strtok(query, "=");
		char *value = strtok(NULL, "=");
		//if key and value are not NULL, and key == move 
		if (key && value && strcmp(key, "move") == 0) {
			printf("Received Value: %s\n", value); // Debug
			acceptInput(value);
			userInputDetected = 1;
		}
	}

	//get formated master word and html_ized game content (both malloc)
	masterWordHolder = displayWord(masterWord->str);
	wordBuffer = displayGameList(gameRoot);
	if (masterWordHolder == NULL || wordBuffer == NULL){
		printf("error check\n");
		close(clientSocket);
		closedir(dir);
		return NULL;
	}

	// Allocate memory for `html_buffer` with proper size (malloc)
    	size_t required_size = BUFFER_SIZE + strlen(masterWordHolder) + strlen(wordBuffer) + 201;
	char *html_buffer = (char *)malloc(required_size);
	if (html_buffer == NULL) {
		fprintf(stderr, "Error: Memory allocation failed for html_buffer\n");
		free(wordBuffer);
		free(masterWordHolder);
		free(html_buffer);
		close(clientSocket);
		closedir(dir);
		return NULL;
	}

	// if All words are guessed, response with different web Page.
	if (isDone() == 1){
		snprintf(html_buffer, required_size,
			"<html>"
			"<head>"
			"<style>"
			"div,h1{"
			"text-align: center;"
			"}"
			"a{font-size:40px;}"
			"</style>"
			"</head>"
			"<body>"
			"<h1>Congratulations! Dumb!</h1>"
			"<div><a href=\"%s\">Another?</a></div>"
			"</body>"
			"</html>", fileName);
	}
	//hard code html page for game content
	else{
		snprintf(html_buffer, required_size,
			"<html>\n"
			"  <head>\n"
			"    <style>"
			"     .container {"
			"     display:grid;"
			"     grid-template-columns: repeat(15, 1fr);"
			"     gap:10px;}"
			"     .container p{"
			"     border: 1px solid #ccc;"
			"     padding: 10px;"
			"     text-align:center;}"
			"    </style>"
			"  </head>\n"
			"  <body>\n"
			"    <form action=\"words\" method=\"GET\">\n"
			"    <label for=\"textbox\">Guess a word: </label>"
			"    <input type=\"text\" id=\"textbox\" name=\"move\" autofocus />\n"
			"    <span style=\"color:red; display: inline-box; margin-left: 10px;\">Hit enter!</span>"
			"    </form>\n"
			"    <p>(%s)</p>\n"
			"    <p> ======================================== </p>\n"
			"    %s"
			"    <p> ======================================== </p>\n"
			"  </body>\n"
			"</html>\n",
			masterWordHolder, wordBuffer);
		//if file not exist or url is not send back by user's input 
		if (fileExist == 0 && userInputDetected  == 0) {
			// File not found, send 404 response
			send(clientSocket, fileNotFoundMsg, strlen(fileNotFoundMsg), 0);
			printf("%s not found!\n", token);
			free(wordBuffer);
			free(masterWordHolder);
			free(html_buffer);
			close(clientSocket);
			closedir(dir);
			return NULL;
		}
	} 
	// construct HTTP response header
	snprintf(buffer, sizeof(buffer), "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n");
	// Send the header and HTML_ized game content
	send(clientSocket, buffer, strlen(buffer), 0);
	send(clientSocket, html_buffer, strlen(html_buffer), 0);

	// Mark the current thread as done by setting isDone to 1
	for (int i = 0; i < 8; i++) {
		if (thread[i].id == currentThreadId) {
			thread[i].isDone = 1;
			break;
		}
	}
	// clean up 
	close(clientSocket);
	closedir(dir);
	free(wordBuffer);
	free(masterWordHolder);
	free(html_buffer);
	return NULL;
}

/*
 * Function: isThreadAvailable
 * ---------------------------
 * Checks if there is at least one available thread from a predefined array of 8 threads.
 * A thread is considered available if its 'isDone' attribute is set to 1.
 *
 * Return:
 *      int - Returns 1 if at least one thread is available, otherwise returns 0.
 */
int isThreadAvailable(){
	for (int i = 0; i < 8; i++){
		// Check if the current thread is marked as done (isDone == 1)
		if (thread[i].isDone == 1){
			return 1;
		}
	}
	return 0;
}
/*
 * tearDown - Cleans up the game list and word list, freeing memory allocated 
 *
 * Parameters:
 *  None
 *
 * Return:
 *  void - This function does not return a value.
 */
void tearDown(){
	cleanupGameListNode();
	cleanupWordListNode();
	printf("All Done\n");
}
/*
 * initialization - Initializes the word list by reading words from a file
 *                   and sets up the linked list for word manipulation.
 *
 * Parameters:
 *  None
 *
 * Return:
 *  int - The total number of words read from the file (word count).
 */
int initialization(){	
	// Generate a random seed based on the current time
	srand(time(NULL));		
	
	// Hold the word position in the dictionary, initially set to 0							
	int wordPositionInDictionary = 0;	
	// Allocate memory for a buffer to hold a single word (max length 100 characters)		
	char *buffer = (char*)malloc(sizeof(char) * 100);
	// File pointer for reading the dictionary file
	FILE *file;

	// Open the "2of12.txt" file in read mode (file contains the dictionary words)
	file = fopen("2of12.txt", "r");
	//reading each word from the file and adding it to the linked list	
	while (fgets(buffer, 100, file)){	
		if (wordRoot == NULL){
			// Remove the newline character from the word (if any)
			buffer[strcspn(buffer, "\n\r")] = '\0';
			wordRoot = createWordList(buffer);
		}
		else {
			buffer[strcspn(buffer, "\n\r")] = '\0';
			addWordNode(buffer, wordRoot);
		}
			
		// Increment the word count
		wordPositionInDictionary++;		
	}

	// Free the allocated memory for the buffer
	free(buffer);

	// Return the total number of words read from the file
	return wordPositionInDictionary;
}
/**
 * gameLoop - Manages the core game loop, where the game continues until isDone return 1 
 *
 * Parameters:
 *  None
 *
 * Return:
 *  void - This function does not return a value.
 */
void gameLoop(int wordPositionInDictionary){
	// Declare a pointer to store user input
	char *userInput;
	// Select a random word from the Word linked list to be the "master word"
	masterWord = getRandomWord(wordRoot, wordPositionInDictionary );
	// Find possible words related to the master word, and store them into new List call Game List
	findWords(masterWord->str);		
	// Set the 'found' status of all words in Game List to 'not found' in preparation for the game 
	setAllWordsToNotFound();	

	// Loop until the game is marked as done (isDone() returns 1)
	while (isDone() != 1){
		// Clear the terminal screen
		system("clear");
		// Display the master word
		displayWord(masterWord->str);
		// Display the current game list
		displayGameList(gameRoot);
		// Accept user's input (answer)
		//userInput = acceptInput();
		// Free the memory allocated for user input after use
		free(userInput);
	}
	//check all the words 
	displayGameList(gameRoot);
}
/*
 * Function: acceptInput
 * ----------------------
 * Processes user input, converts to uppercase, and checks for matches in the game list.
 *
 * Parameters:
 *      input - User input string.
 *
 * Return:
 *      char* - Processed user input.
 */
char *acceptInput(char *input){
	// Temporary pointer to traverse the game list
	struct gameListNode *temp = gameRoot;
	// Allocate memory for the user's input (maximum of 100 characters)
	//char *input = (char *)malloc(sizeof(char) * 100);
	// Prompt the user for a guess
	//printf("Enter a guess: ");
	// Read user input from stdin (up to 100 characters)
	//fgets(input, 100, stdin);			
	// Remove newline characters from the input string		
	input[strcspn(input, "\r\n")] = '\0';		
	// Convert all characters in the input to uppercase		
	for (int i = 0; i < strlen(input); i++){			
		input[i] = toupper(input[i]);				
	}
	// Print the processed user input
	printf("The User Input is: %s\n", input);
	// Check if the input is the cheat code ("110")
	if (strcmp(input, "110") == 0){
		cheat();
	}
	// Traverse the game list and mark words as found if the user's input matches any word
	while(temp){
		if (strcmp(input, temp->str) == 0){
			temp->isFound = 1;
		}
		temp = temp->next;
	}
	// Return the processed input
	return input;
}
/*
 * displayWord - Displays the letters of the master word in uppercase and sorted order.
 *
 * Parameters:
 *  char *masterWordStr - The string representing the master word.
 *
 * Return:
 *  *char - formated master word string
 */
char *displayWord(char *masterWordStr){
	// Print a separator
//	printf("\n-----------------------------------------------------------------------------------\n");

	// Get the length of the master word string
    	int length = strlen(masterWordStr);
    
    	// Create a char array to store the letters of the word
    	char *word = (char *)malloc(sizeof(char) * length);
	memset(word, 0, sizeof(char) * (length + 1));
	char letter;

	// Loop through each character of the word and process it
	for (int i = 0; i < length; i++){
		// Store the current character in the letter variable
		letter = masterWordStr[i];

		// Only process alphabetic characters
		if (isalpha(letter)){ 		
			// Convert the letter to uppercase and store it in the word array
			word[i] = toupper(letter);	
		}
	}

	// Sort the array of letters
	/*
	 * Compare each letter to every letter with a greater index.
	 * If the current letter is smaller, swap their positions.
	 */
	for (int i = 0; i < length; i++){
		for (int j = 1; i + j < length; j++){
			if (word[i] <= word[i + j]){
				// Swap the letters
				letter = word[i];
				word[i] = word[i + j];
				word[i + j] = letter;
			}
		}
	}

	// Print the sorted list of letters
	for (int i = 0; i < length; i++){
		printf("%c\t", word[i]);
	}

	// Print a separator
//	printf("\n-----------------------------------------------------------------------------------\n");
	return word;
}
/*
 * isDone - Checks if all the words in the game list have been found.
 *
 * Parameters:
 *  None
 *
 * Return:
 *  int - Returns 1 if all words have been found, otherwise returns 0 to continue the game.
 */
int isDone(){	
	// Temporary pointer to traverse the game list
	struct gameListNode *temp = gameRoot;

	// Variable to track if any word in the game list is not found
	int switchHolder = 0;

	// Traverse through the game list nodes
	while(temp){
		// If any word is not found, set switchHolder to 1
		if (temp->isFound == 0){
			switchHolder = 1;
		}
		temp = temp->next;
	}

	// If switchHolder is 1, it means not all words are found, so return 0
	if (switchHolder == 1){
		return 0;
	}

	// If all words are found, return 1 to indicate the game is done
	return 1;
}
/*
 * getLetterDistribution - Calculates the frequency of each letter in the input string.
 *
 * Parameters:
 *  char *strInput - The input string for which to count letter occurrences.
 *
 * Return:
 *  int* - An array of 26 integers representing the count of each letter (A-Z) in the input string.
 */
int *getLetterDistribution(char *strInput){
	// Allocate memory for an array of 26 integers (one for each letter of the alphabet)
	int *letterCounter = (int *)malloc(sizeof(int) * 26);

	//init the array to 0
	memset(letterCounter, 0, sizeof(int) * 26);

	// Loop over the input string to count the occurrences of each letter
	for (int i = 0; i < strlen(strInput); i++){				
		// If the character is a lowercase letter (a-z)
		if (strInput[i] >= 'a' && strInput[i] <= 'z'){		
			// Increment the corresponding array index for this letter
			letterCounter[(int)strInput[i] - 97] += 1;		
		}
		// If the character is an uppercase letter (A-Z)
		else if (strInput[i] >= 'A' && strInput[i] <= 'Z'){	
			// Increment the corresponding array index for this letter
			letterCounter[(int)strInput[i] - 65] += 1;		
		}
	}

	// Return the array containing letter counts
	return letterCounter;
}
/**
 * compareCounts - Compares the letter counts of the master word and the words in the dictionary.
 *
 * Parameters:
 *  int *choiceCount - Array representing the letter counts of the master word.
 *  int *userInputCount - Array representing the letter counts of the dictionary word.
 *
 * Return:
 *  int - Returns 1 (true) if the word in the dictionary can be formed from the master word letters, otherwise returns 0 (false).
 */
int compareCounts(int *choiceCount, int *userInputCount){
	// Loop through the letter counts (for each letter A-Z)
	for (int i = 0; i < 26; i++){
		// If the master word has fewer occurrences of a letter than the user's input
		if (choiceCount[i] < userInputCount[i]){			
			// Return 0 (false) as the input cannot be made from the master word
			return 0;						
		}
	}
	// If all conditions are met, return 1 (true)
	return 1;
}
/*
 * wordListCreate - Creates the root node for the word linked list.
 *
 * Parameters:
 *  char *word - The word to store in the linked list node.
 *
 * Return:
 *  struct wordListNode* - Returns a pointer to the newly created word list node.
 */
struct wordListNode *createWordList(char *word){
	// Dynamically allocate memory for a new word list node
	struct wordListNode *root = (struct wordListNode*)malloc(sizeof(struct wordListNode));		

	// Copy the provided word into the node's 'str' field
	strcpy(root->str, word);								

	// Set the 'next' pointer to NULL, as this is the root node
	root->next = NULL;										

	// Return the newly created node
	return root;
}
/*
 * addWordNode - Adds a new word node to the linked list after the last node.
 *
 * Parameters:
 *  char *word - The word to add to the new node.
 *  struct wordListNode *root - The root node of the linked list.
 *
 * Return:
 *  void - This function does not return a value.
 */
void addWordNode(char *word, struct wordListNode *root){
	// Check if the root node exists
	if (root == NULL){										
		printf("Error: root does not exist\n\n");
		return;
	}

	// Create a new node to attach to the end of the list
	struct wordListNode *newNode = createWordList(word);			

	// Check if the new node was successfully created
	if (newNode == NULL){
		printf("newNode Created fail");
	}

	// Traverse the list to find the last node
	while (root->next != NULL){								
		root = root->next;
	}

	// Attach the new node to the end of the list
	root->next = newNode;									
}
/*
 * displayWordList - Displays all the words in the word linked list.
 *
 * Parameters:
 *  struct wordListNode *root - The root node of the word list.
 *
 * Return:
 *  void - This function does not return a value.
 */
void displayWordList(struct wordListNode *root){
    // Temporary pointer to traverse the list
    struct wordListNode *temp = root;
    
    // Loop through each node in the linked list and print the word
    while(temp != NULL){
        printf("%s\n", temp->str);
        temp = temp->next;
    }
}
/**
 * getRandomWord - Selects a random word from the linked list of words.
 *
 * Parameters:
 *  struct wordListNode *root - The root node of the word list.
 *  int totalWordCount - The total number of words in the dictionary.
 *
 * Return:
 *  struct wordListNode* - Returns a pointer to the randomly selected word node.
 */
struct wordListNode *getRandomWord(struct wordListNode* root, int totalWordCount){
	// Check if the function has failed too many times to find a suitable word
	if (findBugHelper > 10){
		printf("There may not be a word larger than 6\n");
		exit(EXIT_FAILURE);
	}

	// Seed the random number generator and generate a random number that are not exceeded total number of words in dictionary 
	srand(time(NULL));
	int randomNum = (rand() % totalWordCount) + 1;

	// Initialize a counter to track word positions in the list
	int counter = 1;									

	// Temporary pointer to traverse the list
	struct wordListNode *temp = root;			

	// Holder for ensuring we don't get a word that is too short
	struct wordListNode *holder = temp;					

	// Traverse the list to reach the randomly selected word position
	while (counter != randomNum){							
		temp = temp->next;
		counter++;										
	}

	// Search for a word long enough (greater than 6 characters)
	while (temp != NULL){
		// If the word is too short, continue to the next node
		if (strlen(temp->str) <= 6){
			printf("%s\n", temp->str);
			temp = temp->next;
			counter++;
		}
		else{
			// Print information about the random number and the current counter for Testing 
			//printf("Random Number: %d\n", randomNum);
			//printf("counter Number: %d\n", counter);

			// Remove any carriage returns or line feeds from the word
			temp->str[strcspn(temp->str, "\r\n")] = '\0';				
			return temp;
		}

		// Handle the case where we reach the end of the dictionary without finding a suitable word
		if (counter == totalWordCount && strlen(temp->str) <= 6){
			findBugHelper++;
			temp = wordRoot;
			temp = getRandomWord(temp, totalWordCount);	// Recursively call this function again
			break;
		}	       
	}

	// Reset findBugHelper if the function successfully finds a suitable word
	findBugHelper = 0;

	// Strip off carriage returns and line feeds from the selected word
	temp->str[strcspn(temp->str, "\r\n")] = '\0';				

	// Return the selected word
	return temp;
}
/**
 * createGameList - Creates a root node for the game list.
 *
 * Parameters:
 *  char *word - The word to store in the game list root node.
 *
 * Return:
 *  struct gameListNode* - Returns a pointer to the newly created game list root node.
 */
struct gameListNode *createGameList(char *word){			
	// Remove the newline character from the word
	word[strcspn(word, "\n")] = '\0';

	// Dynamically allocate memory for the new game list node
	struct gameListNode *root = (struct gameListNode*)malloc(sizeof(struct gameListNode));

	// Copy the word into the node's string field
	strcpy(root->str, word);

	// Set the next pointer to NULL since it's the root node
	root->next = NULL;

	// Return the newly created root node
	return root;
}
/**
 * addGameListNode - Adds a new node to the game list after the last node.
 *
 * Parameters:
 *  char *word - The word to store in the new game list node.
 *  struct gameListNode *root - The root node of the game list.
 *
 * Return:
 *  void - This function does not return a value.
 */
void addGameListNode(char *word, struct gameListNode *root){
	// Remove the newline character from the word
	word[strcspn(word, "\n")] = '\0';

	// Check if the root node exists
	if (root == NULL){
		printf("Error: root does not exist\n\n");
		return;
	}

	// Create a new game list node to add to the list
	struct gameListNode *newNode = createGameList(word);

	// Check if the new node was successfully created
	if (newNode == NULL){
		printf("Game newNode Created fail");
	}

	// Traverse the list to find the last node
	while (root->next != NULL){
		root = root->next;
	}

	// Attach the new node to the end of the list
	root->next = newNode;
}
/**
 * findWords - Finds and creates a game list of words from the dictionary that can be formed using the letters of the master word.
 *
 * Parameters:
 *  char *masterWord - The master word whose letters are used to form other words from the dictionary.
 *
 * Return:
 *  struct gameListNode* - Returns the root of the game list, which contains words formed from the master word.
 */
struct gameListNode *findWords(char *masterWord){
	// Temporary pointer to traverse the word list (dictionary)
	struct wordListNode *temp = wordRoot;

	// Check if the word root (dictionary) exists
	if (temp == NULL){
		printf("wordRoot error\n\n");
		return NULL;
	}

	// Integer pointers to hold the letter distributions for the master word and the current dictionary word
	int *masterWordArray, *dictionaryWordArray;

	// Get the letter distribution for the master word
	masterWordArray = getLetterDistribution(masterWord);

	// Loop over each word in the dictionary linked list
	while (temp){
		// Get the letter distribution for the current dictionary word
		dictionaryWordArray = getLetterDistribution(temp->str);

		// Check if the current dictionary word can be formed using the letters of the master word
		if (compareCounts(masterWordArray, dictionaryWordArray) == 1){
			// If the game list root doesn't exist, create the first node with the current dictionary word
			if (gameRoot == NULL){
				gameRoot = createGameList(temp->str);
			}
			// Otherwise, add the current dictionary word to the end of the game list
			else{
				addGameListNode(temp->str, gameRoot);
			}
		}

		// Move to the next word in the dictionary
		temp = temp->next;

		// Free the memory allocated for the dictionary word's letter distribution
		free(dictionaryWordArray);
	}

	// Free the memory allocated for the master word's letter distribution
	free(masterWordArray);

	// Return the root of the game list containing words formed from the master word
	return gameRoot;
}
/*
 * Function: displayGameList
 * --------------------------
 * Displays words in the game list, showing dashes for unfound words and printing found words.
 *
 * Parameters:
 *      root - Root node of the game list.
 *
 * Return:
 *      char* - HTML representation of the game list.
 */
char *displayGameList(struct gameListNode *root){
	char gameContent[BUFFER_SIZE * 10];
	memset(gameContent, 0, sizeof(gameContent));

	// Variable to hold the length of each word
	int wordLength;

	// Capitalize the words stored in the game list
	capitalizedWordInGameList(root);

	// Check if the root of the game list exists
	if (root == NULL){
		printf("Game List is Empty\n\n");
	}

	// Temporary pointer to traverse the game list
	struct gameListNode *temp = gameRoot;

	strcat(gameContent, "<div class=\"container\">");
	// Traverse the game list and display each word
	while (temp){
		// If the word has not been found, print dashes in place of the letters
		if (temp->isFound == 0){
			strcat(gameContent, "<p>");
			wordLength = strlen(temp->str);		
			for (int i = 0; i < wordLength; i++){
				strcat(gameContent, "_ ");	
				//printf("-  ");
			}
			strcat(gameContent, "</p>\n");
			//printf("\n");
			temp = temp->next;
		}
		// If the word has been found, print the word (html_ized game content)
		else{
			strcat(gameContent, "<p>");
			strcat(gameContent, "Found:");
			strcat(gameContent, temp->str);
			strcat(gameContent, "</p>\n");
			//printf("FOUND: %s\n", temp->str);
			temp = temp->next;
		}
	}
	strcat(gameContent, "</div>");

	char *buffer = (char *)malloc(sizeof(char) * (strlen(gameContent) + 1));
	memset(buffer, 0, sizeof(char) * (strlen(gameContent) + 1));
	strcpy(buffer, gameContent);
	return buffer;
}
/*
 * capitalizedWordInGameList - Converts all words in the game list to uppercase.
 *
 * Parameters:
 *  struct gameListNode *root - The root node of the game list.
 *
 * Return:
 *  void - This function does not return a value.
 */
void capitalizedWordInGameList(struct gameListNode *root){
	// Traverse each node in the game list
	while (root){
		// Convert each character of the word to uppercase
		for (int i = 0; i < strlen(root->str); i++){
			(root->str)[i] = toupper((root->str)[i]);

			// Strip off carriage returns and line feeds from the word
			root->str[strcspn(root->str, "\r\n")] = '\0';				
		}

		// Move to the next node in the list
		root = root->next;
	}
}
/*
 * cheat - Marks all words in the game list as found.
 *
 * Parameters:
 *  None
 *
 * Return:
 *  void - This function does not return a value.
 */
void cheat(){
	// Temporary pointer to traverse the game list
	struct gameListNode *temp = gameRoot;

	// Mark all words as found
	while (temp){
		temp->isFound = 1;
		temp = temp->next;
	}
}
/**
 * setAllWordsToNotFound - Marks all words in the game list as not found.
 *
 * Parameters:
 *  None
 *
 * Return:
 *  void - This function does not return a value.
 */
void setAllWordsToNotFound(){
	// Temporary pointer to traverse the game list
	struct gameListNode *temp = gameRoot;

	// Mark all words as not found
	while (temp){
		temp->isFound = 0;
		temp = temp->next;
	}
}
/*
 * cleanupGameListNode - Frees all nodes in the game list, releasing memory.
 *
 * Parameters:
 *  None
 *
 * Return:
 *  void - This function does not return a value.
 */
void cleanupGameListNode(){
	//error check
	if (gameRoot == NULL){
		return;
	}
	// Temporary pointer to free each node in the game list
	struct gameListNode *temp = NULL;

	// Loop through and free each node
	while (gameRoot){
		temp = gameRoot;
		gameRoot = gameRoot->next;
		free(temp);
	}
}
/*
 * cleanupWordListNode - Frees all nodes in the word list, releasing memory.
 *
 * Parameters:
 *  None
 *
 * Return:
 *  void - This function does not return a value.
 */
void cleanupWordListNode(){
	//error check
	if (wordRoot == NULL){
		return;
	}
    	// Temporary pointer to free each node in the word list
    	struct wordListNode *temp;
    
    	// Loop through and free each node
    	while (wordRoot){
        	temp = wordRoot;
        	wordRoot = wordRoot->next;
        	free(temp);
    	}
}
