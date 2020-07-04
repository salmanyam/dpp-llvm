
typedef struct inner {
    char *buffer1;
    char *buffer2;
} inner_t;

typedef struct outer {
    int number;
    inner_t *ptr_to_inner;
    inner_t nested_inner;
} outer_t;

void use_untyped(char *);
void use_outer(outer_t *);
void use_inner(inner_t *);

int bad(void) {
    outer_t A;

    // Take ponter and cast to char * (okay)
    char *untyped_mem = (char *) &A;

    // Pass the char* (okay, but no idea what to expect)
    use_untyped(untyped_mem);

    // Pass into func by casting to outer_t (okay)
    use_outer((outer_t *) untyped_mem);

    // Pass into func by casting to inner_t (bad!)
    use_inner((inner_t *) untyped_mem);

    return 0;
}
