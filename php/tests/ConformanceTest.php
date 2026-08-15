<?php

declare(strict_types=1);

namespace Multilang\Tests;

use Multilang\Backends\BackendInterface;
use Multilang\Connector;
use Multilang\Strings;
use Multilang\ValidationException;
use PHPUnit\Framework\TestCase;

/**
 * Runs the shared, language-agnostic conformance suite
 * (../../conformance/cases.json) against this PHP implementation.
 *
 * This is the enforcement mechanism for the "same functionality and
 * results across every language and every database" requirement.
 *
 * By default this runs against SQLite (a fresh temp file per case). Set
 * MULTILANG_DB_BACKEND=postgres or =mysql (plus MULTILANG_DB_HOST/_PORT/
 * _USER/_PASSWORD/_NAME) to run the exact same suite against a real
 * server — see ../../conformance/run-live-db-tests.sh, which stands up
 * disposable Postgres/MySQL containers and runs every port's suite
 * against both.
 */
final class ConformanceTest extends TestCase
{
    /** @dataProvider cases */
    public function testConformanceCase(string $name, array $operations): void
    {
        $conn = $this->freshConn($name);

        foreach ($operations as $step) {
            $args = $step['args'];

            if (!empty($step['expect_error'])) {
                try {
                    $this->callOp($step['op'], $conn, $args);
                    $this->fail("expected ValidationException for op {$step['op']} in case $name");
                } catch (ValidationException $e) {
                    $this->assertInstanceOf(ValidationException::class, $e);
                }
                continue;
            }

            $result = $this->callOp($step['op'], $conn, $args);
            if (array_key_exists('expect', $step)) {
                $this->assertSame($step['expect'], $result);
            }
        }
    }

    private function freshConn(string $name): BackendInterface
    {
        $backend = getenv('MULTILANG_DB_BACKEND') ?: 'sqlite';

        if ($backend === 'sqlite') {
            $path = sys_get_temp_dir() . '/multilang_php_conf_' . $name . '_' . uniqid() . '.db';
            return Connector::connect('sqlite', ['path' => $path]);
        }

        if ($backend === 'filesystem') {
            $path = sys_get_temp_dir() . '/multilang_php_conf_fs_' . $name . '_' . uniqid();
            return Connector::connect('filesystem', ['path' => $path]);
        }

        $conn = Connector::connect($backend);
        // Postgres/MySQL share one long-lived server across the whole run
        // (no per-case temp file the way SQLite gets one), so each case
        // truncates the table itself instead of connecting to a
        // throwaway database — cheaper, and just as isolating since
        // every case starts from zero rows either way.
        $conn->pdo()->exec('TRUNCATE TABLE strings');
        return $conn;
    }

    /** conformance/cases.json uses Python-style snake_case argument names, shared across all ports. */
    private function callOp(string $op, $conn, array $args)
    {
        if ($op === 'retrieve_data') {
            return Strings::retrieveData($conn, $args['string_id'], $args['language_id'], $args['context'] ?? '');
        }
        if ($op === 'search_data') {
            // search_data returns full rows, not a single JSON-comparable
            // value like retrieve_data -- cases.json's "expect" for this
            // op is an array of [language_id, string_id, context]
            // triples, so the result is projected down to that same
            // shape here (rather than in testConformanceCase) so the
            // generic assertSame($step['expect'], $result) below needs
            // no per-op special-casing. See docs/conformance.md.
            $rows = Strings::searchData(
                $conn,
                $args['query'],
                $args['mode'] ?? 'natural',
                $args['language_id'] ?? null,
                $args['context'] ?? null,
                $args['status'] ?? null,
                $args['case_sensitive'] ?? false,
                $args['limit'] ?? 50,
                $args['offset'] ?? 0
            );
            return array_map(
                static fn (array $row) => [$row['language_id'], $row['string_id'], $row['context']],
                $rows
            );
        }
        return Strings::insertData(
            $conn,
            $args['string_id'],
            $args['language_id'],
            $args['content'],
            $args['context'] ?? '',
            $args['original_language'] ?? null,
            $args['status'] ?? 'draft',
            $args['updated_by'] ?? null
        );
    }

    public static function cases(): array
    {
        $path = __DIR__ . '/../../conformance/cases.json';
        $suite = json_decode(file_get_contents($path), true, 512, JSON_THROW_ON_ERROR);

        $out = [];
        foreach ($suite['cases'] as $case) {
            $out[$case['name']] = [$case['name'], $case['operations']];
        }
        return $out;
    }
}
