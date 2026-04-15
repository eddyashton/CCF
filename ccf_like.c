/*
 * ccf_like.c — Simulate CCF ledger I/O pattern on a target directory.
 *
 * Mimics the write → complete → commit → rotate cycle from CCF's Ledger
 * class (src/host/ledger.h), producing the same syscall pattern on CIFS.
 *
 * Logs any operation exceeding --threshold-ms (default 10ms).
 *
 * Build: cc -O2 -o ccf_like ccf_like.c
 * Usage: ./ccf_like --dir /mnt/azure-fs/bench --chunks 20
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ── Timing helpers ───────────────────────────────────────────────────── */

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static double wall_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static double g_threshold_ms = 10.0;

#define TIMED(label, fname, file_pos, chunk_idx, code)                     \
    do                                                                     \
    {                                                                      \
        double _t0 = now_ms();                                             \
        code;                                                              \
        double _dt = now_ms() - _t0;                                       \
        if (_dt >= g_threshold_ms)                                         \
            fprintf(                                                       \
                stderr,                                                    \
                "[%.3f] SLOW %7.1fms  %-12s  chunk=%d  size=%zu  file=%s\n", \
                wall_sec(),                                                \
                _dt,                                                       \
                label,                                                     \
                chunk_idx,                                                 \
                (size_t)(file_pos),                                        \
                fname);                                                    \
    } while (0)

/* ── Ledger file operations ───────────────────────────────────────────── */

/*
 * Create a new ledger chunk file.
 * Mirrors: LedgerFile::LedgerFile(dir, start_idx) — the "new file" ctor
 * which calls fopen(path, "w+b") and writes the 8-byte header placeholder.
 * See ledger.h LedgerFile constructor (new file path).
 */
static FILE* ledger_file_create(
    const char* fname, off_t* file_pos, int chunk_idx)
{
    FILE* f = NULL;
    TIMED("openat", fname, 0, chunk_idx, {
        f = fopen(fname, "w+b");
    });
    if (!f)
    {
        perror("fopen w+b");
        return NULL;
    }

    /* Header reserved for the offset to the positions table */
    uint64_t table_offset = 0;
    TIMED("write", fname, 0, chunk_idx, {
        fwrite(&table_offset, sizeof(table_offset), 1, f);
    });
    *file_pos = sizeof(table_offset);
    return f;
}

/*
 * Write a single ledger entry and optionally flush.
 * Mirrors: LedgerFile::write_entry() with committable=true path.
 * fseeko + fwrite + conditional fflush.
 * See ledger.h LedgerFile::write_entry().
 */
static void ledger_write_entry(
    FILE* f,
    const char* fname,
    off_t file_pos,
    const uint8_t* data,
    size_t size,
    int flush,
    int chunk_idx)
{
    TIMED("write", fname, file_pos, chunk_idx, {
        fseeko(f, file_pos, SEEK_SET);
        if (fwrite(data, size, 1, f) != 1)
        {
            perror("fwrite");
            exit(1);
        }
        if (flush && fflush(f) != 0)
        {
            perror("fflush");
            exit(1);
        }
    });
}

/*
 * Complete a ledger chunk: truncate + write positions table + flush.
 * Mirrors: LedgerFile::complete() which calls truncate(get_last_idx()),
 * then writes the positions table at the end of the file, writes the
 * table offset into the header at byte 0, and flushes.
 * See ledger.h LedgerFile::complete().
 */
static void ledger_file_complete(
    FILE* f,
    const char* fname,
    off_t file_pos,
    uint64_t* positions,
    size_t entry_count,
    int chunk_idx)
{
    /* truncate(get_last_idx()) -> fflush + ftruncate */
    TIMED("ftruncate", fname, file_pos, chunk_idx, {
        fflush(f);
        if (ftruncate(fileno(f), file_pos) != 0)
        {
            perror("ftruncate");
            exit(1);
        }
    });

    /* Write positions table at end, then table offset at header */
    TIMED("write_pos", fname, file_pos, chunk_idx, {
        fseeko(f, file_pos, SEEK_SET);
        uint64_t table_offset = ftello(f);
        fwrite(positions, sizeof(uint64_t), entry_count, f);
        fseeko(f, 0, SEEK_SET);
        fwrite(&table_offset, sizeof(table_offset), 1, f);
        fflush(f);
    });
}

/*
 * Commit a completed ledger chunk: fsync + close + rename to .committed.
 * Mirrors: LedgerFile::commit() (fsync + rename) and ~LedgerFile (fclose).
 * See ledger.h LedgerFile::commit().
 */
static void ledger_file_commit(
    FILE* f,
    const char* fname,
    off_t file_pos,
    const char* committed_name,
    int chunk_idx)
{
    TIMED("fsync", fname, file_pos, chunk_idx, {
        fsync(fileno(f));
    });

    TIMED("close", fname, file_pos, chunk_idx, {
        fclose(f);
    });

    TIMED("rename", fname, file_pos, chunk_idx, {
        if (rename(fname, committed_name) != 0)
        {
            perror("rename");
            exit(1);
        }
    });
}

/* ── Main ─────────────────────────────────────────────────────────────── */

static void usage(const char* prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  --dir PATH        Target directory (required)\n"
        "  --chunks N        Number of chunk rotations (default: 20)\n"
        "  --chunk-size N    Bytes per chunk (default: 3800000)\n"
        "  --write-size N    Bytes per entry write (default: 283)\n"
        "  --threshold-ms N  Log ops slower than this (default: 10)\n"
        , prog);
    exit(1);
}

int main(int argc, char** argv)
{
    const char* dir = NULL;
    int num_chunks = 20;
    size_t chunk_size = 3800000;
    size_t write_size = 283;
    size_t ae_batch = 20;       /* AppendEntries batch size — flush interval */

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--dir") && i + 1 < argc)
            dir = argv[++i];
        else if (!strcmp(argv[i], "--chunks") && i + 1 < argc)
            num_chunks = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--chunk-size") && i + 1 < argc)
            chunk_size = (size_t)atol(argv[++i]);
        else if (!strcmp(argv[i], "--write-size") && i + 1 < argc)
            write_size = (size_t)atol(argv[++i]);
        else if (!strcmp(argv[i], "--threshold-ms") && i + 1 < argc)
            g_threshold_ms = atof(argv[++i]);
        else
            usage(argv[0]);
    }

    if (!dir)
        usage(argv[0]);

    uint8_t* wbuf = calloc(1, write_size);
    if (!wbuf)
    {
        perror("calloc");
        return 1;
    }
    memset(wbuf, 0xAB, write_size);

    size_t max_entries = chunk_size / write_size + 1;
    uint64_t* positions = calloc(max_entries, sizeof(uint64_t));
    if (!positions)
    {
        perror("calloc positions");
        return 1;
    }

    mkdir(dir, 0755);

    double t_total_start = now_ms();
    size_t total_writes = 0;

    for (int chunk_idx = 0; chunk_idx < num_chunks; chunk_idx++)
    {
        char fname[512], committed[512];
        snprintf(fname, sizeof(fname), "%s/ledger_%d", dir, chunk_idx);
        snprintf(committed, sizeof(committed),
                 "%s/ledger_%d.committed", dir, chunk_idx);

        off_t file_pos = 0;
        size_t entry_count = 0;
        double t_chunk_start = now_ms();

        /*
         * Phase 1: Create new chunk file.
         * Mirrors Ledger::write_entry() -> new LedgerFile(dir, start_idx).
         */
        FILE* f = ledger_file_create(fname, &file_pos, chunk_idx);
        if (!f)
            return 1;

        /*
         * Phase 2: Write entries until chunk is full.
         * Mirrors the Ledger::write_entry() -> LedgerFile::write_entry() loop
         * driven by ledger_append ring buffer messages.
         * Committable entries (approximated as every ae_batch'th) are flushed.
         */
        while ((size_t)file_pos + write_size <= chunk_size)
        {
            int flush = (entry_count % ae_batch == ae_batch - 1);
            ledger_write_entry(
                f, fname, file_pos, wbuf, write_size, flush, chunk_idx);

            positions[entry_count] = file_pos;
            file_pos += write_size;
            entry_count++;
            total_writes++;
        }

        /*
         * Phase 3: Complete the chunk.
         * Mirrors LedgerFile::complete() — called when FORCE_LEDGER_CHUNK_AFTER
         * flag is set, or when the chunk reaches its size limit.
         * Does: fflush + ftruncate + write positions table + write header.
         */
        ledger_file_complete(
            f, fname, file_pos, positions, entry_count, chunk_idx);

        /*
         * Phase 4: Commit the chunk.
         * Mirrors LedgerFile::commit() — called when the commit index reaches
         * the last entry in the completed chunk.
         * Does: fsync + fclose + rename to .committed suffix.
         */
        ledger_file_commit(f, fname, file_pos, committed, chunk_idx);

        double chunk_ms = now_ms() - t_chunk_start;
        fprintf(
            stderr,
            "[%.3f] chunk %d done: %zu entries, %.0f bytes, %.1fms (%.0f "
            "writes/s)\n",
            wall_sec(), chunk_idx, entry_count, (double)file_pos, chunk_ms,
            entry_count / (chunk_ms / 1000.0));
    }

    double total_ms = now_ms() - t_total_start;
    fprintf(
        stderr,
        "\nSummary: %d chunks, %zu writes, %.1fs total (%.0f writes/s avg)\n",
        num_chunks, total_writes, total_ms / 1000.0,
        total_writes / (total_ms / 1000.0));

    free(wbuf);
    free(positions);
    return 0;
}
