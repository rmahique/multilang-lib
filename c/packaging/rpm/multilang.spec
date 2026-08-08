%global srcname multilang
%global soversion 0

# build-rpm.sh passes --define "version ..."; 0.1.0 here is only a
# fallback for anyone invoking rpmbuild directly against this spec
# without going through the script.
%{!?version: %global version 0.1.0}

Name:           lib%{srcname}
Version:        %{version}
Release:        1%{?dist}
Summary:        Reusable multi-language string storage library

License:        GPL-3.0-or-later
URL:            https://github.com/rmahique/multilang-lib
Source0:        %{srcname}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  pkgconfig
BuildRequires:  pkgconfig(sqlite3)
BuildRequires:  pkgconfig(libpq)
BuildRequires:  pkgconfig(openssl)
BuildRequires:  pkgconfig(libmariadb)

# No %%if 0%%{?suse_version} branching needed here, unlike the Python
# spec: pkgconfig(X) BuildRequires resolve to whichever -devel package
# actually ships that .pc file on each distro (Fedora's sqlite-devel vs
# openSUSE's sqlite3-devel, for instance), so this spec doesn't need to
# hardcode distro-specific -devel package names. Runtime Requires are
# likewise left to rpm's automatic ELF-dependency generator
# (find-requires scans the built .so's NEEDED entries) rather than
# hand-guessed package names -- see ../packaging/README.md.

%description
Provides ml_retrieve_data, ml_insert_data, and ml_connect (plus a C++
wrapper) for storing and retrieving translated strings, keyed by
string_id + BCP 47 language_id, against SQLite, PostgreSQL, or
MySQL/MariaDB.

This is the runtime package: libmultilang.so.%{soversion} and
libmultilangxx.so.%{soversion}. See %{name}-devel for headers.

%package devel
Summary:        Development headers for %{name}
Requires:       %{name} = %{version}-%{release}

%description devel
Headers (multilang.h, multilang.hpp) and unversioned .so symlinks for
linking against %{name}.

%prep
%autosetup -n %{srcname}-%{version}

%build
# handled entirely by `make install` in %%install below -- unlike the
# source-only Go/JS/PHP specs' empty %%build, this one really does
# compile, via ../Makefile's install-runtime/install-devel targets.

%install
make install DESTDIR=%{buildroot} PREFIX=%{_prefix} LIBDIR=%{_libdir} INCLUDEDIR=%{_includedir}

%check
# c/'s own test suite (make test) runs in CI's c-unit job against the
# full monorepo checkout, including the sibling ../../conformance/
# cases.json this source tarball doesn't contain; this packaging build
# only needs to confirm `make install` produces the right layout, which
# %%install already exercises.

%files
%license LICENSE
%doc README.md
%{_libdir}/libmultilang.so.%{soversion}
%{_libdir}/libmultilang.so.%{soversion}.0.0
%{_libdir}/libmultilangxx.so.%{soversion}
%{_libdir}/libmultilangxx.so.%{soversion}.0.0

%files devel
%{_includedir}/multilang.h
%{_includedir}/multilang.hpp
%{_libdir}/libmultilang.so
%{_libdir}/libmultilangxx.so

%changelog
* Thu Aug 06 2026 Raúl Mahiques <claude.ia@raulmahiques.com> - 0.1.0-1
- Initial release: %{name} (runtime) + %{name}-devel (headers) for
  ml_retrieve_data, ml_insert_data, and ml_connect against SQLite,
  PostgreSQL, and MySQL/MariaDB.
