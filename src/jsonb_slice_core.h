/*
 * jsonb_slice_core.h
 *
 * Internal boundary between the SQL entry points and the partial JSONB reader.
 * SliceSource owns the storage context, ContainerLocation bounds one JSONB
 * container, and ValueLocation describes a value that has not been fetched.
 */
#ifndef JSONB_SLICE_CORE_H
#define JSONB_SLICE_CORE_H

#include "postgres.h"

/* Carries one external value and its safe read boundary through the helpers. */
typedef struct SliceSource
{
	/* Raw external datum used for each slice request. */
	struct varlena *attr;
	/* Logical byte size used to reject out-of-range requests. */
	int32 total_size;
	/* Test-only sum of requested slice sizes. */
	int64 fetched_bytes;
} SliceSource;

/* Identifies one JSONB container inside the complete logical value. */
typedef struct ContainerLocation
{
	/* Absolute offset of the container header. */
	int32 start;
	/* Byte size of the container, including its header and directory. */
	int32 size;
	/* Only the root can use JSONB's raw-scalar array wrapper. */
	bool root;
} ContainerLocation;

/* Keeps decoded value metadata separate from fetching and rebuilding it. */
typedef struct ValueLocation
{
	/* JEntry type bits. */
	uint32 type;
	/* Absolute slice offset in the JSONB data. */
	int32 start;
	/* Byte size to request; zero for null and Boolean values. */
	int32 size;
} ValueLocation;

extern bool slice_source_init(struct varlena *attr, SliceSource *source);

/* Validate containers and locate values without fetching value payloads. */
extern bool slice_read_container_header(SliceSource *source,
										const ContainerLocation *container,
										uint32 *header);
extern bool slice_normalize_array_index(int32 requested, uint32 element_count,
										uint32 *index);
extern bool slice_object_lookup(SliceSource *source,
								const ContainerLocation *container,
								uint32 pair_count, const char *key,
								uint32 key_length, ValueLocation *value,
								bool *found);
extern bool slice_array_lookup(SliceSource *source,
							   const ContainerLocation *container,
							   uint32 element_count, uint32 index,
							   ValueLocation *value, bool *found);

/* Fetch one located value and produce the requested SQL result type. */
extern bool slice_value_result(SliceSource *source,
							   const ValueLocation *location, Datum *result);
extern bool slice_value_text_result(SliceSource *source,
									const ValueLocation *location,
									Datum *result, bool *is_null);

#endif /* JSONB_SLICE_CORE_H */
