#include <runtime.h>

typedef u32 offset;
    
#define ENTRY_ALIGNMENT_LOG 2

void buffer_write_le32(buffer b, u32 x)
{
    *(u32 *)(b->contents+b->end) = x;
    b->end += sizeof(u32);
}

boolean compare_bytes(void *a, void *b, bytes len)
{
    for (int i = 0; i < len ; i++) {
        if (((u8 *)a)[i] != ((u8 *)b)[i])
            return false;
    }
    return true;
}

static inline u32 *bucket_count(buffer b, u64 start)
{
    return (u32 *)(b->contents+start);
}

static inline u32 *buckets(buffer b, u64 start)
{
    return bucket_count(b, start) + 1;
}

boolean storage_lookup(buffer b, u64 start, buffer key, u64 *off, bytes *length)
{
    u32 count = *bucket_count(b, start);
    offset where = buckets(b, start)[fnv64(key) % count];

    while (where) {
        offset *e = b->contents + (where<<ENTRY_ALIGNMENT_LOG);
        if ((e[3] == buffer_length(key)) &&
            compare_bytes(key->contents, (void *)(e+4), e[3])) {
            *off = (e[1] << ENTRY_ALIGNMENT_LOG);
            *length = e[2];
            return true;
        }
        where = e[0];
    }
    return false;
}

// for some reason I think we can dedup bodies here easily, idk why
// alignment, empty space.. elminate this indirect?  or add indirection
// for keys?
void storage_set(buffer b, u64 start, buffer key, u64 voff, u64 vlen)
{
    offset *slot = buckets(b, start) + fnv64(key) % *bucket_count(b, start);
    int pk = pad(buffer_length(key), (1<<ENTRY_ALIGNMENT_LOG));
    int nlen = pk + 4 * sizeof(offset);
    offset loc = b->end >> ENTRY_ALIGNMENT_LOG;
    buffer_extend(b, nlen);
    u32 next = *slot;
    buffer_write_le32(b, next);
    buffer_write_le32(b, voff >> ENTRY_ALIGNMENT_LOG);
    buffer_write_le32(b, vlen);
    buffer_write_le32(b, buffer_length(key));
    *slot = loc;
    push_buffer(b, key);
    b->end += pk - buffer_length(key);
}


// key and value and length + offset
void iterate(buffer b, u64 start, void (*f)(buffer b, u64 klen, void *key, u64 vlen, u64 voff))
{
    u32 count = *bucket_count(b, start);
    offset *k = buckets(b, start);
    for (offset i; i < count; i++) {
        offset where = k[i];
        while (where) {
            offset *e = b->contents + (where<<ENTRY_ALIGNMENT_LOG);
            u32 klen = e[3];
            u32 vlen = e[2];
            void *key = k+where;
            f(b, klen, key, vlen, (e[1]<<ENTRY_ALIGNMENT_LOG));
            where = e[0];
        }   
    }
}
 
u64 init_storage(buffer b, int buckets)
{
    u64 off = b->end;
    buffer_write_le32(b, buckets);
    int blen = buckets * sizeof(offset);
    buffer_extend(b, blen);
    zero(b->contents + b->end, blen);
    b->end += blen;
    return off;
}

