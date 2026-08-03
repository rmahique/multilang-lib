<?php

declare(strict_types=1);

namespace Multilang;

use Multilang\Backends\BackendInterface;
use Multilang\Backends\FilesystemBackend;
use Multilang\Backends\MysqlBackend;
use Multilang\Backends\PostgresBackend;
use Multilang\Backends\SqliteBackend;

/**
 * Connector::connect() is the single entry point that turns a backend name
 * + credentials into a connected, schema-ready backend instance.
 *
 * Credentials are never accepted as raw SQL fragments and are never
 * logged. By default they are read from environment variables so they
 * never need to live in source or config files:
 *
 *   MULTILANG_DB_BACKEND    sqlite | postgres | mysql | filesystem
 *   MULTILANG_DB_PATH       (sqlite) file path, (filesystem) root directory
 *   MULTILANG_DB_HOST       (postgres/mysql)
 *   MULTILANG_DB_PORT       (postgres/mysql)
 *   MULTILANG_DB_USER       (postgres/mysql)
 *   MULTILANG_DB_PASSWORD   (postgres/mysql)
 *   MULTILANG_DB_NAME       (postgres/mysql)
 *   MULTILANG_DB_SSLMODE    (postgres/mysql; default "prefer" — use TLS
 *                            opportunistically but don't require it; set
 *                            to "require" to make TLS mandatory, "disable"
 *                            to force plaintext)
 *
 * Explicit entries in $credentials always take precedence over environment
 * variables, which is useful for tests and one-off scripts.
 */
final class Connector
{
    private const SUPPORTED = ['sqlite', 'postgres', 'mysql', 'filesystem'];
    private const DEFAULT_PORTS = ['postgres' => 5432, 'mysql' => 3306];

    private function __construct()
    {
    }

    /**
     * @param array<string,mixed> $credentials
     * @throws \InvalidArgumentException If $backend is missing/unsupported,
     *   or required credentials are missing for a non-sqlite backend.
     */
    public static function connect(?string $backend = null, array $credentials = []): BackendInterface
    {
        $backend = $backend ?? getenv('MULTILANG_DB_BACKEND') ?: null;
        if (!in_array($backend, self::SUPPORTED, true)) {
            $allowed = implode(', ', self::SUPPORTED);
            throw new \InvalidArgumentException("backend must be one of [$allowed] — got " . var_export($backend, true));
        }

        if ($backend === 'sqlite') {
            $path = $credentials['path'] ?? (getenv('MULTILANG_DB_PATH') ?: null);
            if (!$path) {
                throw new \InvalidArgumentException("sqlite backend requires 'path' or MULTILANG_DB_PATH");
            }
            $conn = new SqliteBackend($path);
        } elseif ($backend === 'filesystem') {
            $path = $credentials['path'] ?? (getenv('MULTILANG_DB_PATH') ?: null);
            if (!$path) {
                throw new \InvalidArgumentException("filesystem backend requires 'path' or MULTILANG_DB_PATH");
            }
            $conn = new FilesystemBackend($path);
        } else {
            $host = $credentials['host'] ?? (getenv('MULTILANG_DB_HOST') ?: null);
            $port = $credentials['port'] ?? (getenv('MULTILANG_DB_PORT') ?: self::DEFAULT_PORTS[$backend]);
            $user = $credentials['user'] ?? (getenv('MULTILANG_DB_USER') ?: null);
            $password = $credentials['password'] ?? (getenv('MULTILANG_DB_PASSWORD') ?: null);
            $database = $credentials['database'] ?? (getenv('MULTILANG_DB_NAME') ?: null);

            $missing = array_keys(array_filter(
                ['host' => $host, 'user' => $user, 'password' => $password, 'database' => $database],
                static fn ($v) => !$v
            ));
            if ($missing) {
                throw new \InvalidArgumentException(
                    "$backend backend missing required credentials: " . implode(', ', $missing)
                );
            }

            $sslmode = $credentials['sslmode'] ?? (getenv('MULTILANG_DB_SSLMODE') ?: 'prefer');
            if ($backend === 'postgres') {
                // PDO_PGSQL wraps libpq directly, which already defaults
                // to sslmode=prefer when none is given -- so this stays a
                // simple boolean: only "require" needs to be forced.
                $conn = new PostgresBackend($host, (int) $port, $user, $password, $database, $sslmode === 'require');
            } else {
                $conn = new MysqlBackend($host, (int) $port, $user, $password, $database, $sslmode);
            }
        }

        $conn->ensureSchema();
        return $conn;
    }
}
