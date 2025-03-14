#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc,char* argv){
    char* content_length;
    char* queryString;
    content_length = getenv("CONTENT_LENGTH");
    queryString = getenv("QUERY_STRING");

    int cont_len = atoi(content_length);

    char* body = malloc(cont_len+1);

    read(0,body,cont_len);


    // Response body:
    char* response = malloc(cont_len + strlen(queryString) + 65);
    sprintf(response,"Hello CS324\nQuery string: %s\nRequest body: %s",queryString,body);

    // Send Headers
    printf("Content-Type: text/plain\r\n");
    printf("Content-Length: %ld\r\n",strlen(response));
    printf("\r\n");

    printf("%s",response);
}