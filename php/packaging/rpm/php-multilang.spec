%global srcname multilang

%{!?version: %global version 0.1.0}

Name:           php-%{srcname}
Version:        %{version}
Release:        1%{?dist}
Summary:        Reusable multi-language string storage library (PHP source)

License:        GPL-3.0-or-later
URL:            https://github.com/rmahique/multilang-lib
Source0:        %{srcname}-%{version}.tar.gz

BuildArch:      noarch
Requires:       php >= 8.0
Requires:       php-pdo

# No distro branching here: plain source copy, identical across
# Fedora/RHEL and both openSUSE flavors. The specific PDO driver needed
# (php-pdo_sqlite / php-pdo_pgsql / php-pdo_mysql or the openSUSE
# php8-pdo_* equivalents) is intentionally not Required here -- exact
# package names differ enough across distros/PHP versions that guessing
# wrong would make this package uninstallable; see %%description.

%description
Provides retrieveData, insertData, and dbConnector for storing and
retrieving translated strings, keyed by string_id + BCP 47 language_id,
against SQLite, PostgreSQL, or MySQL/MariaDB.

Ships as source under /usr/share/php/Multilang/ (PSR-4 autoloadable,
namespace Multilang\\), wrapping this project's Composer package -- not
a full Packagist mirror. Install whichever PDO driver package your
distro provides for the backend you need (SQLite/PostgreSQL/MySQL); this
package deliberately does not Require one specific name for it.

%prep
%autosetup -n %{srcname}-%{version}

%build
# nothing to build -- plain PSR-4 source, no compile step.

%install
mkdir -p %{buildroot}%{_datadir}/php/Multilang
cp -r src/* composer.json %{buildroot}%{_datadir}/php/Multilang/

%check
# php/'s own test suite (phpunit) runs against a full `composer install`
# in CI's php-unit job; this packaging build ships source only and
# doesn't vendor vendor/phpunit, so it can't run that suite here without
# reaching Packagist, which a real build farm wouldn't have.

%files
%license LICENSE
%doc README.md
%{_datadir}/php/Multilang/

%changelog
* Thu Aug 06 2026 Raúl Mahiques <claude.ia@raulmahiques.com> - 0.1.0-1
- Initial release: retrieveData, insertData, dbConnector against
  SQLite, PostgreSQL, and MySQL/MariaDB.
