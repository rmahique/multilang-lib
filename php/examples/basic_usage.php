<?php

declare(strict_types=1);

/**
 * Runnable example: insert a source string plus translations, retrieve
 * with context disambiguation, handle ValidationException, and switch
 * backends via environment variables.
 *
 * Run from the php/ directory:
 *
 *   php examples/basic_usage.php
 *
 * By default this uses a throwaway SQLite file. To point it at a real
 * server instead, set the same MULTILANG_DB_* variables every port reads
 * (see ../../docs/connectors.md):
 *
 *   MULTILANG_DB_BACKEND=postgres \
 *   MULTILANG_DB_HOST=localhost MULTILANG_DB_USER=multilang \
 *   MULTILANG_DB_PASSWORD=multilang MULTILANG_DB_NAME=multilang \
 *   php examples/basic_usage.php
 *
 * Or point it at the filesystem backend (no server at all):
 *
 *   MULTILANG_DB_BACKEND=filesystem MULTILANG_DB_PATH=./example-strings \
 *   php examples/basic_usage.php
 */

require __DIR__ . '/../vendor/autoload.php';

use Multilang\Connector;
use Multilang\Strings;
use Multilang\ValidationException;

// Connector::connect reads MULTILANG_DB_BACKEND (and the matching
// MULTILANG_DB_HOST/_USER/_PASSWORD/_NAME/_PORT) if set; falling back to
// a temp SQLite file here just keeps this example runnable with no
// setup at all.
$backend = getenv('MULTILANG_DB_BACKEND') ?: 'sqlite';
if ($backend === 'sqlite' && getenv('MULTILANG_DB_PATH') === false) {
    $path = sys_get_temp_dir() . '/multilang_example_' . uniqid() . '.db';
    $conn = Connector::connect('sqlite', ['path' => $path]);
} else {
    $conn = Connector::connect(); // everything from MULTILANG_DB_* env vars
}

echo "Connected via backend={$backend}\n";

// --- Insert a source string, then a translation of it ----------------------
Strings::insertData($conn, 'greeting', 'en', 'Hello world');
Strings::insertData($conn, 'greeting', 'es', 'Hola mundo', '', 'en');
// The 5th argument, 'en', is originalLanguage: it makes insertData hash
// the current English content and store that hash as source_checksum --
// the basis for detecting later that a translation has gone stale
// relative to its source. retrieveData itself never returns that
// metadata (data only, by design -- see ../../docs/schema.md).

echo Strings::retrieveData($conn, 'greeting', 'es') . "\n"; // -> "Hola mundo"

// --- context disambiguates the same stringId used two ways -----------------
Strings::insertData($conn, 'post', 'en', 'Publish', 'button.publish');
Strings::insertData($conn, 'post', 'en', 'Post', 'menu.item');

echo Strings::retrieveData($conn, 'post', 'en', 'button.publish') . "\n"; // -> "Publish"
echo Strings::retrieveData($conn, 'post', 'en', 'menu.item') . "\n";     // -> "Post"

// --- retrieveData on a row that doesn't exist: null, not an error ----------
var_dump(Strings::retrieveData($conn, 'greeting', 'fr')); // -> NULL

// --- invalid input throws ValidationException, not a bare exception --------
try {
    Strings::insertData($conn, 'greeting', 'not-a-valid-bcp47-tag!!', 'test');
} catch (ValidationException $e) {
    echo "rejected as expected: {$e->getMessage()}\n";
}

$conn->close();
