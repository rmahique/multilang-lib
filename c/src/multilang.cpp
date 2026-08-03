#include "../include/multilang.hpp"

#include <cstdlib>
#include <cstring>

namespace multilang {

namespace {

[[noreturn]] void throw_for_status(ml_status status, const char *errbuf)
{
    std::string msg = errbuf != nullptr ? errbuf : "";
    if (status == ML_ERR_VALIDATION) {
        throw ValidationError(msg);
    }
    throw DbError(msg.empty() ? "multilang: operation failed" : msg);
}

/* nullptr for empty strings, so "" reliably means "not set" on the C side
 * (same convention InsertOptions/Credentials use throughout this port). */
const char *c_str_or_null(const std::string &s)
{
    return s.empty() ? nullptr : s.c_str();
}

} // namespace

Connection::Connection(const std::string &backend, const Credentials &creds)
{
    ml_credentials c{};
    c.path = c_str_or_null(creds.path);
    c.host = c_str_or_null(creds.host);
    c.port = creds.port;
    c.user = c_str_or_null(creds.user);
    c.password = c_str_or_null(creds.password);
    c.database = c_str_or_null(creds.database);

    char errbuf[ML_ERRBUF_LEN] = {0};
    ml_status status = ml_connect(backend.empty() ? nullptr : backend.c_str(), &c, &conn_, errbuf, sizeof(errbuf));
    if (status != ML_OK) {
        throw_for_status(status, errbuf);
    }
}

Connection::~Connection()
{
    ml_close(conn_);
}

Connection::Connection(Connection &&other) noexcept : conn_(other.conn_)
{
    other.conn_ = nullptr;
}

Connection &Connection::operator=(Connection &&other) noexcept
{
    if (this != &other) {
        ml_close(conn_);
        conn_ = other.conn_;
        other.conn_ = nullptr;
    }
    return *this;
}

std::optional<std::string> Connection::retrieve_data(const std::string &string_id,
                                                       const std::string &language_id,
                                                       const std::string &context) const
{
    char *content = nullptr;
    char errbuf[ML_ERRBUF_LEN] = {0};
    ml_status status = ml_retrieve_data(conn_, string_id.c_str(), language_id.c_str(),
                                         c_str_or_null(context), &content, errbuf, sizeof(errbuf));
    if (status != ML_OK) {
        throw_for_status(status, errbuf);
    }
    if (content == nullptr) {
        return std::nullopt;
    }
    std::string result(content);
    free(content);
    return result;
}

void Connection::insert_data(const std::string &string_id, const std::string &language_id,
                              const std::string &content, const InsertOptions &opts)
{
    ml_insert_options c_opts{};
    c_opts.context = c_str_or_null(opts.context);
    c_opts.original_language = c_str_or_null(opts.original_language);
    c_opts.status = c_str_or_null(opts.status);
    c_opts.updated_by = c_str_or_null(opts.updated_by);

    char errbuf[ML_ERRBUF_LEN] = {0};
    ml_status status = ml_insert_data(conn_, string_id.c_str(), language_id.c_str(), content.c_str(),
                                       &c_opts, errbuf, sizeof(errbuf));
    if (status != ML_OK) {
        throw_for_status(status, errbuf);
    }
}

} // namespace multilang
