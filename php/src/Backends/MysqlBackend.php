<?php

declare(strict_types=1);

namespace Multilang\Backends;

use DateTimeImmutable;
use PDO;

/**
 * MySQL/MariaDB backend, via PDO.
 *
 * Requires MySQL 5.7.9+ or MariaDB 10.2.2+ (innodb_large_prefix on by
 * default, DYNAMIC row format default) — the composite primary key
 * (language_id, string_id, context) in utf8mb4 can exceed the legacy
 * 767-byte InnoDB index-prefix limit on older versions/configurations.
 */
final class MysqlBackend implements BackendInterface
{
    private const SCHEMA = <<<SQL
CREATE TABLE IF NOT EXISTS strings (
    language_id       VARCHAR(35) NOT NULL,
    string_id         VARCHAR(200) NOT NULL,
    context           VARCHAR(200) NOT NULL DEFAULT '',
    content           MEDIUMTEXT NOT NULL,
    original_language VARCHAR(35),
    status            VARCHAR(20) NOT NULL DEFAULT 'draft',
    source_checksum   VARCHAR(64),
    updated_by        VARCHAR(200),
    date_updated      DATETIME NOT NULL,
    PRIMARY KEY (language_id, string_id, context)
) ENGINE=InnoDB ROW_FORMAT=DYNAMIC DEFAULT CHARSET=utf8mb4
SQL;

    private const UPSERT = <<<SQL
INSERT INTO strings
    (language_id, string_id, context, content, original_language,
     status, source_checksum, updated_by, date_updated)
VALUES (:language_id, :string_id, :context, :content, :original_language,
        :status, :source_checksum, :updated_by, :date_updated)
ON DUPLICATE KEY UPDATE
    content=VALUES(content),
    original_language=VALUES(original_language),
    status=VALUES(status),
    source_checksum=VALUES(source_checksum),
    updated_by=VALUES(updated_by),
    date_updated=VALUES(date_updated)
SQL;

    private const SELECT = <<<SQL
SELECT content FROM strings
WHERE language_id = :language_id AND string_id = :string_id AND context = :context
SQL;

    private const SELECT_ROWS_BASE = <<<SQL
SELECT string_id, language_id, context, content, original_language,
       status, source_checksum, updated_by, date_updated
FROM strings
SQL;

    private PDO $pdo;

    /**
     * @param string $sslmode "prefer" (default): attempt TLS, fall back
     *   to plaintext if the server doesn't support it, matching the
     *   Postgres backend's default. "require": attempt TLS, throw if the
     *   server didn't actually negotiate it rather than falling back.
     *   "disable": plaintext only, no TLS attempt. PDO_MySQL has no
     *   native "prefer" mode (unlike libpq's sslmode string), so it's
     *   implemented here: try connecting with SSL requested first, and
     *   only fall back to a plain connection if that attempt fails.
     * @throws \RuntimeException If $sslmode is "require" but the server
     *   didn't negotiate TLS.
     */
    public function __construct(
        string $host,
        int $port,
        string $user,
        string $password,
        string $database,
        string $sslmode = 'prefer'
    ) {
        // Credentials are passed as DSN/PDO connection parameters, never
        // interpolated into a query string or logged.
        $dsn = "mysql:host=$host;port=$port;dbname=$database;charset=utf8mb4";

        if ($sslmode === 'disable') {
            $this->pdo = new PDO($dsn, $user, $password);
        } else {
            try {
                // VERIFY_SERVER_CERT=false: encrypt against a self-signed
                // cert on a container/localhost server without failing
                // on an untrusted CA, the same non-verifying posture
                // "require" uses elsewhere.
                $this->pdo = new PDO($dsn, $user, $password, [
                    PDO::MYSQL_ATTR_SSL_VERIFY_SERVER_CERT => false,
                ]);
            } catch (\PDOException $e) {
                if ($sslmode === 'require') {
                    throw $e;
                }
                $this->pdo = new PDO($dsn, $user, $password);
            }
        }
        $this->pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);

        if ($sslmode === 'require') {
            $cipher = $this->pdo->query("SHOW STATUS LIKE 'Ssl_cipher'")->fetch(PDO::FETCH_ASSOC);
            if (empty($cipher['Value'])) {
                throw new \RuntimeException(
                    'MULTILANG_DB_SSLMODE=require but the MySQL server did not negotiate TLS'
                );
            }
        }
    }

    public function ensureSchema(): void
    {
        $this->pdo->exec(self::SCHEMA);
    }

    public function selectContent(string $stringId, string $languageId, string $context): ?string
    {
        $stmt = $this->pdo->prepare(self::SELECT);
        $stmt->execute(['language_id' => $languageId, 'string_id' => $stringId, 'context' => $context]);
        $content = $stmt->fetchColumn();
        return $content === false ? null : $content;
    }

    public function upsert(array $row): void
    {
        $stmt = $this->pdo->prepare(self::UPSERT);
        $stmt->execute([
            'language_id' => $row['language_id'],
            'string_id' => $row['string_id'],
            'context' => $row['context'],
            'content' => $row['content'],
            'original_language' => $row['original_language'],
            'status' => $row['status'],
            'source_checksum' => $row['source_checksum'],
            'updated_by' => $row['updated_by'],
            // MySQL DATETIME rejects the "T" separator and timezone
            // offset that an ISO-8601 string would carry; format plainly.
            'date_updated' => $row['date_updated']->format('Y-m-d H:i:s'),
        ]);
    }

    public function selectRows(?string $languageId, ?string $context, ?string $status): array
    {
        $clauses = [];
        $params = [];
        if ($languageId !== null) {
            $clauses[] = 'language_id = :language_id';
            $params['language_id'] = $languageId;
        }
        if ($context !== null) {
            $clauses[] = 'context = :context';
            $params['context'] = $context;
        }
        if ($status !== null) {
            $clauses[] = 'status = :status';
            $params['status'] = $status;
        }

        $sql = self::SELECT_ROWS_BASE . ($clauses ? ' WHERE ' . implode(' AND ', $clauses) : '');
        $stmt = $this->pdo->prepare($sql);
        $stmt->execute($params);

        $rows = [];
        while ($row = $stmt->fetch(PDO::FETCH_ASSOC)) {
            $row['date_updated'] = new DateTimeImmutable($row['date_updated']);
            $rows[] = $row;
        }
        return $rows;
    }

    public function close(): void
    {
    }

    /** @internal exposed for tests that need to inspect/reset raw rows */
    public function pdo(): PDO
    {
        return $this->pdo;
    }
}
