#include "affinity.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// ============ Affinity Lifecycle ============

k8s_affinity_t* k8s_affinity_new() {
    k8s_affinity_t* affinity = (k8s_affinity_t*)malloc(sizeof(k8s_affinity_t));
    if (!affinity) return NULL;
    
    affinity->node_affinity = NULL;
    affinity->pod_affinity = NULL;
    affinity->pod_anti_affinity = NULL;
    
    return affinity;
}

void k8s_affinity_free(k8s_affinity_t* affinity) {
    if (!affinity) return;
    
    k8s_node_affinity_free(affinity->node_affinity);
    k8s_pod_affinity_free(affinity->pod_affinity);
    k8s_pod_anti_affinity_free(affinity->pod_anti_affinity);
    
    free(affinity);
}

// ============ Node Affinity ============

k8s_node_affinity_t* k8s_node_affinity_new() {
    k8s_node_affinity_t* affinity = (k8s_node_affinity_t*)malloc(sizeof(k8s_node_affinity_t));
    if (!affinity) return NULL;
    
    affinity->required_during_scheduling = NULL;
    affinity->preferred_during_scheduling = NULL;
    affinity->num_preferred = 0;
    
    return affinity;
}

void k8s_node_affinity_free(k8s_node_affinity_t* affinity) {
    if (!affinity) return;
    
    if (affinity->required_during_scheduling) {
        if (affinity->required_during_scheduling->terms) {
            for (int i = 0; i < affinity->required_during_scheduling->num_terms; i++) {
                if (affinity->required_during_scheduling->terms[i]) {
                    if (affinity->required_during_scheduling->terms[i]->match_expressions) {
                        for (int j = 0; j < affinity->required_during_scheduling->terms[i]->num_expressions; j++) {
                            free(affinity->required_during_scheduling->terms[i]->match_expressions[j]);
                        }
                        free(affinity->required_during_scheduling->terms[i]->match_expressions);
                    }
                    free(affinity->required_during_scheduling->terms[i]);
                }
            }
            free(affinity->required_during_scheduling->terms);
        }
        free(affinity->required_during_scheduling);
    }
    
    if (affinity->preferred_during_scheduling) {
        for (int i = 0; i < affinity->num_preferred; i++) {
            if (affinity->preferred_during_scheduling[i]) {
                if (affinity->preferred_during_scheduling[i]->terms) {
                    for (int j = 0; j < affinity->preferred_during_scheduling[i]->num_terms; j++) {
                        if (affinity->preferred_during_scheduling[i]->terms[j]) {
                            free(affinity->preferred_during_scheduling[i]->terms[j]);
                        }
                    }
                    free(affinity->preferred_during_scheduling[i]->terms);
                }
                free(affinity->preferred_during_scheduling[i]);
            }
        }
        free(affinity->preferred_during_scheduling);
    }
    
    free(affinity);
}

int k8s_node_affinity_add_required_term(k8s_node_affinity_t* affinity, k8s_node_selector_term_t* term) {
    if (!affinity || !term) return -1;
    
    if (!affinity->required_during_scheduling) {
        affinity->required_during_scheduling = (k8s_node_affinity_req_t*)malloc(sizeof(k8s_node_affinity_req_t));
        if (!affinity->required_during_scheduling) return -1;
        affinity->required_during_scheduling->terms = NULL;
        affinity->required_during_scheduling->num_terms = 0;
    }
    
    // Add term to required list
    k8s_node_selector_term_t** new_terms = (k8s_node_selector_term_t**)realloc(
        affinity->required_during_scheduling->terms,
        (affinity->required_during_scheduling->num_terms + 1) * sizeof(k8s_node_selector_term_t*)
    );
    if (!new_terms) return -1;
    
    affinity->required_during_scheduling->terms = new_terms;
    affinity->required_during_scheduling->terms[affinity->required_during_scheduling->num_terms] = term;
    affinity->required_during_scheduling->num_terms++;
    
    return 0;
}

int k8s_node_affinity_add_preferred_term(k8s_node_affinity_t* affinity, k8s_pod_affinity_req_t* term, int weight) {
    if (!affinity || !term) return -1;
    
    if (affinity->num_preferred >= 100) return -1;  // Max 100 preferred rules
    
    k8s_pod_affinity_req_t** new_preferred = (k8s_pod_affinity_req_t**)realloc(
        affinity->preferred_during_scheduling,
        (affinity->num_preferred + 1) * sizeof(k8s_pod_affinity_req_t*)
    );
    if (!new_preferred) return -1;
    
    affinity->preferred_during_scheduling = new_preferred;
    term->weight = weight;
    affinity->preferred_during_scheduling[affinity->num_preferred] = term;
    affinity->num_preferred++;
    
    return 0;
}

// ============ Pod Affinity ============

k8s_pod_affinity_t* k8s_pod_affinity_new() {
    k8s_pod_affinity_t* affinity = (k8s_pod_affinity_t*)malloc(sizeof(k8s_pod_affinity_t));
    if (!affinity) return NULL;
    
    affinity->required = NULL;
    affinity->num_required = 0;
    affinity->preferred = NULL;
    affinity->num_preferred = 0;
    
    return affinity;
}

void k8s_pod_affinity_free(k8s_pod_affinity_t* affinity) {
    if (!affinity) return;
    
    if (affinity->required) {
        for (int i = 0; i < affinity->num_required; i++) {
            if (affinity->required[i]) {
                if (affinity->required[i]->terms) {
                    for (int j = 0; j < affinity->required[i]->num_terms; j++) {
                        if (affinity->required[i]->terms[j]) {
                            free(affinity->required[i]->terms[j]->topology_key);
                            free(affinity->required[i]->terms[j]);
                        }
                    }
                    free(affinity->required[i]->terms);
                }
                free(affinity->required[i]);
            }
        }
        free(affinity->required);
    }
    
    if (affinity->preferred) {
        for (int i = 0; i < affinity->num_preferred; i++) {
            if (affinity->preferred[i]) {
                if (affinity->preferred[i]->terms) {
                    for (int j = 0; j < affinity->preferred[i]->num_terms; j++) {
                        if (affinity->preferred[i]->terms[j]) {
                            free(affinity->preferred[i]->terms[j]->topology_key);
                            free(affinity->preferred[i]->terms[j]);
                        }
                    }
                    free(affinity->preferred[i]->terms);
                }
                free(affinity->preferred[i]);
            }
        }
        free(affinity->preferred);
    }
    
    free(affinity);
}

int k8s_pod_affinity_add_required(k8s_pod_affinity_t* affinity, k8s_pod_affinity_term_t* term) {
    if (!affinity || !term) return -1;
    
    k8s_pod_affinity_req_t** new_required = (k8s_pod_affinity_req_t**)realloc(
        affinity->required,
        (affinity->num_required + 1) * sizeof(k8s_pod_affinity_req_t*)
    );
    if (!new_required) return -1;
    
    affinity->required = new_required;
    k8s_pod_affinity_req_t* req = (k8s_pod_affinity_req_t*)malloc(sizeof(k8s_pod_affinity_req_t));
    if (!req) return -1;
    
    req->terms = (k8s_pod_affinity_term_t**)malloc(sizeof(k8s_pod_affinity_term_t*));
    if (!req->terms) { free(req); return -1; }
    
    req->terms[0] = term;
    req->num_terms = 1;
    req->weight = 100;  // Required affinity gets highest weight
    
    affinity->required[affinity->num_required] = req;
    affinity->num_required++;
    
    return 0;
}

int k8s_pod_affinity_add_preferred(k8s_pod_affinity_t* affinity, k8s_pod_affinity_term_t* term, int weight) {
    if (!affinity || !term || weight < 1 || weight > 100) return -1;
    
    k8s_pod_affinity_req_t** new_preferred = (k8s_pod_affinity_req_t**)realloc(
        affinity->preferred,
        (affinity->num_preferred + 1) * sizeof(k8s_pod_affinity_req_t*)
    );
    if (!new_preferred) return -1;
    
    affinity->preferred = new_preferred;
    k8s_pod_affinity_req_t* req = (k8s_pod_affinity_req_t*)malloc(sizeof(k8s_pod_affinity_req_t));
    if (!req) return -1;
    
    req->terms = (k8s_pod_affinity_term_t**)malloc(sizeof(k8s_pod_affinity_term_t*));
    if (!req->terms) { free(req); return -1; }
    
    req->terms[0] = term;
    req->num_terms = 1;
    req->weight = weight;
    
    affinity->preferred[affinity->num_preferred] = req;
    affinity->num_preferred++;
    
    return 0;
}

// ============ Pod Anti-Affinity ============

k8s_pod_anti_affinity_t* k8s_pod_anti_affinity_new() {
    k8s_pod_anti_affinity_t* affinity = (k8s_pod_anti_affinity_t*)malloc(sizeof(k8s_pod_anti_affinity_t));
    if (!affinity) return NULL;
    
    affinity->required = NULL;
    affinity->num_required = 0;
    affinity->preferred = NULL;
    affinity->num_preferred = 0;
    
    return affinity;
}

void k8s_pod_anti_affinity_free(k8s_pod_anti_affinity_t* affinity) {
    if (!affinity) return;
    
    if (affinity->required) {
        for (int i = 0; i < affinity->num_required; i++) {
            if (affinity->required[i]) {
                if (affinity->required[i]->terms) {
                    for (int j = 0; j < affinity->required[i]->num_terms; j++) {
                        if (affinity->required[i]->terms[j]) {
                            free(affinity->required[i]->terms[j]->topology_key);
                            free(affinity->required[i]->terms[j]);
                        }
                    }
                    free(affinity->required[i]->terms);
                }
                free(affinity->required[i]);
            }
        }
        free(affinity->required);
    }
    
    if (affinity->preferred) {
        for (int i = 0; i < affinity->num_preferred; i++) {
            if (affinity->preferred[i]) {
                if (affinity->preferred[i]->terms) {
                    for (int j = 0; j < affinity->preferred[i]->num_terms; j++) {
                        if (affinity->preferred[i]->terms[j]) {
                            free(affinity->preferred[i]->terms[j]->topology_key);
                            free(affinity->preferred[i]->terms[j]);
                        }
                    }
                    free(affinity->preferred[i]->terms);
                }
                free(affinity->preferred[i]);
            }
        }
        free(affinity->preferred);
    }
    
    free(affinity);
}

int k8s_pod_anti_affinity_add_required(k8s_pod_anti_affinity_t* affinity, k8s_pod_affinity_term_t* term) {
    if (!affinity || !term) return -1;
    
    k8s_pod_affinity_req_t** new_required = (k8s_pod_affinity_req_t**)realloc(
        affinity->required,
        (affinity->num_required + 1) * sizeof(k8s_pod_affinity_req_t*)
    );
    if (!new_required) return -1;
    
    affinity->required = new_required;
    k8s_pod_affinity_req_t* req = (k8s_pod_affinity_req_t*)malloc(sizeof(k8s_pod_affinity_req_t));
    if (!req) return -1;
    
    req->terms = (k8s_pod_affinity_term_t**)malloc(sizeof(k8s_pod_affinity_term_t*));
    if (!req->terms) { free(req); return -1; }
    
    req->terms[0] = term;
    req->num_terms = 1;
    req->weight = 100;  // Required gets highest weight
    
    affinity->required[affinity->num_required] = req;
    affinity->num_required++;
    
    return 0;
}

int k8s_pod_anti_affinity_add_preferred(k8s_pod_anti_affinity_t* affinity, k8s_pod_affinity_term_t* term, int weight) {
    if (!affinity || !term || weight < 1 || weight > 100) return -1;
    
    k8s_pod_affinity_req_t** new_preferred = (k8s_pod_affinity_req_t**)realloc(
        affinity->preferred,
        (affinity->num_preferred + 1) * sizeof(k8s_pod_affinity_req_t*)
    );
    if (!new_preferred) return -1;
    
    affinity->preferred = new_preferred;
    k8s_pod_affinity_req_t* req = (k8s_pod_affinity_req_t*)malloc(sizeof(k8s_pod_affinity_req_t));
    if (!req) return -1;
    
    req->terms = (k8s_pod_affinity_term_t**)malloc(sizeof(k8s_pod_affinity_term_t*));
    if (!req->terms) { free(req); return -1; }
    
    req->terms[0] = term;
    req->num_terms = 1;
    req->weight = weight;
    
    affinity->preferred[affinity->num_preferred] = req;
    affinity->num_preferred++;
    
    return 0;
}

// ============ Taint & Toleration ============

k8s_taint_t* k8s_taint_new(const char* key, const char* value, const char* effect) {
    if (!key || !effect) return NULL;
    
    k8s_taint_t* taint = (k8s_taint_t*)malloc(sizeof(k8s_taint_t));
    if (!taint) return NULL;
    
    taint->key = (char*)malloc(strlen(key) + 1);
    if (!taint->key) { free(taint); return NULL; }
    strcpy(taint->key, key);
    
    taint->effect = (char*)malloc(strlen(effect) + 1);
    if (!taint->effect) { free(taint->key); free(taint); return NULL; }
    strcpy(taint->effect, effect);
    
    if (value) {
        taint->value = (char*)malloc(strlen(value) + 1);
        if (!taint->value) { free(taint->key); free(taint->effect); free(taint); return NULL; }
        strcpy(taint->value, value);
    } else {
        taint->value = NULL;
    }
    
    taint->toleration_seconds = -1;  // Infinite by default
    
    return taint;
}

void k8s_taint_free(k8s_taint_t* taint) {
    if (!taint) return;
    
    free(taint->key);
    free(taint->value);
    free(taint->effect);
    free(taint);
}

k8s_toleration_t* k8s_toleration_new(const char* key, const char* operator, const char* value, const char* effect) {
    if (!key || !operator || !effect) return NULL;
    
    k8s_toleration_t* toleration = (k8s_toleration_t*)malloc(sizeof(k8s_toleration_t));
    if (!toleration) return NULL;
    
    toleration->key = (char*)malloc(strlen(key) + 1);
    if (!toleration->key) { free(toleration); return NULL; }
    strcpy(toleration->key, key);
    
    toleration->operator = (char*)malloc(strlen(operator) + 1);
    if (!toleration->operator) { free(toleration->key); free(toleration); return NULL; }
    strcpy(toleration->operator, operator);
    
    toleration->effect = (char*)malloc(strlen(effect) + 1);
    if (!toleration->effect) { free(toleration->key); free(toleration->operator); free(toleration); return NULL; }
    strcpy(toleration->effect, effect);
    
    if (value) {
        toleration->value = (char*)malloc(strlen(value) + 1);
        if (!toleration->value) { free(toleration->key); free(toleration->operator); free(toleration->effect); free(toleration); return NULL; }
        strcpy(toleration->value, value);
    } else {
        toleration->value = NULL;
    }
    
    toleration->toleration_seconds = -1;  // Infinite by default
    
    return toleration;
}

void k8s_toleration_free(k8s_toleration_t* toleration) {
    if (!toleration) return;
    
    free(toleration->key);
    free(toleration->operator);
    free(toleration->value);
    free(toleration->effect);
    free(toleration);
}

bool k8s_toleration_matches_taint(k8s_toleration_t* toleration, k8s_taint_t* taint) {
    if (!toleration || !taint) return false;
    
    // Key must match
    if (strcmp(toleration->key, taint->key) != 0) return false;
    
    // Effect must match or be empty (matches all)
    if (toleration->effect && strcmp(toleration->effect, "") != 0 &&
        strcmp(toleration->effect, taint->effect) != 0) {
        return false;
    }
    
    // Value matching depends on operator
    if (strcmp(toleration->operator, "Equal") == 0) {
        // Values must be exactly equal
        if (!toleration->value || !taint->value) return false;
        return strcmp(toleration->value, taint->value) == 0;
    } else if (strcmp(toleration->operator, "Exists") == 0) {
        // Exists operator matches if key exists, value irrelevant
        return true;
    }
    
    return false;
}
