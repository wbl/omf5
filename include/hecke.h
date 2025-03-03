#ifndef __HECKE_H__
#define __HECKE_H__

#include <carat/matrix.h>

#include <antic/nf_elem.h>

#include "genus.h"
#include "matrix_tools.h"
#include "nbr_data.h"
#include "neighbor.h"

// !! TODO - merge these when we finish debugging

int process_isotropic_vector_nbr_data(nbr_data_t nbr_man, int* T, const genus_t genus, 
				      double* theta_time, double* isom_time, double* total_time, int* num_isom);

int process_isotropic_vector(neighbor_manager* nbr_man, int* T, const genus_t genus,
			     double* theta_time, double* isom_time, double* total_time, int* num_isom);


int process_neighbour_chunk(int* T, int p, int i, int gen_idx, const genus_t genus,
			    double* theta_time, double* isom_time, double* total_time, int* num_isom);

int process_neighbour_chunk_nbr_data(int* T, int p, int k, int gen_idx, const genus_t genus, 
				     double* theta_time, double* isom_time, double* total_time, int* num_isom);

void hecke_col_nbr_data(int* T, int p, int k, int gen_idx, const genus_t genus);

void hecke_col(int* T, int p, int gen_idx, const genus_t genus);

void get_hecke_ev_nbr_data(nf_elem_t e, const genus_t genus, eigenvalues* evs, int p, int k, int ev_idx);

void get_hecke_ev(nf_elem_t e, const genus_t genus, eigenvalues* evs, int p, int ev_idx);

matrix_TYP* hecke_matrix(const genus_t genus, int p);

eigenvalues* hecke_eigenforms(const genus_t genus);

#endif // __HECKE_H__
