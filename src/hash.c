#include "carat/symm.h"

#include "hash.h"
#include "matrix_tools.h"
#include "neighbor.h"

// TODO : We should be able to determine thes in a smarter way

#define INITIAL_THETA_PREC  25

// hash_vec is defined in the end, as different choices of hash functions might lead to extremely
// different performance times
hash_t hash_vec(const int* vec, unsigned int n);

int _add(hash_table_t table, matrix_TYP* key, hash_t val, int do_push_back);

// This should be the declaration, but for some reason short_vectors doesn't restrict the pointer to be constant
// Since we don't want to copy the matrix, we leave it at that.
// hash_t hash_form(const matrix_TYP* Q, W32 theta_prec)
hash_t hash_form(matrix_TYP* Q, W32 theta_prec)
{
  hash_t x;
  int norm;
  int* num_short;

  num_short = (int*)malloc(theta_prec * sizeof(int));
  for (norm = 2; norm <= 2*theta_prec; norm += 2) {
    short_vectors(Q, norm, norm, 0, 1, &(num_short[norm/2 - 1]));
  }

  // x = num_short[0] - num_short[1] + num_short[2] ;
  x = hash_vec(num_short, theta_prec);

  return x;
}

void hash_table_init(hash_table_t table, hash_t hash_size)
{
  hash_t i, log_hash_size, n;

  log_hash_size = 0;
  n = hash_size;
  while (n >>= 1) {
    log_hash_size++;
  }
  
  table->capacity = 1LL << log_hash_size;
  table->mask = (1LL << (log_hash_size + 1)) - 1;
  table->keys = (matrix_TYP**) malloc(table->capacity * sizeof(matrix_TYP*));
  table->vals = (hash_t*) malloc(table->capacity * sizeof(hash_t));
  table->key_ptr = (hash_t*) malloc(2*table->capacity * sizeof(int));
  table->counts = (W32*) malloc(2*table->capacity * sizeof(int));
  table->probs = (fmpq_t*) malloc(table->capacity * sizeof(fmpq_t));
  for (i = 0; i < table->capacity; i++)
    fmpq_init(table->probs[i]);
  table->num_stored = 0;
  table->theta_prec = INITIAL_THETA_PREC;
  table->red_on_isom = TRUE;

  for (i = 0; i < 2*table->capacity; i++) {
    table->key_ptr[i] = -1;
    table->counts[i] = 0;
  }
  
  return;
}

double get_isom_cost(const hash_table_t table, double* red_cost)
{
  double isom_cost;
  clock_t cputime;
  hash_t idx, offset, i;
  neighbor_manager nbr_man;
  matrix_TYP* s;
  matrix_TYP* nbr;

#define NUM_ISOMS 10

  isom_cost = 0;
  if (red_cost != NULL)
    *red_cost = 0;
  idx = 0;
  for (offset = 0; offset < table->num_stored; offset++) {
    init_nbr_process(&nbr_man, table->keys[offset], 3, idx);
    for (i = 0; i <  NUM_ISOMS; i++) {
      nbr = build_nb(&nbr_man);
      if (red_cost != NULL) {
	s = init_mat(5,5,"1");
	cputime = clock();
	greedy(nbr, s, 5, 5);
	(*red_cost) += (clock() - cputime);
	free_mat(s);
      }
      cputime = clock();
      is_isometric(table->keys[i % table->num_stored], nbr);
      isom_cost += (clock() - cputime);
      advance_nbr_process(&nbr_man);
      if (has_ended(&nbr_man)) {
	idx++;
	free_nbr_process(&nbr_man);
	init_nbr_process(&nbr_man, table->keys[offset], 3, idx);
      }
    }
    free_nbr_process(&nbr_man);
  }
  isom_cost /= (NUM_ISOMS * table->num_stored);
  if (red_cost != NULL)
    (*red_cost) /= (NUM_ISOMS * table->num_stored);

  return isom_cost;
}

double get_total_cost(const hash_table_t table, W32 theta_prec, double isom_cost, double red_isom_cost, fmpq_t* wt_cnts,
		      W32* counts, hash_t* vals, bool* red_on_isom, const fmpq_t* probs)
{
  hash_t i, offset, idx;
  double theta_cost, total_cost;
  clock_t cputime;
  
  for (i = 0; i < 2*table->capacity; i++) {
    fmpq_zero(wt_cnts[i]);
    counts[i] = 0;
  }
  
  theta_cost = 0;
  for (offset = 0; offset < table->num_stored; offset++) {
    cputime = clock();
    vals[offset] = hash_form(table->keys[offset], theta_prec);
    theta_cost += (clock() - cputime);
    idx = vals[offset] & table->mask;
    fmpq_add(wt_cnts[idx], wt_cnts[idx], probs[offset]);
    counts[vals[offset] & table->mask]++;
  }
  theta_cost /= table->num_stored;
    
  total_cost = 0;
  for (i = 0; i < 2 * table->capacity; i++) {
    total_cost += counts[i] * fmpq_get_d(wt_cnts[i]);
  }
  total_cost -= 1;
  printf("Expecting average number of %f calls to is_isometric.\n", total_cost);

  *red_on_isom = true;
  for (offset = 0; offset < table->num_stored; offset++)
    *red_on_isom &= (counts[vals[offset] & table->mask] == 1);

  *red_on_isom = !(*red_on_isom);

  if (*red_on_isom)
    total_cost *= red_isom_cost;
  else
    total_cost *= isom_cost;
  
  total_cost += theta_cost;
    
  return total_cost;
}

// This can be done better, by first computing the theta series and
// only then going over them. Since this is not a critical path,
// we postpone doing that.

// table is not const, since we're using it and clearing it in the end
void hash_table_recalibrate(hash_table_t new_table, const hash_table_t table)
{
  hash_t offset, i;
  W32 theta_prec;
  double greedy_cost, red_isom_cost, isom_cost;
  double total_cost;
  double old_cost;
  fmpq_t* wt_cnts;
  fmpq_t mass;

  fmpq_init(mass);
  fmpq_zero(mass);

  hash_table_init(new_table, table->capacity);
  
  wt_cnts = (fmpq_t*) malloc(2*table->capacity*sizeof(fmpq_t));
  for (i = 0; i < 2 * table->capacity; i++) {
    fmpq_init(wt_cnts[i]);
  }

  for (offset = 0; offset < table->num_stored; offset++) {
    fmpq_add(mass, mass, table->probs[offset]);
  }

  for (offset = 0; offset < table->num_stored; offset++) {
    fmpq_div(new_table->probs[offset], table->probs[offset], mass);
  }
  
  // first we check the cost of checking isometries with random neighbors
  isom_cost = get_isom_cost(table, NULL);
  // then we try to reduce them first
  red_isom_cost = get_isom_cost(table, &greedy_cost);

  printf("Expected cost of isometries is %f\n", isom_cost);
  printf("Expected cost of reduced isometries is %f\n", red_isom_cost + greedy_cost);
  
  theta_prec = 0;

  total_cost = (table->num_stored - 1) * (red_isom_cost + greedy_cost);
  old_cost = total_cost + 1;

  // finding the minimum by brute force
  while (total_cost < old_cost) {
    theta_prec++;
    old_cost = total_cost;
    total_cost = get_total_cost(table, theta_prec, isom_cost, red_isom_cost + greedy_cost, wt_cnts,
				new_table->counts, new_table->vals, &(new_table->red_on_isom), new_table->probs);
  }
  // we are now passed the peak, have to go back one
  theta_prec--;

  // #ifdef DEBUG
  printf("Recalibrated with theta_prec = %u and red_on_isom = %d \n", theta_prec, new_table->red_on_isom);
  // #endif // DEBUG
  
  new_table->theta_prec = theta_prec;

  // revert the used counts
  for (i = 0; i < 2*new_table->capacity; i++) {
    new_table->counts[i] = 0;
  }
  
  // adding all keys
  for (offset = 0; offset < table->num_stored; offset++)
    hash_table_add(new_table, table->keys[offset]);
  
  return;
}

// key is non-const to prevent copying the matrices
int hash_table_insert(hash_table_t table, matrix_TYP* key, hash_t val,
		      int index, int do_push_back)
{
  int offset;
  bravais_TYP* aut_grp;
  
  if (table->num_stored == table->capacity) {
    hash_table_expand(table);
    return _add(table, key, val, do_push_back);
  }

  offset = table->num_stored;
  ++table->num_stored;

  if (do_push_back) {
    table->keys[offset] = key;
    table->vals[offset] = val;
    aut_grp = automorphism_group(key);
    fmpq_set_si(table->probs[offset], 1, aut_grp->order);
  }

  table->key_ptr[index] = offset;
  table->counts[(val & table->mask)]++;

  return 1; 
}

int _add(hash_table_t table, matrix_TYP* key, hash_t val, int do_push_back)
{
  int offset, i;
  hash_t index;

  index = val & table->mask;
  i = 0;
  while (i <= table->counts[val & table->mask]) {
    offset = table->key_ptr[index];
    if (offset == -1) 
      return hash_table_insert(table, key, val, index, do_push_back);
    // we first check equal hashes before testing an isometry
    if ((table->vals[offset] & table->mask) == (val & table->mask)) {
      i++;
      if (table->vals[offset] == val) {
	if (is_isometric(table->keys[offset], key))
	  return 0;
      }
    }
    index = (index + 1) & table->mask;
  }

  offset = table->key_ptr[index];
  while (offset != -1) {
    index = (index + 1) & table->mask;
    offset = table->key_ptr[index];
  }
  return hash_table_insert(table, key, val, index, do_push_back);
  
}

int hash_table_add(hash_table_t table, matrix_TYP* key)
{
  return _add(table, key, hash_form(key, table->theta_prec), 1);
}

/* a modified version of exists to be able to run over all with the same
hash value */
matrix_TYP* hash_table_get_key(const hash_table_t table, matrix_TYP* key, int* index)
{
  int offset;
  hash_t value;

  value = hash_form(key, table->theta_prec);
  
  if (*index == -1) 
    *index = value & table->mask;
  else
    *index = (*index + 1) & table->mask;
  
  offset = table->key_ptr[*index];
  if (offset == -1)
    return NULL;

  while (hash_form(table->keys[offset], table->theta_prec) != value) {
    *index = (*index + 1) & table->mask;
    offset = table->key_ptr[*index];
    if (offset == -1)
      return NULL;
  }
  
  return table->keys[offset];
}

int hash_table_exists(const hash_table_t table, matrix_TYP* key, int check_isom)
{
  int offset, i;
  hash_t index, value;

  value = hash_form(key, table->theta_prec);
  
  index = value & table->mask;
  i = 0;
  while (i < table->counts[value & table->mask]) {
    offset = table->key_ptr[index];
    if (offset == -1)
      return 0;
    if ((table->vals[offset] & table->mask) == (value & table->mask)) {
      if ((table->counts[value & table->mask] == i + 1) && (!check_isom))
	return 1;
      i++;
      if (table->vals[offset] == value) {
	if (is_isometric(table->keys[offset], key))
	   return 1;
      }
    }

    index = (index + 1) & table->mask;
  }

  return 0;
}

int hash_table_indexof(const hash_table_t table, matrix_TYP* key, int check_isom, double* theta_time, double* isom_time, int* num_isom)
{
  int offset, i;
  hash_t index, value;
  clock_t cputime;
  matrix_TYP* s;

  cputime = clock();
  value = hash_form(key, table->theta_prec);
  (*theta_time) += clock() - cputime;
  index = value & table->mask;
  i = 0;
  while (i < table->counts[value & table->mask]) {
    offset = table->key_ptr[index];
    if (offset == -1) {
      printf("Error! Key not found!\n");
      return -1;
    }
    if ((table->vals[offset] & table->mask) == (value & table->mask)) {
      if ((table->counts[value & table->mask] == i + 1) && (!check_isom))
	return offset;
      i++;
      if (table->vals[offset] == value) {
	(*num_isom)++;
	cputime = clock();
	if (table->red_on_isom) {
	  s = init_mat(5,5,"1");
	  greedy(key, s, 5, 5);
	  free_mat(s);
	}
	if (is_isometric(table->keys[offset], key)) {
	  (*isom_time) += clock() - cputime;
	  return offset;
	}
	(*isom_time) += clock() - cputime;
      }
    }

    index = (index + 1) & table->mask;
  }

  printf("Error! Key not found!\n");
  exit(-1);

  return -1;
}

void hash_table_expand(hash_table_t table)
{
  int i, stored;
  
  table->capacity <<= 1;
  table->mask = (table->capacity << 1)-1;
  // problem - this might invalidate existing references to the HashMap (e.g. mother)
  table->keys = (matrix_TYP**)realloc(table->keys, (table->capacity)*sizeof(matrix_TYP*));
  table->vals = (hash_t*)realloc(table->vals, (table->capacity)*sizeof(hash_t));
  table->probs = (fmpq_t*)realloc(table->probs, (table->capacity)*sizeof(fmpq_t));

  // initializing the new ones
  for (i = (table->capacity)>>1; i < table->capacity; i++)
    fmpq_init(table->probs[i]);
  
  free(table->key_ptr);
  table->key_ptr = (hash_t*)malloc((table->capacity << 1)*sizeof(hash_t));
  free(table->counts);
  table->counts = (W32*)malloc((table->capacity << 1)*sizeof(W32));

  for (i = 0; i < 2*table->capacity; i++)
    table->key_ptr[i] = -1;

  for (i = 0; i < 2*table->capacity; i++)
    table->counts[i] = 0;

  stored = table->num_stored;
  table->num_stored = 0;

  for (i=0; i<stored; i++)
    _add(table, table->keys[i], table->vals[i], 0);

  return;
}

void hash_table_clear(hash_table_t table)
{
  hash_t i;
  
  free(table->key_ptr);
  free(table->vals);
  free(table->keys);
  for (i = 0; i < table->capacity; i++)
    fmpq_clear(table->probs[i]);
  free(table->probs);
  free(table->counts);

  return;
}

/*************************************************************************************
 * Here we list several different hash functions that can be used for the hash_table
 *
 *************************************************************************************/

hash_t RSHash(const int* vec, unsigned int n)
{
  hash_t b = 378551;
  hash_t a = 63689;
  hash_t hash = 0;
  unsigned int i = 0;
  for (i = 0; i < n; i++) {
    hash = hash * a + vec[i];
    a *= b;
  }
  return hash;
}

hash_t hash_vec(const int* vec, unsigned int n)
{
  return RSHash(vec, n);
}

/* hash_t hash_vec(int* vec, int n) */
/* { */
/*   hash_t fnv = FNV_OFFSET; */
/*   for (int i = 0; i < n; i++) */
/*     // too slow */
/*     // fnv = (fnv ^ vec[i]) * FNV_PRIME; */
/*     // This one follows a hashing scheme by D J Bernstein */
/*     fnv = ((fnv << 5) + fnv) + vec[i]; */
/*   return fnv; */
/* } */
