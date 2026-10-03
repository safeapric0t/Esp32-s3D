#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t* data;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t size;
} ring_buffer_t;

ring_buffer_t* ring_buffer_create(size_t capacity);
void ring_buffer_destroy(ring_buffer_t* rb);

bool ring_buffer_push(ring_buffer_t* rb, const uint8_t* data, size_t len);
size_t ring_buffer_pop(ring_buffer_t* rb, uint8_t* data, size_t len);
size_t ring_buffer_peek(const ring_buffer_t* rb, uint8_t* data, size_t len);

// Find and extract a complete line (ending with \n)
// Returns true if line found, false if no complete line in buffer
bool ring_buffer_find_line(ring_buffer_t* rb, char* line, size_t max_len);

size_t ring_buffer_size(const ring_buffer_t* rb);
size_t ring_buffer_free(const ring_buffer_t* rb);
bool ring_buffer_is_empty(const ring_buffer_t* rb);
bool ring_buffer_is_full(const ring_buffer_t* rb);
void ring_buffer_clear(ring_buffer_t* rb);

#ifdef __cplusplus
}
#endif