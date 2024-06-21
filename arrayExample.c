#include <assert.h>
#include <libpmem.h>
#include <libpmemobj.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define LAYOUT_NAME "geopat_binary_tree"
// #define POOL_PATH   "/mnt/pmem0/geopat/MYPOOL2"
#define POOL_PATH "/mnt/pmem0/myrontsa/array_example_pool"
#define MAX_ITEMS 1000000
#define MAX_THREADS 1
#define ELEMENTS MAX_ITEMS / MAX_THREADS
#define TIME_FILE "time_output.txt"

POBJ_LAYOUT_BEGIN(array);
POBJ_LAYOUT_ROOT(array, struct root);
POBJ_LAYOUT_TOID(array, struct array);
POBJ_LAYOUT_TOID(array, struct data);
POBJ_LAYOUT_END(array);

struct data {
  int key;
};

struct array {
  int capacity;
  int size;
  TOID(struct data) data;
};

struct root {
  TOID(struct array) root;
};

struct thread_data {
  TOID(struct array) * root_ptr;
  int id;
  int max_elements;
};

int is_pmem() {
  //   const char *tmpPath = POOL_PATH + "tmp_pool_tmp";
  char tmpPath[512] = POOL_PATH;
  strncat(tmpPath, "_tmp_pool_tmp", sizeof("_tmp_pool_tmp"));
  size_t mappedLen;
  int isPmem;
  void *addr =
      pmem_map_file(tmpPath, 1, PMEM_FILE_CREATE, 0666, &mappedLen, &isPmem);
  assert(addr);
  int error = pmem_unmap(addr, mappedLen);
  assert(!error);
  char rmBuf[512] = "rm ";
  strncat(rmBuf, tmpPath, sizeof(tmpPath));
  error = system(rmBuf);
  assert(!error);
  return isPmem;
}

// Function to insert a node into the array
void insert_element(PMEMobjpool *pop, TOID(struct array) * root_ptr, int key) {
  /*Here it does not work. WHY????????????????????????????????? ZALLOC DOESNT
   * WORK*/

  TOID(struct array) expected = TOID_NULL(struct array);

  printf("ROOT POINTER IS %p\n", D_RW(*root_ptr));

  if (TOID_IS_NULL(*root_ptr)) {  // If array is NULL, initialize it and    then
                                  // place the first element
    TOID(struct array) new_array;
    POBJ_ZNEW(pop, &new_array, struct array);
    printf("NEW_NODE = %lu\n", new_array.oid.off);
    D_RW(new_array)->capacity = MAX_ITEMS;
    D_RW(new_array)->size = 0;
    printf("New_node->%p\n", D_RW(D_RW(new_array)->data));

    pmemobj_persist(pop, &D_RW(new_array)->capacity,
                    sizeof(D_RW(new_array)->capacity));
    pmemobj_persist(pop, &D_RW(new_array)->size, sizeof(D_RW(new_array)->size));
    printf("NEW_ARRAY_DATA : SIZE = %d,      Capacity = %d \n",
           D_RW(new_array)->size, D_RW(new_array)->capacity);

    // Allocate memory for data array with a capacity of 100,000 elements
    POBJ_ZALLOC(pop, &D_RW(new_array)->data, struct data,
                D_RW(new_array)->capacity * sizeof(struct data));
    printf("START: New_node->%p\n", D_RW(D_RW(new_array)->data));
    printf("END:   New_node->%p\n",
           &D_RW(D_RW(new_array)->data)[D_RW(new_array)->capacity - 1]);
    // Persist the changes

    pmemobj_persist(pop, &D_RW(new_array)->data, sizeof(D_RW(new_array)->data));

    // Assign the new array to the root pointer and persist the root pointer
    if (!__atomic_compare_exchange(root_ptr, &expected, &new_array, 0,
                                   __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
      printf("Failed\n");
      POBJ_FREE(&new_array);
    }
    printf("Root->%p\n", D_RW(D_RW(*root_ptr)->data));
    printf("Root->%p\n",
           &D_RW(D_RW(new_array)->data)[D_RW(new_array)->capacity - 1]);

    //*root_ptr = new_array;
    pmemobj_persist(pop, root_ptr, sizeof(*root_ptr));
  }

  // Add the key to the data array at the position indicated by size
  struct array *arr = D_RW(*root_ptr);
  int pos = __atomic_add_fetch(&arr->size, 1, __ATOMIC_RELAXED);
  if (pos < arr->capacity) {
    D_RW(arr->data)[pos].key = pos;
    pmemobj_persist(pop, &D_RW(arr->data)[pos], sizeof(D_RW(arr->data)[pos]));
    // Increment the size and persist the change
    // arr->size++;
    pmemobj_persist(pop, &arr->size, sizeof(arr->size));
  } else {
    // Handle the case where the array is full (e.g., resize or error message)
    printf("Array is full %d, cannot insert key %d\n", arr->capacity, key);
  }
}

void print_array(PMEMobjpool *pop, TOID(struct array) root_ptr) {
  if (TOID_IS_NULL(root_ptr)) {
    printf("Array is empty.\n");
    return;
  }

  struct array *arr = D_RW(root_ptr);
  for (int i = 0; i < arr->size; i++) {
    printf("Key at position %d: %d\n", i, D_RW(arr->data)[i].key);
  }
}

PMEMobjpool *pop;

void *thread_function(void *arg) {
  struct thread_data *data = (struct thread_data *)arg;

  for (int i = 0; i < data->max_elements; i++) {
    insert_element(pop, data->root_ptr, i);
  }
  return NULL;
}

int main(int argc, char *argv[]) {
  printf("is_pmem: %d\n", is_pmem());
  char rmBuf[512] = "rm -f ";
  strncat(rmBuf, POOL_PATH, sizeof(POOL_PATH));
  int error = system(rmBuf);
  assert(!error);

  pop = pmemobj_create(POOL_PATH, POBJ_LAYOUT_NAME(array), PMEMOBJ_MIN_POOL,
                       0666);
  int k = 0;
  if (pop == NULL) {
    pop = pmemobj_open(POOL_PATH, POBJ_LAYOUT_NAME(array));
    k = 1;
    if (pop == NULL) {
      perror("pmemobj_open");
      return 1;
    }
  }

  TOID(struct root) root = POBJ_ROOT(pop, struct root);
  struct timespec start, end;
  double time_spent;
  clock_gettime(CLOCK_MONOTONIC, &start);
  if (k == 0) {
    /*-----------------------HERE IT
     * WORKS------------------------------------------------------------------*/
    /*
        TOID(struct array) expected = TOID_NULL(struct array);
        TOID(struct array) *root_ptr = &D_RW(root)->root;

        printf("ROOT POINTER IS %p\n", D_RW(*root_ptr));

        if (TOID_IS_NULL(*root_ptr)) {  // If array is NULL, initialize it and
                                        // then place the first element
          TOID(struct array) new_array;
          POBJ_ZNEW(pop, &new_array, struct array);
          printf("NEW_NODE = %lu\n", new_array.oid.off);
          D_RW(new_array)->capacity = MAX_ITEMS;
          D_RW(new_array)->size = 0;
          printf("New_node->%p\n", D_RW(D_RW(new_array)->data));
          // Allocate memory for data array with a capacity of 100,000 elements
          POBJ_ZALLOC(pop, &D_RW(new_array)->data, struct data,
                      D_RW(new_array)->capacity * sizeof(struct data));
          printf("START: New_node->%p\n", D_RW(D_RW(new_array)->data));
          printf("END: New_node->%p\n",
                 &D_RW(D_RW(new_array)->data)[D_RW(new_array)->capacity - 1]);
          // Persist the changes
          pmemobj_persist(pop, &D_RW(new_array)->capacity,
                          sizeof(D_RW(new_array)->capacity));
          pmemobj_persist(pop, &D_RW(new_array)->size,
                          sizeof(D_RW(new_array)->size));
          pmemobj_persist(pop, &D_RW(new_array)->data,
                          sizeof(D_RW(new_array)->data));

          // Assign the new array to the root pointer and persist the root
          //      pointer

          if (!__atomic_compare_exchange(root_ptr, &expected, &new_array, 0,
                                         __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
            printf("Failed\n");
            POBJ_FREE(&new_array);
          }
          printf("Root->%p\n", D_RW(D_RW(*root_ptr)->data));
          printf("Root->%p\n",
                 &D_RW(D_RW(new_array)->data)[D_RW(new_array)->capacity - 1]);

          //*root_ptr = new_array;
          pmemobj_persist(pop, root_ptr, sizeof(*root_ptr));
        }
    */
    pthread_t threads[MAX_THREADS];
    struct thread_data thread_args[MAX_THREADS];

    for (int i = 0; i < MAX_THREADS; i++) {
      thread_args[i].id = i;
      thread_args[i].root_ptr = &D_RW(root)->root;
      thread_args[i].max_elements = ELEMENTS;
    }

    for (int i = 0; i < MAX_THREADS; i++) {
      pthread_create(&threads[i], NULL, thread_function, &thread_args[i]);
    }

    for (int i = 0; i < MAX_THREADS; i++) {
      pthread_join(threads[i], NULL);
    }

    // for(int i = 0; i < MAX_ITEMS; i++){
    //     insert_element(pop,&D_RW(root)->root,i);
    // }

    clock_gettime(CLOCK_MONOTONIC, &end);
    time_spent =
        (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("Time taken to insert %d elements: %f seconds\n", MAX_ITEMS,
           time_spent);

    FILE *time_file = fopen(TIME_FILE, "a");
    if (time_file != NULL) {
      fprintf(time_file, "Time taken to insert %d elements: %f seconds\n",
              MAX_ITEMS, time_spent);
      fclose(time_file);
    } else {
      perror("fopen");
    }
  } else {
    print_array(pop, D_RO(root)->root);
  }
  pmemobj_close(pop);

  return 0;
}
