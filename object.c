#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/evp.h>
#include <errno.h>

// ─── PROVIDED ────────────────────────────────────────────────────────────────

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_out + i * 2, "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) < HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        id_out->hash[i] = (uint8_t)byte;
    }
    return 0;
}

void compute_hash(const void *data, size_t len, ObjectID *id_out) {
    unsigned int hash_len;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, id_out->hash, &hash_len);
    EVP_MD_CTX_free(ctx);
}

void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, "%s/%.2s/%s", OBJECTS_DIR, hex, hex + 2);
}

int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}

// ─── TODO: Implement these ──────────────────────────────────────────────────

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    const char *type_str;
    switch (type) {
        case OBJ_BLOB:   type_str = "blob"; break;
        case OBJ_TREE:   type_str = "tree"; break;
        case OBJ_COMMIT: type_str = "commit"; break;
        default: return -1;
    }

    // 1. Build the full object with header: "type size\0data"
    char header[64];
    int header_len = snprintf(header, sizeof(header), "%s %zu", type_str, len) + 1; // +1 for \0
    
    size_t full_size = header_len + len;
    uint8_t *full_obj = malloc(full_size);
    if (!full_obj) return -1;

    memcpy(full_obj, header, header_len);
    memcpy(full_obj + header_len, data, len);

    // 2. Compute SHA-256 of FULL object
    compute_hash(full_obj, full_size, id_out);

    // 3. Deduplication check
    if (object_exists(id_out)) {
        free(full_obj);
        return 0;
    }

    // 4. Create shard directory (.pes/objects/XX)
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id_out, hex);
    char shard_dir[512];
    snprintf(shard_dir, sizeof(shard_dir), "%s/%.2s", OBJECTS_DIR, hex);
    mkdir(shard_dir, 0755);

    // 5. Write to a temporary file
    char temp_path[512], final_path[512];
    object_path(id_out, final_path, sizeof(final_path));
    snprintf(temp_path, sizeof(temp_path), "%s/tmp_XXXXXX", shard_dir);
    
    int fd = mkstemp(temp_path);
    if (fd < 0) {
        free(full_obj);
        return -1;
    }

    if (write(fd, full_obj, full_size) != (ssize_t)full_size) {
        close(fd);
        unlink(temp_path);
        free(full_obj);
        return -1;
    }

    // 6. fsync to ensure data reaches disk
    fsync(fd);
    close(fd);

    // 7. Atomic rename
    if (rename(temp_path, final_path) != 0) {
        unlink(temp_path);
        free(full_obj);
        return -1;
    }

    // 8. Sync directory to persist rename
    int dir_fd = open(shard_dir, O_RDONLY);
    if (dir_fd >= 0) {
        fsync(dir_fd);
        close(dir_fd);
    }

    free(full_obj);
    return 0;
}

int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {
    char path[512];
    object_path(id, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    // Get file size
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = malloc(file_size);
    if (!buf) {
        fclose(f);
        return -1;
    }

    if (fread(buf, 1, file_size, f) != (size_t)file_size) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);

    // Integrity Verify
    ObjectID actual_id;
    compute_hash(buf, file_size, &actual_id);
    if (memcmp(id->hash, actual_id.hash, HASH_SIZE) != 0) {
        free(buf);
        return -1;
    }

    // Parse header
    char *null_byte = memchr(buf, '\0', file_size);
    if (!null_byte) {
        free(buf);
        return -1;
    }

    char type_str[16];
    size_t size;
    if (sscanf((char *)buf, "%15s %zu", type_str, &size) != 2) {
        free(buf);
        return -1;
    }

    if (strcmp(type_str, "blob") == 0) *type_out = OBJ_BLOB;
    else if (strcmp(type_str, "tree") == 0) *type_out = OBJ_TREE;
    else if (strcmp(type_str, "commit") == 0) *type_out = OBJ_COMMIT;
    else {
        free(buf);
        return -1;
    }

    *len_out = size;
    *data_out = malloc(size);
    memcpy(*data_out, null_byte + 1, size);

    free(buf);
    return 0;
}