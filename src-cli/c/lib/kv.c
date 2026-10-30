#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "fsutil.h"
#include "kv.h"

//SNIP_kv

struct kv_line {
    char *raw;          /* the line as read (without the newline); NULL for a line made by tc_kv_set */
    char *key;          /* NULL for comments, blank and malformed lines */
    char *value;        /* unescaped */
    int dirty;
};

struct tc_kvdoc {
    struct kv_line *lines;
    size_t n, cap;
};

static char *dup_n(const char *s, size_t n)
{
    char *d = malloc(n + 1);

    if (d != NULL) {
        memcpy(d, s, n);
        d[n] = '\0';
    }
    return d;
}

static int is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

/* [s, e) without surrounding blanks */
static char *trimmed(const char *s, const char *e)
{
    while (s < e && is_blank(*s)) {
        s++;
    }
    while (e > s && is_blank(e[-1])) {
        e--;
    }
    return dup_n(s, (size_t)(e - s));
}

static char *unescape(const char *s)
{
    char *out = malloc(strlen(s) + 1), *o = out;

    if (out == NULL) {
        return NULL;
    }
    for (; *s; s++) {
        if (*s == '\\' && s[1] == 'n') {
            *o++ = '\n';
            s++;
        } else if (*s == '\\' && s[1] == '\\') {
            *o++ = '\\';
            s++;
        } else {
            *o++ = *s;
        }
    }
    *o = '\0';
    return out;
}

static char *escape(const char *s)
{
    char *out = malloc(strlen(s) * 2 + 1), *o = out;

    if (out == NULL) {
        return NULL;
    }
    for (; *s; s++) {
        if (*s == '\\') {
            *o++ = '\\';
            *o++ = '\\';
        } else if (*s == '\n') {
            *o++ = '\\';
            *o++ = 'n';
        } else if (*s == '\r') {
            /* not representable, dropped */
        } else {
            *o++ = *s;
        }
    }
    *o = '\0';
    return out;
}

static struct kv_line *add_line(tc_kvdoc *doc)
{
    if (doc->n == doc->cap) {
        size_t cap = doc->cap ? doc->cap * 2 : 16;
        struct kv_line *nl = realloc(doc->lines, cap * sizeof *nl);
        if (nl == NULL) {
            return NULL;
        }
        doc->lines = nl;
        doc->cap = cap;
    }
    memset(&doc->lines[doc->n], 0, sizeof doc->lines[0]);
    return &doc->lines[doc->n++];
}

tc_kvdoc *tc_kv_parse(const char *text, size_t len)
{
    tc_kvdoc *doc = calloc(1, sizeof *doc);
    const char *p = text, *end = text + len;

    if (doc == NULL) {
        return NULL;
    }
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *le = nl ? nl : end, *q, *colon;
        struct kv_line *l = add_line(doc);
        size_t rawlen = (size_t)(le - p);

        if (l == NULL) {
            tc_kv_free(doc);
            return NULL;
        }
        if (rawlen > 0 && le[-1] == '\r') {
            rawlen--;
        }
        l->raw = dup_n(p, rawlen);
        if (l->raw == NULL) {
            tc_kv_free(doc);
            return NULL;
        }
        for (q = l->raw; *q && is_blank(*q); q++) {
        }
        colon = (*q == '\0' || *q == '#') ? NULL : strchr(l->raw, ':');
        if (colon != NULL) {
            char *raw_value = trimmed(colon + 1, l->raw + rawlen);
            l->key = trimmed(l->raw, colon);
            l->value = raw_value ? unescape(raw_value) : NULL;
            free(raw_value);
            if (l->key == NULL || l->value == NULL) {
                tc_kv_free(doc);
                return NULL;
            }
        }
        p = nl ? nl + 1 : end;
    }
    return doc;
}

int tc_kv_load(const char *path, tc_kvdoc **doc, int *existed)
{
    char *data;
    size_t len;

    if (tc_read_file(path, &data, &len) != 0) {
        if (errno == ENOENT) {
            *doc = tc_kv_parse("", 0);
            if (existed) {
                *existed = 0;
            }
            return *doc ? 0 : -1;
        }
        return -1;
    }
    *doc = tc_kv_parse(data, len);
    free(data);
    if (existed) {
        *existed = 1;
    }
    return *doc ? 0 : -1;
}

char *tc_kv_serialise(const tc_kvdoc *doc, size_t *len)
{
    size_t i, total = 1;
    char *out, *o;

    for (i = 0; i < doc->n; i++) {
        const struct kv_line *l = &doc->lines[i];
        total += (l->key && l->dirty ? strlen(l->key) + 2 + strlen(l->value) * 2 : strlen(l->raw)) + 1;
    }
    out = o = malloc(total);
    if (out == NULL) {
        return NULL;
    }
    for (i = 0; i < doc->n; i++) {
        const struct kv_line *l = &doc->lines[i];
        if (l->key && l->dirty) {
            char *esc = escape(l->value);
            if (esc == NULL) {
                free(out);
                return NULL;
            }
            o += sprintf(o, "%s: %s", l->key, esc);
            free(esc);
        } else {
            o += sprintf(o, "%s", l->raw);
        }
        *o++ = '\n';
    }
    *o = '\0';
    if (len) {
        *len = (size_t)(o - out);
    }
    return out;
}

int tc_kv_save(const tc_kvdoc *doc, const char *path)
{
    size_t len;
    char *text = tc_kv_serialise(doc, &len);
    int rc;

    if (text == NULL) {
        errno = ENOMEM;
        return -1;
    }
    rc = tc_write_atomic(path, text, len);
    free(text);
    return rc;
}

void tc_kv_free(tc_kvdoc *doc)
{
    size_t i;

    if (doc == NULL) {
        return;
    }
    for (i = 0; i < doc->n; i++) {
        free(doc->lines[i].raw);
        free(doc->lines[i].key);
        free(doc->lines[i].value);
    }
    free(doc->lines);
    free(doc);
}

static long find_last(const tc_kvdoc *doc, const char *key)
{
    size_t i = doc->n;

    while (i-- > 0) {
        if (doc->lines[i].key && strcmp(doc->lines[i].key, key) == 0) {
            return (long)i;
        }
    }
    return -1;
}

const char *tc_kv_get(const tc_kvdoc *doc, const char *key)
{
    long i = find_last(doc, key);

    return i < 0 ? NULL : doc->lines[i].value;
}

long long tc_kv_get_int(const tc_kvdoc *doc, const char *key, long long def)
{
    const char *v = tc_kv_get(doc, key);
    char *end;
    long long n;

    if (v == NULL) {
        return def;
    }
    n = strtoll(v, &end, 10);
    return end == v ? def : n;
}

double tc_kv_get_double(const tc_kvdoc *doc, const char *key, double def)
{
    const char *v = tc_kv_get(doc, key);
    char *end;
    double d;

    if (v == NULL) {
        return def;
    }
    d = strtod(v, &end);
    return end == v ? def : d;
}

int tc_kv_set(tc_kvdoc *doc, const char *key, const char *value)
{
    long i = find_last(doc, key);
    char *v = strdup(value);
    struct kv_line *l;

    if (v == NULL) {
        return -1;
    }
    if (i >= 0) {
        l = &doc->lines[i];
        free(l->value);
    } else {
        char *k = strdup(key);
        l = add_line(doc);
        if (l == NULL || k == NULL) {
            free(v);
            free(k);
            return -1;
        }
        l->key = k;
    }
    l->value = v;
    l->dirty = 1;
    return 0;
}

int tc_kv_set_int(tc_kvdoc *doc, const char *key, long long value)
{
    char buf[32];

    snprintf(buf, sizeof buf, "%lld", value);
    return tc_kv_set(doc, key, buf);
}

void tc_kv_remove(tc_kvdoc *doc, const char *key)
{
    size_t i, out = 0;

    for (i = 0; i < doc->n; i++) {
        struct kv_line *l = &doc->lines[i];
        if (l->key && strcmp(l->key, key) == 0) {
            free(l->raw);
            free(l->key);
            free(l->value);
            continue;
        }
        doc->lines[out++] = *l;
    }
    doc->n = out;
}

int tc_kv_add_comment(tc_kvdoc *doc, const char *text)
{
    struct kv_line *l = add_line(doc);

    if (l == NULL) {
        return -1;
    }
    l->raw = malloc(strlen(text) + 3);
    if (l->raw == NULL) {
        doc->n--;
        return -1;
    }
    sprintf(l->raw, "# %s", text);
    return 0;
}

size_t tc_kv_count(const tc_kvdoc *doc)
{
    size_t i, n = 0;

    for (i = 0; i < doc->n; i++) {
        if (doc->lines[i].key) {
            n++;
        }
    }
    return n;
}

void tc_kv_print(FILE *f, const char *key, const char *value)
{
    char *esc = escape(value);

    fprintf(f, "%s: %s\n", key, esc ? esc : "");
    free(esc);
}

//SNIP_kv
