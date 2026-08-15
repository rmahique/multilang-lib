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

std::vector<SearchResult> Connection::search_data(const std::string &query, SearchMode mode,
                                                    const SearchOptions &opts) const
{
    ml_search_mode c_mode = mode == SearchMode::Exact   ? ML_SEARCH_EXACT
                             : mode == SearchMode::Regex ? ML_SEARCH_REGEX
                                                          : ML_SEARCH_NATURAL;

    ml_search_options c_opts{};
    c_opts.language_id = c_str_or_null(opts.language_id);
    // opts.context: unset -> nullptr (no filter); set to "" -> a pointer
    // to an empty (but non-null) C string, which ml_search_data reads as
    // "filter for the default/un-contextualized row" -- distinct from
    // "no filter" only because std::optional lets us tell "unset" and
    // "set to empty" apart, unlike a plain std::string.
    c_opts.context = opts.context.has_value() ? opts.context->c_str() : nullptr;
    c_opts.status = c_str_or_null(opts.status);
    c_opts.case_sensitive = opts.case_sensitive ? 1 : 0;
    c_opts.limit = opts.limit;
    c_opts.offset = opts.offset;

    ml_search_result *results = nullptr;
    size_t count = 0;
    char errbuf[ML_ERRBUF_LEN] = {0};
    ml_status status =
        ml_search_data(conn_, query.c_str(), c_mode, &c_opts, &results, &count, errbuf, sizeof(errbuf));
    if (status != ML_OK) {
        throw_for_status(status, errbuf);
    }

    std::vector<SearchResult> out;
    out.reserve(count);
    for (size_t i = 0; i < count; i++) {
        SearchResult r;
        r.string_id = results[i].string_id;
        r.language_id = results[i].language_id;
        r.context = results[i].context;
        r.content = results[i].content;
        r.original_language =
            results[i].original_language ? std::optional<std::string>(results[i].original_language) : std::nullopt;
        r.status = results[i].status;
        r.source_checksum =
            results[i].source_checksum ? std::optional<std::string>(results[i].source_checksum) : std::nullopt;
        r.updated_by = results[i].updated_by ? std::optional<std::string>(results[i].updated_by) : std::nullopt;
        r.date_updated = results[i].date_updated;
        out.push_back(std::move(r));
    }
    ml_free_search_results(results, count);
    return out;
}

} // namespace multilang
