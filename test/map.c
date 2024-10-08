#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <clib/Map/map.h>

int main() {
    // Create an Instace of Map
    Map *m = create_map();
    m->Utils(m, __ADD_KEY, "TEST", "THIS");
    m->Utils(m, __ADD_KEY, "LUL", "ME");
    m->Utils(m, __ADD_KEY, "BEEP", "BOOP");
    m->Utils(m, __ADD_KEY, "ALIAS", "CHRIS");

    // Display Fields
    printf("Field(s): %ld\n", m->idx);
    for(int i = 0; i < m->idx - 1; i++)
        if(m->keys[i] != NULL)
            printf("%s %s\n", (char *)((Key *)(m->keys[i]))->name, (char *)((Key *)m->keys[i])->value);

    // Get Key
    Key *k = (Key *)m->Utils(m, __GET_KEY, "TEST");
    if(k == NULL)
        err_n_exit("Failed");

    printf("__GET_KEY: %s\n", k->value);

    // Get Key Value
    char *value = (char *)m->Utils(m, __GET_KEY_VALUE, "TEST");
    if(value == NULL)
        err_n_exit("Failed");

    printf("__GET_KEY_VALUE: %s\n", value);


    return 0;
}