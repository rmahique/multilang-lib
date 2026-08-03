# Errors

Two categories of failure, handled differently on purpose:

- **Validation errors** — caller-supplied data fails one of the rules in
  [`validation.md`](validation.md) (bad BCP 47 tag, disallowed character
  in an identifier, oversized field, wrong `status` value, ...). Every
  port raises/returns its own idiomatic type for this, listed below, but
  the trigger conditions are identical everywhere — that's what the
  conformance suite enforces.
- **Database errors** — a connection failure, a constraint violation the
  database itself rejects, a network timeout. Only C/C++ wrap these in a
  library-specific type; the other four ports deliberately let the
  native driver's own exception propagate unchanged (see below for why).

## Validation errors, by language

| Language | Type | Raised as | Message access |
|---|---|---|---|
| Python | `multilang.validation.ValidationError` (subclasses `ValueError`) | `raise ValidationError(...)` | `str(exc)` |
| JavaScript | `ValidationError` (subclasses `Error`) | `throw new ValidationError(...)` | `exc.message`; `exc.name === 'ValidationError'` |
| PHP | `Multilang\ValidationException` (subclasses `\InvalidArgumentException`) | `throw new ValidationException(...)` | `$exc->getMessage()` |
| Go | `*multilang.ValidationError` (implements `error`) | returned as the second value, never panics | `err.Error()`; `errors.As(err, &ve)` to get the typed value |
| C | `ML_ERR_VALIDATION` (an `ml_status` enum value) | returned as the function's return code; message written into the caller-supplied `err`/`errbuf` | the `char*` buffer passed in |
| C++ | `multilang::ValidationError` (subclasses `std::runtime_error`) | `throw` | `exc.what()` |

Python's choice to subclass `ValueError` and PHP's choice to subclass
`InvalidArgumentException` are deliberate, not incidental: both let a
caller catch the *language's own* standard "bad argument" hierarchy
without importing anything from this library, in addition to catching
the specific `ValidationError`/`ValidationException` type by name.

## Database errors: not wrapped, except in C/C++

Python, JavaScript, PHP, and Go all let the underlying driver's native
exception surface as-is — a Postgres connection refusal comes back as
`psycopg2.OperationalError`, `pg`'s own rejected-promise error, PDO's
`PDOException`, or `*pgconn.PgError`/whatever `database/sql` wraps it as,
respectively. That's intentional: those exception types already carry
driver-specific detail (SQLSTATE codes, retryability hints) that a
library-wide wrapper would either lose or have to imperfectly
re-abstract, and every one of those languages has a mature, idiomatic way
to catch "some database error happened" already (`except Exception`,
catching `Error`, catching `PDOException`, checking `err != nil`)
without this library inventing a parallel one.

C and C++ are the exception, and necessarily so: C has no exception
mechanism at all, so every function already returns an `ml_status` and
writes a human-readable message into a caller-supplied buffer —
`ML_ERR_DB` covers connection/query failures the same way
`ML_ERR_VALIDATION` covers bad input, both through that one channel.
C++'s wrapper turns `ML_ERR_DB` into a distinct `multilang::DbError`
(alongside `ValidationError`) specifically so a C++ caller can
`catch (const multilang::ValidationError&)` and
`catch (const multilang::DbError&)` separately, which isn't possible
through the C layer's single status-code return.

## `retrieve_data` on a missing row is not an error

Worth stating explicitly because it's easy to assume otherwise: asking
for a `(language_id, string_id, context)` combination that doesn't exist
is a normal, expected outcome in a system where most translations are
missing most of the time — it is **not** a validation error and **not** a
database error. Every port returns an explicit "nothing here" value
instead of raising: `None` (Python), `null` (JavaScript), `null` (PHP),
`("", false, nil)` — a zero value plus a `found bool` (Go), and
`ML_OK` with `*content` set to `NULL` (C) / `std::nullopt` (C++).
