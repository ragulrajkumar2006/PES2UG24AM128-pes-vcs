#include "index.h"
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <inttypes.h>

// ─── PROVIDED ────────────────────────────────────────────────────────────────

IndexEntry* index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0)
            return &index->entries[i];
    }
    return NULL;
}

int index_status(const Index *index) {
    printf("Staged changes:\n");
    for (int i = 0; i < index->count; i++) {
        printf("  staged:     %s\n", index->entries[i].path);
    }
    return 0;
}

static int compare_entries(const void *a, const void *b) {
    return strcmp(((IndexEntry*)a)->path, ((IndexEntry*)b)->path);
}

// ─── TODO IMPLEMENTATION ─────────────────────────────────────────────────────

int index_load(Index *index) {
    index->count = 0;
    FILE *f = fopen(INDEX_FILE, "r");
    if (!f) return 0;

    char hex[HASH_HEX_SIZE + 1];
    while (index->count < MAX_INDEX_ENTRIES) {
        IndexEntry *e = &index->entries[index->count];
        if (fscanf(f, "%o %64s %" SCNu64 " %u %511s\n", 
                   &e->mode, hex, &e->mtime_sec, (unsigned int*)&e->size, e->path) != 5) {
            break;
        }
        hex_to_hash(hex, &e->hash);
        index->count++;
    }
    fclose(f);
    return 0;
}

int index_save(const Index *index) {
    qsort((void*)index->entries, index->count, sizeof(IndexEntry), compare_entries);
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", INDEX_FILE);
    FILE *f = fopen(tmp_path, "w");
    if (!f) return -1;

    for (int i = 0; i < index->count; i++) {
        const IndexEntry *e = &index->entries[i];
        char hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&e->hash, hex);
        fprintf(f, "%o %s %" PRIu64 " %u %s\n", 
                e->mode, hex, e->mtime_sec, (unsigned int)e->size, e->path);
    }
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    return rename(tmp_path, INDEX_FILE);
}

int index_add(Index *index, const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;

    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    void *data = malloc(st.st_size);
    if (!data) { fclose(f); return -1; }
    fread(data, 1, st.st_size, f);
    fclose(f);

    ObjectID id;
    if (object_write(OBJ_BLOB, data, st.st_size, &id) != 0) {
        free(data); return -1;
    }
    free(data);

    IndexEntry *e = index_find(index, path);
    if (!e) {
        if (index->count >= MAX_INDEX_ENTRIES) return -1;
        e = &index->entries[index->count++];
        strncpy(e->path, path, 511);
    }

    e->hash = id;
    e->mode = st.st_mode;
    e->mtime_sec = (uint64_t)st.st_mtime;
    e->size = (uint32_t)st.st_size;

    return index_save(index);
}