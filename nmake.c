/************************************************************
Filename: nmake.c 
Creator: Austin Lachance
Email: austin.lachance@yale.edu
Program Description: recreation of the nmake program. Parses 
makefiles into targets, prerequisites, and commands (allowing)
for MACROS and additional functionality. Uses this information
to "make" and update user specified files
************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <math.h>
#include "/c/cs323/Hwk1/getLine.h"

#define DIE(msg) exit(fprintf(stderr, "%s\n", msg))

//Struct used for prerequisites and commands
typedef struct other_node { 
	char* name;						//name of prereq or command
	bool print_me;					//does this need to be output?
	struct other_node* next_node;	//link to next node in linked list
} other_node;

//Struct used for target nodes
typedef struct target_node {
	char* name;						//name of target node
	bool erase_commands;			//true if a target's commands must be reset
	bool visited;					//marks if a current node is in our path
	bool was_updated;				//marks that a node was updated
	struct target_node* next_target;//link to next node in linked list
	struct other_node* prereq;		//points to head of linked list of prereqs
	struct other_node* command;		//points to head of linked list of commands
} target_node;

/* FUNCTION PROTOTYPES *******************************************************/
bool target_check(char *line);
bool is_blank(char *line);
bool all_whitespace(char *line);
void add_target(target_node **head, char *start, char *finish);
void add_prereq(target_node **head, target_node **current_targs,
  char *start, char *finish);
void add_command(target_node **head, target_node **current_targs,
  char *start, char *finish);
char *target_macro_check(char *line, char *target);
void parse_target(char *line, target_node **head, target_node **current_targs);
bool parse_command(char *line, target_node **head, target_node **current_targs,
  bool semi);
void parse(FILE *nmake, target_node **head);
void print_prereq(other_node **prereq, bool all);
void edit_command(char *line, other_node **prereq);
void output(target_node **head);
bool time_check(char *target, char *prereq);
void remove_visited(target_node **head);
int nmake(target_node **head, char *file);
/*****************************************************************************/

int main(int argc, char *argv[]) {

	//makefile to be opened
	FILE *file = NULL;
	//signifies a -f namechange to the default "makefile"
	bool name_change = false;
	//index of the first target to be made
	int first_target = 1;

	//if there is a -f flag try to open the next arg as the makefile
	if((argc > 1) && !strcmp(argv[1], "-f")) {
		name_change = true;
		if(argc == 2) DIE("nmake: malformed -f option");
		file = fopen(argv[2], "r");
		if(!file) {
			fprintf(stderr, "nmake: unable to open %s\n", argv[2]);
		}
		first_target = 3;
	}
	//if no -f flag then try to open "makefile"
	if(!name_change) {
		file = fopen("nmakefile", "r");
		if(!file) {
			fprintf(stderr, "nmake: unable to open makefile\n");
		}
	}

	//parse the makefile into a linked list of targets, prereqs, and commands
	target_node *head = NULL;
	parse(file, &head);
	int results;

	//iterate through the remaining command line args, making each file
	for(int i = first_target; i < argc; i++) {
		results = nmake(&head, argv[i]);
		if((results == 3) || (!head)) {
			printf("nmake: \'%s\' is up to date.\n", argv[i]);
		}
		remove_visited(&head);
	}
	exit(0);
}

//Insertes "name" into the target_node linked list
void insert_target(target_node **head, char *name) {
	target_node *new_target_node;
	new_target_node = (target_node*)calloc(1, sizeof(target_node));
	new_target_node->name = name;
	new_target_node->next_target = NULL;
	new_target_node->prereq = NULL;
	new_target_node->command = NULL;
	new_target_node->erase_commands = false;
	new_target_node->visited = false;
	new_target_node->was_updated = false;
	if(!(*head)) {
		*head = new_target_node;
	}
	else {
		target_node *temp = *head;
		while(temp->next_target != NULL) {
			temp = temp->next_target;
		}
		temp->next_target = new_target_node;
	}
}

//Inserts "name" into either the prereq or command linked list
void insert_other(other_node** head, char *name) {
	other_node *new_other_node;
	new_other_node = (other_node*)calloc(1, sizeof(other_node));
	new_other_node->name = name;
	new_other_node->next_node = NULL;
	new_other_node->print_me = false;
	other_node *temp = *head;
	if(!temp) {
		*head = new_other_node;
	}
	else {
		while(temp->next_node != NULL) {
			temp = temp->next_node;
		}
		temp->next_node = new_other_node;
	}
}

//Finds a target "name" within a target_node linked list. NULL if not found
target_node* find_target(target_node **head, char *name) {
	target_node *temp = *head;
	if(!temp) {
		return NULL;
	}
	else {
		while(temp != NULL) {
			if(!strcmp(temp->name, name)) {
				return temp;
			}
			temp = temp->next_target;
		}
		return NULL;
	}
}

//Finds a node "name" within an other_node linked list. NULL if not found
other_node *find_other(other_node **node, char *name) {
	other_node *temp = *node;
	if(!temp) {
		return NULL;
	}
	else {
		while(temp != NULL) {
			if(!strcmp(name, temp->name)) {
				return temp;
			}
			temp = temp->next_node;
		}
		return NULL;
	}
}

//Frees an other_node linked list
void clear_other(other_node **head) {
	other_node *temp = *head;
	while(*head != NULL) {
		temp = *head;
		*head = (*head)->next_node;
		free(temp->name);
		free(temp);
	}
}

//Frees a target_node linked list
void clear_list(target_node **head) {
	target_node *temp = *head;
	while(*head != NULL) {
		temp = *head;
		clear_other(&((*head)->prereq));
		clear_other(&((*head)->command));
		*head = (*head)->next_target;
		free(temp->name);
		free(temp);
	}
}

//determines if "line" has the proper target: [prereqs]* format
bool target_check(char *line) {
	char *temp = line;
	bool first = true;
	bool saw_colon = false;
	while(*temp != '\n') {
		if(first) {
			if(isspace(*temp) || (*temp == ':') || (*temp == '#')) {
				return false;
			}
			first = false;
		}
		else if((*temp == '#') && !saw_colon) {
			return false;
		}
		else if(*temp == ':') {
			saw_colon = true;
		}
		temp++;
	}
	if(saw_colon) {
		return true;
	}
	else {
		return false;
	}
}

//determines if "line" is all whitespace and comments
bool is_blank(char *line) {
	char *temp = NULL;
	temp = line;
	bool saw_char = false;
	while(*temp != '\0') {
		if((*temp == '#') && !saw_char) {
			return true;
		}
		else if(!isspace(*temp)) {
			saw_char = true;
		}
		temp++;
	}
	return !saw_char;
}

//determines if "line" is exclusively whitespace
bool all_whitespace(char *line) {
	char *temp = line;
	while(*temp != '\n') {
		if(!isspace(*temp)) {
			return false;
		}
		temp++;
	}
	return true;
}

//add a substring of chars (start -> finish) as a target to the linked list
void add_target(target_node **head, char *start, char *finish) {
	
	char *new_target = (char *)calloc(finish-start+1, 1);
	strncpy(new_target, start, finish-start);
	new_target[strlen(new_target)] = '\0';
	if(!find_target(head, new_target)) {
		insert_target(head, new_target);
	}
	else {
		if(find_target(head, new_target)->command != NULL) {
			find_target(head,new_target)->erase_commands = true;
		}
		free(new_target);
	}
}

//add a substring (start -> finish) as a prereq to each of the targets in
//current_targs's linked_list
void add_prereq(target_node **head, target_node **current_targs,
 char *start, char *finish) {

	//create appropriate string from start/finish pointers
	char *new_prereq = (char *)calloc(finish-start+1, 1);
	strncpy(new_prereq, start, finish-start);
	new_prereq[strlen(new_prereq)] = '\0';
	target_node *temp = *current_targs;
	target_node *find_targ;
	bool no_add = true;
	//iterate through targets in current targs
	while(temp != NULL) {
		no_add = false;
		find_targ = find_target(head, temp->name);
		if(!find_targ) {
			DIE("TARGET WAS NOT FOUND!!!");
		}
		else {
			//if targets found in "head" list then add prereq if not present
			if(!find_other(&(find_targ->prereq), new_prereq)) {
				insert_other(&(find_targ->prereq), new_prereq);
			}
			else {
				free(new_prereq);
			}
		}
		temp = temp->next_target;
	}
	if(no_add) {
		free(new_prereq);
	}
}

//add substring (start->finish) as a command to each of hte targets in the
//current_targs linked list
void add_command(target_node **head, target_node **current_targs,
 char *start, char *finish) {

	//create appropriate command string from start and finish pointers
	char *new_command = (char*)calloc(finish-start+1, 1);
	strncpy(new_command, start, finish-start);
	new_command[strlen(new_command)] = '\0';
	target_node *temp = *current_targs;
	target_node *find_targ;
	char *macros;
	//iterate through current targets
	while(temp != NULL) {
		find_targ = find_target(head, temp->name);
		if(!find_targ) {
			DIE("TARGET WAS NOT FOUND!!!");
		}
		else {
			//Warn users if a target has commands in multiple rules
			if(find_targ->erase_commands) {
				printf("nmake: %s has two commands\n", find_targ->name);
				clear_other(&(find_targ->command));
				find_targ->erase_commands = false;
			}
			//add in $@ macro (replaces $@ with target)
			macros = target_macro_check(new_command, find_targ->name);
			insert_other(&(find_targ->command), macros);
		}
		temp = temp->next_target;
	}
	free(new_command);
}

//parses a command string replacing the $@ macro with the target of the command
char *target_macro_check(char *line, char *target) {
	char *temp = line;
	char *newline = calloc(strlen(line) +1, 1);
	char *begin;
	bool first = true;
	//iterate through string until $@ is no longer found
	while((begin = strstr(temp, "$@")) != NULL) {
		if(first) {
			newline = realloc(newline, begin - temp + strlen(target) + 1);
			strncpy(newline, temp, begin - temp);
			strcat(newline, target);
			first = false;
		}
		else {
			newline = realloc(newline, strlen(newline) + (begin - temp) + 
				strlen(target) + 1);
			strncat(newline, temp, begin - temp);
			strcat(newline, target);
		}
		temp = begin + 2;
	}
	if(first) {
		strcpy(newline, line);
	}
	else {
		newline = realloc(newline, strlen(newline) + strlen(temp) + 1);
		strcat(newline, temp);
	}
	return newline;
}

//parses a target line "line" into targets and prereqs
void parse_target(char *line, target_node **head, target_node **current_targs){
	char *temp = line;
	char *start = NULL;
	bool is_word = false;
	//loop through line adding targets (':' signifies end of targets)
	while(*temp != ':') {
		if(isspace(*temp)) {
			if(is_word) {
				//add substring (start->temp) to head and current targs lists
				add_target(head, start, temp);
				add_target(current_targs, start, temp);
				is_word = false;
			}
		}
		else if(!is_word) {
			start = temp;
			is_word = true;
		}
		temp++;
	}
	if(is_word) {
		//add substring (start->temp) to head and current targs lists
		add_target(head, start, temp);
		add_target(current_targs, start, temp);
		is_word = false;
	}
	temp++;
	is_word = false;
	//iterate until end of line (or semicolon) parsing prereqs
	while(((*temp) != '\n') && ((*temp) != ';')) {
		if(isspace(*temp)) {
			if(is_word) {
				//add substring (start->temp) as prereq to head list
				add_prereq(head, current_targs, start, temp);
				is_word = false;
			}
		}
		//comment was found. break here
		else if(*temp == '#') {
			if(is_word) {
				//add substring (start->temp) as prereq to head list
				add_prereq(head, current_targs, start, temp);
			}
			break;
		}
		else if(!is_word) {
			start = temp;
			is_word = true;
		}
		temp++;
	}
	if(is_word) {
		add_prereq(head, current_targs, start, temp);
	}
	//semicolon was found. parse the rest of the line as a command
	if(*temp == ';') {
		parse_command(temp+1, head, current_targs, true);
	}
}

//parses a command str "line" and adds commands to head & current_targs lists
//returns false if command is ill-formed (has no initial whitespace)
bool parse_command(char *line, target_node **head, target_node **current_targs,
 bool semi) {

	bool first = true;
	bool is_word = false;
	char *temp = line;
	char *start;
	while(*temp != '\n') {
		if(first && !isspace(*temp) && !semi) {
			return false;
		}
		else {
			first = false;
		}
		if(!isspace(*temp) && !is_word) {
			start = temp;
			is_word = true;
		}
		temp++;
	}
	add_command(head, current_targs, start, temp);
	return true;
}

//outline function for parsing a makefile "nmake" into targets, prereqs,
//and commands and adding those into the "head" linked list
void parse(FILE *nmake, target_node **head) {

	//if file does not exist return
	if(!nmake) {
		return;
	}
	//boolean set true when the current line might be a command
	bool command = false;
	char *line = NULL;
	//keeps track of the current targets (for adding prereqs and commands)
	target_node* current_targs = NULL;

	while((line = getLine(nmake)) != NULL) {
		//prevents error if last line ends in EOF
		if(!strchr(line, '\n')) {
			line = realloc(line, strlen(line) + 2);
			strcat(line, "\n");
		}
		//if a line is "blank" check is it might be a command otherwise
		//clear current targets
		if(is_blank(line)) {
			if(command && !all_whitespace(line) && (*line != '#')) {
				parse_command(line, head, &current_targs, false);
			}
			else {
				clear_list(&current_targs);
				command = false;
			}
		}
		//throw error if line is not a command and has improper target format
		else if(!target_check(line) && !command) {
			exit(fprintf(stderr, "nmake: malformed rule %s", line));
		}
		//parse for targets		
		else if(target_check(line) && !command) {
			clear_list(&current_targs);
			parse_target(line, head, &current_targs);
			command = true;
		}
		//check if line might be a command
		else if(command) {
			if(!all_whitespace(line)) {
				if(!parse_command(line, head, &current_targs, false)) {
					if(target_check(line)) {
						clear_list(&current_targs);
						parse_target(line, head, &current_targs);
						command = true;
					}
					else {
					   exit(fprintf(stderr, "nmake: malformed rule %s", line));
					}
				}
			}
			else {
				clear_list(&current_targs);
				command = false;
			}
		}
		free(line);
	}
	//free the current targets once parsing is done
	clear_list(&current_targs);
}

//prints the prerequistes based on all flag
//all = print all, !all = print only prereqs with "print me" flag on
void print_prereq(other_node **prereq, bool all) {

	bool first = true;
	other_node *temp = *prereq;
	if(all) {
		while(temp != NULL) {
			if(temp->next_node != NULL) {
				printf("%s ", temp->name);
			}
			else {
				printf("%s", temp->name);
			}
			temp = temp->next_node;
		}
	}
	else {
		while(temp != NULL) {
			if(temp->print_me) {
				if(first) {
					printf("%s", temp->name);
					first = false;
				}
				else {
					printf(" %s", temp->name);
				}
			}
			temp = temp->next_node;
		}
	}
}

//print a command replacing the $^ and $? MACROS with the appropriate prereqs
void edit_command(char *line, other_node **prereq) {
	char *temp = line;
	while((*temp) != '\0') {
		if((*temp) == '$') {
			if(*(temp + 1) == '^') {
				//if $^ print all prereqs
				print_prereq(prereq, true);
				temp += 1;
			}
			else if(*(temp + 1) == '?') {
				//if $? print updated prereqs (have print_me flag)
				print_prereq(prereq, false);
				temp += 1; 
			}
			else {
				printf("%c", *temp);
			}
		}
		else {
			printf("%c", *temp);
		}
		temp++;
	}
}

//loop through and print all commands in a target node "*head"
void output(target_node **head) {
	other_node *temp = (*head)->command;
	while(temp != NULL) {
		edit_command(temp->name, &((*head)->prereq));
		printf("\n");
		temp = temp->next_node;
	}
}

//compare the last modification time of two files, "target" and "prereq"
bool time_check(char *target, char *prereq) {
	struct stat targ_file;
	struct stat prereq_file;
	stat(target, &targ_file);
	stat(prereq, &prereq_file);
	//return true if prereq is newer than target
	if(difftime(targ_file.st_mtime, prereq_file.st_mtime) < 0) {
		return true;
	}
	return false;
}

//Reset the "visited" flag on all target nodes in head after making a file
void remove_visited(target_node **head) {
	target_node *temp = *head;
	while(temp != NULL) {
		temp->visited = false;
		temp = temp->next_target;
	}
}

//make the file "file" based on the make rules found in the linked_list "head"
int nmake(target_node **head, char *file) {

	//search the targets from the makefile for "file"
	target_node *targets = find_target(head, file);
	target_node *prereq_targ;
	other_node *prereq_head;
	bool update_targ = false;
	FILE *fp;
	int value;
	//if "file" was a target go here
	if(targets) {
		//if file is up to date then return 3 to signify this
		if(targets->was_updated) {
			return 3;
		}
		//if target has been visited return 1 to signify circular dependecy
		if(targets->visited) {
			return 1;
		}
		else {
			targets->visited = true;
		}
		//iterate through all of target's prereqs
		prereq_head = targets->prereq;
		while(prereq_head != NULL) {
			//recurse down into each prereq
			if((value = nmake(head, prereq_head->name)) == 1) {
				//check for circular dependency
			   fprintf(stderr, "nmake: %s depends on itself\n", targets->name);
			   targets->was_updated = true;
			}
			if((value != 1) &&(find_target(head, prereq_head->name) != NULL)) {
				find_target(head, prereq_head->name)->visited = false;
			}
			prereq_head = prereq_head->next_node;
		}
		//check if "file" exists in the directory
		fp = fopen(file, "r");
		if(!fp) {
			//if it does not exist then "make" and set all prereqs as updated
			prereq_head = targets->prereq;
			while(prereq_head != NULL) {
				prereq_head->print_me = true;
				prereq_head = prereq_head->next_node;
			}
			targets->was_updated = true;
			output(&targets);
			return 0;

		}
		else {
			//if "file" exists in directory compare last mod time with each
			//of its prereqs. If any are newer then update
			prereq_head = targets->prereq;
			while(prereq_head != NULL) {

				prereq_targ = find_target(head, prereq_head->name);

				if(!prereq_targ) {
					if(time_check(file, prereq_head->name)) {
						prereq_head->print_me = true;
						update_targ = true;
					}
				}
				else if(prereq_targ->was_updated){
					prereq_head->print_me = true;
					update_targ = true;
				}
				prereq_head = prereq_head->next_node;
			}
			if(update_targ) {
				targets->was_updated = true;
				output(&targets);
				return 0;
			}
			else {
				//return 3 to signify that file is up to date
				return 3;
			}
		}
	}
	//if "file" does not exist as a target in the makefile go here
	else {
		fp = fopen(file, "r");
		//if file does not exist then throw error
		if(!fp) {
			exit(fprintf(stderr, "nmake: no rule for %s\n",file));
		}
		return 0;
	}
}






















