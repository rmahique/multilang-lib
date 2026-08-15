/*
 * multilang.hpp — a thin, RAII C++ wrapper over the multilang.h C API.
 *
 * Same schema, same validation rules, same results as every other
 * language port in this project — see ../../conformance/README.md. This
 * wrapper adds no behavior of its own: it translates ml_status into
 * exceptions and owns the ml_backend* lifetime, nothing more.
 */

#ifndef MULTILANG_HPP
#define MULTILANG_HPP

#include "multilang.h"

#include <ctime>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace multilang {

/** Thrown when caller-supplied data fails validation (ML_ERR_VALIDATION). */
class ValidationError : public std::runtime_error {
public:
    explicit ValidationError(const std::string &msg) : std::runtime_error(msg) {}
};

/** Thrown for connection/query failures or bad arguments (any other non-OK ml_status). */
class DbError : public std::runtime_error {
public:
    explicit DbError(const std::string &msg) : std::runtime_error(msg) {}
};

/** Mirrors ml_credentials; empty fields fall back to MULTILANG_DB_* env vars. */
struct Credentials {
    std::string path;     // sqlite
    std::string host;     // postgres/mysql
    int port = 0;          // postgres/mysql; 0 = use backend default
    std::string user;     // postgres/mysql
    std::string password; // postgres/mysql
    std::string database; // postgres/mysql
};

/** Mirrors ml_insert_options; empty fields mean "use the default." */
struct InsertOptions {
    std::string context;
    std::string original_language;
    std::string status = "draft";
    std::string updated_by;
};

/** Mirrors ml_search_mode — see docs/search.md for the exact/natural/regex semantics. */
enum class SearchMode { Natural, Exact, Regex };

/**
 * Mirrors ml_search_options. language_id/status: "" means no filter.
 * context: unset (std::nullopt, the default) means no filter; set to ""
 * filters for only the default/un-contextualized row -- "" can't double
 * as both "no filter" and "a real filter value" the way it can for
 * language_id/status, which are never valid as "".
 */
struct SearchOptions {
    std::string language_id;
    std::optional<std::string> context;
    std::string status;
    bool case_sensitive = false;
    int limit = 0; // 0 = default (ML_DEFAULT_SEARCH_LIMIT)
    int offset = 0;
};

/** Mirrors ml_search_result — one matching row, returned by search_data. */
struct SearchResult {
    std::string string_id;
    std::string language_id;
    std::string context;
    std::string content;
    std::optional<std::string> original_language;
    std::string status;
    std::optional<std::string> source_checksum;
    std::optional<std::string> updated_by;
    std::time_t date_updated;
};

/**
 * An open, schema-ready connection. Move-only: owns an ml_backend* and
 * closes it in the destructor, so a Connection going out of scope always
 * releases its underlying connection deterministically.
 */
class Connection {
public:
    /**
     * Opens a connection via ml_connect. Throws DbError on failure
     * (missing credentials, connection refused, etc).
     */
    Connection(const std::string &backend, const Credentials &creds = {});

    ~Connection();

    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;
    Connection(Connection &&other) noexcept;
    Connection &operator=(Connection &&other) noexcept;

    /**
     * Look up one piece of text by its identity. Returns std::nullopt if
     * no matching row exists — data only, no metadata.
     * Throws ValidationError if any argument fails validation.
     */
    std::optional<std::string> retrieve_data(const std::string &string_id,
                                              const std::string &language_id,
                                              const std::string &context = "") const;

    /**
     * Insert a new row, or update it in place if (string_id, language_id,
     * context) already exists. Throws ValidationError if any argument
     * fails validation.
     */
    void insert_data(const std::string &string_id, const std::string &language_id,
                      const std::string &content, const InsertOptions &opts = {});

    /**
     * Search content across every row matching opts' optional filters,
     * ordered by match score descending then (language_id, string_id,
     * context) ascending as a deterministic tiebreak. Matching runs
     * entirely in-process, guaranteeing identical results across every
     * backend — see docs/search.md. Throws ValidationError if any
     * argument fails validation.
     */
    std::vector<SearchResult> search_data(const std::string &query, SearchMode mode,
                                           const SearchOptions &opts = {}) const;

    /** Raw handle, for interop with the C API. */
    ml_backend *raw() const { return conn_; }

private:
    ml_backend *conn_ = nullptr;
};

} // namespace multilang

#endif // MULTILANG_HPP
