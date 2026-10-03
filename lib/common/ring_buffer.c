#include "ring_buffer.h"
#include <string.h>
#include <stdlib.h>

ring_buffer_t* ring_buffer_create(size_t capacity) {
    ring_buffer_t* rb = (ring_buffer_t*)malloc(sizeof(ring_buffer_t));
    if (!rb) return NULL;
    
    rb->data = (uint8_t*)malloc(capacity);
    if (!rb->data) {
        free(rb);
        return NULL;
    }
    
    rb->capacity = capacity;
    rb->head = 0;
    rb->tail = 0;
    rb->size = 0;
    return rb;
}

void ring_buffer_destroy(ring_buffer_t* rb) {
    if (rb) {
        free(rb->data);
        free(rb);
    }
}

bool ring_buffer_push(ring_buffer_t* rb, const uint8_t* data, size_t len) {
    if (!rb || !data || len == 0) return false;
    if (len > ring_buffer_free(rb)) return false;
    
    size_t first_part = rb->capacity - rb->head;
    if (len <= first_part) {
        memcpy(rb->data + rb->head, data, len);
        rb->head = (rb->head + len) % rb->capacity;
    } else {
        memcpy(rb->data + rb->head, data, first_part);
        memcpy(rb->data, data + first_part, len - first_part);
        rb->head = len - first_part;
    }
    rb->size += len;
    return true;
}

size_t ring_buffer_pop(ring_buffer_t* rb, uint8_t* data, size_t len) {
    if (!rb || !data || len == 0 || rb->size == 0) return 0;
    
    size_t available = rb->size;
    if (len > available) len = available;
    
    size_t first_part = rb->capacity - rb->tail;
    if (len <= first_part) {
        memcpy(data, rb->data + rb->tail, len);
        rb->tail = (rb->tail + len) % rb->capacity;
    } else {
        memcpy(data, rb->data + rb->tail, first_part);
        memcpy(data + first_part, rb->data, len - first_part);
        rb->tail = len - first_part;
    }
    rb->size -= len;
    return len;
}

size_t ring_buffer_peek(const ring_buffer_t* rb, uint8_t* data, size_t len) {
    if (!rb || !data || len == 0 || rb->size == 0) return 0;
    
    size_t available = rb->size;
    if (len > available) len = available;
    
    size_t first_part = rb->capacity - rb->tail;
    if (len <= first_part) {
        memcpy(data, rb->data + rb->tail, len);
    } else {
        memcpy(data, rb->data + rb->tail, first_part);
        memcpy(data + first_part, rb->data, len - first_part);
    }
    return len;
}

bool ring_buffer_find_line(ring_buffer_t* rb, char* line, size_t max_len) {
    if (!rb || rb->size == 0 || !line || max_len == 0) return false;
    
    size_t available = rb->size;
    if (available >= max_len) available = max_len - 1;
    
    uint8_t* temp = (uint8_t*)malloc(available + 1);
    if (!temp) return false;
    
    size_t read = ring_buffer_peek(rb, temp, available);
    temp[read] = '\0';
    
    char* newline = strchr((char*)temp, '\n');
    if (!newline) {
        free(temp);
        return false;
    }
    
    size_t line_len = newline - (char*)temp;
    if (line_len >= max_len) line_len = max_len - 1;
    memcpy(line, temp, line_len);
    line[line_len] = '\0';
    
    // Remove the line including \n and potential \r
    size_t consume = line_len + 1;
    if (consume < read && temp[consume] == '\r') consume++;
    
    ring_buffer_pop(rb, NULL, consume);  // Discard
    
    free(temp);
    return true;
}

size_t ring_buffer_size(const ring_buffer_t* rb) {
    return rb ? rb->size : 0;
}

size_t ring_buffer_free(const ring_buffer_t* rb) {
    return rb ? (rb->capacity - rb->size) : 0;
}

bool ring_buffer_is_empty(const ring_buffer_t* rb) {
    return rb && rb->size == 0;
}

bool ring_buffer_is_full(const ring_buffer_t* rb) {
    return rb && rb->size == rb->capacity;
}

void ring_buffer_clear(ring_buffer_t* rb) {
    if (rb) {
        rb->head = 0;
        rb->tail = 0;
        rb->size = 0;
    }
}