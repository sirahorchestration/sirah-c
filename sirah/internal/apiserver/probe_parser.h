// internal/apiserver/probe_parser.h
// Parse health probes from pod JSON spec

#ifndef K8S_PROBE_PARSER_H
#define K8S_PROBE_PARSER_H

#include <json-c/json.h>
#include "probes.h"

// Parse startupProbe from container JSON
probe_spec_t* parse_startup_probe(json_object* container);

// Parse readinessProbe from container JSON
probe_spec_t* parse_readiness_probe(json_object* container);

// Parse livenessProbe from container JSON
probe_spec_t* parse_liveness_probe(json_object* container);

// Helper: parse a probe handler from JSON
static probe_spec_t* parse_probe_spec(json_object* probe_obj, probe_type_t type);

#endif
