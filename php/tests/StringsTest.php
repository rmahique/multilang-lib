<?php

declare(strict_types=1);

namespace Multilang\Tests;

use Multilang\Backends\SqliteBackend;
use Multilang\Connector;
use Multilang\Strings;
use Multilang\ValidationException;
use PHPUnit\Framework\TestCase;

final class StringsTest extends TestCase
{
    private function freshConn(string $name): SqliteBackend
    {
        $path = sys_get_temp_dir() . '/multilang_php_' . $name . '_' . uniqid() . '.db';
        return Connector::connect('sqlite', ['path' => $path]);
    }

    public function testInsertThenRetrieve(): void
    {
        $conn = $this->freshConn('t1');
        Strings::insertData($conn, 'greeting', 'en', 'Hello world');
        $this->assertSame('Hello world', Strings::retrieveData($conn, 'greeting', 'en'));
    }

    public function testMissingRowReturnsNull(): void
    {
        $conn = $this->freshConn('t2');
        $this->assertNull(Strings::retrieveData($conn, 'nope', 'en'));
    }

    public function testUpsertUpdatesExistingRow(): void
    {
        $conn = $this->freshConn('t3');
        Strings::insertData($conn, 'greeting', 'en', 'Hello');
        Strings::insertData($conn, 'greeting', 'en', 'Hello!');
        $this->assertSame('Hello!', Strings::retrieveData($conn, 'greeting', 'en'));
    }

    public function testDifferentlyCasedLanguageIdIsSameRow(): void
    {
        $conn = $this->freshConn('t4');
        Strings::insertData($conn, 'greeting', 'en-US', 'Hello');
        Strings::insertData($conn, 'greeting', 'en-us', 'Hello there');
        $this->assertSame('Hello there', Strings::retrieveData($conn, 'greeting', 'EN-US'));
    }

    public function testDifferentlyCasedStringIdIsSameRow(): void
    {
        $conn = $this->freshConn('t5');
        Strings::insertData($conn, 'Greeting', 'en', 'Hello');
        Strings::insertData($conn, 'GREETING', 'en', 'Hello there');
        $this->assertSame('Hello there', Strings::retrieveData($conn, 'greeting', 'en'));
    }

    public function testDifferentlyCasedContextIsSameRow(): void
    {
        $conn = $this->freshConn('t6');
        Strings::insertData($conn, 'post', 'fr', 'Publier', 'Button.Publish');
        Strings::insertData($conn, 'post', 'fr', 'Publier!', 'button.publish');
        $this->assertSame('Publier!', Strings::retrieveData($conn, 'post', 'fr', 'BUTTON.PUBLISH'));
    }

    public function testContextDisambiguatesSameStringId(): void
    {
        $conn = $this->freshConn('t7');
        Strings::insertData($conn, 'post', 'fr', 'Publier', 'button.publish');
        Strings::insertData($conn, 'post', 'fr', 'Article', 'menu.item');
        $this->assertSame('Publier', Strings::retrieveData($conn, 'post', 'fr', 'button.publish'));
        $this->assertSame('Article', Strings::retrieveData($conn, 'post', 'fr', 'menu.item'));
    }

    public function testTranslationComputesSourceChecksum(): void
    {
        $conn = $this->freshConn('t8');
        Strings::insertData($conn, 'greeting', 'en', 'Hello world');
        Strings::insertData($conn, 'greeting', 'es', 'Hola mundo', '', 'en');

        $stmt = $conn->pdo()->query(
            "SELECT source_checksum, original_language FROM strings " .
                "WHERE language_id='es' AND string_id='greeting' AND context=''"
        );
        $row = $stmt->fetch(\PDO::FETCH_ASSOC);
        $this->assertNotNull($row['source_checksum']);
        $this->assertSame('en', $row['original_language']);
    }

    public function testSourceRowHasNoChecksum(): void
    {
        $conn = $this->freshConn('t9');
        Strings::insertData($conn, 'greeting', 'en', 'Hello world');
        $stmt = $conn->pdo()->query(
            "SELECT source_checksum FROM strings WHERE language_id='en' AND string_id='greeting' AND context=''"
        );
        $row = $stmt->fetch(\PDO::FETCH_ASSOC);
        $this->assertNull($row['source_checksum']);
    }

    public function testRetrieveRejectsInvalidLanguageId(): void
    {
        $conn = $this->freshConn('t10');
        $this->expectException(ValidationException::class);
        Strings::retrieveData($conn, 'greeting', 'not-a-real-lang-tag-!!');
    }

    public function testInsertRejectsInvalidStatus(): void
    {
        $conn = $this->freshConn('t11');
        $this->expectException(ValidationException::class);
        Strings::insertData($conn, 'greeting', 'en', 'Hello', '', null, 'live');
    }

    public function testInsertRejectsEmptyContent(): void
    {
        $conn = $this->freshConn('t12');
        $this->expectException(ValidationException::class);
        Strings::insertData($conn, 'greeting', 'en', '');
    }
}
