#ifndef snake_structures.h
#define snake_structures.h

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

/* Snake Structure Definition */
	
typedef struct node {
    int x;
    int y;
    struct node *next;
} Node;


typedef struct snake {
    Node *head;
} Snake;

#endif