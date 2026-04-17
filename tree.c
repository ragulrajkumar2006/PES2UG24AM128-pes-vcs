#include "tree.h"
#include "index.h"
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

int tree_serialize(const Tree *tree, void **data_out, size_t *len_out) {
    uint8_t *buffer = malloc(tree->count * 600);
    size_t offset = 0;
    for (int i = 0; i < tree->count; i++) {
        const TreeEntry *e = &tree->entries[i];
        offset += sprintf((char*)buffer + offset, "%o %s", e->mode, e->name) + 1;
        memcpy(buffer + offset, e->hash.hash, HASH_SIZE);
        offset += HASH_SIZE;
    }
    *data_out = buffer; *len_out = offset;
    return 0;
}

int tree_from_index(ObjectID *id_out) {
    Index index;
    if (index_load(&index) != 0) return -1;
    Tree tree; tree.count = index.count;
    for (int i = 0; i < index.count; i++) {
        TreeEntry *e = &tree.entries[i];
        e->mode = index.entries[i].mode;
        strncpy(e->name, index.entries[i].path, sizeof(e->name));
        e->hash = index.entries[i].hash;
    }
    void *data; size_t len;
    tree_serialize(&tree, &data, &len);
    int rc = object_write(OBJ_TREE, data, len, id_out);
    free(data);
    return rc;
}