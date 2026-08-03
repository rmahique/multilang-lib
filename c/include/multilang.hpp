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

#include <optional>
#include <stdexcept>
#include <string>

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

    /** Raw handle, for interop with the C API. */
    ml_backend *raw() const { return conn_; }

private:
    ml_backend *conn_ = nullptr;
};

} // namespace multilang

#endif // MULTILANG_HPP
