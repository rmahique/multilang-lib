%global srcname multilang

%{!?version: %global version 0.1.0}

Name:           nodejs-%{srcname}
Version:        %{version}
Release:        1%{?dist}
Summary:        Reusable multi-language string storage library (Node.js source)

License:        GPL-3.0-or-later
URL:            https://github.com/rmahique/multilang-lib
Source0:        %{srcname}-%{version}.tar.gz

BuildArch:      noarch
Requires:       nodejs >= 18

# No distro branching here: this is a plain source copy, identical
# across Fedora/RHEL and both openSUSE flavors. "nodejs" is Fedora's and
# openSUSE's package name for the runtime alike, unlike some of the
# database-driver packages this deliberately doesn't Require (see below).

%description
Provides retrieveData, insertData, and dbConnector for storing and
retrieving translated strings, keyed by string_id + BCP 47 language_id,
against SQLite, PostgreSQL, or MySQL/MariaDB.

Ships as source under /usr/lib/node_modules/%{srcname}/ -- a plain
require()-able package, not a full npm registry mirror. This package
deliberately declares no Requires on the database driver packages
(better-sqlite3, mysql2, pg): they're native-addon/npm-registry
packages, not reliably available as system packages with a stable name
across distros -- install whichever backend you need via `npm install`
alongside this package.

%prep
%autosetup -n %{srcname}-%{version}

%build
# nothing to build -- plain CommonJS source, no bundler/transpiler step.

%install
# Node's global module path is always /usr/lib/node_modules, never
# /usr/lib64 -- unlike compiled libraries, this isn't arch-dependent, so
# don't use %%{_libdir}.
mkdir -p %{buildroot}%{_prefix}/lib/node_modules/%{srcname}
cp -r src package.json %{buildroot}%{_prefix}/lib/node_modules/%{srcname}/

%check
# javascript/'s own test suite (npm test) runs against a full
# node_modules install in CI's javascript-unit job; this packaging build
# ships source only and doesn't vendor node_modules, so it can't run
# that suite here without reaching the npm registry, which a real build
# farm wouldn't have.

%files
%license LICENSE
%doc README.md
%{_prefix}/lib/node_modules/%{srcname}/

%changelog
* Thu Aug 06 2026 Raúl Mahiques <claude.ia@raulmahiques.com> - 0.1.0-1
- Initial release: retrieveData, insertData, dbConnector against
  SQLite, PostgreSQL, and MySQL/MariaDB.
