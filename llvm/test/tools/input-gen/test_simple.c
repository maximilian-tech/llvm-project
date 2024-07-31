
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

// Function pointer to the original malloc
void *(*original_malloc)(size_t size);

// Annotated malloc function
void *my_annotated_malloc(size_t size) {
    printf("Calling annotated malloc\n");
    return original_malloc(size);
}

// Constructor function to initialize and replace malloc
__attribute__((constructor)) void init() {
    original_malloc = (void *(*)(size_t))dlsym(RTLD_NEXT, "malloc");
    if (original_malloc == NULL) {
        fprintf(stderr, "Error in dlsym: %s\n", dlerror());
        exit(1);
    }
}

// Override malloc with the annotated version
//#define malloc(size) my_annotated_malloc(size)

#include <time.h>

__attribute__((noinline)) void add(int* A, int* B, int* C, const int size)
{
    //for (int i = 0; i < size; ++i)
    for (int i = size -1 ; i >= 0 ; --i)
    {
        C[i] = A[i] + B[i];
    }
}

int main(int argc, char* argv[])
{
    const int size = 10;

    int* A = (int*) malloc(size*sizeof(int));
    int* B = (int*) malloc(size*sizeof(int));
    int* C = (int*) malloc(size*sizeof(int));

    srand(0);   // Initialization, should only be called once.

    for (int i = 0; i < size; ++i)
    {
        A[i] = rand();
        //printf("%i\n",A[i]);
        B[i] = rand()/100;
    }

    add(A,B,C,size);

    printf("%i\n", C[size-1]);

    free(A);
    free(B);
    free(C);
}
