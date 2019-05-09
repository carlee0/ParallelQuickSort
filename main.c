#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>

int load_input(int** l, char *filename);
void write_output(int n, int* l, char* filename);
void swap(int* x, int* y);
int partition (int arr[], int lo, int hi);
void quicksort(int *arr, int lo, int hi);
int parti (int arr[], int n, int pvt);
int median (int arr[], int n);
int cmpfunc (const void * a, const void * b);
int * merge(int *v1, int n1, int *v2, int n2);
void print_arr(int* arr, int n);
int compare_arr(int *a, int *b, int n);

int main(int argc, char *argv[]) {

  if (argc != 4) {
    printf("Usage: ./quicksort <inputfilename> <outputfilename> <option> \n");
  } 

  /* Retrieve the arguments */
  char *inputfile = argv[1];
  char *outputfile = argv[2];
  int opt = atoi(argv[3]);       /* choice of pivot strateg */

  /* global variables */
  int rank, size;
  int i, k, n;                  /* k - dimension, n - length  */
  int d;                        /* dimension of the hypercube */
  int chunk;                    /* Amount of work each processor will do */
  int* arr;
  int pvt;                     
  int color;                   
  double starttime, t;
  int* arr2;                    /* copy of the array for later comparison */

  /* Initialize MPI */
  MPI_Init(&argc, &argv);       
  starttime = MPI_Wtime();

  /* Global IDs */
  MPI_Comm_size(MPI_COMM_WORLD, &size);     
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);    
  MPI_Request request;
  MPI_Status  status;

  /* lower-level communicator */
  MPI_Comm n_comm;                       
  int sub_rank, sub_size;                  /* IDs for the next level comm */

  d = log2(size);                           
  t = MPI_Wtime() - starttime;

  if (rank == 0) {
    /* Loading the input file into l */
    n = load_input(&arr, inputfile);      /* return length of the arr to be sorted */
    arr2 = malloc(sizeof(int)*n);
    for (i=0; i<n; i++) {
      arr2[i] = arr[i];
    }
    quicksort(arr2, 0, n-1);

    /* Print the initial states */
    printf("Hypercube dimension: %d\n", d);
    printf("Number of elements to sort: %d\n----------------------------------------------------------\n", n);
    //print_arr(arr, n);
  }

  /* Telling the global slaves how large (n) the array is */
  MPI_Bcast(&n, 1, MPI_INT, 0, MPI_COMM_WORLD);
  /* ----------------------------------------------------------------- */
  MPI_Barrier(MPI_COMM_WORLD);

  /* Send data to local arrays and reorder */
  chunk  = n/size; 
  int *local_arr = malloc(sizeof(int)*chunk);
  MPI_Scatter(arr, chunk, MPI_INT, local_arr, chunk, MPI_INT, 0, MPI_COMM_WORLD);
  qsort(local_arr, chunk, sizeof(int), cmpfunc);
  //printf("Rank %d received and sorted: ", rank);
  //print_arr(local_arr, chunk);
  
  /* Bunch of local variables */
  int pos;            /* position of pvt, also length of left arr */ 
  int right;          /* length of right arr */
  int *lo; //= (int*) malloc(sizeof(int)*1);            /* left arr */
  int *hi; //= (int*) malloc(sizeof(int)*1);            /* right arr */
  int partner;        /* ID of the partner */
  int package_size;   /* self explanatory */
  int* tmp;           /* tmp arr to store the package */
  
  /* initial comm split, identical to COMM_WORLD */
  color = rank/size;
  MPI_Comm_split(MPI_COMM_WORLD, color, sub_rank, &n_comm);
  MPI_Comm_rank(n_comm, &sub_rank);
  MPI_Comm_size(n_comm, &sub_size);


  /* looping through the hypercube dimensions */
  for (k=d-1; k>=0; k--) {
    //if (rank==0) printf("The dimension is %d ------------------#### \n", k);
    /* ID of the partner in crime in the comm */
    partner = sub_rank ^ ((int) pow(2,k));

    //printf("Rank: %d/%d \t SUB Rank: %d/%d \t SUB Color: %d\n",
    //	rank, size, sub_rank, sub_size, color);
    /* Sub master determine the pivot */
    if (sub_rank == 0)
      pvt = median(local_arr, chunk);

    /* Broadcast to sub slaves */
    MPI_Bcast(&pvt, 1, MPI_INT, 0, n_comm);
    //printf("Pivot in %d is %d\n", sub_rank, pvt);

    /* ----------------------------------------------------------------- */
    MPI_Barrier(n_comm);
    
    /* Splitting local arr */
    pos = parti(local_arr, chunk, pvt);
    right = chunk - pos;
    //lo = realloc(lo, pos  );
    //hi = realloc(hi, right);
    lo = malloc(sizeof(int) * pos);
    hi = malloc(sizeof(int) * right);
    for (i=0; i<pos  ; i++) lo[i] = local_arr[i];
    for (i=0; i<right; i++) hi[i] = local_arr[pos+i];

    /* Sending the corrrect part */
    if (sub_rank < partner) {
        MPI_Isend(hi, right, MPI_INT, partner, 0, n_comm, &request);
    } else {
        MPI_Isend(lo, pos, MPI_INT, partner, 0, n_comm, &request);
    }

    /* Probing and receiving from partner in crime. */
    MPI_Probe(partner, 0, n_comm, &status);  
    MPI_Get_count(&status, MPI_INT, &package_size);
    MPI_Barrier(n_comm);
    tmp = malloc(sizeof(int)*package_size);
    MPI_Irecv(tmp, package_size, MPI_INT, partner, 0, n_comm, &request);

    /* ----------------------------------------------------------------- */
    MPI_Barrier(n_comm);

    //printf("Rank %d received the package: ", sub_rank);
    //print_arr(tmp, package_size);

    /* merge package and the rest of the arr to keep */
    if (sub_rank < partner) {
      chunk = pos + package_size;
      local_arr = merge(lo, pos, tmp, package_size);
    } else {
      chunk = right + package_size; 
      local_arr = merge(hi, right, tmp, package_size);
    }
    //printf("Global rank %d, New local array size %d and they are: ----------------- ", rank, chunk);
    //print_arr(local_arr, chunk);

    MPI_Barrier(n_comm);

    /* Splitting the comm for the next iternation */
    color = sub_rank / (pow(2, k));
    MPI_Comm_split(n_comm, color, sub_rank, &n_comm);
    MPI_Comm_rank(n_comm, &sub_rank);
    MPI_Comm_size(n_comm, &sub_size);

  }

  /* Gather the results */
  int rev_counts[size];
  int rev_displs[size]; rev_displs[0] = 0;
  MPI_Barrier(MPI_COMM_WORLD); 
  MPI_Gather(&chunk, 1, MPI_INT, rev_counts, 1, MPI_INT, 0, MPI_COMM_WORLD);
  int acc = 0;
  for (i=0; i<size-1; i++) {
      acc += rev_counts[i];
      rev_displs[i+1] = acc; 
  }
	
  //if (rank==0) {
  //  printf("The receive count array: ------------------- ");
  //  print_arr(rev_counts, size);
  //  printf("The receive displ arrya: ------------------- ");
  //  print_arr(rev_displs, size); 
  //}

  MPI_Gatherv(local_arr, chunk, MPI_INT, arr, rev_counts, rev_displs, MPI_INT, 0, MPI_COMM_WORLD);
 
  /* ----------------------------------------------------------------- */
  MPI_Barrier(MPI_COMM_WORLD);  

  /* Inspect the output */
  if (rank==0) {
    //printf("\n");
    //printf("The moment of the fucking truth: --------------------------- \n");
    //print_arr(arr, n);
    //print_arr(arr2, n);
    //printf("------------------------------------------------------------ \n ");
    int comp = compare_arr(arr, arr2, n);
    printf((comp==1)? "******** The result is correct *********\n":"NOOO!!!\n");
    
    /* Write to output */ 
    write_output(n, arr, outputfile);
  }


  //   /* Free the arr */
  // if (rank==0) {
  //   //free(arr);
  // }
  // free(local_arr);





  MPI_Comm_free(&n_comm);
  MPI_Finalize(); /* Shut down and clean up MPI */
  

  return 0;
}

/* Load array */
int load_input(int** l, char *filename) {
  FILE *fp = fopen(filename, "rb");
  int n;
  if (!fp) {
    printf("load_data error: failed to open input file '%s'.\n", filename);
    return -1;
  }
  fread(&n, sizeof(int), 1, fp);
  *l = malloc(sizeof(int) * n);
  fread(*l, sizeof(int), n, fp);
  fclose(fp);
  return n;
}

void write_output(int n, int* l, char* filename) {
  FILE *fp = fopen(filename, "wb");
  fwrite(l, sizeof(int), n, fp);
  fclose(fp);
}


/* Swapping two numbers */
void swap(int* x, int* y) {
  int tmp = *x;
  *x = *y;
  *y = tmp;
}

/* Partition the arr such that the elements smaller than pivot will be on the left
 * the large ones on the right of the pivot
 * returns the position of the pivot
 */
int partition (int arr[], int lo, int hi) {
  int pivot = arr[hi];  // choose the last element to be the pivot
  int i = (lo-1);       // Index of the smaller element
  int j;

  for (j=lo; j<hi; j++) {
    if (arr[j] <= pivot) {
      i++;
      swap(&arr[i], &arr[j]);
    }
  }
  swap(&arr[i+1], &arr[hi]);  // swapping pivot and high
  return (i+1);
}


/* Quick sort with divide and conquer */
void quicksort(int *arr, int lo, int hi) {
  if (lo < hi)
  {
    int pivot = partition(arr, lo, hi);
    quicksort(arr, lo, pivot-1);
    quicksort(arr, pivot+1, hi);
  }
}


/* Return the index of the first value greater or euqal than the pivot 
 * Or n if all elements are less than the pivot
 * arr is sorted to begin with
 */
int parti (int arr[], int n, int pvt) {
  int i;
  if (arr == NULL) return n;
  // n - the length of arr
  for (i=0; i<n; i++) {
    if (arr[i]>=pvt) return i;
  }
  return n;    // all elements are less than the pivot
}


/* Return the median of a sorted arr of length n */
int median (int arr[], int n) {
  if (n==0) return 0;  /* set zero as the pivot if encounters an empty set */
  if (n%2==0) {
    return (arr[n/2-1] + arr[n/2]) / 2;
  } else {
    return arr[n/2];
  }
}


/* Utility function for qsort() */
int cmpfunc (const void * a, const void * b) {
   return ( *(int*)a - *(int*)b );
}

int * merge(int *v1, int n1, int *v2, int n2)
{
	int i,j,k;
	int * result;

	result = (int *)malloc((n1+n2)*sizeof(int));
  
  // catch the empty arrays
  if (n1==0) return v2;
  if (n2==0) return v1;

	i=0; j=0; k=0;
	while(i<n1 && j<n2)
		if(v1[i]<v2[j])
		{
			result[k] = v1[i];
			i++; k++;
		}
		else
		{
			result[k] = v2[j];
			j++; k++;
		}
	if(i==n1)
		while(j<n2)
		{
			result[k] = v2[j];
			j++; k++;
		}
	else
		while(i<n1)
		{
			result[k] = v1[i];
			i++; k++;
		}
	return result;
}

/* utility function printing out arr */
void print_arr(int* arr, int n) {
  int i;
  for (i=0; i<n; i++) {
    printf("%d ", arr[i]);
  }
  printf("\n");
}


/* utility function comparing two arrays */
int compare_arr(int *a, int *b, int n) {
   int i;
   for (i=0; i<n; i++) {
      if (a[i] != b[i]) return 0;
   }
   return 1;
}
